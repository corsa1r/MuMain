// ============================================================================
//  PostProcessSettings.h  —  plain config snapshot handed to the chain
// ----------------------------------------------------------------------------
//  A dependency-free POD describing every tunable post-process knob. Winmain
//  fills it from GameConfig (the config store) and passes it to
//  Chain::ApplySettings(), which distributes the values to the individual
//  passes. This keeps the chain and passes decoupled from GameConfig — they
//  only know about this neutral struct, never the INI layer.
//
//  Defaults here mirror the GameConfig defaults; they double as the values used
//  if a field is never set.
// ============================================================================
#pragma once

#include <string>

namespace PostProcess
{
    struct Settings
    {
        // LUT (.cube 3D color grade). Filename under Data/PostProcess/; empty or
        // missing => pass skips (no grade). Off by default — opt-in look.
        bool        lut     = false;
        std::string lutFile = "look.cube";

        // SSAO (depth-based ambient occlusion; darkens contact/crevice areas).
        // Heaviest, most scene-dependent effect — off by default. Runs first in
        // the chain so bloom/grading act on the occluded scene.
        bool  ssao          = false;
        float ssaoRadius    = 60.0f;     // world units sampled around each pixel
        float ssaoStrength  = 1.0f;      // 0 = none .. higher = darker crevices
        float ssaoPower     = 1.5f;      // contrast of the AO falloff

        // Fog (depth-based atmospheric haze; distance + optional height fog).
        bool  fog               = false;
        float fogR              = 0.04f; // cool dark-fantasy haze color
        float fogG              = 0.05f;
        float fogB              = 0.07f;
        float fogDensity        = 0.6f;  // overall thickness (0..1-ish)
        float fogStart          = 0.30f; // 0..1 of far plane before fog begins
        float fogHeightStrength = 0.0f;  // 0 = distance fog only
        float fogHeightTop      = 200.0f;// world Y below which height fog grows

        // Bloom (threshold bright-extract + blur + additive composite)
        bool  bloom           = true;
        int   bloomStrength   = 1;       // integer multiplier (1 = baseline glow)
        float bloomThreshold  = 0.75f;   // luminance above which a pixel blooms

        // Tone mapping (ACES filmic curve + exposure)
        bool  toneMap         = true;
        float exposure        = 1.0f;    // linear multiplier before the curve

        // Color grading (mood: contrast / saturation / brightness / warmth)
        bool  colorGrade      = true;
        float contrast        = 1.05f;   // 1 = neutral
        float saturation      = 1.10f;   // 1 = neutral, 0 = grayscale
        float brightness      = 1.0f;    // 1 = neutral
        float temperature     = 0.10f;   // -1 cool .. 0 neutral .. +1 warm
        float gradeShadows    = 1.0f;    // tonal-zone gain (1 = neutral)
        float gradeMidtones   = 1.0f;
        float gradeHighlights = 1.0f;

        // Vignette (darkened edges)
        bool  vignette        = true;
        float vignetteStrength = 0.35f;  // 0 = none .. 1 = strong
        float vignetteRadius   = 0.75f;  // where darkening begins (0..1 of corner)

        // MSAA (multisample anti-aliasing of geometry edges). samples = 2/4/8.
        // Heavier than FXAA and only smooths polygon edges (not alpha-test
        // cutouts), so off by default; FXAA stays the cheap general option.
        bool  msaa           = false;
        int   msaaSamples    = 4;

        // FXAA (edge anti-aliasing)
        bool  fxaa            = true;

        // Sharpen (CAS-style unsharp)
        bool  sharpen         = true;
        float sharpenStrength = 0.30f;   // 0 = none .. ~1 strong

        // Depth of field (cinematic focus blur). On by default — user-validated
        // focus on the hero. focusDistance = view-space distance kept sharp;
        // focusRange = sharp half-width around it; blur = bokeh size.
        bool  depthOfField     = true;
        float dofFocusDistance = 1834.0f;
        float dofFocusRange    = 318.0f;
        float dofBlur          = 0.73f;

        // Film grain / dither (breaks 8-bit banding in dark gradients)
        bool  filmGrain       = true;
        float filmGrainStrength = 0.05f; // additive noise amplitude

        // God rays (screen-space volumetric light shafts). Off by default —
        // opt-in, most dramatic on outdoor maps. Light position is in screen UV.
        bool  godRays          = false;
        float godRaysLightX    = 0.0f;   // sun AZIMUTH in degrees (0..360); 0 = world +X (legacy shadow dir)
        float godRaysLightY    = 0.0f;   // unused (legacy field; azimuth now lives in godRaysLightX)
        float godRaysSunZ      = 2.0f;   // sun height/elevation; higher = sun more overhead = shorter shadows
        float godRaysDensity   = 0.9f;   // shaft reach toward the light
        float godRaysWeight    = 0.5f;   // per-sample contribution
        float godRaysDecay     = 0.95f;  // per-step falloff along the shaft
        float godRaysThreshold = 100.0f; // occluder height: world-Z above local ground that blocks light
        float godRaysIntensity = 0.6f;   // final additive scale
        int   godRaysSamples   = 64;     // radial march steps (quality vs cost)
        float godRaysR         = 1.0f;   // ray tint (warm white)
        float godRaysG         = 0.9f;
        float godRaysB         = 0.7f;

        // Per-pixel model lighting + normal mapping (geometry shading, NOT a
        // screen-space pass). Distributed by Chain::ApplySettings to the
        // ModelLighting module. Off by default => legacy per-vertex CPU lighting.
        bool  perPixelLighting  = false;
        float normalMapStrength = 1.0f;  // tangent-normal perturbation (0 = flat)
        float specularStrength  = 0.30f; // Blinn-Phong glint (0 = off)
        float specularPower     = 24.0f; // highlight tightness (higher = sharper)

        // Dynamic point lights (per-pixel torches/lanterns/skills). Needs
        // perPixelLighting on. intensity scales their contribution.
        bool  dynamicLights         = true;
        float dynamicLightIntensity = 4.0f;
        float dynamicLightFlicker   = 0.0f;  // 0 = steady, 1 = raw rand() flicker
        int   dynamicLightCount     = 24;    // cap on simultaneously-active nearest lights (1..24)
        bool  playerLight           = true;  // always-on hero light
        float playerLightRadius     = 700.0f;

        // Real-time sun shadow map (forward shadow mapping; SunShadow module —
        // NOT a screen-space pass). Casters: characters/monsters/NPCs (+wings/
        // capes) and static world objects (incl. alpha-tested cutouts like
        // fences). Receivers: terrain + static objects. Driven by the shared sun
        // (God Rays azimuth / Sun Height Z). Distributed by Chain::ApplySettings
        // to SunShadow::SetParams. Off by default => no shadow pass cost.
        bool  sunShadows           = true;
        int   sunShadowResolution  = 4096;    // depth map size: 1024 / 2048 / 4096
        float sunShadowDistance    = 1500.0f; // ortho half-extent (world u) around hero
        float sunShadowDarkness    = 0.5f;    // 0 = none .. 1 = black under shadow
        float sunShadowSoftness    = 1.5f;   // PCF radius (texels); higher = softer edge
        float sunShadowBias        = 1.0f;    // slope-bias scale; raise to kill acne

        // Planar water reflection (flat water-surface plane + mirror RTT;
        // WaterReflection module, NOT a screen-space pass). Per-map so each map
        // keeps its own water color/look. Distributed by Chain::ApplySettings.
        bool  waterReflect      = true;
        float waterStrength     = 0.45f;   // reflection blend (0 = flat tint .. 1 = mirror)
        float waterTintR        = 0.10f;   // flat water color
        float waterTintG        = 0.18f;
        float waterTintB        = 0.26f;
        float waterTintOpacity  = 0.30f;   // master water opacity
        float waterGrow         = 0.0f;    // plane grow margin in tiles (grow-only)
        float waterBlur         = 1.5f;    // reflection global blur (texels)
        float waterClipDepth    = 90.0f;   // how far below surface still reflects
        float waterHeightGate   = 9999.0f; // skip water tiles this far from plane (off=big)
    };
}
