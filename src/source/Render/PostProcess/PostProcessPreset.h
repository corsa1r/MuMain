// ============================================================================
//  PostProcessPreset.h  —  per-map post-process presets + global override
// ----------------------------------------------------------------------------
//  Lets each world carry its own post-process look, auto-applied on map entry,
//  with a master "global override" that forces one look everywhere.
//
//  Selection logic (ApplyForWorld):
//    * GlobalOverride ON  -> always apply the global base settings.
//    * GlobalOverride OFF -> apply the map's preset file if it exists, else
//                            fall back to the global base. (So nothing is ever
//                            left unstyled.)
//
//  Storage: the global base is the [Graphics] block in config.ini (owned by
//  GameConfig). Per-map presets are small INI files under Data/PostProcess/maps/
//  (map<id>.ini), one full Settings each — serialized here via the Win32 INI
//  API, independent of GameConfig.
//
//  Layering: this module is the ONLY place that knows both "files" and "which
//  map". It hands a finished Settings to Chain::ApplySettings; the chain/passes
//  stay oblivious to maps and files.
// ============================================================================
#pragma once

#include "PostProcessSettings.h"
#include <string>

namespace PostProcess
{
    namespace Presets
    {
        // Seed the module from config at startup: the global base look + whether
        // global override is on. Does not itself apply anything.
        void Init(const Settings& globalBase, bool globalOverride);

        // Update the in-memory global base (e.g. editor saved global to config).
        void SetGlobalBase(const Settings& globalBase);
        const Settings& GetGlobalBase();

        void SetGlobalOverride(bool enabled);
        bool GetGlobalOverride();

        // Choose the right settings for 'worldId' and push to Chain::ApplySettings.
        // Called on map change and whenever the editor changes override/presets.
        void ApplyForWorld(int worldId);

        // The settings ApplyForWorld last pushed, and the world id it ran for.
        // The editor panel polls these to resync its sliders when the map (and
        // thus the active preset) changes. GetActiveWorld() starts at a sentinel
        // so the panel can detect "never applied yet".
        const Settings& GetCurrent();
        int GetActiveWorld();

        // Per-map preset file ops (Data/PostProcess/maps/map<id>.ini).
        bool HasMapPreset(int worldId);
        bool LoadMapPreset(int worldId, Settings& out);   // false if missing/bad
        bool SaveMapPreset(int worldId, const Settings& s);
        bool DeleteMapPreset(int worldId);
    }
}
