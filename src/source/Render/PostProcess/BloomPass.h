// ============================================================================
//  BloomPass.h  —  threshold bloom for the dark-fantasy item/skill glow
// ----------------------------------------------------------------------------
//  The first real post-process effect. Extracts bright pixels (weapon/armor
//  +9..+15 glows, skill FX, lights), blurs them, and adds them back over the
//  scene. Tuned to enhance the classic Webzen glow aesthetic rather than wash
//  the frame out.
//
//  This is also the canonical example of a self-contained multi-pass effect on
//  top of the chain: the chain hands us ONE source texture (the scene) and ONE
//  destination FBO; everything in between — bright extraction, separable blur
//  ping-pong, additive composite — is owned and sequenced inside Execute().
//  A future FXAA/SSAO/fog pass follows the exact same shape.
//
//  Internal scratch targets are HALF-resolution: cheaper, and the downscale
//  gives a softer, wider glow for free. Recreated on resize.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class BloomPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "Bloom"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

        // Skipped by the chain while inactive (zero cost until enabled).
        bool Enabled() const override { return m_active; }

        void SetActive(bool active) { m_active = active; }
        bool IsActive() const       { return m_active; }

        // Runtime-tunable knobs (sensible defaults; could be surfaced to config
        // or ImGui later without touching the chain).
        void SetThreshold(float t) { m_threshold = t; }
        void SetIntensity(float i) { m_intensity = i; }

    private:
        void Destroy();
        bool Create(int halfW, int halfH);

        // Programs
        GLuint m_brightProg = 0; GLint m_brightLocScene = -1; GLint m_brightLocThreshold = -1;
        GLuint m_blurProg   = 0; GLint m_blurLocTex = -1;     GLint m_blurLocStep = -1;
        GLuint m_compProg   = 0; GLint m_compLocScene = -1;   GLint m_compLocBloom = -1; GLint m_compLocIntensity = -1;

        // Half-res scratch targets: bright extract + one blur ping buffer.
        GLuint m_brightFBO = 0, m_brightTex = 0;
        GLuint m_blurFBO   = 0, m_blurTex   = 0;

        int   m_halfW = 0, m_halfH = 0;     // current scratch size
        float m_threshold = 0.75f;          // luminance above which a pixel blooms
        float m_intensity = 0.9f;           // additive strength of the glow
        bool  m_active    = true;           // on by default when the chain runs
    };
}
