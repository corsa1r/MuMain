// ============================================================================
//  ColorEffectPasses.h  —  the simple single-in/single-out color effects
// ----------------------------------------------------------------------------
//  Six full-screen effects that all share the same shape: sample ONE source
//  texture, run a fragment shader, write ONE destination. They differ only in
//  their fragment program and uniforms, so they share a tiny base class
//  (FullscreenPass) that owns the boilerplate (compile, bind, draw). Bloom is
//  NOT here — it is multi-target internally and lives in BloomPass.
//
//  Canonical chain order (set in PostProcessChain): Bloom -> ToneMap ->
//  ColorGrade -> FXAA -> Sharpen -> Vignette -> FilmGrain. Each is gated by its
//  own config flag; disabled passes are skipped by the chain.
//
//  Adding a new color effect later = add a subclass here with a Fragment()
//  string and (optionally) OnProgramLinked()/SetParams(). Nothing else changes.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    // ------------------------------------------------------------------------
    //  Base: one source texture (uScene @ unit 0) -> one destination FBO.
    // ------------------------------------------------------------------------
    class FullscreenPass : public IPostProcessPass
    {
    public:
        bool Enabled() const override { return m_active; }
        void SetActive(bool active)   { m_active = active; }
        bool IsActive() const         { return m_active; }

        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

    protected:
        // The fragment shader source for this effect (GLSL 120).
        virtual const char* Fragment() const = 0;
        // Cache extra uniform locations after the program links.
        virtual void OnProgramLinked() {}
        // Set this effect's extra uniforms each frame before the draw.
        virtual void SetParams(const PassContext& ctx) {}

        GLuint m_program = 0;
        GLint  m_locScene = -1;
        bool   m_active   = true;
    };

    // ------------------------------------------------------------------------
    //  Tone mapping — ACES filmic curve with exposure. Rolls bright values
    //  (incl. bloom) off smoothly instead of clipping to flat white.
    // ------------------------------------------------------------------------
    class ToneMapPass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "ToneMap"; }
        void SetExposure(float e) { m_exposure = e; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        float m_exposure = 1.0f;
        GLint m_locExposure = -1;
    };

    // ------------------------------------------------------------------------
    //  Color grading — brightness, warm/cool shift, contrast, saturation.
    // ------------------------------------------------------------------------
    class ColorGradePass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "ColorGrade"; }
        void SetContrast(float v)    { m_contrast = v; }
        void SetSaturation(float v)  { m_saturation = v; }
        void SetBrightness(float v)  { m_brightness = v; }
        void SetTemperature(float v) { m_temperature = v; }
        // Per-tonal-zone gain (1 = neutral). Shadows act on dark pixels,
        // highlights on bright pixels, midtones on the middle — weighted by
        // pixel luminance so each control targets its own range.
        void SetShadows(float v)     { m_shadows = v; }
        void SetMidtones(float v)    { m_midtones = v; }
        void SetHighlights(float v)  { m_highlights = v; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        float m_contrast = 1.05f, m_saturation = 1.10f, m_brightness = 1.0f, m_temperature = 0.10f;
        float m_shadows = 1.0f, m_midtones = 1.0f, m_highlights = 1.0f;
        GLint m_locContrast = -1, m_locSaturation = -1, m_locBrightness = -1, m_locTemperature = -1;
        GLint m_locShadows = -1, m_locMidtones = -1, m_locHighlights = -1;
    };

    // ------------------------------------------------------------------------
    //  FXAA — luma-based edge anti-aliasing (NVIDIA simplified, GLSL 120).
    // ------------------------------------------------------------------------
    class FxaaPass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "FXAA"; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        GLint m_locTexel = -1;   // (1/width, 1/height)
    };

    // ------------------------------------------------------------------------
    //  Sharpen — CAS-style unsharp (center vs 4-neighbour).
    // ------------------------------------------------------------------------
    class SharpenPass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "Sharpen"; }
        void SetStrength(float v) { m_strength = v; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        float m_strength = 0.30f;
        GLint m_locTexel = -1, m_locStrength = -1;
    };

    // ------------------------------------------------------------------------
    //  Vignette — radial edge darkening.
    // ------------------------------------------------------------------------
    class VignettePass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "Vignette"; }
        void SetStrength(float v) { m_strength = v; }
        void SetRadius(float v)   { m_radius = v; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        float m_strength = 0.35f, m_radius = 0.75f;
        GLint m_locStrength = -1, m_locRadius = -1;
    };

    // ------------------------------------------------------------------------
    //  Film grain / dither — animated hash noise to break 8-bit banding.
    // ------------------------------------------------------------------------
    class FilmGrainPass final : public FullscreenPass
    {
    public:
        const char* Name() const override { return "FilmGrain"; }
        void SetStrength(float v) { m_strength = v; }
    protected:
        const char* Fragment() const override;
        void OnProgramLinked() override;
        void SetParams(const PassContext& ctx) override;
    private:
        float m_strength = 0.05f;
        float m_time = 0.0f;     // accumulates so the grain animates
        GLint m_locStrength = -1, m_locTime = -1;
    };
}
