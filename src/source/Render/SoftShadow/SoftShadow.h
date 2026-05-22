// SoftShadow.h
//
// Offscreen-FBO accumulator + separable Gaussian blur for character/monster
// shadows. Drop-in replacement for the previous straight-to-backbuffer shadow
// path:
//
//   Once at GL context creation: SoftShadow::Init()
//   Once at window resize:       SoftShadow::Resize(w, h)
//   Once per frame, before any character/object rendering:
//                                SoftShadow::BeginFrame()
//   For each individual shadow draw (called from BMD::RenderBodyShadow):
//                                SoftShadow::BeginShadowDraw()
//                                ...issue shadow geometry to current GL state...
//                                SoftShadow::EndShadowDraw()
//   After all shadow-casting entities are done rendering:
//                                SoftShadow::Composite()
//   At shutdown:                 SoftShadow::Shutdown()
//
// Implementation notes:
//   - We avoid GLEW init by loading the FBO/shader entry points via
//     wglGetProcAddress at Init() time. See LoadEntrypoints() in the .cpp.
//   - The shadow FBO has a single RGBA8 color texture and an 8-bit stencil
//     renderbuffer (no depth — shadows already disable depth-mask, and the
//     stencil-based non-stacking trick needs the stencil bits).
//   - Blur is a two-pass separable Gaussian (horizontal then vertical) with a
//     11-tap kernel rendered to a ping-pong FBO at the same resolution.
//   - Composite is a fullscreen alpha-blended quad over the back buffer.
//
#pragma once

#include <cstdint>

namespace SoftShadow
{
    bool Init();
    void Shutdown();

    // Window/back-buffer dimensions. Safe to call every frame; the FBO is
    // only reallocated when size actually changes.
    void Resize(int width, int height);

    // Whether the soft-shadow path is healthy. If false, callers should fall
    // back to the legacy straight-to-backbuffer shadow render.
    bool IsAvailable();

    // Per-frame lifecycle.
    void BeginFrame();   // Binds the shadow FBO and clears it.
    void Composite();    // Blurs the FBO and composites over the back buffer.

    // Per-shadow lifecycle — wraps a single BMD::RenderBodyShadow draw so
    // its triangles land in the shadow FBO instead of the back buffer.
    // The caller's modelview/projection are reused (FBO matches back-buffer
    // dimensions, so scene matrices project the same).
    void BeginShadowDraw();
    void EndShadowDraw();
}
