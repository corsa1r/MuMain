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

namespace GameLogic::PrimeStatus
{
    // Called from the network thread when the server applies a prime mark.
    void Set(uint16_t targetId, EPrimeElement element);

    // Called when the server clears a prime mark (detonated or expired).
    // Only clears if the stored element matches to avoid stomping a fresher prime.
    void Clear(uint16_t targetId, EPrimeElement element);

    // Called from the render thread to query an entity's current prime mark.
    EPrimeElement Get(uint16_t targetId);
}
