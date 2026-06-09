//*****************************************************************************
// File: EliteMonsterStore.h
//*****************************************************************************

#pragma once

#include <cstdint>

namespace GameLogic::EliteMonster
{
    // Elite info pushed by the server (C1 0xAB sub-op 0x08), keyed by the monster's network id.
    struct Info
    {
        uint8_t rank = 0;                 // 0 = not elite; 1..N = tier rank (drives nameplate marker).
        uint8_t r = 255, g = 215, b = 0;  // aura / nameplate color.
        bool    enraged = false;          // shows the enrage pip on the nameplate.
        wchar_t name[33] = { 0 };         // optional unique name (empty => keep the definition name).
    };

    // Network handler: store the elite info for a target. Returns true if the target was NOT previously
    // marked elite (a fresh appearance) — used to trigger the Rare/Champion spawn cue exactly once.
    bool Set(uint16_t targetId, uint8_t rank, uint8_t r, uint8_t g, uint8_t b, bool enraged, const wchar_t* name);

    // Render / UI thread: fills 'out' and returns true if the target is currently an elite.
    bool Get(uint16_t targetId, Info& out);

    // Cheap check whether the target is currently marked elite.
    bool IsElite(uint16_t targetId);

    // Clears the elite state for a target. Call when a character slot is recycled so a new entity that
    // reuses the same network id starts clean.
    void Reset(uint16_t targetId);
}
