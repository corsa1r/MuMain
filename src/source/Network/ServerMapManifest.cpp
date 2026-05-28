#include "stdafx.h"
#include "ServerMapManifest.h"

#include <cstring>
#include <algorithm>

// UTF-8 -> wide string. The packet carries map / warp names as raw UTF-8 with
// a byte-length prefix; we widen to wstring for compatibility with the rest of
// the client code (which already uses wide strings for text rendering).
static std::wstring Utf8ToWide(const uint8_t* bytes, size_t length)
{
    if (length == 0) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length),
        nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length),
        out.data(), needed);
    return out;
}

namespace BloodlustMU
{

ServerMapManifest& ServerMapManifest::Instance()
{
    static ServerMapManifest s_instance;
    return s_instance;
}

void ServerMapManifest::Clear()
{
    m_maps.clear();
    m_warps.clear();
    m_isLoaded = false;
}

bool ServerMapManifest::ApplyFromPacket(const uint8_t* ReceiveBuffer, int32_t Size)
{
    // Packet shape (see ShowCustomMapManifestPlugIn.cs on the server):
    //   [0]      0xC2
    //   [1-2]    total length (big-endian)
    //   [3]      0xBA           (HeadCode)
    //   [4]      0x01           (SubCode: full manifest)
    //   [5-6]    mapCount       (little-endian ushort)
    //   <map records...>
    //   <warpCount little-endian ushort>
    //   <warp records...>
    constexpr int HeaderSize = 5;
    if (Size < HeaderSize + 2) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x01) return false;

    // Total length from header (defensive — must agree with the Size we were passed).
    uint16_t declaredLen = (static_cast<uint16_t>(ReceiveBuffer[1]) << 8) | ReceiveBuffer[2];
    if (declaredLen != static_cast<uint16_t>(Size)) return false;

    int offset = HeaderSize;

    auto ReadU8 = [&](uint8_t& out) -> bool
    {
        if (offset + 1 > Size) return false;
        out = ReceiveBuffer[offset++];
        return true;
    };
    auto ReadU16LE = [&](uint16_t& out) -> bool
    {
        if (offset + 2 > Size) return false;
        out = static_cast<uint16_t>(ReceiveBuffer[offset] | (ReceiveBuffer[offset + 1] << 8));
        offset += 2;
        return true;
    };
    auto ReadI16LE = [&](int16_t& out) -> bool
    {
        uint16_t u; if (!ReadU16LE(u)) return false;
        out = static_cast<int16_t>(u);
        return true;
    };
    auto ReadI32LE = [&](int32_t& out) -> bool
    {
        if (offset + 4 > Size) return false;
        out = ReceiveBuffer[offset]
            | (ReceiveBuffer[offset + 1] << 8)
            | (ReceiveBuffer[offset + 2] << 16)
            | (ReceiveBuffer[offset + 3] << 24);
        offset += 4;
        return true;
    };
    auto ReadName = [&](std::wstring& out) -> bool
    {
        uint8_t nameLen;
        if (!ReadU8(nameLen)) return false;
        if (offset + nameLen > Size) return false;
        out = Utf8ToWide(ReceiveBuffer + offset, nameLen);
        offset += nameLen;
        return true;
    };

    uint16_t mapCount;
    if (!ReadU16LE(mapCount)) return false;

    std::vector<MapManifestEntry> newMaps;
    newMaps.reserve(mapCount);
    for (uint16_t i = 0; i < mapCount; ++i)
    {
        MapManifestEntry e;
        if (!ReadI16LE(e.number)) return false;
        if (!ReadU8(e.discriminator)) return false;
        uint8_t isCustom;
        if (!ReadU8(isCustom)) return false;
        e.isCustomMap = isCustom != 0;
        if (!ReadName(e.name)) return false;
        newMaps.push_back(std::move(e));
    }

    uint16_t warpCount;
    if (!ReadU16LE(warpCount)) return false;

    std::vector<WarpManifestEntry> newWarps;
    newWarps.reserve(warpCount);
    for (uint16_t i = 0; i < warpCount; ++i)
    {
        WarpManifestEntry w;
        if (!ReadI16LE(w.index)) return false;
        if (!ReadI16LE(w.targetMapNumber)) return false;
        if (!ReadI16LE(w.levelRequirement)) return false;
        if (!ReadI32LE(w.costs)) return false;
        if (!ReadU8(w.gateX)) return false;
        if (!ReadU8(w.gateY)) return false;
        if (!ReadName(w.name)) return false;
        newWarps.push_back(std::move(w));
    }

    if (offset != Size)
    {
        // Trailing bytes — packet is malformed. Reject so we don't half-update the cache.
        return false;
    }

    m_maps = std::move(newMaps);
    m_warps = std::move(newWarps);
    m_isLoaded = true;
    return true;
}

const MapManifestEntry* ServerMapManifest::FindMap(int mapNumber) const
{
    for (const auto& m : m_maps)
    {
        if (m.number == mapNumber) return &m;
    }
    return nullptr;
}

const WarpManifestEntry* ServerMapManifest::FindWarp(int index) const
{
    for (const auto& w : m_warps)
    {
        if (w.index == index) return &w;
    }
    return nullptr;
}

bool ServerMapManifest::IsCustomMap(int mapNumber) const
{
    const auto* m = FindMap(mapNumber);
    return m && m->isCustomMap;
}

const wchar_t* ServerMapManifest::CurrentMapDisplayName() const
{
    const auto* m = FindMap(m_currentMap);
    return m ? m->name.c_str() : L"";
}

} // namespace BloodlustMU
