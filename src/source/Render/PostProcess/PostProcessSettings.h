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

namespace PostProcess
{
    struct Settings
    {
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
