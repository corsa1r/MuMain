#pragma once

#include <string>
#include <vector>

// Custom-map I/O. Split into a runtime half (always compiled, for
// production clients that need to load custom slots) and an authoring
// half (#ifdef _EDITOR, for the in-game editor that writes the files).
// Both halves share the path helpers + manifest reader.
//
// The on-disk format is byte-for-byte the classic client format
// (OpenTerrainAttribute / OpenObjectsEnc in MuMain), just under a custom
// directory root — files written here load back through the existing
// engine loaders without modification.
namespace MuEditor::CustomMap
{
    // ------------------------------------------------------------------
    // Runtime — available in both editor and production builds.
    // ------------------------------------------------------------------

    // Loads a custom slot's .att and .obj into the live TerrainWall and
    // ObjectBlock buffers, replacing whatever was there. ObjectBlock is
    // fully torn down first so models don't accumulate across loads.
    //
    // Caveat: the underlying OpenTerrainAttribute applies a per-world
    // sentinel check tied to gMapManager.WorldActive — if the live world
    // is one of WD_0LORENCIA..WD_4LOSTTOWER and the custom map fails its
    // sentinel tile, the classic loader will MessageBox + WM_DESTROY.
    // Load custom maps from a non-classic world (or warp first).
    bool LoadCustomMap(int mapId);

    // Same as LoadCustomMap but reads from the classic asset tree
    // Data\World<n>\EncTerrain<n>.{att,obj}. `worldFolderIndex` is the
    // 1-based folder number — e.g. 2 for Dungeon, 3 for Devias.
    bool LoadClassicMap(int worldFolderIndex);

    // Manifest weatherFlags accessor — reads the saved CW_* bitmask
    // without loading the whole slot. Returns 0 if the manifest is
    // missing or the field is absent (older slots).
    unsigned int ReadWeatherFlags(int mapId);

    // Re-reads the slot's TerrainLight.OZJ through the engine's
    // OpenTerrainLight path (which also re-runs CreateTerrainNormal
    // and CreateTerrainLight against the live heightmap, so dynamic
    // slope shading composites correctly with the new ambient buffer).
    // No-op if the file isn't present.
    void ReloadTerrainLightFromSlot(int mapId);

    // Picks the highest-priority weather flag and loads its source
    // world's leaf01/leaf02 bitmaps into BITMAP_LEAF1/LEAF2 (force-
    // evicting the existing slot first so the bitmap cache can't
    // short-circuit by filename). Also side-loads MODEL_BAT01 /
    // MODEL_BUTTERFLY01 BMDs when the corresponding flags are set,
    // since MapManager::Load only triggers those AccessModel calls on
    // the Dungeon / Noria world enums. Called by LoadCustomMap and by
    // the editor UI on live flag toggles.
    void ApplyWeatherAssets(unsigned int flags);

    // Enumerates mapIds with an existing slot under Data\World\Custom\.
    // Sorted ascending. Returns {} if the Custom root doesn't exist.
    std::vector<int> ListCustomMapIds();

    // Path helpers — exposed so the "Load Map" dialog can scan the
    // custom root and so tests can locate the files.
    std::wstring GetCustomRootDirectory();          // Data\World\Custom
    std::wstring GetCustomMapDirectory(int mapId);  // ...\World<mapId+1>
    std::wstring GetCustomMapAttPath(int mapId);    // ...\EncTerrain<n>.att
    std::wstring GetCustomMapObjPath(int mapId);    // ...\EncTerrain<n>.obj
    std::wstring GetCustomMapMapPath(int mapId);    // ...\EncTerrain<n>.map  (texture mapping)
    std::wstring GetCustomMapHeightPath(int mapId); // ...\TerrainHeight.OZB (legacy 8-bit heightmap)
    std::wstring GetCustomMapLightPath(int mapId);  // ...\TerrainLight.jpg
    // Per-tile grass density (256x256 bytes, density * 255). The engine
    // randomizes TerrainGrassTexture[] on every OpenTerrainMapping, so
    // we load this file in LoadCustomMap *after* the random init to
    // restore the user-painted state.
    std::wstring GetCustomMapGrassPath(int mapId);

    // Per-vertex darkness mask painted in the DevEditor. 256x256 raw
    // bytes; 0 = no darkening (default), 255 = forced black after bake.
    // Consumed by LightingBaker; persisted alongside grass/light/etc.
    std::wstring GetCustomMapDarknessPath(int mapId);

    // Manifest of side-loaded source worlds — sources.json at the slot
    // root. Each entry is a 1-based World folder index (e.g. 33 = Aida).
    // LoadCustomMap consults this list to side-load source banks before
    // reading per-source .obj files.
    std::wstring GetCustomMapManifestPath(int mapId);

    // Per-source object directory and .obj path:
    //   ...\source_World<sourceFolderIndex>\EncTerrain<mapId+1>.obj
    // Per-source .obj files store source-relative Type indices (0..159);
    // LoadCustomMap fixes them up to absolute Models[] indices using the
    // side-loaded bank's base offset.
    std::wstring GetCustomMapSourceDir(int mapId, int sourceFolderIndex);
    std::wstring GetCustomMapSourceObjPath(int mapId, int sourceFolderIndex);

#ifdef _EDITOR
    // ------------------------------------------------------------------
    // Authoring — editor build only. Save/Create/Reseed write the slot
    // files that the runtime above loads. None of these are reachable
    // from a non-editor binary.
    // ------------------------------------------------------------------

    // Creates a fresh slot at Data\World\Custom\World<mapId+1>\ with a
    // blank EncTerrain<mapId+1>.att (256x256 attribute grid: border ring
    // of TW_NOMOVE|TW_NOGROUND so the player can't fall off the edge,
    // interior 0x0000) and an empty EncTerrain<mapId+1>.obj (object
    // count 0). Both files are written encrypted; the classic client
    // loaders read them back as-is. Returns false on directory or write
    // failure.
    //
    // baseWorld determines which classic world's tile bitmaps are
    // copied into the slot folder to seed the texture palette (1-based
    // World folder index — 1 for Lorencia, 9 for Tarkan, 34 for Aida).
    // Stored in sources.json so subsequent loads / lazy re-seeds use
    // the same set.
    bool CreateNewCustomMap(int mapId, int baseWorld = 1);

    // Overwrites the slot's tile bitmaps + .map indices remain — useful
    // for swapping the visual palette of an existing slot without
    // discarding painted attributes or placed objects.
    bool ReseedTileTexturesFromWorld(int mapId, int baseWorld);

    // Snapshots the *live* TerrainWall buffer and ObjectBlock
    // spatial-hash and writes them, encrypted, into the slot's
    // .att / .obj files, creating the slot directory if it doesn't
    // exist. Inverse of OpenTerrainAttribute + OpenObjectsEnc.
    bool SaveCustomMap(int mapId);

    // Persists a new weatherFlags value into the slot's manifest,
    // preserving sources / baseWorld. Used by Save Map. Returns false
    // on directory/IO failure.
    bool WriteWeatherFlags(int mapId, unsigned int flags);

    // Writes a fresh TerrainLight.OZJ for the slot from a caller-
    // supplied 256×256 RGB buffer. `rgb256` must point to
    // TERRAIN_SIZE*TERRAIN_SIZE*3 bytes laid out row-major as
    // (R,G,B) per vertex. JPEG-encodes the buffer with the same OZJ
    // wire format the engine reads (24-byte prefix + JPEG payload).
    // Used by the Lighting Bake button. Returns false on encode or
    // file-write failure.
    bool WriteTerrainLightRGB(int mapId, const unsigned char* rgb256);
#endif // _EDITOR
}
