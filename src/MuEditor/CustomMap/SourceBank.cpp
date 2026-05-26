#include "stdafx.h"

#ifdef _EDITOR

#include "SourceBank.h"

#include <array>
#include <cstdio>
#include <filesystem>

#include "Core/Globals/_enum.h"            // MAX_MODELS, MAX_WORLD_OBJECTS
#include "Data/DataHandler/LoadData.h"     // gLoadData

namespace fs = std::filesystem;

namespace
{
    // Bank slots live just above the engine's universal model range, in
    // the 1024-slot trailing region of the Models[] allocation made by
    // OpenPlayers (ZzzOpenData.cpp). The deterministic offset patch in
    // that file is what makes this address window stable.
    constexpr int SIDE_BANK_START = MAX_MODELS;

    static_assert(
        MuEditor::CustomMap::MAX_SOURCE_BANKS *
            MuEditor::CustomMap::SOURCE_BANK_SLOT_COUNT <= 1024,
        "source banks exceed the trailing headroom carved out by "
        "ZzzOpenData.cpp's Models[] allocation");

    // Per-bank tracking. Each entry covers SOURCE_BANK_SLOT_COUNT slots
    // starting at SIDE_BANK_START + idx*SOURCE_BANK_SLOT_COUNT. -1 in
    // worldFolderIndex = unused.
    std::array<MuEditor::CustomMap::SourceBank,
               MuEditor::CustomMap::MAX_SOURCE_BANKS> g_Banks{};

    int FindBankIndexByWorld(int worldFolderIndex)
    {
        for (int i = 0; i < MuEditor::CustomMap::MAX_SOURCE_BANKS; ++i)
        {
            if (g_Banks[i].worldFolderIndex == worldFolderIndex)
                return i;
        }
        return -1;
    }

    int FindFreeBankIndex()
    {
        for (int i = 0; i < MuEditor::CustomMap::MAX_SOURCE_BANKS; ++i)
        {
            if (g_Banks[i].worldFolderIndex < 0)
                return i;
        }
        return -1;
    }

    int OffsetForBankIndex(int bankIndex)
    {
        return SIDE_BANK_START +
               bankIndex * MuEditor::CustomMap::SOURCE_BANK_SLOT_COUNT;
    }

    // Worlds 2..81 ship their bank as Object<N>\Object01.bmd .. Object160.bmd
    // (see CMapManager::LoadObjects's uniform "Object" loop). Lorencia
    // (worldFolderIndex == 1) is the odd one out: Object1\ uses
    // descriptive filenames (Tree, Stone, House, ...) keyed to the
    // universal MODEL_TREE01 / MODEL_STONE01 / ... enum positions, so a
    // simple "Object<i>.bmd" sweep wouldn't find anything there. Hook
    // for Lorencia is left as a follow-up.
    bool SourceWorldHasUniformBank(int worldFolderIndex)
    {
        return worldFolderIndex >= 2 && worldFolderIndex <= 99;
    }

    // Check Data\Object<N>\Object01.bmd as a quick existence probe for
    // the source world before commiting a bank slot.
    bool SourceWorldDirExists(int worldFolderIndex)
    {
        wchar_t path[64];
        std::swprintf(path, std::size(path),
            L"Data\\Object%d\\Object01.bmd", worldFolderIndex);
        std::error_code ec;
        return fs::exists(path, ec);
    }

    // Issues the AccessModel + OpenTexture pair the engine uses in
    // CMapManager::LoadObjects's uniform path, but at our offset window
    // instead of the universal Models[0..159] range.
    void LoadUniformWorldBankAt(int worldFolderIndex, int baseOffset)
    {
        wchar_t bmdDir[32];
        wchar_t texDir[32];
        std::swprintf(bmdDir, std::size(bmdDir),
            L"Data\\Object%d\\", worldFolderIndex);
        std::swprintf(texDir, std::size(texDir),
            L"Object%d\\", worldFolderIndex);

        for (int i = 0; i < MuEditor::CustomMap::SOURCE_BANK_SLOT_COUNT; ++i)
        {
            gLoadData.AccessModel(baseOffset + i, bmdDir, L"Object", i + 1);
            gLoadData.OpenTexture(baseOffset + i, texDir);
        }
    }
}

namespace MuEditor::CustomMap
{
    int LoadSourceBank(int worldFolderIndex)
    {
        if (worldFolderIndex < 1) return -1;

        // Already loaded? Return existing offset (idempotent).
        const int existing = FindBankIndexByWorld(worldFolderIndex);
        if (existing >= 0) return OffsetForBankIndex(existing);

        if (!SourceWorldHasUniformBank(worldFolderIndex)) return -1;
        if (!SourceWorldDirExists(worldFolderIndex))       return -1;

        const int slot = FindFreeBankIndex();
        if (slot < 0) return -1;  // all 6 banks in use

        const int offset = OffsetForBankIndex(slot);
        LoadUniformWorldBankAt(worldFolderIndex, offset);

        g_Banks[slot].worldFolderIndex = worldFolderIndex;
        g_Banks[slot].baseOffset       = offset;
        return offset;
    }

    int GetSourceBankBaseOffset(int worldFolderIndex)
    {
        const int idx = FindBankIndexByWorld(worldFolderIndex);
        if (idx < 0) return -1;
        return g_Banks[idx].baseOffset;
    }

    int FindSourceWorldByModelIndex(int absoluteType)
    {
        if (absoluteType < SIDE_BANK_START) return -1;
        const int slot = (absoluteType - SIDE_BANK_START) / SOURCE_BANK_SLOT_COUNT;
        if (slot < 0 || slot >= MAX_SOURCE_BANKS) return -1;
        return g_Banks[slot].worldFolderIndex;  // -1 if that slot's empty
    }

    void ResetAllSourceBanks()
    {
        // Memory model intentionally matches MarkAllObjectsDead: leave
        // the BMD data in place. Effects/joints can hold raw Models[]
        // pointers via OBJECT->Owner; freeing under them would dangle.
        // The BMD slots will be overwritten on the next LoadSourceBank.
        for (auto& b : g_Banks)
        {
            b.worldFolderIndex = -1;
            b.baseOffset       = -1;
        }
    }

    bool UnloadSourceBank(int worldFolderIndex)
    {
        const int idx = FindBankIndexByWorld(worldFolderIndex);
        if (idx < 0) return false;
        g_Banks[idx].worldFolderIndex = -1;
        g_Banks[idx].baseOffset       = -1;
        return true;
    }

    std::vector<SourceBank> GetLoadedSourceBanks()
    {
        std::vector<SourceBank> out;
        for (const auto& b : g_Banks)
        {
            if (b.worldFolderIndex >= 0) out.push_back(b);
        }
        return out;
    }

    const std::vector<int>& EnumerateAvailableSourceWorlds()
    {
        // Cached on first call — disk-touching every dropdown frame
        // would be wasteful, and the set is stable for the session.
        static std::vector<int> cached;
        static bool             populated = false;
        if (populated) return cached;

        // Probe a generous range. The classic enum tops out around 81
        // (Karutan2); leave headroom for private-server extensions.
        constexpr int MAX_PROBE_WORLD = 200;
        for (int n = 2; n <= MAX_PROBE_WORLD; ++n)
        {
            wchar_t probe[64];
            std::swprintf(probe, std::size(probe),
                L"Data\\Object%d\\Object01.bmd", n);
            std::error_code ec;
            if (fs::exists(probe, ec)) cached.push_back(n);
        }
        populated = true;
        return cached;
    }
}

#endif // _EDITOR
