//*****************************************************************************
// File: PrimeStatusStore.h
//*****************************************************************************

#pragma once

#include <cstdint>

// Matches server-side SkillComboElement enum ordinals.
enum class EPrimeElement : uint8_t
{
    None      = 0,
    Fire      = 1,
    Ice       = 2,
    Lightning = 3,
    Physical  = 4,
};

// Bitmask of active elements on a single target.
// Bit N is set when element (N+1) is active: bit0=Fire, bit1=Ice, bit2=Lightning, bit3=Physical.
using PrimeElementMask = uint8_t;

constexpr PrimeElementMask ElementBit(EPrimeElement e)
{
    return static_cast<PrimeElementMask>(1u << (static_cast<uint8_t>(e) - 1u));
}

namespace GameLogic::PrimeStatus
{
    // Called from the network thread when the server applies a prime mark.
    void Set(uint16_t targetId, EPrimeElement element);

    // Called when the server clears a prime mark (detonated or expired).
    void Clear(uint16_t targetId, EPrimeElement element);

    // Returns the bitmask of all currently active elements on a target.
    // Called from the render thread.
    PrimeElementMask GetMask(uint16_t targetId);

    // Clears all prime marks for a target. Call when the character slot is recycled
    // so a new entity that receives the same network ID starts with a clean state.
    void Reset(uint16_t targetId);
}
