#pragma once
// ============================================================================
//  SunDirection.h  —  one shared world-space sun for shadows + god rays
// ----------------------------------------------------------------------------
//  Single source of truth for the scene's sun direction. BOTH the legacy
//  character/object shadow shear (BMD::RenderBodyShadow) and the volumetric
//  god-ray pass read it, so the two are consistent by construction — point the
//  sun once and shadows + shafts agree.
//
//  x,y = horizontal azimuth (which way the sun is), z = up/elevation. Shadows
//  fall opposite the horizontal direction; their length scales with horiz/z.
//  The default (1,0,2) reproduces the legacy look exactly (shearX = -0.5,
//  shearY = 0 — i.e. shadows along world -X at half the object height).
// ============================================================================
namespace BloodlustMU
{
    struct SunDirection { float x = 1.0f, y = 0.0f, z = 2.0f; };

    // Live, world-space direction TO the sun. Updated from the god-ray settings
    // (config / editor / per-map preset) via PostProcess::Chain::ApplySettings.
    extern SunDirection g_SunDirection;

    // Per-unit-height shadow shear in world X/Y derived from the sun: a vertex at
    // height h is displaced by (h*shearX, h*shearY). Falls back to the legacy
    // (-0.5, 0) when the sun is degenerate (no horizontal / not above ground).
    void GetShadowShear(float& shearX, float& shearY);
}
