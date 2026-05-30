// ============================================================================
//  PostProcessChain.h  —  off-screen scene RTV + modular post-process chain
// ----------------------------------------------------------------------------
//  Requirement #3, realised for the GL renderer.
//
//  FRAME SHAPE (when enabled):
//      BeginSceneCapture()        // bind scene RTV; the whole game frame draws
//                                 // into our color+depth textures, not FBO 0
//        ...existing scene render (terrain, models, effects, soft shadows)...
//      EndSceneCaptureAndPresent()// run the pass list scene -> ... -> backbuffer
//      (ImGui overlay + SwapBuffers happen AFTER, drawing onto the backbuffer)
//
//  PARITY SWITCH:
//      When disabled (the default), BeginSceneCapture/EndSceneCaptureAndPresent
//      are no-ops, the scene renders straight to FBO 0 exactly as before, and
//      ActiveSceneFramebuffer() stays 0. "Off" is therefore pixel-identical to
//      the legacy path — the safe baseline we ship behind a config flag.
//
//  SOFTSHADOW INTEROP:
//      SoftShadow::Composite() historically assumes the scene lives in FBO 0
//      (it composites to 0 and blits scene depth from 0). When we redirect the
//      scene into the RTV, that assumption must follow the scene. SoftShadow
//      reads ActiveSceneFramebuffer() instead of the literal 0, so its proven
//      blur+composite algorithm is unchanged — only its target moves with us.
//
//  EXTENSIBILITY:
//      AddPass() appends an IPostProcessPass. FXAA, bloom, dynamic fog, and the
//      Dear ImGui overlay are all just passes added here — no scene-code edits.
// ============================================================================
#pragma once

// NOTE: deliberately no <gl/glew.h> here. This header is included by legacy
// translation units (Winmain.cpp, SceneManager.cpp) that may already have
// pulled in <gl/gl.h>; glew.h hard-errors if gl.h preceded it. We expose the
// framebuffer handle as a plain unsigned int (which is exactly what GLuint is)
// so the public surface stays GL-include-agnostic. The .cpp pulls in glew.
namespace PostProcess
{
    class IPostProcessPass;

    namespace Chain
    {
        // Create GL objects for the post chain (proc table, default passthrough
        // pass). Call once on a current GL context, after the window/context and
        // any glewInit-equivalent are ready. Safe to call when the feature is
        // disabled — it just primes resources. Returns false if the context
        // lacks the required entry points (chain then stays inert/passthrough).
        bool Init(int width, int height);
        void Shutdown();

        // (Re)allocate scene-sized targets. Call on startup and on every window
        // resize, alongside SoftShadow::Resize. No-op if size is unchanged.
        void Resize(int width, int height);

        // Master switch (wired to the config flag). When false the capture
        // calls below do nothing and the scene renders straight to the
        // backbuffer — guaranteed parity.
        void SetEnabled(bool enabled);
        bool IsEnabled();

        // True only when enabled AND the GL objects are healthy.
        bool IsActive();

        // ---- Per-frame -------------------------------------------------------
        // Bind the scene RTV so subsequent scene drawing is captured. Must be
        // called BEFORE the scene's glClear. No-op when inactive.
        void BeginSceneCapture();

        // Run the pass list (scene RTV -> ... -> backbuffer). Restores GL state
        // and leaves FBO 0 bound so the ImGui overlay + SwapBuffers proceed
        // normally. No-op when inactive. 'deltaSeconds' feeds time-based passes.
        void EndSceneCaptureAndPresent(float deltaSeconds);

        // ---- Pass registry ---------------------------------------------------
        // Append a pass (chain takes ownership). Passes run in registration
        // order; the last one outputs to the backbuffer.
        void AddPass(IPostProcessPass* pass);

        // ---- SoftShadow interop ---------------------------------------------
        // The framebuffer the scene is currently rendering into: the scene RTV
        // while capturing, otherwise 0 (the backbuffer). Exposed for any future
        // capture-aware code that needs to know the live scene target. (The
        // SoftShadow module stays decoupled and instead reads the currently
        // bound framebuffer, which gives the same answer with no dependency.)
        // ---- Bloom / runtime toggle -----------------------------------------
        // Toggle the bloom effect at runtime; returns the new state. Also
        // force-enables the chain so bloom shows even if PostProcess was off in
        // config. Bound to F7 in Winmain for live A/B comparison.
        bool ToggleBloom();
        bool IsBloomActive();

        // Apply config-driven bloom settings: 'enabled' is [Graphics] Bloom,
        // 'strength' is [Graphics] BloomStrength (1 = baseline glow, scaled
        // linearly, clamped). Call after Init(). Does not enable the chain
        // itself — that is governed by SetEnabled()/[Graphics] PostProcess.
        void ConfigureBloom(bool enabled, int strength);

        unsigned int ActiveSceneFramebuffer();
    }
}
