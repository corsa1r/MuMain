// ============================================================================
//  PassthroughPass.h  —  the identity copy that guarantees visual parity
// ----------------------------------------------------------------------------
//  The baseline pass: samples the scene texture and writes it verbatim to the
//  destination. With only this pass registered, the post chain is a no-op in
//  appearance — the off-screen detour produces the exact same pixels as drawing
//  straight to the backbuffer. It is the proof that the RTV plumbing is correct
//  before any "real" effect (FXAA, bloom, fog) is layered on top.
// ============================================================================
#pragma once

#include "IPostProcessPass.h"

namespace PostProcess
{
    class PassthroughPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "Passthrough"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

    private:
        GLuint m_program = 0;
        GLint  m_locTex  = -1;
    };
}
