#pragma once

// Bit flags for per-custom-map weather selection. Engine code can
// include this without dragging in MuEditor headers (which only exist
// under _EDITOR builds). The full editor-side state machine and
// accessors live in MuEditor/CustomMap/CustomWeather.h, which includes
// this file so the bit values stay synchronized across both sides.

enum CustomWeatherBit : unsigned int
{
    CW_NONE              = 0,
    CW_LORENCIA_LEAVES   = 1u << 0,  // drifting green leaves
    CW_LORENCIA_BIRDS    = 1u << 1,  // bird boid flock
    CW_DEVIAS_SNOW       = 1u << 2,  // falling snowflakes
    CW_ATLANS_LEAVES     = 1u << 3,  // Atlans/Noria-style leaves
    CW_HEAVEN_RAIN       = 1u << 4,  // rain particles
    CW_TARKAN_WIND       = 1u << 5,  // high-amplitude grass wind + dust gusts
    CW_NORIA_BUTTERFLIES = 1u << 6,  // butterfly boid flock
    CW_DUNGEON_BATS      = 1u << 7,  // bat boid flock
    CW_RAKLION_SNOW      = 1u << 8,  // Ice City / Raklion-style snow
};

// Particle-spawning flags (the ones that occupy Leaves[] slots). Used by
// the spawn dispatcher to round-robin slots across enabled weathers so
// no single weather monopolizes the pool. CW_TARKAN_WIND is included
// because the dust-puff effect (CGM3rdChangeUp::CreateFireSnuff) also
// draws from Leaves[]; the grass-sway side of the flag is independent
// and runs every frame regardless of the pool dispatch.
constexpr unsigned int CW_PARTICLE_FLAGS =
    CW_LORENCIA_LEAVES | CW_DEVIAS_SNOW   | CW_ATLANS_LEAVES |
    CW_HEAVEN_RAIN     | CW_RAKLION_SNOW  | CW_TARKAN_WIND;
