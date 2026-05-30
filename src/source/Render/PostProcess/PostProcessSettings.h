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

        // Film grain / dither (breaks 8-bit banding in dark gradients)
        bool  filmGrain       = true;
        float filmGrainStrength = 0.05f; // additive noise amplitude
    };
}
