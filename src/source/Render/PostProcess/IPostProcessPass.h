// ============================================================================
//  IPostProcessPass.h  —  the post-process extension seam
// ----------------------------------------------------------------------------
//  This is the interface requirement #3 asked for: a clean place to dock
//  future full-screen effects (FXAA/MSAA-resolve, bloom, dynamic fog) and the
//  Dear ImGui overlay — WITHOUT touching the scene render or asset loading.
//
//  CONTRACT
//  The chain renders the game scene into an off-screen color texture, then runs
//  an ordered list of IPostProcessPass objects, ping-ponging between two
//  scene-sized color textures. Each pass is a simple "sample one texture, draw
//  one full-screen quad into a destination framebuffer." The chain decides the
//  source/dest for each pass, so a pass never has to know its position in the
//  chain. The final pass's destination is the real backbuffer (FBO 0).
//
//  To add an effect later: implement this interface, register it on the chain.
//  That is the entire integration surface — by design.
// ============================================================================
#pragma once

#include <gl/glew.h>

namespace PostProcess
{
    // Everything a pass needs to read the previous result and write the next.
    struct PassContext
    {
        GLuint sourceColorTex = 0; // input: previous pass output (or scene)
        GLuint sourceDepthTex = 0; // scene depth (0 if unavailable)
        GLuint destFBO        = 0; // output framebuffer (0 == backbuffer)

        int    width  = 0;
        int    height = 0;
        float  deltaSeconds = 0.0f;

        // Camera projection params for depth-aware passes (SSAO, future fog).
        // Extracted from the live GL projection matrix at resolve time. With
        // these + sourceDepthTex a pass can reconstruct view-space position:
        //   linearDepth(d) = (2*near*far) / (far+near - (d*2-1)*(far-near))
        //   viewPos.xy     = (uv*2-1) * vec2(tanHalfFovX,tanHalfFovY) * linZ
        //   viewPos.z      = -linZ
        // All zero when the projection couldn't be read (pass should no-op AO).
        float  nearZ       = 0.0f;
        float  farZ        = 0.0f;
        float  tanHalfFovX = 0.0f;
        float  tanHalfFovY = 0.0f;
    };

    class IPostProcessPass
    {
    public:
        virtual ~IPostProcessPass() = default;

        // Stable id for ImGui toggles / config / debugging.
        virtual const char* Name() const = 0;

        // Lazily (re)create size-dependent GL objects. Called by the chain when
        // the pass is first run and after every resize. Return false to have the
        // chain skip this pass (treated as a passthrough for that frame).
        virtual bool EnsureResources(int width, int height) = 0;

        // Bind ctx.destFBO, sample ctx.sourceColorTex, draw a fullscreen quad.
        // Must NOT bind the backbuffer itself (the chain owns destFBO routing),
        // must NOT call SwapBuffers, and should leave global GL state as it found
        // it (the chain snapshots/restores the big stuff, but be a good citizen).
        virtual void Execute(const PassContext& ctx) = 0;

        // Per-frame enable. A disabled pass is skipped (its input is forwarded
        // to the next pass), so effects can be toggled at runtime with zero cost
        // and without disturbing the rest of the chain.
        virtual bool Enabled() const { return true; }
    };
}
