#include "stdafx.h"
#include "MapPackageWriter.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

#include "../../ThirdParty/json.hpp"
#include "CustomMapIO.h"

#include <wincrypt.h>           // CryptAcquireContext / CryptHashData (for SHA-256)
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MuEditor::CustomMap
{
namespace
{
    // ----------- CRC-32 (IEEE 802.3, polynomial 0xEDB88320) -----------
    // Used in ZIP local file header + central directory entries. Table-driven
    // so we don't compute the polynomial 256 times.
    struct Crc32Table
    {
        uint32_t table[256];
        Crc32Table()
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int j = 0; j < 8; ++j)
                    c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0u);
                table[i] = c;
            }
        }
    };
    const Crc32Table& Crc32T()
    {
        static const Crc32Table t;
        return t;
    }
    uint32_t Crc32(const uint8_t* data, size_t len)
    {
        const auto& T = Crc32T();
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i)
            c = T.table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    }

    // ----------- SHA-256 hex (via Windows CryptoAPI) -----------
    // The manifest declares a sha256 per asset; the server validates these before
    // accepting the import. Using the system provider avoids dragging in OpenSSL.
    std::string Sha256Hex(const uint8_t* data, size_t len)
    {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        std::string out;
        if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_AES,
                                  CRYPT_VERIFYCONTEXT))
            return out;
        if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
        {
            CryptReleaseContext(hProv, 0);
            return out;
        }
        if (CryptHashData(hHash, data, static_cast<DWORD>(len), 0))
        {
            BYTE hash[32];
            DWORD hashLen = sizeof(hash);
            if (CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)
                && hashLen == 32)
            {
                std::ostringstream oss;
                oss << std::hex << std::setfill('0');
                for (BYTE b : hash) oss << std::setw(2) << static_cast<int>(b);
                out = oss.str();
            }
        }
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return out;
    }

    // ----------- Little-endian writers -----------
    void WriteLE16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    }
    void WriteLE32(std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back(static_cast<uint8_t>(x & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
        v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    }
    void Append(std::vector<uint8_t>& v, const void* src, size_t len)
    {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        v.insert(v.end(), p, p + len);
    }

    // DOS time/date for "now" — ZIP records require these but we don't care
    // about the value beyond "well-formed". Use a fixed timestamp.
    void DosTimeDate(uint16_t& outTime, uint16_t& outDate)
    {
        time_t now = time(nullptr);
        tm t{};
        localtime_s(&t, &now);
        outTime = static_cast<uint16_t>(
            (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
        outDate = static_cast<uint16_t>(
            ((t.tm_year - 80) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    }

    struct CentralEntry
    {
        std::string name;
        uint32_t    crc32{};
        uint32_t    size{};
        uint32_t    localHeaderOffset{};
        uint16_t    dosTime{};
        uint16_t    dosDate{};
    };

    // Append a local file record (header + raw data, "stored" = no compression).
    // Returns the central-directory entry to be written later.
    CentralEntry AppendStoredEntry(std::vector<uint8_t>& zip,
                                   const std::string& entryName,
                                   const uint8_t* data,
                                   size_t len)
    {
        CentralEntry ce;
        ce.name = entryName;
        ce.crc32 = Crc32(data, len);
        ce.size = static_cast<uint32_t>(len);
        ce.localHeaderOffset = static_cast<uint32_t>(zip.size());
        DosTimeDate(ce.dosTime, ce.dosDate);

        // Local File Header (30 bytes + name + extra)
        WriteLE32(zip, 0x04034b50);           // signature
        WriteLE16(zip, 20);                   // version needed
        WriteLE16(zip, 0);                    // flags
        WriteLE16(zip, 0);                    // method = stored
        WriteLE16(zip, ce.dosTime);
        WriteLE16(zip, ce.dosDate);
        WriteLE32(zip, ce.crc32);
        WriteLE32(zip, ce.size);              // compressed size
        WriteLE32(zip, ce.size);              // uncompressed size
        WriteLE16(zip, static_cast<uint16_t>(entryName.size()));
        WriteLE16(zip, 0);                    // extra field length
        Append(zip, entryName.data(), entryName.size());
        Append(zip, data, len);
        return ce;
    }

    void AppendCentralAndEnd(std::vector<uint8_t>& zip,
                             const std::vector<CentralEntry>& entries)
    {
        const uint32_t centralStart = static_cast<uint32_t>(zip.size());
        for (const auto& ce : entries)
        {
            WriteLE32(zip, 0x02014b50);           // central dir signature
            WriteLE16(zip, 20);                   // version made by
            WriteLE16(zip, 20);                   // version needed
            WriteLE16(zip, 0);                    // flags
            WriteLE16(zip, 0);                    // method = stored
            WriteLE16(zip, ce.dosTime);
            WriteLE16(zip, ce.dosDate);
            WriteLE32(zip, ce.crc32);
            WriteLE32(zip, ce.size);
            WriteLE32(zip, ce.size);
            WriteLE16(zip, static_cast<uint16_t>(ce.name.size()));
            WriteLE16(zip, 0);                    // extra field length
            WriteLE16(zip, 0);                    // comment length
            WriteLE16(zip, 0);                    // disk number start
            WriteLE16(zip, 0);                    // internal attrs
            WriteLE32(zip, 0);                    // external attrs
            WriteLE32(zip, ce.localHeaderOffset);
            Append(zip, ce.name.data(), ce.name.size());
        }
        const uint32_t centralSize = static_cast<uint32_t>(zip.size() - centralStart);

        // EOCD (22 bytes)
        WriteLE32(zip, 0x06054b50);
        WriteLE16(zip, 0);                        // disk number
        WriteLE16(zip, 0);                        // central dir start disk
        WriteLE16(zip, static_cast<uint16_t>(entries.size()));
        WriteLE16(zip, static_cast<uint16_t>(entries.size()));
        WriteLE32(zip, centralSize);
        WriteLE32(zip, centralStart);
        WriteLE16(zip, 0);                        // comment length
    }

    std::vector<uint8_t> ReadAllBytes(const std::wstring& path)
    {
        std::vector<uint8_t> out;
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return out;
        auto sz = static_cast<std::streamoff>(f.tellg());
        if (sz <= 0) return out;
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<size_t>(sz));
        f.read(reinterpret_cast<char*>(out.data()), sz);
        return out;
    }

    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int needed = WideCharToMultiByte(CP_UTF8, 0,
            w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return std::string();
        std::string out(static_cast<size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
            out.data(), needed, nullptr, nullptr);
        return out;
    }

    // Asset filenames we attempt to bundle. Optional ones are skipped silently
    // if missing on disk (older slots, slots that never had grass painted, etc.).
    struct AssetSpec
    {
        std::wstring localFileName;   // file under <slot>/
        bool required;
    };
} // namespace

ExportResult ExportMapPackage(int mapId,
                              const std::wstring& mapDisplayName,
                              const std::wstring& outputPath,
                              const std::string& warpName,
                              int levelRequirement,
                              int costs,
                              uint8_t spawnX,
                              uint8_t spawnY)
{
    ExportResult res;
    res.outputPath = outputPath;

    const int folderIndex = mapId + 1;
    const std::wstring slotDir = GetCustomMapDirectory(mapId);
    if (!fs::exists(slotDir))
    {
        res.error = "Slot directory not found.";
        return res;
    }

    auto pattern = [folderIndex](const wchar_t* fmt) {
        wchar_t buf[64];
        std::swprintf(buf, std::size(buf), fmt, folderIndex);
        return std::wstring(buf);
    };

    std::vector<AssetSpec> assets = {
        { pattern(L"EncTerrain%d.att"),    true  },
        { pattern(L"EncTerrain%d.obj"),    true  },
        { pattern(L"EncTerrain%d.map"),    true  },
        { L"TerrainHeight.OZB",            true  },
        { L"TerrainLight.OZJ",             true  },
        { pattern(L"EncTerrain%d.grass"),  false },
        { L"sources.json",                 false },
    };

    std::vector<uint8_t> zipBuffer;
    std::vector<CentralEntry> central;

    // Build manifest as we go so we can include accurate per-asset hashes/sizes.
    json manifest;
    manifest["schemaVersion"] = 1;
    // ISO-8601 UTC timestamp ("2026-05-28T11:30:00Z"). The server deserialises this into a
    // DateTimeOffset; an empty string or wrong format would fail to parse.
    char nowBuf[32];
    {
        time_t nowT = time(nullptr);
        tm utc{};
        gmtime_s(&utc, &nowT);
        std::strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    }
    manifest["package"] = {
        {"createdAt", nowBuf},
        {"createdBy", "mu-editor"},
        {"editorVersion", "0.1"}
    };
    manifest["map"] = {
        {"number", mapId},
        {"discriminator", 0},
        {"name", ToUtf8(mapDisplayName)},
        {"isCustomMap", true},
        {"expMultiplier", 1.0}
    };
    if (spawnX != 0 || spawnY != 0)
    {
        manifest["spawnGate"] = {
            {"x1", spawnX}, {"x2", spawnX},
            {"y1", spawnY}, {"y2", spawnY},
            {"direction", "Undefined"}
        };
    }
    if (!warpName.empty())
    {
        manifest["warpInfo"] = {
            {"name", warpName},
            {"levelRequirement", levelRequirement},
            {"costs", costs}
        };
    }

    json assetsArr = json::array();
    for (const auto& spec : assets)
    {
        const std::wstring fullPath = (fs::path(slotDir) / spec.localFileName).wstring();
        auto data = ReadAllBytes(fullPath);
        if (data.empty())
        {
            if (spec.required)
            {
                res.error = "Required asset missing: " + ToUtf8(spec.localFileName);
                return res;
            }
            continue;
        }
        const std::string entryName = "assets/" + ToUtf8(spec.localFileName);
        auto ce = AppendStoredEntry(zipBuffer, entryName, data.data(), data.size());
        central.push_back(ce);
        assetsArr.push_back({
            {"path",   entryName},
            {"sha256", Sha256Hex(data.data(), data.size())},
            {"bytes",  static_cast<int64_t>(data.size())}
        });
    }
    manifest["assets"] = assetsArr;

    // manifest.json — written last in source order so the EOCD/central directory
    // still places it inside the archive; readers find it by name, not position.
    const std::string manifestStr = manifest.dump(2);
    auto manifestCe = AppendStoredEntry(zipBuffer, "manifest.json",
        reinterpret_cast<const uint8_t*>(manifestStr.data()), manifestStr.size());
    central.push_back(manifestCe);

    AppendCentralAndEnd(zipBuffer, central);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out)
    {
        res.error = "Failed to open output file.";
        return res;
    }
    out.write(reinterpret_cast<const char*>(zipBuffer.data()),
              static_cast<std::streamsize>(zipBuffer.size()));
    out.close();

    res.success = true;
    return res;
}

} // namespace MuEditor::CustomMap
