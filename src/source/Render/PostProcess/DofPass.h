// ============================================================================
//  DofPass.h  —  depth of field (cinematic focus blur)
// ----------------------------------------------------------------------------
//  Keeps a focus band sharp and blurs everything nearer/farther, using the
//  scene depth the chain already captures. Same 3-stage shape as SsaoPass:
//    1. Blur H : downsample the scene color into a HALF-res target (blur X).
//    2. Blur V : blur the half-res target on Y.
//    3. Composite: per pixel compute the circle-of-confusion from depth vs the
//                  focus plane, then mix(sharp scene, blurred) by it -> dest.
//
//  Needs the camera projection (ctx.nearZ/farZ) to linearize depth. If those
//  are zero (proj couldn't be read) it outputs the scene unchanged.
//
//  Focus is a fixed view-space distance (the MU camera trails the hero at a
//  ~constant distance, so a tuned focus plane keeps the hero sharp). Off by
//  default — opt-in cinematic look.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class DofPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "DepthOfField"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

        bool Enabled() const override { return m_active; }
        void SetActive(bool active) { m_active = active; }

        // Focus = view-space distance kept sharp. Range = half-width of the sharp
        // band around Focus. Blur = bokeh size (half-res Gaussian step scale).
        void SetFocus(float v) { m_focus = v; }
        void SetRange(float v) { m_range = v; }
        void SetBlur(float v)  { m_blur  = v; }

    private:
        void Destroy();
        bool Create(int w, int h);

        // Separable blur (half-res).
        GLuint m_blurProg = 0;
        GLint  m_blurLocTex = -1, m_blurLocStep = -1;
        // Composite.
        GLuint m_compProg = 0;
        GLint  m_compScene = -1, m_compBlur = -1, m_compDepth = -1;
        GLint  m_compNear = -1, m_compFar = -1, m_compFocus = -1, m_compRange = -1;

        // Half-res scratch targets.
        GLuint m_fboA = 0, m_texA = 0;
        GLuint m_fboB = 0, m_texB = 0;

        int   m_w = 0, m_h = 0;     // full scene size
        int   m_hw = 0, m_hh = 0;   // half size
        float m_focus = 800.0f;     // view-space focus distance
        float m_range = 400.0f;     // sharp half-width
        float m_blur  = 1.5f;       // bokeh size (Gaussian step scale)
        bool  m_active = false;
    };
}
