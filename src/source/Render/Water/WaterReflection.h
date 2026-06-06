// ============================================================================
//  WaterReflection.h  —  planar water reflection (render-to-texture mirror)
// ----------------------------------------------------------------------------
//  Renders the world from a camera MIRRORED across the water plane (z -> 2h-z)
//  into an offscreen texture, to be sampled by the water tiles for true planar
//  reflections (replacing the legacy scrolled-texture "reflection").
//
//  M1 scope: produce a correct mirrored-world texture + a debug overlay to
//  validate it. Water-tile sampling, clip plane, and Fresnel/distortion come in
//  later milestones.
//
//  Reuses the world renderers (RenderTerrain/RenderObjects) with overridden GL
//  matrices, like the (reverted) minimap bake. Runs each frame BEFORE the main
//  world render (so the texture is ready when water draws), and suppresses the
//  sun-shadow caster collection while rendering (Rendering()).
// ============================================================================
#pragma once

namespace WaterReflection
{
    void SetEnabled(bool on);
    bool Enabled();

    // The water plane height in world Z (the mirror plane). Auto-set per frame
    // for now from the hero's ground; will become per-map tunable.
    void SetWaterHeight(float h);

    // Reflection blend strength over the water texture (0..1).
    void SetStrength(float s);

    // Edge-feather radius in texels for the reflection blur (0 = off).
    void SetBlur(float texels);

    // Depth (world units) below the water plane that still reflects; geometry
    // deeper than (waterH - this) is clipped out of the reflection.
    void SetClipDepth(float d);

    // Skip water tiles whose height differs from the mirror plane by more than
    // this (cuts water texture painted up cliffs). Large value = no gate.
    void SetHeightGate(float g);

    // Shore-fade radius in TILES ("border-radius"): rounds the blocky water
    // outline into a smooth curve. The terrain pass reads this when collecting.
    void SetEdgeRadius(float t);

    // Water tint laid under the reflection (covers the blocky base-water edge and
    // colors the organic shore extension). Opacity 0 = off.
    void SetTint(float r, float g, float b);
    void SetTintOpacity(float o);

    // Toggle the bottom-left reflection preview overlay.
    void SetDebugOverlay(bool on);

    // --- Live-tuning accessors (debug panel) --------------------------------
    float GetStrength();
    float GetBlur();
    float GetClipDepth();
    float GetHeightGate();
    float GetEdgeRadius();
    void  GetTint(float* rgb);
    float GetTintOpacity();
    float GetMeasuredLevel();   // current dominant water level (read-only)
    bool  DebugOverlayEnabled();

    // Render the mirrored world into the reflection texture. Call after the
    // camera matrices are set, before the main world render. No-op when off.
    void Build();

    // --- Water-surface mesh (built per map on load) -------------------------
    // Draw the prebuilt water-surface mesh (tint + projective reflection). Call
    // after the terrain renders (so depth occludes it). No-op when off / no water.
    void DrawSurface();

    // True only while the reflection pass is rendering — read by the sun-shadow
    // caster collectors so the mirrored geometry doesn't pollute the shadow map.
    bool Rendering();

    unsigned int Texture();   // GL color texture of the mirrored world
    bool         Ready();

    void DebugDraw();         // corner overlay (validation)
    void Shutdown();
}
