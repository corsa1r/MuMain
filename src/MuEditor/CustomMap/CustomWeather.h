#pragma once

#include "Core/Globals/CustomWeatherFlags.h"  // CW_* bit values

// Per-custom-map weather opt-in flags.
//
// Classic worlds drive weather entirely from gMapManager.WorldActive
// (e.g. "if WorldActive == WD_2DEVIAS spawn snow particles"). Custom
// slots set WorldActive = WD_0LORENCIA to neutralize per-world special
// behaviors, which would otherwise force-enable Lorencia leaves + birds
// and lock out every other effect.
//
// Instead we drive the spawn checks off this flag set. Engine spawn
// sites read it via HasWeatherFlag(); the editor writes to it via
// SetActiveCustomWeather() when LoadCustomMap runs. When no custom map
// is active (classic worlds) IsCustomWeatherActive() returns false and
// engine checks fall back to their original WorldActive logic.

namespace MuEditor { namespace CustomMap {

// Lifecycle: LoadCustomMap calls SetActiveCustomWeather(flags) with the
// manifest's weatherFlags. ResetActiveCustomWeather() switches the
// engine back to WorldActive-driven weather for classic worlds.
// ClearLiveWeatherParticles() wipes the Leaves[] and Boids[] pools so
// stale effects from the previous map don't bleed into the new one.
void SetActiveCustomWeather(unsigned int flags);
void ResetActiveCustomWeather();
unsigned int GetActiveCustomWeather();
bool IsCustomWeatherActive();
bool HasWeatherFlag(unsigned int flag);

void ClearLiveWeatherParticles();

// Per-slot dispatch. When multiple particle flags are enabled, each
// new particle slot picks ONE of them so a single weather doesn't
// monopolize the Leaves[] pool. SetSpawnSlotIndex() is called by the
// spawn loop with the slot index; HasWeatherFlagForSpawn() returns
// true only if the queried flag matches the round-robin pick AND is
// part of the active set. Used by the per-weather Create* dispatch.
void SetSpawnSlotIndex(int slotIndex);
bool HasWeatherFlagForSpawn(unsigned int flag);

}} // namespace MuEditor::CustomMap
