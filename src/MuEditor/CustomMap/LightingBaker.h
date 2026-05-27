#pragma once

#ifdef _EDITOR

// Offline lighting baker for custom maps. Writes a per-vertex 256×256
// RGB ambient buffer into TerrainLight.OZJ at the slot root, then reloads
// it through the engine's OpenTerrainLight path so the change appears
// without a map reload. The engine's CreateTerrainLight pass (slope-vs-
// hardcoded-light) layers a soft directional fill on top.
//
// Algorithm per vertex:
//   1. Compute world position from BackTerrainHeight + the (x*scale,y*scale)
//      grid coordinate, plus a finite-difference surface normal.
//   2. Cast one sun-shadow ray along -sunDir; if it clears the heightmap
//      (no occluder within max distance), the vertex is sunlit.
//   3. Cast `aoSamples` hemisphere rays for ambient occlusion. Each ray
//      that escapes a short distance counts toward the AO factor.
//   4. final = ambientFloor + skyColor*ao + (sunlit ? sunColor*max(0,N·L) : 0)
//
// Costs roughly aoSamples * 65536 ray-marches; with default 32 samples
// and a heightmap-only intersector that's a few hundred ms to a few
// seconds depending on terrain complexity — fine for a Bake button.

namespace MuEditor { namespace CustomMap {

struct LightingBakeParams
{
    // Sun expressed as azimuth (0..360 around world Z, 0 = +X) plus
    // altitude (0..90, 0 = horizon, 90 = directly overhead). The bake
    // converts to a unit direction vector internally.
    float sunAzimuth   = 135.0f;
    float sunAltitude  = 55.0f;

    // Direct-sun color (multiplied by N·L when the shadow ray clears).
    float sunColor[3]  = { 1.00f, 0.95f, 0.85f };

    // Ambient sky tint. Modulated by the AO escape factor so crevices
    // and under-object zones darken naturally.
    float skyColor[3]  = { 0.45f, 0.55f, 0.70f };

    // Minimum lighting added everywhere so deep shadows aren't pure
    // black. Keep small (0.05–0.20) for stylized contrast.
    float ambientFloor = 0.12f;

    // Hemisphere ray count for AO. 16 = fast preview, 64 = good
    // quality. Each ray walks a short distance (aoMaxDistance) and
    // counts as "escaped" if no occluder is found.
    int   aoSamples    = 32;

    // Max AO ray length in world units. TERRAIN_SCALE is 100, so
    // 1500 ≈ 15 tiles — long enough to catch nearby walls/cliffs
    // without darkening the entire map under distant geometry.
    float aoMaxDistance = 1500.0f;

    // Per-channel multiplier on the AO contribution. 1.0 = full,
    // <1 = wash out, >1 = darken more aggressively (caps at 1.0).
    float aoStrength   = 1.0f;
};

// Bakes lighting using the live BackTerrainHeight buffer (the height
// the user just sculpted), writes a fresh TerrainLight.OZJ to the
// slot folder, and reloads it via OpenTerrainLight so the user sees
// the new lighting on the next frame. Returns false if the slot id
// is out of range or the JPEG encode/file write fails.
bool BakeLighting(int mapId, const LightingBakeParams& params);

}} // namespace MuEditor::CustomMap

#endif // _EDITOR
