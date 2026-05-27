#pragma once

#ifdef _EDITOR

#include <string>
#include <vector>

// Editor-side authoring pipeline for the custom-map slot tree under
// Data\World\Custom\World<n>\. The on-disk format is byte-for-byte the
// classic client format (OpenTerrainAttribute / OpenObjectsEnc in
// MuMain), just under a custom directory root — files written here load
// back through the existing engine loaders without modification.
namespace MuEditor::CustomMap
{
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
}

#endif // _EDITOR
