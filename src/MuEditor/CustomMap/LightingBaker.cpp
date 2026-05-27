#include "stdafx.h"

#ifdef _EDITOR

#include "CustomMap/LightingBaker.h"
#include "CustomMap/CustomMapIO.h"           // WriteTerrainLightRGB, ReloadTerrainLightFromSlot

#include "Core/Globals/_define.h"            // TERRAIN_SIZE, TERRAIN_SCALE
#include "Render/Terrain/ZzzLodTerrain.h"    // BackTerrainHeight extern + TERRAIN_INDEX

#include <algorithm>
#include <cmath>
#include <vector>

namespace MuEditor { namespace CustomMap {

namespace {

constexpr float PI_F = 3.14159265358979323846f;

// World-space step size for ray-marching the heightmap. Half a tile
// strikes a decent balance: too coarse and small ridges leak light,
// too fine and the bake time blows up. With TERRAIN_SCALE=100 this is
// 50 units = 0.5 tiles per step.
constexpr float MARCH_STEP = TERRAIN_SCALE * 0.5f;

// Sun shadow rays travel this far before declaring the vertex sunlit.
// Anything within 15000 units (~150 tiles ≈ 60% of the map) is what
// you'd reasonably consider as casting a meaningful shadow; beyond
// that the sun ray would exit the map anyway.
constexpr float SUN_RAY_MAX = 15000.0f;

// Bias the ray origin slightly along the surface normal so it doesn't
// self-intersect with its own vertex. 2.0 units is plenty given our
// 50-unit march step.
constexpr float RAY_BIAS = 2.0f;

inline int ClampIndex(int v) { return std::max(0, std::min(TERRAIN_SIZE - 1, v)); }

// Bilinear height sample at arbitrary world (x, y). Outside the map
// we return a large positive value so rays leaving the bounds are
// counted as escaped (no occlusion) rather than blocked.
float SampleHeight(float wx, float wy)
{
    const float fx = wx / TERRAIN_SCALE;
    const float fy = wy / TERRAIN_SCALE;
    if (fx < 0.0f || fx >= TERRAIN_SIZE - 1 ||
        fy < 0.0f || fy >= TERRAIN_SIZE - 1)
    {
        return -1e9f; // Off-map: treat as "below" so rays escape cleanly.
    }
    const int   ix = static_cast<int>(fx);
    const int   iy = static_cast<int>(fy);
    const float tx = fx - ix;
    const float ty = fy - iy;
    const float h00 = BackTerrainHeight[TERRAIN_INDEX(ix,     iy    )];
    const float h10 = BackTerrainHeight[TERRAIN_INDEX(ix + 1, iy    )];
    const float h01 = BackTerrainHeight[TERRAIN_INDEX(ix,     iy + 1)];
    const float h11 = BackTerrainHeight[TERRAIN_INDEX(ix + 1, iy + 1)];
    const float h0 = h00 * (1.0f - tx) + h10 * tx;
    const float h1 = h01 * (1.0f - tx) + h11 * tx;
    return h0 * (1.0f - ty) + h1 * ty;
}

// Returns true if the ray from `origin` along `dir` (unit) hits the
// heightmap within `maxDist`. Walks the ray in fixed steps, comparing
// ray Z against the bilinear terrain height at the ray's XY footprint.
// Skips rays going downward into the ground via the bias on origin.
bool RayHitsTerrain(const float origin[3], const float dir[3], float maxDist)
{
    // Reject rays pointing downward enough to immediately re-enter the
    // surface — those would always "hit" and produce false occlusion.
    // The hemisphere sampler should never generate these (we orient to
    // the normal), but be defensive.
    if (dir[2] < -0.001f && origin[2] - SampleHeight(origin[0], origin[1]) < 1.0f)
        return true;

    for (float t = MARCH_STEP; t < maxDist; t += MARCH_STEP)
    {
        const float px = origin[0] + dir[0] * t;
        const float py = origin[1] + dir[1] * t;
        const float pz = origin[2] + dir[2] * t;
        const float h  = SampleHeight(px, py);
        if (h > -1e8f && pz < h)
            return true;
    }
    return false;
}

// Surface normal from height differences in a 3x3 stencil. Edges use
// clamped indices so the border vertices get sensible (mostly-up)
// normals instead of zero.
void ComputeNormal(int x, int y, float out[3])
{
    const float hL = BackTerrainHeight[TERRAIN_INDEX(ClampIndex(x - 1), y)];
    const float hR = BackTerrainHeight[TERRAIN_INDEX(ClampIndex(x + 1), y)];
    const float hD = BackTerrainHeight[TERRAIN_INDEX(x, ClampIndex(y - 1))];
    const float hU = BackTerrainHeight[TERRAIN_INDEX(x, ClampIndex(y + 1))];
    // Tangent vectors across one tile in X and Y, then cross product.
    // dz/dx = (hR-hL) / (2*scale); dz/dy = (hU-hD) / (2*scale).
    // Normal = (-dz/dx, -dz/dy, 1) before normalize.
    const float dzdx = (hR - hL) / (2.0f * TERRAIN_SCALE);
    const float dzdy = (hU - hD) / (2.0f * TERRAIN_SCALE);
    float nx = -dzdx;
    float ny = -dzdy;
    float nz = 1.0f;
    const float inv = 1.0f / std::sqrt(nx*nx + ny*ny + nz*nz);
    out[0] = nx * inv;
    out[1] = ny * inv;
    out[2] = nz * inv;
}

// Halton sequence (radical inverse base b) — produces a low-discrepancy
// 1D point in [0,1). We use base 2 and base 3 together to get a stable
// 2D distribution for hemisphere sampling, which prevents the AO from
// flickering and gives much cleaner convergence than uniform rand().
float Halton(int index, int base)
{
    float f = 1.0f;
    float r = 0.0f;
    int   i = index;
    while (i > 0)
    {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

// Cosine-weighted hemisphere sample around +Z, then rotated to the
// vertex's normal frame. Cosine weighting matches Lambert AO formula
// and concentrates samples where the AO weighting is highest.
void HemisphereSample(int sampleIdx, const float normal[3], float out[3])
{
    const float u1 = Halton(sampleIdx + 1, 2);
    const float u2 = Halton(sampleIdx + 1, 3);
    const float r  = std::sqrt(u1);
    const float theta = 2.0f * PI_F * u2;
    const float lx = r * std::cos(theta);
    const float ly = r * std::sin(theta);
    const float lz = std::sqrt(std::max(0.0f, 1.0f - u1));

    // Build an orthonormal basis (tangent, bitangent, normal) using the
    // standard "pick the smaller axis as tangent helper" trick — avoids
    // the degenerate case when the normal is parallel to a fixed axis.
    float t[3];
    if (std::abs(normal[0]) > std::abs(normal[2]))
    {
        const float inv = 1.0f / std::sqrt(normal[0]*normal[0] + normal[1]*normal[1]);
        t[0] = -normal[1] * inv;
        t[1] =  normal[0] * inv;
        t[2] = 0.0f;
    }
    else
    {
        const float inv = 1.0f / std::sqrt(normal[1]*normal[1] + normal[2]*normal[2]);
        t[0] = 0.0f;
        t[1] = -normal[2] * inv;
        t[2] =  normal[1] * inv;
    }
    float b[3] = {
        normal[1]*t[2] - normal[2]*t[1],
        normal[2]*t[0] - normal[0]*t[2],
        normal[0]*t[1] - normal[1]*t[0],
    };

    out[0] = lx * t[0] + ly * b[0] + lz * normal[0];
    out[1] = lx * t[1] + ly * b[1] + lz * normal[1];
    out[2] = lx * t[2] + ly * b[2] + lz * normal[2];
    const float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    if (len > 1e-6f)
    {
        out[0] /= len; out[1] /= len; out[2] /= len;
    }
}

inline unsigned char ToByte(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return static_cast<unsigned char>(v * 255.0f + 0.5f);
}

} // anonymous namespace

bool BakeLighting(int mapId, const LightingBakeParams& params)
{
    // Convert sun angles → unit direction in world space. Azimuth is
    // measured from +X around +Z (counter-clockwise looking down);
    // altitude is the elevation above the XY plane.
    const float az = params.sunAzimuth  * (PI_F / 180.0f);
    const float al = params.sunAltitude * (PI_F / 180.0f);
    const float sunDir[3] = {
        std::cos(al) * std::cos(az),
        std::cos(al) * std::sin(az),
        std::sin(al),
    };

    const int   N           = TERRAIN_SIZE;
    const int   aoSamples   = std::max(1, params.aoSamples);
    const float aoDistance  = std::max(50.0f, params.aoMaxDistance);
    const float aoStrength  = std::max(0.0f, params.aoStrength);
    const float ambient     = std::max(0.0f, params.ambientFloor);

    std::vector<unsigned char> rgb(static_cast<size_t>(N) * N * 3, 0);

    for (int y = 0; y < N; ++y)
    {
        for (int x = 0; x < N; ++x)
        {
            const int idx = TERRAIN_INDEX(x, y);

            float normal[3];
            ComputeNormal(x, y, normal);

            const float origin[3] = {
                x * TERRAIN_SCALE + normal[0] * RAY_BIAS,
                y * TERRAIN_SCALE + normal[1] * RAY_BIAS,
                BackTerrainHeight[idx] + normal[2] * RAY_BIAS + RAY_BIAS,
            };

            // Sun shadow: ray toward the sun. Sunlit if it clears.
            float sunContrib[3] = { 0.0f, 0.0f, 0.0f };
            const float NdotL = normal[0]*sunDir[0] + normal[1]*sunDir[1] + normal[2]*sunDir[2];
            if (NdotL > 0.0f)
            {
                if (!RayHitsTerrain(origin, sunDir, SUN_RAY_MAX))
                {
                    sunContrib[0] = params.sunColor[0] * NdotL;
                    sunContrib[1] = params.sunColor[1] * NdotL;
                    sunContrib[2] = params.sunColor[2] * NdotL;
                }
            }

            // Sky AO: hemisphere samples; count escapes for the AO factor.
            int   escaped = 0;
            for (int s = 0; s < aoSamples; ++s)
            {
                float dir[3];
                HemisphereSample(s, normal, dir);
                if (!RayHitsTerrain(origin, dir, aoDistance))
                    ++escaped;
            }
            const float aoFactor = static_cast<float>(escaped) / static_cast<float>(aoSamples);
            const float ao       = std::pow(aoFactor, aoStrength);

            // Compose final per-vertex color.
            float r = ambient + params.skyColor[0] * ao + sunContrib[0];
            float g = ambient + params.skyColor[1] * ao + sunContrib[1];
            float b = ambient + params.skyColor[2] * ao + sunContrib[2];

            const size_t outIdx = (static_cast<size_t>(y) * N + x) * 3;
            rgb[outIdx + 0] = ToByte(r);
            rgb[outIdx + 1] = ToByte(g);
            rgb[outIdx + 2] = ToByte(b);
        }
    }

    if (!WriteTerrainLightRGB(mapId, rgb.data())) return false;
    ReloadTerrainLightFromSlot(mapId);
    return true;
}

}} // namespace MuEditor::CustomMap

#endif // _EDITOR
