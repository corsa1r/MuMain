#include "stdafx.h"
#include "CustomMap/CustomWeather.h"

#include "Core/Globals/_define.h"          // MAX_LEAVES, MAX_BOIDS
#include "Render/Effects/ZzzEffect.h"      // Leaves[]
#include "Engine/Object/ZzzObject.h"       // Boids[], PARTICLE, OBJECT

namespace MuEditor { namespace CustomMap {

namespace {
    // Active flag set. A non-zero value implicitly means "a custom map
    // is loaded"; zero means classic-world routing through WorldActive.
    unsigned int g_ActiveFlags = CW_NONE;

    // Latch separate from g_ActiveFlags so SetActiveCustomWeather(0)
    // still distinguishes "custom map with no weather selected" from
    // "no custom map active". Without this, the engine couldn't tell
    // whether to consult the flags at all — and a custom map opted into
    // zero weather would inherit Lorencia leaves through the fallback.
    bool g_CustomMapActive = false;
}

void SetActiveCustomWeather(unsigned int flags)
{
    g_ActiveFlags     = flags;
    g_CustomMapActive = true;
}

void ResetActiveCustomWeather()
{
    g_ActiveFlags     = CW_NONE;
    g_CustomMapActive = false;
}

unsigned int GetActiveCustomWeather()
{
    return g_ActiveFlags;
}

bool IsCustomWeatherActive()
{
    return g_CustomMapActive;
}

bool HasWeatherFlag(unsigned int flag)
{
    return g_CustomMapActive && (g_ActiveFlags & flag) != 0;
}

void ClearLiveWeatherParticles()
{
    // Drop in-flight Leaves[] particles (snow, rain, leaves, mist…).
    // Their Create* functions early-out when WorldActive doesn't match,
    // so once we clear the live set they won't respawn unless the new
    // map's weather flags (or WorldActive) opt back in.
    for (int i = 0; i < MAX_LEAVES; ++i)
        Leaves[i].Live = false;

    // Same story for Boids[] — birds, bats, butterflies, crows.
    for (int i = 0; i < MAX_BOIDS; ++i)
        Boids[i].Live = false;
}

namespace {
    // Slot-to-weather pick. The spawn loop assigns each new slot one of
    // the enabled particle flags so the dispatch order (CreateLorenciaLeaf
    // first, etc.) doesn't starve later creators. Set once per spawn
    // attempt; only one of the Create* will see HasWeatherFlagForSpawn()
    // return true and claim the slot.
    unsigned int g_SpawnTargetFlag = 0;
}

void SetSpawnSlotIndex(int slotIndex)
{
    if (!g_CustomMapActive)
    {
        g_SpawnTargetFlag = 0;
        return;
    }

    const unsigned int active = g_ActiveFlags & CW_PARTICLE_FLAGS;
    if (active == 0)
    {
        g_SpawnTargetFlag = 0;
        return;
    }

    // popcount over the low byte where particle flags live (covers all
    // current entries with room to spare).
    int count = 0;
    for (unsigned int m = active; m; m &= m - 1) ++count;

    const int target = (slotIndex >= 0 ? slotIndex : 0) % count;
    int seen = 0;
    for (int b = 0; b < 32; ++b)
    {
        const unsigned int bit = (1u << b);
        if ((active & bit) == 0) continue;
        if (seen == target)
        {
            g_SpawnTargetFlag = bit;
            return;
        }
        ++seen;
    }
    g_SpawnTargetFlag = 0;
}

bool HasWeatherFlagForSpawn(unsigned int flag)
{
    return g_CustomMapActive && (g_SpawnTargetFlag & flag) != 0;
}

}} // namespace MuEditor::CustomMap
