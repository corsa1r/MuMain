#pragma once

// Dungeon ready-check client state.
//
// After a party member sends "/dungeon N" and the server validates the request, it pushes a ready-check
// packet (C2 / 0xCC / 0x02) to every participant carrying the dungeon name and a live "ready (X/N)"
// count. This module owns that state so the ready-check window (CNewUIDungeonReadyCheckBox) can render
// it live, and so the window knows when the server dismissed the check (0xCC / 0x03).
//
// Layering: networking-side holder. The parsers live here; the UI only reads the accessors.

#include <string>
#include <cstdint>

namespace BloodlustMU
{
    class DungeonReadyCheckState
    {
    public:
        static DungeonReadyCheckState& Instance();

        // Parse a server-pushed ready-check show/update packet (C2 / 0xCC / 0x02). Returns true on
        // success, in which case the state is updated and IsActive() becomes true.
        bool ApplyShowPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        // Parse a server-pushed dismiss packet (C2 / 0xCC / 0x03). Marks the check inactive so the
        // window closes itself.
        bool ApplyDismissPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        bool IsActive() const { return m_active; }
        bool IsThisPlayerReady() const { return m_myReady; }
        int  GetTotalPlayers() const { return m_total; }
        int  GetReadyCount() const { return m_readyCount; }
        const std::wstring& GetDungeonName() const { return m_dungeonName; }

    private:
        DungeonReadyCheckState() = default;

        bool         m_active{false};
        bool         m_myReady{false};
        int          m_warpIndex{-1};
        int          m_total{0};
        int          m_readyCount{0};
        std::wstring m_dungeonName;
    };
}
