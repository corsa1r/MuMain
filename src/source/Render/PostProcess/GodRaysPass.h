// ============================================================================
//  GodRaysPass.h  —  volumetric light shafts (depth-marched, directional)
// ----------------------------------------------------------------------------
//  For every pixel we reconstruct its world height from the depth buffer and
//  MARCH toward the sun's screen position, sampling the scene height at each
//  step. A step that rises more than 'occluder height' world-units above THIS
//  pixel means a 3D object stands between the pixel and the sun, so the pixel is
//  in shadow. The lit fraction along that march is the shaft value: open
//  corridors stay bright (beams), object-blocked streaks go dark (shadow-rays).
//
//  Key properties (vs. the old radial-scatter version):
//    * Directional — shadows fall away from the sun, reading as sky-light at an
//      angle, not a glow rising from the ground.
//    * Stable — each pixel compares against its OWN reconstructed height, so
//      there is no global ground estimate to jump as the camera scrolls (no
//      flicker).
//    * Real occlusion — 3D objects genuinely block the beams; works with no
//      visible sky (top-down / underwater / indoor).
//
//  Three half-res stages: march (light fraction) -> soften (box blur) ->
//  additive composite over the scene. Recreated on resize.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class GodRaysPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "GodRays"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;
        bool Enabled() const override { return m_active; }

        void SetActive(bool a) { m_active = a; }
        bool IsActive() const  { return m_active; }

        // uLight = sun screen position [0,1] (Y: 1 = top). The march direction is
        // toward it, so shadows fall away from it. density = march reach toward
        // the sun; decay = along-march falloff; threshold = occluder height (world
        // units a sample must rise above the pixel to block it); intensity = final
        // additive scale; samples = march steps; color = beam tint. (weight is
        // retained for config compatibility but no longer used.)
        void SetLightPos(float x, float y) { m_lightX = x; m_lightY = y; }
        void SetSunZ(float z)              { m_sunZ = z; }
        void SetDensity(float v)   { m_density = v; }
        void SetWeight(float v)    { m_weight = v; }
        void SetDecay(float v)     { m_decay = v; }
        void SetThreshold(float v) { m_threshold = v; }
        void SetIntensity(float v) { m_intensity = v; }
        void SetSamples(int v)     { m_samples = v; }
        void SetColor(float r, float g, float b) { m_r = r; m_g = g; m_b = b; }

    private:
        void Destroy();
        bool Create(int halfW, int halfH);

        // March stage: depth -> light fraction.
        GLuint m_occProg = 0;
        GLint  m_occLocDepth = -1, m_occLocLight = -1, m_occLocThresh = -1,
               m_occLocInvView = -1, m_occLocProj = -1, m_occLocHasInv = -1,
               m_occLocDensity = -1, m_occLocDecay = -1, m_occLocSamples = -1,
               m_occLocSunDir = -1, m_occLocHasSunDir = -1, m_occLocSlope = -1,
               m_occLocSunWorld = -1;

        // Soften stage: box blur of the half-res mask.
        GLuint m_blurProg = 0;
        GLint  m_blurLocTex = -1, m_blurLocTexel = -1;

        // Composite stage: scene + shafts.
        GLuint m_compProg = 0;
        GLint  m_compLocScene = -1, m_compLocRays = -1, m_compLocColor = -1, m_compLocShadowDark = -1;

        // Half-res scratch: light-fraction mask + softened mask.
        GLuint m_maskFBO = 0, m_maskTex = 0;
        GLuint m_blurFBO = 0, m_blurTex = 0;

        int   m_halfW = 0, m_halfH = 0;

        bool  m_active    = false;
        float m_lightX    = 1.0f,  m_lightY = 0.0f;
        float m_sunZ      = 2.0f;
        float m_density   = 0.9f;
        float m_weight    = 0.5f;   // legacy/no-op (kept for config compatibility)
        float m_decay     = 0.95f;
        float m_threshold = 100.0f; // occluder height (world units above the pixel)
        float m_intensity = 0.6f;
        int   m_samples   = 64;
        float m_r = 1.0f, m_g = 0.9f, m_b = 0.7f;
    };
}
