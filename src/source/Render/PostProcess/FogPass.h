// ============================================================================
//  FogPass.h  —  depth-based atmospheric fog
// ----------------------------------------------------------------------------
//  Depth-aware post pass (reuses the SSAO infrastructure: scene depth texture +
//  camera projection in PassContext). Blends a fog color into the scene by
//  view distance, so far geometry fades into haze — the classic dark-fantasy
//  "depth" cue. Richer and art-directable vs. MU's legacy flat glFog.
//
//  Two combined falloffs:
//    * Distance fog  — exponential-squared on linear view depth (start..density)
//    * Height fog    — optional extra density below a world height, recovered
//                      from the reconstructed view-space Y (mist pooling in low
//                      ground / dungeons). Disabled when heightStrength == 0.
//
//  Sky/!geometry pixels (depth >= ~1) get FULL fog so the horizon reads as haze
//  rather than a hard skybox edge. Needs valid projection (nearZ>0); otherwise
//  it passes the scene through unchanged. Off by default.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class FogPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "Fog"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

        bool Enabled() const override { return m_active; }
        void SetActive(bool active) { m_active = active; }
        bool IsActive() const       { return m_active; }

        void SetColor(float r, float g, float b) { m_r = r; m_g = g; m_b = b; }
        void SetDensity(float d)        { m_density = d; }        // overall thickness
        void SetStart(float s)          { m_start = s; }          // 0..1 of far plane before fog begins
        void SetHeightStrength(float h) { m_heightStrength = h; } // 0 = no height fog
        void SetHeightTop(float y)      { m_heightTop = y; }      // world Y below which height fog grows

    private:
        GLuint m_program = 0;
        GLint  m_locScene = -1, m_locDepth = -1;
        GLint  m_locNear = -1, m_locFar = -1, m_locTanX = -1, m_locTanY = -1;
        GLint  m_locColor = -1, m_locDensity = -1, m_locStart = -1;
        GLint  m_locHeightStrength = -1, m_locHeightTop = -1;
        GLint  m_locInvView = -1;

        float m_r = 0.04f, m_g = 0.05f, m_b = 0.07f;  // cool dark-fantasy haze
        float m_density = 0.6f;
        float m_start   = 0.30f;
        float m_heightStrength = 0.0f;   // off by default — distance fog only
        float m_heightTop      = 200.0f; // world units; tune per map
        bool  m_active  = false;
    };
}
