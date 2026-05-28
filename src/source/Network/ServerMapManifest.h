#pragma once

// Server-pushed map manifest + warp list cache.
//
// The OpenMU server sends a single packet (C2 / 0xBA / 0x01) right after world
// entry that carries:
//   1. A manifest of every GameMapDefinition the player may visit, with the
//      IsCustomMap flag — so we know to render the map from
//      Data\World\Custom\WorldN+1\ instead of the classic Data\WorldN+1\.
//   2. The full warp list — replaces the static Data\Local\<lang>\MoveReq_<lang>.bmd
//      so the Move List UI always reflects the server's current configuration.
//
// This module owns the parsed data and exposes it via accessors that the
// rendering side (CMapManager::LoadWorld) and UI side (CMoveCommandData) read.

#include <string>
#include <vector>
#include <cstdint>

namespace BloodlustMU
{
    struct MapManifestEntry
    {
        int16_t     number{};
        uint8_t     discriminator{};
        bool        isCustomMap{false};
        std::wstring name;
    };

    struct WarpManifestEntry
    {
        int16_t     index{};
        int16_t     targetMapNumber{};
        int16_t     levelRequirement{};
        int32_t     costs{};
        uint8_t     gateX{};
        uint8_t     gateY{};
        std::wstring name;
    };

    class ServerMapManifest
    {
    public:
        static ServerMapManifest& Instance();

        // Parse a server-pushed manifest packet (already de-framed by the dispatcher).
        // ReceiveBuffer points at the C2 header byte; size is the total packet size.
        // Returns true if parsing succeeded and the cache was updated.
        bool ApplyFromPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        // True once we've received at least one manifest from the server.
        bool IsLoaded() const { return m_isLoaded; }

        const std::vector<MapManifestEntry>& Maps() const { return m_maps; }
        const std::vector<WarpManifestEntry>& Warps() const { return m_warps; }

        // Lookup helpers. Return nullptr when the entry isn't in the manifest
        // (which means "fall back to legacy behavior" — the client used to do this
        // without server help, so missing entries are not fatal).
        const MapManifestEntry* FindMap(int mapNumber) const;
        const WarpManifestEntry* FindWarp(int index) const;

        // Convenience: "should LoadWorld render this map from the custom slot?"
        bool IsCustomMap(int mapNumber) const;

        // Server-pushed map number we're currently rendering. Set by LoadWorld at the
        // top of every map transition. Needed because LoadCustomMap overrides
        // gMapManager.WorldActive to WD_0LORENCIA for renderer compatibility, which
        // confuses anything that asks "what map am I on?" via WorldActive.
        // Returns -1 before any map has been entered.
        int CurrentServerMapNumber() const { return m_currentMap; }
        void SetCurrentServerMapNumber(int mapNumber) { m_currentMap = mapNumber; }

        // True when CurrentServerMapNumber is a custom map per the manifest.
        bool IsCurrentlyInCustomMap() const { return IsCustomMap(m_currentMap); }

        // Display name for the current server-side map, or empty if unknown.
        const wchar_t* CurrentMapDisplayName() const;

        void Clear();

    private:
        ServerMapManifest() = default;

        bool m_isLoaded{false};
        int m_currentMap{-1};
        std::vector<MapManifestEntry> m_maps;
        std::vector<WarpManifestEntry> m_warps;
    };
}
