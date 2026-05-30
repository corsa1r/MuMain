// ============================================================================
//  SsaoPass.h  —  screen-space ambient occlusion (depth-based)
// ----------------------------------------------------------------------------
//  The first DEPTH-AWARE effect, and the payoff of capturing scene depth in the
//  RTV. It darkens contact areas (crevices, where objects meet the ground, wall
//  corners) to ground the scene and add cheap perceived depth.
//
//  Self-contained multi-pass effect, same shape as BloomPass:
//    1. AO compute  : reconstruct view pos + normal from depth, sample a
//                     hash-rotated hemisphere, accumulate occlusion -> aoTex.
//    2. Blur        : separable box blur of the AO to kill the per-pixel noise
//                     introduced by the random rotation.
//    3. Composite   : output = sceneColor * ao  ->  ctx.destFBO.
//
//  Needs the camera projection (ctx.nearZ/farZ/tanHalfFovX/Y) to turn the
//  non-linear depth buffer into view-space positions. If those are zero (proj
//  couldn't be read) it outputs the scene unchanged.
//
//  NOTE: standard SSAO only sees the solid depth the world writes; additive /
//  alpha-blended effect sprites don't contribute occluders. Off by default —
//  it's the heaviest effect and the most scene-dependent to tune.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class SsaoPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "SSAO"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

        bool Enabled() const override { return m_active; }
        void SetActive(bool active) { m_active = active; }
        bool IsActive() const       { return m_active; }

        // Radius in world units (sampling reach around each pixel). Strength
        // scales the darkening (0 = none). Power sharpens the falloff.
        void SetRadius(float r)   { m_radius = r; }
        void SetStrength(float s) { m_strength = s; }
        void SetPower(float p)    { m_power = p; }

    private:
        void Destroy();
        bool Create(int w, int h);

        // AO compute
        GLuint m_aoProg = 0;
        GLint  m_aoLocDepth = -1, m_aoLocTexel = -1, m_aoLocNear = -1, m_aoLocFar = -1;
        GLint  m_aoLocTanX = -1, m_aoLocTanY = -1, m_aoLocRadius = -1, m_aoLocStrength = -1;
        GLint  m_aoLocPower = -1, m_aoLocTime = -1;
        // Blur
        GLuint m_blurProg = 0;
        GLint  m_blurLocTex = -1, m_blurLocStep = -1;
        // Composite
        GLuint m_compProg = 0;
        GLint  m_compLocScene = -1, m_compLocAO = -1;

        // Scratch targets (full scene resolution).
        GLuint m_aoFBO = 0,   m_aoTex = 0;
        GLuint m_blurFBO = 0, m_blurTex = 0;

        int   m_w = 0, m_h = 0;
        float m_radius   = 60.0f;   // world units
        float m_strength = 1.0f;
        float m_power    = 1.5f;
        float m_time     = 0.0f;    // animates the sample rotation slightly
        bool  m_active   = false;   // off by default (heavy, scene-dependent)
    };
}
