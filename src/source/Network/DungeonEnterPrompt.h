#pragma once

// Dungeon-entry confirmation prompt state.
//
// After the player sends "/dungeon N" and the server validates the request, it
// pushes a confirm packet (C2 / 0xCC / 0x02) carrying the dungeon's warp index
// and display name. This module owns that pending request so the OK/Cancel
// message box (CDungeonEnterMsgBoxLayout) can read the name to display and the
// warp index to echo back when the player confirms.
//
// Layering: this is a small networking-side holder. The parser
// (ApplyFromPacket) lives here; the UI layer only reads the accessors.

#include <string>
#include <cstdint>

namespace BloodlustMU
{
    class DungeonEnterPrompt
    {
    public:
        static DungeonEnterPrompt& Instance();

        // Parse a server-pushed dungeon confirm packet (already de-framed by the
        // dispatcher). ReceiveBuffer points at the C2 header byte; size is the
        // total packet size. On success the pending request is stored and the
        // OK/Cancel message box is shown. Returns true if parsing succeeded.
        bool ApplyFromPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        int GetWarpIndex() const { return m_warpIndex; }
        const std::wstring& GetDungeonName() const { return m_dungeonName; }

    private:
        DungeonEnterPrompt() = default;

        int          m_warpIndex{-1};
        std::wstring m_dungeonName;
    };
}
