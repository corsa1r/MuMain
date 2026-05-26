#pragma once

#ifdef _EDITOR

#include <string>
#include <vector>

// Side-loaded model banks for custom-map authoring.
//
// The engine's Models[] array has 160 universal world-object slots
// (MODEL_WORLD_OBJECT..MAX_WORLD_OBJECTS), which every classic world
// overwrites on warp — Lorencia's tree at slot 0 and Aida's tree at slot 0
// are different BMD files at the same array index. That collision is why
// a custom map's .obj can't naively reference assets from multiple worlds.
//
// This module side-loads additional source-world banks into the trailing
// 1024 slots above MAX_MODELS (allocated but unused by the engine — see
// the deterministic offset patch in ZzzOpenData.cpp). Each source occupies
// a contiguous 160-slot window: World1 (Lorencia) at [MAX_MODELS+0..159],
// World2 (Dungeon) at [MAX_MODELS+160..319], etc. — wherever a free
// window is assigned at side-load time.
//
// Custom-map .obj records store **absolute** Type indices, so OBJECT.Type
// directly indexes Models[] regardless of which bank the object came from.
// The .obj save path partitions records by bank (writes one per-source
// file per active bank); the .obj load path side-loads each referenced
// bank and applies a per-bank base offset before CreateObject.
namespace MuEditor::CustomMap
{
    // Slots per source-world bank. Matches MAX_WORLD_OBJECTS exactly so
    // a side-loaded bank is a drop-in replacement for the live world's
    // bank for the slot range it covers.
    constexpr int SOURCE_BANK_SLOT_COUNT = 160;

    // Maximum number of source banks we can side-load concurrently. The
    // headroom in Models[] is 1024 slots (ZzzOpenData.cpp), so we fit
    // floor(1024 / 160) = 6 banks.
    constexpr int MAX_SOURCE_BANKS = 6;

    struct SourceBank
    {
        int worldFolderIndex = -1;  // 1-based "World<N>" folder; -1 = empty slot
        int baseOffset       = -1;  // index into Models[] where this bank starts
    };

    // Side-loads world `worldFolderIndex` (1-based; e.g. 33 = Aida) into
    // the first free side-bank slot. Returns the assigned baseOffset
    // (>= MAX_MODELS) so callers can fix up .obj Types, or -1 if all
    // bank slots are full or the source world's Object directory is
    // missing. Idempotent: re-loading an already-loaded world returns
    // its existing offset without re-reading the BMD files.
    int LoadSourceBank(int worldFolderIndex);

    // Returns the baseOffset for an already-loaded source, or -1 if not
    // loaded. Use this to convert a source-relative Type to an absolute
    // Models[] index at save time without rerunning the loader.
    int GetSourceBankBaseOffset(int worldFolderIndex);

    // Inverse lookup: given an absolute Models[] index, returns the
    // worldFolderIndex of the bank that owns it (or -1 if the index is
    // in the engine's universal range or unassigned).
    int FindSourceWorldByModelIndex(int absoluteType);

    // Drops every side-loaded bank from tracking. Does NOT free the BMD
    // data — same memory model as MarkAllObjectsDead: the engine holds
    // raw pointers into those slots through long-lived effects/joints
    // and has no callback to drop them. Reuse via re-load is safe.
    void ResetAllSourceBanks();

    // Drops a single bank from tracking. Same no-free semantics as
    // ResetAllSourceBanks — the slot becomes available for the next
    // LoadSourceBank to reuse. Returns true if the bank was loaded.
    bool UnloadSourceBank(int worldFolderIndex);

    // Snapshot of currently-loaded banks. Save uses this to enumerate
    // which per-source .obj files to write; UI uses it for the source-
    // world dropdown.
    std::vector<SourceBank> GetLoadedSourceBanks();

    // Enumerates every Data\Object<N>\ directory that ships a uniform
    // bank (probed by Object01.bmd's existence). 1-based folder index;
    // World1 is intentionally excluded — its Object1\ uses descriptive
    // filenames (Tree01.bmd, ...) that LoadSourceBank can't ingest.
    // Sorted ascending. Cached on first call.
    const std::vector<int>& EnumerateAvailableSourceWorlds();
}

#endif // _EDITOR
