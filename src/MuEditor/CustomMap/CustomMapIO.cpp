#include "stdafx.h"

#ifdef _EDITOR

#include "CustomMapIO.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

#include "../../ThirdParty/json.hpp"

#include "Core/Globals/_crypt.h"             // BuxConvert
#include "Core/Globals/_define.h"            // TERRAIN_SIZE, TW_*
#include "Render/Terrain/ZzzLodTerrain.h"    // TerrainWall, MapFileEncrypt, OpenTerrainAttribute,
                                             // OpenTerrainMapping, CreateTerrain, CreateTerrainNormal,
                                             // CreateTerrainLight, OpenTerrainLight
#include "Render/Textures/ZzzTexture.h"      // WriteJpeg, LoadBitmap
#include "Core/Globals/_TextureIndex.h"      // BITMAP_MAPTILE etc.
#include "turbojpeg.h"                       // tjCompress2 (for OZJ encode)
#include "Engine/Object/ZzzObject.h"         // ObjectBlock, OpenObjectsEnc, DeleteObject, CreateObject
#include "Engine/Object/ZzzCharacter.h"      // CharactersClient, Hero
#include "Engine/Object/w_ObjectInfo.h"      // OBJECT, CHARACTER
#include "Core/Globals/_enum.h"              // MAX_WORLD_OBJECTS
#include "World/MapInfra/MapManager.h"       // gMapManager, WD_* enum
#include "CustomMap/CustomWeather.h"         // SetActiveCustomWeather, ClearLiveWeatherParticles
#include "Core/Globals/CustomWeatherFlags.h" // CW_* bits
#include "Core/Globals/_enum.h"              // MODEL_BIRD01, MODEL_BAT01, MODEL_BUTTERFLY01
#include "Data/DataHandler/LoadData.h"       // gLoadData, AccessModel / OpenTexture

// Per-tile grass mask — 0 = suppress grass at the tile, 0xFF = render.
// The engine's grass-quad path early-outs on a zero mask (see
// ZzzLodTerrain.cpp). TerrainGrassTexture (a float buffer used by the
// engine as a per-row UV scrambler) is NOT what controls grass
// presence, so we persist the mask instead.
extern unsigned char TerrainGrassMask[];

#include "SourceBank.h"                      // LoadSourceBank, FindSourceWorldByModelIndex

namespace fs = std::filesystem;

namespace
{
    // Asset path layout:
    //   Data\World\Custom\World<n>\EncTerrain<n>.{att,obj}
    // where n = mapId+1 (matches iMapWorld = WorldActive+1 used elsewhere).
    constexpr const wchar_t* CUSTOM_ROOT          = L"Data\\World\\Custom";
    constexpr const wchar_t* CUSTOM_FOLDER_PREFIX = L"World";
    constexpr const wchar_t* CUSTOM_FILE_PREFIX   = L"EncTerrain";
    constexpr const wchar_t* TERRAIN_ATT_EXT      = L".att";
    constexpr const wchar_t* TERRAIN_OBJ_EXT      = L".obj";
    constexpr const wchar_t* TERRAIN_MAP_EXT      = L".map";
    constexpr const wchar_t* HEIGHT_FILE_NAME     = L"TerrainHeight.OZB";
    constexpr const wchar_t* LIGHT_FILE_NAME      = L"TerrainLight.OZJ";
    constexpr const wchar_t* GRASS_FILE_EXT       = L".grass";
    constexpr const wchar_t* MANIFEST_FILE_NAME   = L"sources.json";
    constexpr const wchar_t* SOURCE_DIR_PREFIX    = L"source_World";

    // sources.json schema version. Bump when the on-disk JSON shape
    // changes incompatibly so older loaders can refuse cleanly.
    constexpr int SOURCE_MANIFEST_VERSION = 1;

    // .att header (see OpenTerrainAttribute in ZzzLodTerrain.cpp).
    // 4 raw bytes: { Version, iMap, Width, Height }. The classic loader
    // validates Version==0 and Width==Height==255 before accepting.
    constexpr BYTE TERRAIN_FILE_VERSION  = 0;
    constexpr BYTE TERRAIN_HEADER_WIDTH  = 255;
    constexpr BYTE TERRAIN_HEADER_HEIGHT = 255;
    constexpr int  TERRAIN_HEADER_SIZE   = 4;

    // .att payload: row-major WORD grid. Always emit the 16-bit
    // ("extAtt") variant — the legacy 8-bit form is read-only compat.
    constexpr int  TERRAIN_TILE_COUNT    = TERRAIN_SIZE * TERRAIN_SIZE;
    constexpr int  TERRAIN_PAYLOAD_SIZE  =
        TERRAIN_TILE_COUNT * static_cast<int>(sizeof(WORD));
    constexpr int  TERRAIN_FILE_SIZE     =
        TERRAIN_HEADER_SIZE + TERRAIN_PAYLOAD_SIZE;

    // .obj header (see OpenObjectsEnc in ZzzObject.cpp).
    // 4 raw bytes: { Version, MapNumber, short Count }, followed by
    // `Count` fixed-size object records.
    constexpr BYTE OBJECT_FILE_VERSION = 0;
    constexpr int  OBJECT_HEADER_SIZE  = 4;
    constexpr int  OBJECT_RECORD_SIZE  =
        sizeof(short) + sizeof(vec3_t) * 2 + sizeof(float);   // 30 bytes
    constexpr int  OBJECT_COUNT_OFFSET = 2;                    // short

    // ObjectBlock is a 16x16 spatial hash; iterating all buckets walks
    // the entire live object set (see SaveObjects in ZzzObject.cpp).
    constexpr int  OBJECT_BLOCK_GRID  = 16;
    constexpr int  OBJECT_BLOCK_COUNT = OBJECT_BLOCK_GRID * OBJECT_BLOCK_GRID;

    // .map header (see OpenTerrainMapping in ZzzLodTerrain.cpp).
    // Cleartext layout: { BYTE Version, BYTE MapNumber, BYTE Layer1[256*256],
    // BYTE Layer2[256*256], BYTE Alpha[256*256] }. Encrypted with
    // MapFileEncrypt only (no BuxConvert on the .map stream).
    constexpr BYTE MAPPING_FILE_VERSION = 0;
    constexpr int  MAPPING_HEADER_SIZE  = 2;
    constexpr int  MAPPING_LAYER_BYTES  = TERRAIN_TILE_COUNT;     // 65536
    constexpr int  MAPPING_FILE_SIZE    =
        MAPPING_HEADER_SIZE + MAPPING_LAYER_BYTES * 3;            // 196610

    // TerrainHeight.OZB (legacy 8-bit, see OpenTerrainHeight in
    // ZzzLodTerrain.cpp). Layout: 4-byte prefix + 1080-byte BMP-like header +
    // 256*256 height bytes (1 byte per tile; world Z = byte * 1.5f for the
    // non-login worlds we target). Not encrypted.
    constexpr int HEIGHT_OZB_PREFIX_SIZE = 4;
    constexpr int HEIGHT_OZB_HEADER_SIZE = 1080;
    constexpr int HEIGHT_OZB_FILE_SIZE   =
        HEIGHT_OZB_PREFIX_SIZE + HEIGHT_OZB_HEADER_SIZE + TERRAIN_TILE_COUNT;

    // World tile bitmap set — these are the ~35 files the engine's classic
    // warp loads from <world-dir>\ into the BITMAP_MAPTILE / MAPGRASS / LEAF
    // texture slots. The .map file's Layer1/Layer2 indices select textures
    // *out of these slots*, so the same .map renders different ground
    // depending on what's loaded here. New custom slots copy this set from
    // BASE_WORLD_TEXTURE_DIR so they ship with a known, consistent look.
    constexpr const wchar_t* BASE_WORLD_TEXTURE_DIR = L"Data\\World1"; // Lorencia
    constexpr const wchar_t* WORLD_TEXTURE_FILES[] =
    {
        L"TileGrass01.jpg", L"TileGrass02.jpg",
        L"TileGround01.jpg", L"TileGround02.jpg", L"TileGround03.jpg",
        L"TileWater01.jpg",
        L"TileWood01.jpg",
        L"TileRock01.jpg", L"TileRock02.jpg", L"TileRock03.jpg",
        L"TileRock04.jpg", L"TileRock05.jpg", L"TileRock06.jpg", L"TileRock07.jpg",
        L"ExtTile01.jpg", L"ExtTile02.jpg", L"ExtTile03.jpg", L"ExtTile04.jpg",
        L"ExtTile05.jpg", L"ExtTile06.jpg", L"ExtTile07.jpg", L"ExtTile08.jpg",
        L"ExtTile09.jpg", L"ExtTile10.jpg", L"ExtTile11.jpg", L"ExtTile12.jpg",
        L"ExtTile13.jpg", L"ExtTile14.jpg", L"ExtTile15.jpg", L"ExtTile16.jpg",
        L"TileGrass01.tga", L"TileGrass02.tga", L"TileGrass03.tga",
        L"leaf01.tga", L"leaf02.jpg",
    };

    // TerrainLight.OZJ — 256x256 RGB8 stored as { 24-byte prefix, JPEG bytes }
    // (see OpenJpegBuffer in ZzzTexture.cpp — it fseek()s to 24 and feeds the
    // remainder to turbojpeg). Filling with full white means CreateTerrainLight
    // modulates only by the normal/shade pass, so a flat map ends up evenly
    // lit instead of inheriting whatever was in TerrainLight[] from a
    // previously-warped world.
    constexpr unsigned char LIGHT_OZJ_FILL    = 255;
    constexpr int           LIGHT_OZJ_QUALITY = 90;
    constexpr int           LIGHT_OZJ_PREFIX_SIZE = 24;

    // New-map template: stamp the outer ring of the 256x256 grid so a
    // player teleporting in can't immediately walk off the void edge.
    constexpr int  CUSTOM_MAP_BORDER_THICKNESS = 1;
    constexpr WORD CUSTOM_MAP_BORDER_ATTR      = TW_NOMOVE | TW_NOGROUND;

    inline BYTE FileMapNumber(int mapId)
    {
        return static_cast<BYTE>(mapId + 1);
    }

    // Header field is a BYTE — folder index (mapId+1) must fit 0..255.
    // 255 is also the loader's Width/Height sentinel, so we cap at 254.
    constexpr int CUSTOM_MAP_ID_MIN = 0;
    constexpr int CUSTOM_MAP_ID_MAX = 254;

    std::wstring FormatIndexed(const wchar_t* prefix, int n)
    {
        wchar_t buf[64];
        std::swprintf(buf, std::size(buf), L"%ls%d", prefix, n);
        return buf;
    }

    void StampBorderAttribute(WORD* tiles, WORD attr, int thickness)
    {
        for (int y = 0; y < TERRAIN_SIZE; ++y)
        {
            const bool yEdge =
                y < thickness || y >= TERRAIN_SIZE - thickness;
            for (int x = 0; x < TERRAIN_SIZE; ++x)
            {
                const bool xEdge =
                    x < thickness || x >= TERRAIN_SIZE - thickness;
                if (xEdge || yEdge)
                {
                    tiles[x + y * TERRAIN_SIZE] = attr;
                }
            }
        }
    }

    std::vector<BYTE> BuildAttCleartext(int mapId, const WORD* tiles)
    {
        std::vector<BYTE> buf(TERRAIN_FILE_SIZE);
        buf[0] = TERRAIN_FILE_VERSION;
        buf[1] = FileMapNumber(mapId);
        buf[2] = TERRAIN_HEADER_WIDTH;
        buf[3] = TERRAIN_HEADER_HEIGHT;
        std::memcpy(buf.data() + TERRAIN_HEADER_SIZE,
                    tiles, TERRAIN_PAYLOAD_SIZE);
        return buf;
    }

    // Loader order: MapFileDecrypt -> BuxConvert -> parse.
    // Saver order:  BuxConvert (self-inverse XOR) -> MapFileEncrypt.
    std::vector<BYTE> EncryptAttStream(std::vector<BYTE> cleartext)
    {
        BuxConvert(cleartext.data(),
                   static_cast<int>(cleartext.size()));
        std::vector<BYTE> encrypted(cleartext.size());
        MapFileEncrypt(encrypted.data(), cleartext.data(),
                       static_cast<int>(cleartext.size()));
        return encrypted;
    }

    // Loader order: MapFileDecrypt -> parse  (no BuxConvert on .obj).
    // Saver order:  MapFileEncrypt only.
    std::vector<BYTE> EncryptObjStream(const std::vector<BYTE>& cleartext)
    {
        std::vector<BYTE> encrypted(cleartext.size());
        MapFileEncrypt(encrypted.data(),
                       const_cast<BYTE*>(cleartext.data()),
                       static_cast<int>(cleartext.size()));
        return encrypted;
    }

    // Appends one OBJECT record with an override Type — used by the
    // partitioned save so per-source .obj files can store source-
    // relative indices (0..159) instead of the absolute Models[] index
    // the live OBJECT carries.
    void AppendObjectRecordTyped(std::vector<BYTE>& buf,
                                 const OBJECT& o, short writeType)
    {
        const size_t base = buf.size();
        buf.resize(base + OBJECT_RECORD_SIZE);
        BYTE* p = buf.data() + base;

        std::memcpy(p, &writeType, sizeof(short));    p += sizeof(short);
        std::memcpy(p, o.Position, sizeof(vec3_t));   p += sizeof(vec3_t);
        std::memcpy(p, o.Angle,    sizeof(vec3_t));   p += sizeof(vec3_t);
        std::memcpy(p, &o.Scale,   sizeof(float));
    }

    // Native call: pass through the OBJECT's existing Type.
    void AppendObjectRecord(std::vector<BYTE>& buf, const OBJECT& o)
    {
        AppendObjectRecordTyped(buf, o, static_cast<short>(o.Type));
    }

    // Walks the 16x16 ObjectBlock spatial-hash and emits one record per
    // Live object. Header is reserved up front; count is patched in last.
    std::vector<BYTE> BuildObjCleartext(int mapId)
    {
        std::vector<BYTE> buf(OBJECT_HEADER_SIZE, 0);
        buf[0] = OBJECT_FILE_VERSION;
        buf[1] = FileMapNumber(mapId);

        short count = 0;
        for (int b = 0; b < OBJECT_BLOCK_COUNT; ++b)
        {
            for (OBJECT* o = ObjectBlock[b].Head; o != nullptr; o = o->Next)
            {
                if (!o->Live) continue;
                AppendObjectRecord(buf, *o);
                ++count;
            }
        }
        std::memcpy(buf.data() + OBJECT_COUNT_OFFSET,
                    &count, sizeof(short));
        return buf;
    }

    // Per-source object stream container: the main .obj plus one buffer
    // per source-world bank we're emitting. Build with BuildPartitionedObjStreams,
    // write each entry with WritePartitionedObjStreams.
    struct PartitionedObjStreams
    {
        std::vector<BYTE>                          mainStream;
        std::map<int /*worldFolderIndex*/, std::vector<BYTE>> bySource;
    };

    // Initialises a cleartext .obj header in `buf`. Count gets patched
    // later via OBJECT_COUNT_OFFSET. mapId+1 is written to the
    // MapNumber field for legacy-loader compatibility.
    void InitObjStreamHeader(std::vector<BYTE>& buf, int mapId)
    {
        buf.assign(OBJECT_HEADER_SIZE, 0);
        buf[0] = OBJECT_FILE_VERSION;
        buf[1] = FileMapNumber(mapId);
    }

    void PatchObjStreamCount(std::vector<BYTE>& buf, short count)
    {
        std::memcpy(buf.data() + OBJECT_COUNT_OFFSET,
                    &count, sizeof(short));
    }

    // Walks the live ObjectBlock spatial-hash and partitions each live
    // OBJECT into the main stream (Type < MAX_WORLD_OBJECTS — native
    // universal range) or into the per-source stream for whichever
    // side-bank window owns its Type. Objects whose Type falls outside
    // both ranges are silently skipped (likely a partially-loaded
    // bank); they'd render as garbage anyway. Counts are patched into
    // each stream's header before return.
    PartitionedObjStreams BuildPartitionedObjStreams(int mapId)
    {
        PartitionedObjStreams out;
        InitObjStreamHeader(out.mainStream, mapId);

        short mainCount = 0;
        std::map<int, short> sourceCounts;

        for (int b = 0; b < OBJECT_BLOCK_COUNT; ++b)
        {
            for (OBJECT* o = ObjectBlock[b].Head; o != nullptr; o = o->Next)
            {
                if (!o->Live) continue;

                if (o->Type >= 0 && o->Type < MAX_WORLD_OBJECTS)
                {
                    AppendObjectRecord(out.mainStream, *o);
                    ++mainCount;
                    continue;
                }

                const int sourceWorld =
                    MuEditor::CustomMap::FindSourceWorldByModelIndex(o->Type);
                if (sourceWorld < 0) continue;       // Type isn't ours.

                const int baseOffset =
                    MuEditor::CustomMap::GetSourceBankBaseOffset(sourceWorld);
                if (baseOffset < 0) continue;        // bank vanished — drop.

                const short relType =
                    static_cast<short>(o->Type - baseOffset);

                auto& stream = out.bySource[sourceWorld];
                if (stream.empty()) InitObjStreamHeader(stream, mapId);
                AppendObjectRecordTyped(stream, *o, relType);
                ++sourceCounts[sourceWorld];
            }
        }

        PatchObjStreamCount(out.mainStream, mainCount);
        for (auto& [src, count] : sourceCounts)
        {
            PatchObjStreamCount(out.bySource[src], count);
        }
        return out;
    }

    // sources.json carries two fields: the list of side-loaded object
    // banks (for cross-world model placement) and the slot's base
    // world (which classic world's tile bitmaps to re-seed from if
    // the slot's local copies disappear). Loose schema — older
    // manifests without baseWorld still parse and fall back to 1.
    struct SourceManifest
    {
        std::vector<int> sources;
        int              baseWorld    = 1;   // 1 = Lorencia default
        unsigned int     weatherFlags = 0;   // CW_* bitmask (see CustomWeatherFlags.h)
    };

    SourceManifest ReadSourceManifest(const std::wstring& path)
    {
        SourceManifest result;
        std::error_code ec;
        if (!fs::exists(path, ec)) return result;

        std::ifstream in(fs::path(path), std::ios::binary);
        if (!in) return result;

        try
        {
            nlohmann::json j;
            in >> j;
            if (!j.is_object()) return result;

            const auto srcIt = j.find("sources");
            if (srcIt != j.end() && srcIt->is_array())
            {
                for (const auto& v : *srcIt)
                    if (v.is_number_integer())
                        result.sources.push_back(v.get<int>());
            }
            const auto baseIt = j.find("baseWorld");
            if (baseIt != j.end() && baseIt->is_number_integer())
                result.baseWorld = baseIt->get<int>();
            const auto wfIt = j.find("weatherFlags");
            if (wfIt != j.end() && wfIt->is_number_unsigned())
                result.weatherFlags = wfIt->get<unsigned int>();
        }
        catch (...) { return SourceManifest{}; }
        return result;
    }

    bool WriteSourceManifest(const std::wstring& path,
                             const SourceManifest& m)
    {
        nlohmann::json j;
        j["version"]      = SOURCE_MANIFEST_VERSION;
        j["baseWorld"]    = m.baseWorld;
        j["sources"]      = m.sources;
        j["weatherFlags"] = m.weatherFlags;

        std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << j.dump(2);
        return out.good();
    }

    // Loads a per-source .obj from disk with Type fixup. The on-disk
    // file uses source-relative Types (0..159); CreateObject needs the
    // absolute Models[] index, so we add the bank's baseOffset before
    // each CreateObject call.
    //
    // Mirrors OpenObjectsEnc's parse loop (decrypt → header → records),
    // diverging only in the Type fixup step. We can't call OpenObjectsEnc
    // directly because it doesn't know about offset translation.
    bool LoadSourceObjStream(const std::wstring& path, int baseOffset)
    {
        std::error_code ec;
        if (!fs::exists(path, ec)) return false;

        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (fp == nullptr) return false;

        std::fseek(fp, 0, SEEK_END);
        const long fileSize = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (fileSize <= 0)
        {
            std::fclose(fp);
            return false;
        }

        std::vector<BYTE> encrypted(fileSize);
        const size_t readBytes =
            std::fread(encrypted.data(), 1, fileSize, fp);
        std::fclose(fp);
        if (static_cast<long>(readBytes) != fileSize) return false;

        const int decryptedSize =
            MapFileDecrypt(nullptr, encrypted.data(), fileSize);
        std::vector<BYTE> decrypted(decryptedSize);
        MapFileDecrypt(decrypted.data(), encrypted.data(), fileSize);

        if (decryptedSize < OBJECT_HEADER_SIZE) return false;

        int dataPtr = 0;
        dataPtr += 1;                                            // Version
        dataPtr += 1;                                            // MapNumber
        short count = 0;
        std::memcpy(&count, decrypted.data() + dataPtr, sizeof(short));
        dataPtr += sizeof(short);

        const int requiredSize =
            OBJECT_HEADER_SIZE + count * OBJECT_RECORD_SIZE;
        if (decryptedSize < requiredSize) return false;

        for (int i = 0; i < count; ++i)
        {
            short relType = 0;
            vec3_t pos, ang;
            float scale = 1.0f;
            std::memcpy(&relType, decrypted.data() + dataPtr, sizeof(short));
            dataPtr += sizeof(short);
            std::memcpy(pos, decrypted.data() + dataPtr, sizeof(vec3_t));
            dataPtr += sizeof(vec3_t);
            std::memcpy(ang, decrypted.data() + dataPtr, sizeof(vec3_t));
            dataPtr += sizeof(vec3_t);
            std::memcpy(&scale, decrypted.data() + dataPtr, sizeof(float));
            dataPtr += sizeof(float);

            // Ground-snap on import: the .obj records absolute Z values
            // measured against the source world's terrain (Aida's hills,
            // for instance). Our custom slot has its own heightmap — a
            // flat Z=0 plane for new maps — so the source's Z floats
            // above (or sinks below) our ground. Override with the
            // current terrain's height at this XY so trees plant
            // themselves on whatever map they get imported into.
            pos[2] = RequestTerrainHeight(pos[0], pos[1]);

            const int absoluteType = static_cast<int>(relType) + baseOffset;
            CreateObject(absoluteType, pos, ang, scale);
        }
        return true;
    }

    bool WriteBinary(const std::wstring& path,
                     const std::vector<BYTE>& bytes)
    {
        FILE* fp = _wfopen(path.c_str(), L"wb");
        if (fp == nullptr) return false;
        const size_t written =
            std::fwrite(bytes.data(), 1, bytes.size(), fp);
        std::fclose(fp);
        return written == bytes.size();
    }

    bool EnsureSlotDirectory(int mapId)
    {
        std::error_code ec;
        fs::create_directories(
            MuEditor::CustomMap::GetCustomMapDirectory(mapId), ec);
        return !ec;
    }

    // Force-evicts the world-tile bitmap slots. Needed before re-issuing
    // LoadBitmap with the same filename, because CGlobalBitmap::LoadImage
    // short-circuits when the slot already holds a bitmap with that exact
    // filename — it just bumps the refcount and never re-reads the file.
    // For reseed (same .OZJ path, new bytes on disk) we have to drop the
    // cached GL textures so the next LoadBitmap actually hits the disk.
    void EvictWorldTileBitmapSlots()
    {
        for (int i = 0; i < 30; ++i)
            DeleteBitmap(static_cast<GLuint>(BITMAP_MAPTILE) + i, /*bForce=*/true);
        for (int i = 0; i < 3; ++i)
            DeleteBitmap(static_cast<GLuint>(BITMAP_MAPGRASS) + i, /*bForce=*/true);
        DeleteBitmap(static_cast<GLuint>(BITMAP_LEAF1), /*bForce=*/true);
        DeleteBitmap(static_cast<GLuint>(BITMAP_LEAF2), /*bForce=*/true);
    }

    // Reloads the per-world tile bitmap slots from <relDir>\<file>. relDir
    // is engine-relative (LoadBitmap prepends "Data\\"). Without this step,
    // BITMAP_MAPTILE+0..N still hold whatever world's textures were loaded
    // last warp, so a freshly-loaded map's .map indices alias the previous
    // world's tiles — which is why a custom slot loaded from Devias shows
    // snow and the same slot loaded from Noria shows grass.
    void LoadWorldTextures(const std::wstring& relDir)
    {
        static_assert(
            sizeof(WORLD_TEXTURE_FILES) / sizeof(WORLD_TEXTURE_FILES[0]) >= 35,
            "tile bitmap slot table truncated — must match the engine warp");

        // Slot layout matches MapManager::OpenWorld (see comment block at
        // top of WORLD_TEXTURE_FILES). Anything LoadBitmap can't find on
        // disk just leaves the slot at its prior value, which is fine — a
        // missing ExtTile is normal on plenty of classic worlds.
        const int tileBase  = static_cast<int>(BITMAP_MAPTILE);
        const int grassBase = static_cast<int>(BITMAP_MAPGRASS);

        int idx = 0;
        // Slots BITMAP_MAPTILE + 0..29 — 30 base+ext tile bitmaps.
        for (int i = 0; i < 30; ++i, ++idx)
        {
            const std::wstring path = relDir + L"\\" + WORLD_TEXTURE_FILES[idx];
            LoadBitmap(path.c_str(), tileBase + i, GL_NEAREST, GL_REPEAT, false);
        }
        // Slots BITMAP_MAPGRASS + 0..2 — overlay grass tgas.
        for (int i = 0; i < 3; ++i, ++idx)
        {
            const std::wstring path = relDir + L"\\" + WORLD_TEXTURE_FILES[idx];
            LoadBitmap(path.c_str(), grassBase + i, GL_NEAREST, GL_REPEAT, false);
        }
        // BITMAP_LEAF1, BITMAP_LEAF2 — drifting-leaf particle textures.
        {
            const std::wstring leaf1 = relDir + L"\\" + WORLD_TEXTURE_FILES[idx++];
            LoadBitmap(leaf1.c_str(), BITMAP_LEAF1,
                       GL_NEAREST, GL_CLAMP_TO_EDGE, false);
        }
        {
            const std::wstring leaf2 = relDir + L"\\" + WORLD_TEXTURE_FILES[idx++];
            LoadBitmap(leaf2.c_str(), BITMAP_LEAF2,
                       GL_NEAREST, GL_CLAMP_TO_EDGE, false);
        }
    }

    // Translates a logical filename ("Tile.jpg" / "leaf01.tga") to the
    // extension the engine actually stores on disk. The bitmap loaders
    // (OpenJpegTurbo, OpenTga) ExchangeExt(".jpg" -> "OZJ") /
    // (".tga" -> "OZT") internally and read the encrypted variant; so
    // both the engine's calls and our copy step have to refer to OZJ/OZT
    // on disk, even though the logical lookup uses .jpg/.tga.
    std::wstring TranslateToDiskFilename(const wchar_t* logicalFile)
    {
        std::wstring s = logicalFile;
        const auto dot = s.find_last_of(L'.');
        if (dot == std::wstring::npos) return s;
        const std::wstring ext = s.substr(dot + 1);
        if (_wcsicmp(ext.c_str(), L"jpg") == 0)
            return s.substr(0, dot) + L".OZJ";
        if (_wcsicmp(ext.c_str(), L"tga") == 0)
            return s.substr(0, dot) + L".OZT";
        return s;
    }

    // Seeds a custom slot with a chosen classic world's tile bitmaps.
    // Each entry in WORLD_TEXTURE_FILES is a *logical* path
    // (".jpg"/".tga"); the bytes we copy come from the on-disk
    // encrypted twin (".OZJ"/".OZT"). Missing files on the source side
    // are skipped (some worlds don't ship every ExtTile slot).
    //
    // baseWorld is the 1-based World folder index (Data\World<n>\).
    // Falls back to World1 (Lorencia) when the requested directory
    // doesn't exist so a misconfigured manifest can't strand the slot.
    void CopyWorldAssetsFromTo(int baseWorld, const fs::path& dst)
    {
        std::error_code ec;
        wchar_t srcDir[32];
        std::swprintf(srcDir, std::size(srcDir),
            L"Data\\World%d", baseWorld);
        fs::path src = srcDir;
        if (!fs::exists(src, ec))
            src = BASE_WORLD_TEXTURE_DIR;   // fallback

        for (const wchar_t* file : WORLD_TEXTURE_FILES)
        {
            const std::wstring diskName = TranslateToDiskFilename(file);
            const fs::path srcFile = src / diskName;
            if (!fs::exists(srcFile, ec)) continue;
            fs::copy_file(srcFile, dst / diskName,
                          fs::copy_options::overwrite_existing, ec);
        }
    }

    // Convenience wrapper for the default Create path.
    void CopyDefaultWorldAssets(int mapId, int baseWorld)
    {
        CopyWorldAssetsFromTo(baseWorld,
            MuEditor::CustomMap::GetCustomMapDirectory(mapId));
    }

    // Editor-safe replacement for OpenTerrainAttribute. The classic loader
    // ([ZzzLodTerrain.cpp:181-211]) runs a per-world sentinel-tile check
    // keyed off gMapManager.WorldActive — e.g. Lorencia demands
    // TerrainWall[123*256+135] == 5 — and on mismatch calls ExitProgram()
    // which shows GlobalText[11] ("Data error") and PostQuitMessage().
    // That sentinel is designed to detect tampered shipping assets; it has
    // no business firing on a slot the editor knows is a custom map. This
    // does the same decrypt/BuxConvert/memcpy pipeline as the classic
    // loader, minus the validation that's hostile to authoring.
    bool LoadAttBufferRaw(const std::wstring& path)
    {
        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (fp == nullptr) return false;

        std::fseek(fp, 0, SEEK_END);
        const long fileSize = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (fileSize <= TERRAIN_HEADER_SIZE)
        {
            std::fclose(fp);
            return false;
        }

        std::vector<BYTE> encrypted(fileSize);
        const size_t readBytes =
            std::fread(encrypted.data(), 1, fileSize, fp);
        std::fclose(fp);
        if (static_cast<long>(readBytes) != fileSize) return false;

        const int decryptedSize =
            MapFileDecrypt(nullptr, encrypted.data(), fileSize);
        std::vector<BYTE> decrypted(decryptedSize);
        MapFileDecrypt(decrypted.data(), encrypted.data(), fileSize);
        BuxConvert(decrypted.data(), decryptedSize);

        // Header + payload sanity. Two accepted forms: 8-bit legacy or
        // 16-bit "extAtt". We only emit the 16-bit form in CreateNewCustomMap,
        // but accept either when reading so hand-edited slots still load.
        const int payloadSize = decryptedSize - TERRAIN_HEADER_SIZE;
        const bool isExtAtt = (payloadSize == TERRAIN_PAYLOAD_SIZE);
        const bool isLegacy = (payloadSize == TERRAIN_TILE_COUNT);
        if (!isExtAtt && !isLegacy) return false;

        const BYTE* payload = decrypted.data() + TERRAIN_HEADER_SIZE;
        if (isExtAtt)
        {
            std::memcpy(TerrainWall, payload, TERRAIN_PAYLOAD_SIZE);
        }
        else
        {
            for (int i = 0; i < TERRAIN_TILE_COUNT; ++i)
                TerrainWall[i] = payload[i];
        }
        return true;
    }

    // Hides every static object in the spatial-hash so a subsequent
    // OpenObjectsEnc effectively shows only the new map's objects.
    //
    // We deliberately do NOT free the OBJECT* nodes (the obvious
    // implementation would walk and DeleteObject each Head). The engine's
    // effect/joint/buff systems cache `OBJECT* Owner` pointers into
    // ObjectBlock entries (see ZzzEffect.cpp uses of `o->Owner`) and have
    // no global "world changed, drop your refs" callback. Freeing the
    // backing memory leaves those caches with dangling pointers that
    // segfault on the next frame the moment the player interacts with
    // anything (click-to-move, attack, etc.).
    //
    // Setting Live = false makes the renderer + every iteration path skip
    // the node (they all gate on o->Live before dereferencing), while
    // keeping the memory valid for any external pointer holders. Memory
    // accretes across loads, but each slot tops out at a few hundred
    // objects and a session does this a handful of times — fine.
    void MarkAllObjectsDead()
    {
        for (int b = 0; b < OBJECT_BLOCK_COUNT; ++b)
        {
            for (OBJECT* o = ObjectBlock[b].Head; o != nullptr; o = o->Next)
            {
                o->Live = false;
            }
        }
    }

    // Hides every CharactersClient entry except the Hero. The server's
    // per-world NPCs, monsters, decorative entities, and other players
    // live here (not in ObjectBlock), so MarkAllObjectsDead alone leaves
    // them visible. Marking the OBJECT slot dead drops them from the
    // render path; the server is still authoritative over them and may
    // re-broadcast spawn packets at any time, which would bring fresh
    // entries back live — that's intentional and matches the "offline
    // authoring is a visual snapshot" framing of editor mode.
    //
    // Same caveat as MarkAllObjectsDead: we do NOT free anything, because
    // the buff/effect/joint subsystems hold raw OBJECT* pointers into
    // CharactersClient entries and have no callback to drop them on a
    // world swap. Live=false is safe; SAFE_DELETE is not.
    void MarkAllNonHeroCharactersDead()
    {
        for (int i = 0; i < MAX_CHARACTERS_CLIENT; ++i)
        {
            CHARACTER* c = &CharactersClient[i];
            if (c == Hero) continue;
            if (c->Object.Live) c->Object.Live = false;
        }
    }

    // The classic loaders take non-const wchar_t* — wrap them.
    bool InvokeClassicLoaders(const std::wstring& attPath,
                              const std::wstring& objPath)
    {
        std::error_code ec;
        if (!fs::exists(attPath, ec)) return false;
        if (!fs::exists(objPath, ec)) return false;

        MarkAllObjectsDead();
        MarkAllNonHeroCharactersDead();

        // Use our raw att reader rather than OpenTerrainAttribute. The
        // classic loader's sentinel-tile check is gated on the *live*
        // gMapManager.WorldActive, not the world whose .att is being read.
        // After any swap-without-warp (which is what the editor does),
        // WorldActive and the file disagree, and the loader fires
        // ExitProgram() with GlobalText[11] = "Data error". This is the
        // same fix that landed in LoadCustomMap; classic-map loads were
        // still hitting the legacy path.
        std::wstring objMut = objPath;
        if (!LoadAttBufferRaw(attPath))        return false;
        if (OpenObjectsEnc(objMut.data()) < 0) return false;
        return true;
    }

    // Path under Data\, relative to the game root — what the height/light
    // loaders expect to be handed (they prepend "Data\\" and rewrite the
    // extension to OZB/OZJ themselves).
    std::wstring BuildCustomRelativeDir(int mapId)
    {
        return std::wstring(L"World\\Custom\\") +
               FormatIndexed(CUSTOM_FOLDER_PREFIX, mapId + 1);
    }

    // OpenTerrainHeight builds its disk path as
    //   "Data\\" + <input-up-to-first-dot> + "OZB"
    // so we pass a relative ".bmp"-suffixed arg that resolves to the OZB
    // returned by GetCustomMapHeightPath.
    std::wstring BuildHeightLoaderArg(int mapId)
    {
        return BuildCustomRelativeDir(mapId) + L"\\TerrainHeight.bmp";
    }

    // OpenJpegBuffer (called by OpenTerrainLight) does the same trick but
    // appends ".OZJ" — so this arg resolves to GetCustomMapLightPath.
    std::wstring BuildLightLoaderArg(int mapId)
    {
        return BuildCustomRelativeDir(mapId) + L"\\TerrainLight.jpg";
    }

    // Cleartext for a blank .map: header bytes set, Layer1/Layer2/Alpha all zero.
    // Layer-id 0 -> tiles use the first texture in the active world's bank
    // (which is whatever WorldActive was when the map gets loaded).
    std::vector<BYTE> BuildBlankMapFileCleartext(int mapId)
    {
        std::vector<BYTE> buf(MAPPING_FILE_SIZE, 0);
        buf[0] = MAPPING_FILE_VERSION;
        buf[1] = FileMapNumber(mapId);
        return buf;
    }

    // Blank OZB: full file zero-initialized. The 1080-byte BMP header is
    // read by OpenTerrainHeight but only round-tripped (memcpy'd into a
    // global so SaveTerrainHeight can write it back unchanged) — its
    // contents do not affect height parsing. Height byte 0 -> Z = 0.
    std::vector<BYTE> BuildBlankHeightOZB()
    {
        return std::vector<BYTE>(HEIGHT_OZB_FILE_SIZE, 0);
    }

    bool WriteBlankMapFile(int mapId)
    {
        std::vector<BYTE> cleartext = BuildBlankMapFileCleartext(mapId);
        std::vector<BYTE> encrypted(cleartext.size());
        MapFileEncrypt(encrypted.data(), cleartext.data(),
                       static_cast<int>(cleartext.size()));
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapMapPath(mapId), encrypted);
    }

    // Snapshots the live texture-mapping buffers into the .map
    // cleartext format used by OpenTerrainMapping. Mirror image of
    // BuildBlankMapFileCleartext — header is the same; payload comes
    // from TerrainMappingLayer1/2/Alpha[]. Alpha gets requantized from
    // its in-memory float (0..1) back to the on-disk BYTE (0..255).
    std::vector<BYTE> BuildLiveMapFileCleartext(int mapId)
    {
        std::vector<BYTE> buf(MAPPING_FILE_SIZE, 0);
        buf[0] = MAPPING_FILE_VERSION;
        buf[1] = FileMapNumber(mapId);

        BYTE* p = buf.data() + MAPPING_HEADER_SIZE;
        std::memcpy(p, TerrainMappingLayer1, MAPPING_LAYER_BYTES);
        p += MAPPING_LAYER_BYTES;
        std::memcpy(p, TerrainMappingLayer2, MAPPING_LAYER_BYTES);
        p += MAPPING_LAYER_BYTES;

        for (int i = 0; i < TERRAIN_TILE_COUNT; ++i)
        {
            float a = TerrainMappingAlpha[i];
            if (a < 0.f) a = 0.f;
            else if (a > 1.f) a = 1.f;
            p[i] = static_cast<BYTE>(a * 255.f);
        }
        return buf;
    }

    bool WriteLiveMapFile(int mapId)
    {
        std::vector<BYTE> cleartext = BuildLiveMapFileCleartext(mapId);
        std::vector<BYTE> encrypted(cleartext.size());
        MapFileEncrypt(encrypted.data(), cleartext.data(),
                       static_cast<int>(cleartext.size()));
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapMapPath(mapId), encrypted);
    }

    // Encodes live BackTerrainHeight into the legacy OZB byte layout:
    //   4-byte prefix (zero, loader fseeks past it),
    //   1080-byte BMP-like header (zero, loader only memcpys for round-trip),
    //   256*256 height bytes (Z / TERRAIN_HEIGHT_DECODE_FACTOR, clamped).
    // The decode factor matches OpenTerrainHeight for non-login worlds
    // (login uses 3.0; everything else 1.5).
    // Per-tile grass mask file. 256*256 raw bytes; 0xFF = grass on,
    // 0x00 = grass off. No header, no encryption — purely an editor
    // artifact (the engine's InitTerrainMappingLayer fills the buffer
    // with 0xFF defaults on every OpenTerrainMapping; we override
    // after that with the persisted mask in LoadCustomMap).
    bool WriteLiveGrassDensity(int mapId)
    {
        // memcpy the engine buffer straight into the file — same shape
        // and semantics.
        std::vector<BYTE> buf(TERRAIN_TILE_COUNT, 0);
        std::memcpy(buf.data(), TerrainGrassMask, TERRAIN_TILE_COUNT);
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapGrassPath(mapId), buf);
    }

    bool LoadLiveGrassDensity(int mapId)
    {
        const std::wstring path =
            MuEditor::CustomMap::GetCustomMapGrassPath(mapId);
        std::error_code ec;
        if (!fs::exists(path, ec)) return false;

        FILE* fp = _wfopen(path.c_str(), L"rb");
        if (fp == nullptr) return false;

        std::vector<BYTE> buf(TERRAIN_TILE_COUNT, 0);
        const size_t read =
            std::fread(buf.data(), 1, TERRAIN_TILE_COUNT, fp);
        std::fclose(fp);
        if (read != TERRAIN_TILE_COUNT) return false;

        // Backward compat: existing .grass files written before this
        // semantic change contain density-quantized bytes (0..255 from
        // a 0..1 float). Any non-zero is "on" — matches the engine's
        // gate (`!= 0`), so old files load cleanly.
        std::memcpy(TerrainGrassMask, buf.data(), TERRAIN_TILE_COUNT);
        return true;
    }

    bool WriteLiveHeightOZB(int mapId)
    {
        constexpr float TERRAIN_HEIGHT_DECODE_FACTOR = 1.5f;
        std::vector<BYTE> buf(HEIGHT_OZB_FILE_SIZE, 0);
        BYTE* heights =
            buf.data() + HEIGHT_OZB_PREFIX_SIZE + HEIGHT_OZB_HEADER_SIZE;
        for (int i = 0; i < TERRAIN_TILE_COUNT; ++i)
        {
            float h = BackTerrainHeight[i] / TERRAIN_HEIGHT_DECODE_FACTOR;
            if (h < 0.f)        h = 0.f;
            else if (h > 255.f) h = 255.f;
            heights[i] = static_cast<BYTE>(h);
        }
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapHeightPath(mapId), buf);
    }

    bool WriteBlankHeightOZB(int mapId)
    {
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapHeightPath(mapId),
            BuildBlankHeightOZB());
    }

    // OZJ wire format: { 24 bytes prefix, JPEG bytes }. The classic
    // OpenJpegBuffer fseek()s past the prefix and feeds the rest to
    // turbojpeg, so the prefix content is unread — zero-fill is fine.
    bool WriteBlankTerrainLight(int mapId)
    {
        std::vector<unsigned char> rgb(
            TERRAIN_TILE_COUNT * 3, LIGHT_OZJ_FILL);

        tjhandle handle = tjInitCompress();
        if (handle == nullptr) return false;

        const unsigned long maxJpegSize =
            tjBufSize(TERRAIN_SIZE, TERRAIN_SIZE, TJSAMP_444);
        std::vector<unsigned char> jpegBuf(maxJpegSize);
        unsigned char* jpegPtr = jpegBuf.data();
        unsigned long  jpegSize = maxJpegSize;
        const int flags = TJFLAG_BOTTOMUP | TJFLAG_NOREALLOC;

        const int rc = tjCompress2(
            handle, rgb.data(),
            TERRAIN_SIZE, /*pitch=*/0, TERRAIN_SIZE,
            TJPF_RGB, &jpegPtr, &jpegSize,
            TJSAMP_444, LIGHT_OZJ_QUALITY, flags);
        tjDestroy(handle);
        if (rc != 0) return false;

        std::vector<BYTE> ozj(LIGHT_OZJ_PREFIX_SIZE + jpegSize, 0);
        std::memcpy(ozj.data() + LIGHT_OZJ_PREFIX_SIZE,
                    jpegPtr, jpegSize);
        return WriteBinary(
            MuEditor::CustomMap::GetCustomMapLightPath(mapId), ozj);
    }

    // Parses "World123" -> 123. Returns -1 on any mismatch.
    int ParseWorldFolderNumber(const std::wstring& folderName)
    {
        const std::wstring prefix(CUSTOM_FOLDER_PREFIX);
        if (folderName.size() <= prefix.size()) return -1;
        if (folderName.compare(0, prefix.size(), prefix) != 0) return -1;
        try
        {
            size_t consumed = 0;
            const int n = std::stoi(folderName.substr(prefix.size()), &consumed);
            const bool wholeRemainderParsed =
                consumed == folderName.size() - prefix.size();
            return wholeRemainderParsed ? n : -1;
        }
        catch (...) { return -1; }
    }
}

namespace MuEditor::CustomMap
{
    std::wstring GetCustomRootDirectory()
    {
        return std::wstring(CUSTOM_ROOT);
    }

    std::wstring GetCustomMapDirectory(int mapId)
    {
        return GetCustomRootDirectory() + L"\\" +
               FormatIndexed(CUSTOM_FOLDER_PREFIX, mapId + 1);
    }

    std::wstring GetCustomMapAttPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" +
               FormatIndexed(CUSTOM_FILE_PREFIX, mapId + 1) +
               TERRAIN_ATT_EXT;
    }

    std::wstring GetCustomMapObjPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" +
               FormatIndexed(CUSTOM_FILE_PREFIX, mapId + 1) +
               TERRAIN_OBJ_EXT;
    }

    std::wstring GetCustomMapMapPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" +
               FormatIndexed(CUSTOM_FILE_PREFIX, mapId + 1) +
               TERRAIN_MAP_EXT;
    }

    std::wstring GetCustomMapHeightPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" + HEIGHT_FILE_NAME;
    }

    std::wstring GetCustomMapLightPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" + LIGHT_FILE_NAME;
    }

    std::wstring GetCustomMapGrassPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" +
               FormatIndexed(CUSTOM_FILE_PREFIX, mapId + 1) +
               GRASS_FILE_EXT;
    }

    std::wstring GetCustomMapManifestPath(int mapId)
    {
        return GetCustomMapDirectory(mapId) + L"\\" + MANIFEST_FILE_NAME;
    }

    std::wstring GetCustomMapSourceDir(int mapId, int sourceFolderIndex)
    {
        wchar_t suffix[32];
        std::swprintf(suffix, std::size(suffix), L"%d", sourceFolderIndex);
        return GetCustomMapDirectory(mapId) + L"\\" + SOURCE_DIR_PREFIX + suffix;
    }

    std::wstring GetCustomMapSourceObjPath(int mapId, int sourceFolderIndex)
    {
        return GetCustomMapSourceDir(mapId, sourceFolderIndex) + L"\\" +
               FormatIndexed(CUSTOM_FILE_PREFIX, mapId + 1) + TERRAIN_OBJ_EXT;
    }

    bool CreateNewCustomMap(int mapId, int baseWorld)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;
        if (baseWorld < 1) baseWorld = 1;
        if (!EnsureSlotDirectory(mapId))
            return false;

        // Blank attribute grid with a walled border.
        std::vector<WORD> tiles(TERRAIN_TILE_COUNT, 0);
        StampBorderAttribute(tiles.data(),
                             CUSTOM_MAP_BORDER_ATTR,
                             CUSTOM_MAP_BORDER_THICKNESS);

        std::vector<BYTE> attEncrypted =
            EncryptAttStream(BuildAttCleartext(mapId, tiles.data()));
        if (!WriteBinary(GetCustomMapAttPath(mapId), attEncrypted))
            return false;

        // Empty object list: header only, count=0 (already zero-filled).
        std::vector<BYTE> objCleartext(OBJECT_HEADER_SIZE, 0);
        objCleartext[0] = OBJECT_FILE_VERSION;
        objCleartext[1] = FileMapNumber(mapId);
        std::vector<BYTE> objEncrypted = EncryptObjStream(objCleartext);
        if (!WriteBinary(GetCustomMapObjPath(mapId), objEncrypted))
            return false;

        // Blank companion assets so the slot is fully self-contained — the
        // terrain mesh, textures, and lighting otherwise inherit from
        // whichever world was active when the slot is loaded.
        if (!WriteBlankMapFile(mapId))      return false;
        if (!WriteBlankHeightOZB(mapId))    return false;
        if (!WriteBlankTerrainLight(mapId)) return false;

        // Seed the slot with tile bitmaps from a base classic world so the
        // ground textures don't inherit from the world the editor happens
        // to be standing in when the slot is loaded. See WORLD_TEXTURE_FILES
        // comment for the slot layout.
        CopyDefaultWorldAssets(mapId, baseWorld);

        // Manifest: no side-banks yet, but record the base world so
        // future reseed / lazy-restore knows where to pull from.
        SourceManifest m;
        m.baseWorld = baseWorld;
        WriteSourceManifest(GetCustomMapManifestPath(mapId), m);

        // Default grass mask = 0xFF everywhere (engine default —
        // tufts render on every tile until the user erases). The
        // global "Grass overlay (tufts)" toggle in Display options can
        // suppress the whole pass if the user prefers a bare slot.
        std::vector<BYTE> blankGrass(TERRAIN_TILE_COUNT, 0xFF);
        WriteBinary(GetCustomMapGrassPath(mapId), blankGrass);

        return true;
    }

    bool WriteTerrainLightRGB(int mapId, const unsigned char* rgb256)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;
        if (rgb256 == nullptr) return false;
        if (!EnsureSlotDirectory(mapId)) return false;

        tjhandle handle = tjInitCompress();
        if (handle == nullptr) return false;

        const unsigned long maxJpegSize =
            tjBufSize(TERRAIN_SIZE, TERRAIN_SIZE, TJSAMP_444);
        std::vector<unsigned char> jpegBuf(maxJpegSize);
        unsigned char* jpegPtr = jpegBuf.data();
        unsigned long  jpegSize = maxJpegSize;
        // BOTTOMUP because the engine's OpenJpegBuffer reads the
        // decoded buffer into TerrainLight[] starting from the bottom
        // row (matches BMP/TGA conventions). NOREALLOC keeps jpegBuf
        // owning the storage instead of turbojpeg reallocating it.
        const int flags = TJFLAG_BOTTOMUP | TJFLAG_NOREALLOC;
        const int rc = tjCompress2(
            handle, rgb256,
            TERRAIN_SIZE, /*pitch=*/0, TERRAIN_SIZE,
            TJPF_RGB, &jpegPtr, &jpegSize,
            TJSAMP_444, LIGHT_OZJ_QUALITY, flags);
        tjDestroy(handle);
        if (rc != 0) return false;

        std::vector<BYTE> ozj(LIGHT_OZJ_PREFIX_SIZE + jpegSize, 0);
        std::memcpy(ozj.data() + LIGHT_OZJ_PREFIX_SIZE, jpegPtr, jpegSize);
        return WriteBinary(GetCustomMapLightPath(mapId), ozj);
    }

    void ReloadTerrainLightFromSlot(int mapId)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX) return;
        std::error_code ec;
        if (!fs::exists(GetCustomMapLightPath(mapId), ec)) return;
        std::wstring lightArg = BuildLightLoaderArg(mapId);
        OpenTerrainLight(lightArg.data());
    }

    void ApplyWeatherAssets(unsigned int flags)
    {
        // Priority order for LEAF1/LEAF2 texture pick — snow wins over
        // leaves (snowflakes are visually distinctive; mixing weathers
        // already loses fidelity since we only have two GL slots).
        // Heaven rain uses BITMAP_RAIN, loaded globally at startup
        // from World1\rain01.tga, so no swap needed.
        //
        // All sources load the `.tga` (OZT) variant — every world we
        // care about ships an alpha-correct OZT, and using alpha
        // textures lets the default alpha-test render path work
        // uniformly (no forced additive blend needed, which would
        // turn TGAs with white-RGB-in-transparent-pixels into white
        // squares — the bug Noria's leaf01.OZT hits otherwise).
        struct LeafSource { unsigned int flag; int worldFolder; };
        const LeafSource kSources[] = {
            { CW_DEVIAS_SNOW,     3  }, // Data\World3\leaf01.OZT  (snowflake)
            { CW_RAKLION_SNOW,    58 }, // Data\World58\leaf01.OZT (icy snow)
            { CW_ATLANS_LEAVES,   4  }, // Data\World4\leaf01.OZT  (Noria leaf)
            { CW_LORENCIA_LEAVES, 1  }, // Data\World1\leaf01.OZT  (Lorencia leaf)
        };
        int picked = -1;
        for (int i = 0; i < static_cast<int>(std::size(kSources)); ++i)
        {
            if (flags & kSources[i].flag) { picked = i; break; }
        }

        if (picked >= 0)
        {
            // Drop the cached BITMAP_LEAF1/LEAF2 (loaded from the slot
            // folder by LoadWorldTextures) so the next LoadBitmap with
            // the new filename isn't short-circuited by the slot's old
            // path being the same. DeleteBitmap(force=true) tears it.
            DeleteBitmap(static_cast<GLuint>(BITMAP_LEAF1), /*bForce=*/true);
            DeleteBitmap(static_cast<GLuint>(BITMAP_LEAF2), /*bForce=*/true);

            wchar_t leaf1[64];
            wchar_t leaf2[64];
            std::swprintf(leaf1, std::size(leaf1),
                          L"World%d\\leaf01.tga",
                          kSources[picked].worldFolder);
            // leaf02 only ships as .jpg (OZJ) — no OZT variant exists.
            // The classic CreateDeviasSnow / CreateLorenciaLeaf paths
            // pick LEAF2 ~10% of the time; with alpha-test that 10%
            // renders as a fully-opaque square. Tolerable since rare,
            // and matches vanilla behavior for leaf02-using worlds.
            std::swprintf(leaf2, std::size(leaf2),
                          L"World%d\\leaf02.jpg",
                          kSources[picked].worldFolder);
            LoadBitmap(leaf1, BITMAP_LEAF1, GL_NEAREST, GL_CLAMP_TO_EDGE, false);
            LoadBitmap(leaf2, BITMAP_LEAF2, GL_NEAREST, GL_CLAMP_TO_EDGE, false);
        }

        // BMD models for boid flocks. MapManager::Load only triggers
        // these AccessModel calls when the engine warps into a matching
        // WorldActive — custom slots run in WD_0LORENCIA so birds work
        // automatically but bats and butterflies don't. Side-load them
        // here so the corresponding flags can spawn live boids.
        if (flags & CW_DUNGEON_BATS)
        {
            gLoadData.AccessModel(MODEL_BAT01, L"Data\\Object2\\", L"Bat", 1);
            gLoadData.OpenTexture(MODEL_BAT01, L"Object2\\");
        }
        if (flags & CW_NORIA_BUTTERFLIES)
        {
            gLoadData.AccessModel(MODEL_BUTTERFLY01, L"Data\\Object1\\", L"Butterfly", 1);
            gLoadData.OpenTexture(MODEL_BUTTERFLY01, L"Object1\\");
        }
        if (flags & CW_LORENCIA_BIRDS)
        {
            // Birds normally load via the WD_0LORENCIA branch (which we
            // do hit because of the WorldActive override), but reissuing
            // is cheap and protects against load orders where the flag
            // gets toggled live before MapManager::Load runs.
            gLoadData.AccessModel(MODEL_BIRD01, L"Data\\Object1\\", L"Bird", 1);
            gLoadData.OpenTexture(MODEL_BIRD01, L"Object1\\");
        }
    }

    unsigned int ReadWeatherFlags(int mapId)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return 0;
        return ReadSourceManifest(GetCustomMapManifestPath(mapId)).weatherFlags;
    }

    bool WriteWeatherFlags(int mapId, unsigned int flags)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;
        if (!EnsureSlotDirectory(mapId))
            return false;

        const std::wstring manifestPath = GetCustomMapManifestPath(mapId);
        SourceManifest m = ReadSourceManifest(manifestPath);
        m.weatherFlags = flags;
        return WriteSourceManifest(manifestPath, m);
    }

    bool ReseedTileTexturesFromWorld(int mapId, int baseWorld)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;
        if (baseWorld < 1) return false;
        if (!EnsureSlotDirectory(mapId)) return false;

        CopyDefaultWorldAssets(mapId, baseWorld);

        // Persist the choice so reloads use the new base from now on.
        const std::wstring manifestPath = GetCustomMapManifestPath(mapId);
        SourceManifest m = ReadSourceManifest(manifestPath);
        m.baseWorld = baseWorld;
        if (!WriteSourceManifest(manifestPath, m))
            return false;

        // Apply the new tile bytes to the live GL slots immediately so the
        // user doesn't have to teleport out and back to see the change.
        // CGlobalBitmap caches by filename — if we skipped the eviction the
        // next LoadBitmap would see the same .OZJ path and short-circuit.
        EvictWorldTileBitmapSlots();
        LoadWorldTextures(BuildCustomRelativeDir(mapId));
        return true;
    }

    bool SaveCustomMap(int mapId)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;
        if (!EnsureSlotDirectory(mapId))
            return false;

        std::vector<BYTE> attEncrypted =
            EncryptAttStream(BuildAttCleartext(mapId, TerrainWall));
        if (!WriteBinary(GetCustomMapAttPath(mapId), attEncrypted))
            return false;

        // Texture mapping (.map): persists the live painter state from
        // TerrainMappingLayer1/2/Alpha. Without this, painted textures
        // get wiped on the next Load because the on-disk .map still
        // carries whatever was last written (initially WriteBlankMapFile).
        if (!WriteLiveMapFile(mapId)) return false;

        // Heightmap (TerrainHeight.OZB): persists the live sculptor
        // state from BackTerrainHeight. Without this, raised/lowered
        // terrain reverts to whatever was on disk (initially the
        // all-zero flat OZB written by WriteBlankHeightOZB).
        if (!WriteLiveHeightOZB(mapId)) return false;

        // Grass density (.grass): persists per-tile TerrainGrassTexture
        // from the editor's grass painter. The engine never writes
        // this back; it just rolls a fresh rand()%4/4 every map load,
        // so without our save it'd be lost across sessions.
        WriteLiveGrassDensity(mapId);

        // Partition live ObjectBlock into the main stream + one stream
        // per source-world bank that currently owns at least one live
        // object. Each per-source stream stores source-relative Types.
        PartitionedObjStreams parts = BuildPartitionedObjStreams(mapId);

        std::vector<BYTE> mainEncrypted = EncryptObjStream(parts.mainStream);
        if (!WriteBinary(GetCustomMapObjPath(mapId), mainEncrypted))
            return false;

        std::error_code ec;
        std::vector<int> activeSources;
        for (auto& [sourceWorld, stream] : parts.bySource)
        {
            const std::wstring sourceDir =
                GetCustomMapSourceDir(mapId, sourceWorld);
            fs::create_directories(sourceDir, ec);
            if (ec) return false;

            std::vector<BYTE> sourceEncrypted = EncryptObjStream(stream);
            if (!WriteBinary(GetCustomMapSourceObjPath(mapId, sourceWorld),
                             sourceEncrypted)) return false;
            activeSources.push_back(sourceWorld);
        }

        // Manifest reflects the banks that actually carry live objects.
        // Banks that were side-loaded but emptied (every object deleted)
        // drop off the next save's manifest, which is the right semantics:
        // the slot only declares dependencies on what it actually uses.
        // baseWorld is preserved from the existing manifest so saving
        // doesn't reset the texture-seed choice the user made.
        std::sort(activeSources.begin(), activeSources.end());
        const std::wstring manifestPath = GetCustomMapManifestPath(mapId);
        SourceManifest m = ReadSourceManifest(manifestPath);
        m.sources = activeSources;
        if (!WriteSourceManifest(manifestPath, m))
            return false;

        return true;
    }

    bool LoadCustomMap(int mapId)
    {
        if (mapId < CUSTOM_MAP_ID_MIN || mapId > CUSTOM_MAP_ID_MAX)
            return false;

        const std::wstring attPath    = GetCustomMapAttPath(mapId);
        const std::wstring objPath    = GetCustomMapObjPath(mapId);
        const std::wstring mapPath    = GetCustomMapMapPath(mapId);
        const std::wstring heightPath = GetCustomMapHeightPath(mapId);
        const std::wstring lightPath  = GetCustomMapLightPath(mapId);

        std::error_code ec;
        if (!fs::exists(attPath,    ec)) return false;
        if (!fs::exists(objPath,    ec)) return false;
        if (!fs::exists(mapPath,    ec)) return false;
        if (!fs::exists(heightPath, ec)) return false;
        if (!fs::exists(lightPath,  ec)) return false;

        MarkAllObjectsDead();
        MarkAllNonHeroCharactersDead();
        // Drop any in-flight Leaves[]/Boids[] left over from the previous
        // map. Without this, e.g. teleporting from Lorencia into a custom
        // slot still shows the previous map's birds drifting on screen
        // until they hit the despawn radius. The new map's weather flags
        // get applied below, so live particles repopulate as expected.
        MuEditor::CustomMap::ClearLiveWeatherParticles();

        // The classic loaders all take non-const wchar_t*. (att uses our
        // own raw loader, see below — it takes a const path directly.)
        std::wstring objMut    = objPath;
        std::wstring mapMut    = mapPath;
        // OpenTerrainHeight / OpenJpegBuffer build their own paths from a
        // relative dotted-stub argument; the actual files on disk live at
        // GetCustomMapHeightPath / GetCustomMapLightPath.
        std::wstring heightArg = BuildHeightLoaderArg(mapId);
        std::wstring lightArg  = BuildLightLoaderArg(mapId);

        // Attributes -> TerrainWall. Uses our raw loader (no sentinel
        // check) so a blank custom slot doesn't trip the classic loader's
        // per-world tile validation and tear down the app.
        if (!LoadAttBufferRaw(attPath))        return false;
        if (OpenObjectsEnc(objMut.data()) < 0) return false;

        // Texture mapping layers -> TerrainMappingLayer1/2/Alpha. The loader
        // calls InitTerrainMappingLayer() internally before reading. That
        // init also re-rolls TerrainGrassTexture[] with rand()%4/4 — so
        // overriding with the persisted grass density has to happen AFTER
        // this call (see LoadLiveGrassDensity below).
        if (OpenTerrainMapping(mapMut.data())   < 0) return false;

        // Restore per-tile grass density painted in a previous session.
        // Quiet no-op when the file isn't present (older slots).
        LoadLiveGrassDensity(mapId);

        // Heightmap -> BackTerrainHeight. `false` = legacy 8-bit OZB path
        // (what we write in CreateNewCustomMap; not the 24-bit RGB variant
        // used by a few specialized worlds).
        CreateTerrain(heightArg.data(), /*bNew=*/false);

        // Lighting JPEG -> TerrainLight[]. OpenTerrainLight already chains
        // CreateTerrainNormal() + CreateTerrainLight() at the end, so the
        // renderer's normals + per-vertex shading get rebuilt off the new
        // heights and the freshly-loaded light buffer.
        OpenTerrainLight(lightArg.data());

        // Read manifest once — its baseWorld field tells the lazy
        // re-seed and the side-bank loop both where to pull from.
        const std::wstring manifestPath = GetCustomMapManifestPath(mapId);
        const SourceManifest manifest = ReadSourceManifest(manifestPath);

        // Slots created before the texture-copy step landed don't ship
        // their own .OZJ/.OZT tiles. Detect that and run the copy now
        // so older slots still come up correctly without the user
        // having to recreate them. Use the manifest's baseWorld so a
        // slot that originally seeded from Tarkan doesn't quietly get
        // re-seeded with Lorencia bitmaps.
        std::error_code ec2;
        const fs::path probe =
            fs::path(GetCustomMapDirectory(mapId)) / L"TileGrass01.OZJ";
        if (!fs::exists(probe, ec2))
        {
            CopyDefaultWorldAssets(mapId, manifest.baseWorld);
        }

        // Reload tile bitmaps from the slot's own folder so the ground
        // textures match the .map indices, regardless of which world the
        // editor was standing in when the slot was loaded.
        LoadWorldTextures(BuildCustomRelativeDir(mapId));

        // Override the live WorldActive to a neutral value so all the
        // per-world client behaviors (Atlans swim, Devias snow, Hellas
        // water, Cursed Temple sprites, etc.) stop firing on our custom
        // map. Lorencia (WD_0LORENCIA) is the lightest neutral choice —
        // a basic continent the codebase treats as the default fallback.
        // This is purely a client-side override; the server's notion of
        // where we are is unchanged (and irrelevant in offline mode).
        gMapManager.WorldActive = WD_0LORENCIA;

        // Activate the slot's opt-in weather flags. Engine spawn/render
        // checks consult these via HasWeatherFlag() and short-circuit
        // their WorldActive comparison while a custom map is active —
        // so an empty flag set yields no weather at all (which is the
        // expected default for "blank" slots).
        MuEditor::CustomMap::SetActiveCustomWeather(manifest.weatherFlags);

        // Load weather-specific assets: per-flag leaf/snow bitmaps into
        // BITMAP_LEAF1/LEAF2 (overwriting the slot-folder copies that
        // LoadWorldTextures just loaded), plus BMD side-loads for
        // boid flocks the WorldActive override would otherwise miss.
        ApplyWeatherAssets(manifest.weatherFlags);

        // Side-load source-world object banks the slot depends on, then
        // load each per-source .obj with Type fixup. Done AFTER the main
        // .obj loaded above — order doesn't actually matter (each goes
        // into its own ObjectBlock slot), but doing per-source last keeps
        // the iteration order predictable for debugging.
        for (int sourceWorld : manifest.sources)
        {
            const int baseOffset =
                MuEditor::CustomMap::LoadSourceBank(sourceWorld);
            if (baseOffset < 0)
            {
                // Couldn't side-load (out of bank slots or missing dir).
                // Skip this source's .obj — its objects would render as
                // garbage without the bank loaded.
                continue;
            }
            LoadSourceObjStream(
                GetCustomMapSourceObjPath(mapId, sourceWorld),
                baseOffset);
        }

        return true;
    }

    bool LoadClassicMap(int worldFolderIndex)
    {
        constexpr int MIN_WORLD_FOLDER = 1;
        constexpr int MAX_WORLD_FOLDER = 255;
        if (worldFolderIndex < MIN_WORLD_FOLDER ||
            worldFolderIndex > MAX_WORLD_FOLDER) return false;

        wchar_t attBuf[64];
        wchar_t objBuf[64];
        wchar_t mapBuf[64];
        std::swprintf(attBuf, std::size(attBuf),
            L"Data\\World%d\\EncTerrain%d.att",
            worldFolderIndex, worldFolderIndex);
        std::swprintf(objBuf, std::size(objBuf),
            L"Data\\World%d\\EncTerrain%d.obj",
            worldFolderIndex, worldFolderIndex);
        std::swprintf(mapBuf, std::size(mapBuf),
            L"Data\\World%d\\EncTerrain%d.map",
            worldFolderIndex, worldFolderIndex);

        // Switch the engine back to WorldActive-driven weather and drop
        // any in-flight particles/boids from the previous map (custom
        // or classic) so they don't briefly bleed into the loaded one.
        MuEditor::CustomMap::ResetActiveCustomWeather();
        MuEditor::CustomMap::ClearLiveWeatherParticles();

        // .att + .obj via the editor-safe path (skips the sentinel check).
        if (!InvokeClassicLoaders(attBuf, objBuf)) return false;

        // Texture mapping layers — different worlds reference different
        // tile indices, so this matters even though MAPPING uses the same
        // loader as custom maps.
        std::wstring mapMut = mapBuf;
        std::error_code ec;
        if (fs::exists(mapBuf, ec))
            OpenTerrainMapping(mapMut.data());

        // OpenTerrainHeight / OpenJpegBuffer construct their own paths from
        // a relative dotted stub (see BuildHeightLoaderArg). For classic
        // worlds the stub is just "World<n>\TerrainHeight.bmp" etc.
        wchar_t heightArg[64];
        wchar_t lightArg[64];
        std::swprintf(heightArg, std::size(heightArg),
            L"World%d\\TerrainHeight.bmp", worldFolderIndex);
        std::swprintf(lightArg,  std::size(lightArg),
            L"World%d\\TerrainLight.jpg",  worldFolderIndex);

        // Classic worlds split between two height formats — the same flag
        // the engine's MapManager checks. Most worlds use legacy 8-bit OZB;
        // a few (Karutan, PK Field, DoppelGanger2) use the 24-bit variant.
        // We can't ask gMapManager here (it tracks the WORLD WE'RE STANDING
        // ON, not the one we're loading), so we just try legacy first; the
        // 24-bit specialty worlds aren't in the editor's classic-world
        // dropdown anyway.
        CreateTerrain(heightArg, /*bNew=*/false);

        wchar_t lightDisk[80];
        std::swprintf(lightDisk, std::size(lightDisk),
            L"Data\\World%d\\TerrainLight.OZJ", worldFolderIndex);
        if (fs::exists(lightDisk, ec))
            OpenTerrainLight(lightArg);

        // Tile bitmaps for the world's slot layout.
        wchar_t worldDir[32];
        std::swprintf(worldDir, std::size(worldDir),
            L"World%d", worldFolderIndex);
        LoadWorldTextures(worldDir);

        // Align WorldActive with the world we just loaded so the
        // per-world client behaviors (water in Atlans, snow in Devias,
        // etc.) match the visuals. If the previous load was a custom
        // map, we'd otherwise be stuck with the neutralized
        // WD_0LORENCIA override.
        gMapManager.WorldActive = worldFolderIndex - 1;

        return true;
    }

    std::vector<int> ListCustomMapIds()
    {
        std::vector<int> result;
        std::error_code ec;
        const std::wstring root = GetCustomRootDirectory();
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
            return result;

        for (const auto& entry : fs::directory_iterator(root, ec))
        {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            const int folder =
                ParseWorldFolderNumber(entry.path().filename().wstring());
            // folder is 1-based (World<n>); mapId is 0-based (n - 1).
            if (folder >= 1) result.push_back(folder - 1);
        }
        std::sort(result.begin(), result.end());
        return result;
    }
}

#endif // _EDITOR
