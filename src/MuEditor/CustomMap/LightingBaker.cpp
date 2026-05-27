#include "stdafx.h"

#ifdef _EDITOR

#include "CustomMap/LightingBaker.h"
#include "CustomMap/CustomMapIO.h"           // WriteTerrainLightRGB, ReloadTerrainLightFromSlot

#include "Core/Globals/_define.h"            // TERRAIN_SIZE, TERRAIN_SCALE
#include "Core/Globals/_TextureIndex.h"      // BITMAP_MAPTILE
#include "Render/Terrain/ZzzLodTerrain.h"    // BackTerrainHeight, TerrainMappingLayer1, TERRAIN_INDEX
#include "Render/Sprites/GlobalBitmap.h"     // Bitmaps.FindTexture
#include "Engine/Object/ZzzObject.h"         // ObjectBlock, OBJECT
#include "Engine/Object/w_ObjectInfo.h"      // OBJECT struct

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace MuEditor { namespace CustomMap {

namespace {

constexpr float PI_F = 3.14159265358979323846f;

// World-space step size for ray-marching the heightmap. Half a tile
// balances quality vs. speed (with TERRAIN_SCALE=100 → 50 units / step).
constexpr float MARCH_STEP = TERRAIN_SCALE * 0.5f;

// Sun shadow rays travel this far before the vertex is declared sunlit.
// Beyond ~150 tiles the ray would exit the map regardless.
constexpr float SUN_RAY_MAX = 15000.0f;

// Origin bias along the surface normal to keep rays from self-hitting.
constexpr float RAY_BIAS = 2.0f;

inline int ClampIndex(int v) { return std::max(0, std::min(TERRAIN_SIZE - 1, v)); }

// Bilinear height sample at arbitrary world (x, y). Off-map returns a
// large negative value so rays leaving the world escape cleanly.
inline float SampleHeight(float wx, float wy)
{
    const float fx = wx / TERRAIN_SCALE;
    const float fy = wy / TERRAIN_SCALE;
    if (fx < 0.0f || fx >= TERRAIN_SIZE - 1 ||
        fy < 0.0f || fy >= TERRAIN_SIZE - 1)
    {
        return -1e9f;
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

// Object-as-sphere shadow caster. The bake pre-builds one of these per
// live OBJECT_BLOCK occupant so the per-vertex ray loop can hit-test
// against a flat array instead of walking the spatial hash repeatedly.
struct ObjectSphere
{
    float center[3];
    float radius;          // pre-squared in `radius2` for cheap rejection
    float radius2;
};

// Ray-vs-sphere (analytic). Returns true if the ray origin+dir*t hits
// the sphere within [0, maxDist]. Only the first hit matters — we don't
// need the actual t-value, just visibility.
inline bool RayHitsSphere(const float origin[3], const float dir[3],
                          const ObjectSphere& s, float maxDist)
{
    const float ox = origin[0] - s.center[0];
    const float oy = origin[1] - s.center[1];
    const float oz = origin[2] - s.center[2];
    const float b  = ox*dir[0] + oy*dir[1] + oz*dir[2];
    const float c  = ox*ox + oy*oy + oz*oz - s.radius2;
    // If origin is outside (c > 0) and we're not heading toward the
    // sphere (b > 0), miss.
    if (c > 0.0f && b > 0.0f) return false;
    const float disc = b*b - c;
    if (disc < 0.0f) return false;
    const float t = -b - std::sqrt(disc);
    if (t < 0.0f) return false;       // sphere is behind the ray
    return t <= maxDist;
}

// Hit result: occluder found at t, plus the surface color at the hit.
// `tHit` < 0 means no hit. Color is RGB in [0,1]. Used for both shadow
// rejection (any t<maxDist counts) and one-bounce indirect (color is
// looked up at the closest hit).
struct HitInfo
{
    float t;
    float color[3];
    bool  isTerrain;
};

// Ray-march the heightmap and optionally the object spheres, returning
// the closest hit info. `objects` may be empty to bake terrain-only
// (faster). `tileColors` provides per-tile average RGB for one-bounce
// — if all-zero, the caller can interpret hits as visibility-only.
inline HitInfo TraceRay(const float origin[3], const float dir[3],
                        float maxDist,
                        const ObjectSphere* objects, int objCount,
                        const float* tileColors)
{
    HitInfo hit;
    hit.t = -1.0f;
    hit.color[0] = hit.color[1] = hit.color[2] = 0.0f;
    hit.isTerrain = false;

    // Terrain march.
    for (float t = MARCH_STEP; t < maxDist; t += MARCH_STEP)
    {
        const float px = origin[0] + dir[0] * t;
        const float py = origin[1] + dir[1] * t;
        const float pz = origin[2] + dir[2] * t;
        const float h  = SampleHeight(px, py);
        if (h > -1e8f && pz < h)
        {
            hit.t = t;
            hit.isTerrain = true;
            if (tileColors != nullptr)
            {
                // Look up the tile under the hit point and pull its
                // pre-averaged Layer1 color. The mapping array is
                // 256×256 BYTE indices into BITMAP_MAPTILE+N.
                const int tx = ClampIndex(static_cast<int>(px / TERRAIN_SCALE));
                const int ty = ClampIndex(static_cast<int>(py / TERRAIN_SCALE));
                const int tileIdx = TerrainMappingLayer1[TERRAIN_INDEX(tx, ty)];
                if (tileIdx >= 0 && tileIdx < 30)
                {
                    hit.color[0] = tileColors[tileIdx * 3 + 0];
                    hit.color[1] = tileColors[tileIdx * 3 + 1];
                    hit.color[2] = tileColors[tileIdx * 3 + 2];
                }
            }
            break;
        }
    }

    // Object spheres. Walk all (no spatial accel for now — typically
    // <1000 per slot, well under the cost of the terrain march). If an
    // object hit is closer than the terrain hit, override.
    for (int i = 0; i < objCount; ++i)
    {
        const ObjectSphere& s = objects[i];
        const float ox = origin[0] - s.center[0];
        const float oy = origin[1] - s.center[1];
        const float oz = origin[2] - s.center[2];
        const float b  = ox*dir[0] + oy*dir[1] + oz*dir[2];
        const float c  = ox*ox + oy*oy + oz*oz - s.radius2;
        if (c > 0.0f && b > 0.0f) continue;
        const float disc = b*b - c;
        if (disc < 0.0f) continue;
        const float t = -b - std::sqrt(disc);
        if (t < 0.0f || t > maxDist) continue;
        if (hit.t < 0.0f || t < hit.t)
        {
            hit.t = t;
            hit.isTerrain = false;
            if (tileColors != nullptr)
            {
                // Neutral gray for object bounces — accurate per-mesh
                // sampling would require BMD texture decode which is
                // way out of scope for first-pass bake quality.
                hit.color[0] = 0.5f;
                hit.color[1] = 0.5f;
                hit.color[2] = 0.5f;
            }
        }
    }
    return hit;
}

// Surface normal from height differences in a 3x3 stencil.
void ComputeNormal(int x, int y, float out[3])
{
    const float hL = BackTerrainHeight[TERRAIN_INDEX(ClampIndex(x - 1), y)];
    const float hR = BackTerrainHeight[TERRAIN_INDEX(ClampIndex(x + 1), y)];
    const float hD = BackTerrainHeight[TERRAIN_INDEX(x, ClampIndex(y - 1))];
    const float hU = BackTerrainHeight[TERRAIN_INDEX(x, ClampIndex(y + 1))];
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

// Halton radical inverse, base b — low-discrepancy 1D sample in [0,1).
inline float Halton(int index, int base)
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

// Cosine-weighted hemisphere sample rotated to the vertex normal frame.
inline void HemisphereSample(int sampleIdx, const float normal[3], float out[3])
{
    const float u1 = Halton(sampleIdx + 1, 2);
    const float u2 = Halton(sampleIdx + 1, 3);
    const float r  = std::sqrt(u1);
    const float theta = 2.0f * PI_F * u2;
    const float lx = r * std::cos(theta);
    const float ly = r * std::sin(theta);
    const float lz = std::sqrt(std::max(0.0f, 1.0f - u1));

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

// Compute the average RGB color of each of the 30 BITMAP_MAPTILE slots.
// Used by one-bounce indirect — when a hemisphere ray hits terrain at
// world (px,py), we look up its tile index from TerrainMappingLayer1
// and bleed that tile's average color back to the source vertex. Tiles
// without a loaded bitmap (FindTexture returns null) get a neutral
// 0.5,0.5,0.5 so the bounce stays consistent.
void ComputeTileColors(float outRgbPerTile[30 * 3])
{
    for (int t = 0; t < 30; ++t)
    {
        outRgbPerTile[t*3 + 0] = 0.5f;
        outRgbPerTile[t*3 + 1] = 0.5f;
        outRgbPerTile[t*3 + 2] = 0.5f;

        BITMAP_t* b = Bitmaps.FindTexture(BITMAP_MAPTILE + t);
        if (b == nullptr || b->Buffer == nullptr) continue;

        const int w  = static_cast<int>(b->Width);
        const int h  = static_cast<int>(b->Height);
        const int comps = b->Components;
        if (w <= 0 || h <= 0 || (comps != 3 && comps != 4)) continue;

        // Sample at most 256 pixels uniformly across the bitmap — full
        // averaging on a 256x256 RGBA texture is 65k mults per tile,
        // which is fine but unnecessary at this fidelity.
        const int step = std::max(1, (w * h) / 256);
        double sumR = 0.0, sumG = 0.0, sumB = 0.0;
        int    count = 0;
        for (int p = 0; p < w * h; p += step)
        {
            const unsigned char* px = b->Buffer + p * comps;
            sumR += px[0];
            sumG += px[1];
            sumB += px[2];
            ++count;
        }
        if (count > 0)
        {
            outRgbPerTile[t*3 + 0] = static_cast<float>(sumR / count / 255.0);
            outRgbPerTile[t*3 + 1] = static_cast<float>(sumG / count / 255.0);
            outRgbPerTile[t*3 + 2] = static_cast<float>(sumB / count / 255.0);
        }
    }
}

// Build a flat list of bounding-sphere occluders from every live OBJECT
// in the ObjectBlock spatial hash. Sphere = midpoint of AABB + half its
// diagonal. Objects with degenerate boxes (Min==Max==0) are skipped —
// happens for engine sentinels and dead/stub entries.
void CollectObjectSpheres(std::vector<ObjectSphere>& out)
{
    out.clear();
    out.reserve(256);
    for (int b = 0; b < 256; ++b)
    {
        for (OBJECT* o = ObjectBlock[b].Head; o != nullptr; o = o->Next)
        {
            if (!o->Live) continue;

            const float bmin[3] = { o->BoundingBoxMin[0], o->BoundingBoxMin[1], o->BoundingBoxMin[2] };
            const float bmax[3] = { o->BoundingBoxMax[0], o->BoundingBoxMax[1], o->BoundingBoxMax[2] };
            const float dx = bmax[0] - bmin[0];
            const float dy = bmax[1] - bmin[1];
            const float dz = bmax[2] - bmin[2];
            if (dx <= 0.5f && dy <= 0.5f && dz <= 0.5f) continue;

            ObjectSphere s;
            // Center in world space = object position + local AABB center.
            // Local-space rotation is ignored on the assumption that the
            // sphere bounds the rotated box conservatively — true since
            // the diagonal radius is rotation-invariant.
            s.center[0] = o->Position[0] + (bmin[0] + bmax[0]) * 0.5f;
            s.center[1] = o->Position[1] + (bmin[1] + bmax[1]) * 0.5f;
            s.center[2] = o->Position[2] + (bmin[2] + bmax[2]) * 0.5f;
            s.radius    = 0.5f * std::sqrt(dx*dx + dy*dy + dz*dz);
            s.radius2   = s.radius * s.radius;
            out.push_back(s);
        }
    }
}

// Bake a single horizontal stripe of vertices [yStart, yEnd) into the
// destination RGB buffer. Pure read-only access to inputs (heightmap,
// tile colors, object spheres) so this is trivially thread-safe.
void BakeStripe(
    int yStart, int yEnd,
    const LightingBakeParams& params,
    const float sunDir[3],
    const float* tileColors,                // may be null if bounce=0
    const ObjectSphere* objects, int objCount,
    unsigned char* rgbOut)
{
    const int   N           = TERRAIN_SIZE;
    const int   aoSamples   = std::max(1, params.aoSamples);
    const float aoDistance  = std::max(50.0f, params.aoMaxDistance);
    const float aoStrength  = std::max(0.0f, params.aoStrength);
    const float ambient     = std::max(0.0f, params.ambientFloor);
    const float bounce      = std::max(0.0f, params.bounceStrength);
    const bool  doBounce    = (bounce > 0.0f) && (tileColors != nullptr);

    for (int y = yStart; y < yEnd; ++y)
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

            // Sun direct: shadow ray + Lambert.
            float sunContrib[3] = { 0.0f, 0.0f, 0.0f };
            const float NdotL = normal[0]*sunDir[0] + normal[1]*sunDir[1] + normal[2]*sunDir[2];
            if (NdotL > 0.0f)
            {
                const HitInfo shadow = TraceRay(origin, sunDir, SUN_RAY_MAX,
                                                objects, objCount, nullptr);
                if (shadow.t < 0.0f)
                {
                    sunContrib[0] = params.sunColor[0] * NdotL;
                    sunContrib[1] = params.sunColor[1] * NdotL;
                    sunContrib[2] = params.sunColor[2] * NdotL;
                }
            }

            // Sky AO + one-bounce in a single hemisphere pass.
            int   escaped = 0;
            float bounceR = 0.0f, bounceG = 0.0f, bounceB = 0.0f;
            for (int s = 0; s < aoSamples; ++s)
            {
                float dir[3];
                HemisphereSample(s, normal, dir);
                const HitInfo hit = TraceRay(origin, dir, aoDistance,
                                             objects, objCount,
                                             doBounce ? tileColors : nullptr);
                if (hit.t < 0.0f)
                {
                    ++escaped;
                }
                else if (doBounce)
                {
                    bounceR += hit.color[0];
                    bounceG += hit.color[1];
                    bounceB += hit.color[2];
                }
            }
            const float aoFactor = static_cast<float>(escaped) / static_cast<float>(aoSamples);
            const float ao       = std::pow(aoFactor, aoStrength);
            const float bInv     = 1.0f / static_cast<float>(aoSamples);

            // Compose: ambient floor + sky*AO + sun + bounce.
            float r = ambient + params.skyColor[0] * ao + sunContrib[0];
            float g = ambient + params.skyColor[1] * ao + sunContrib[1];
            float b = ambient + params.skyColor[2] * ao + sunContrib[2];
            if (doBounce)
            {
                r += bounceR * bInv * bounce;
                g += bounceG * bInv * bounce;
                b += bounceB * bInv * bounce;
            }

            const size_t outIdx = (static_cast<size_t>(y) * N + x) * 3;
            rgbOut[outIdx + 0] = ToByte(r);
            rgbOut[outIdx + 1] = ToByte(g);
            rgbOut[outIdx + 2] = ToByte(b);
        }
    }
}

} // anonymous namespace

bool BakeLighting(int mapId, const LightingBakeParams& params)
{
    const float az = params.sunAzimuth  * (PI_F / 180.0f);
    const float al = params.sunAltitude * (PI_F / 180.0f);
    const float sunDir[3] = {
        std::cos(al) * std::cos(az),
        std::cos(al) * std::sin(az),
        std::sin(al),
    };

    // Pre-build per-tile average colors (for one-bounce). Cheap — 30
    // bitmaps with ~256 samples each. Skip entirely if bounce is off.
    std::vector<float> tileColors;
    const bool wantBounce = params.bounceStrength > 0.0f;
    if (wantBounce)
    {
        tileColors.resize(30 * 3, 0.5f);
        ComputeTileColors(tileColors.data());
    }

    // Pre-build object shadow spheres.
    std::vector<ObjectSphere> objects;
    if (params.includeObjects) CollectObjectSpheres(objects);
    const ObjectSphere* objPtr = objects.empty() ? nullptr : objects.data();
    const int           objCount = static_cast<int>(objects.size());

    const int N = TERRAIN_SIZE;
    std::vector<unsigned char> rgb(static_cast<size_t>(N) * N * 3, 0);

    // Parallelize the per-vertex loop by Y stripes. Each thread writes
    // disjoint regions of `rgb` so there's no synchronization beyond
    // join. Defaults to all hardware threads; the param can pin to 1
    // for repeatable timing.
    int nThreads = params.threadCount;
    if (nThreads <= 0)
    {
        nThreads = static_cast<int>(std::thread::hardware_concurrency());
        if (nThreads <= 0) nThreads = 1;
    }
    nThreads = std::min(nThreads, N);

    const float* tilePtr = wantBounce ? tileColors.data() : nullptr;

    if (nThreads == 1)
    {
        BakeStripe(0, N, params, sunDir, tilePtr, objPtr, objCount, rgb.data());
    }
    else
    {
        std::vector<std::thread> workers;
        workers.reserve(nThreads);
        const int rowsPerThread = (N + nThreads - 1) / nThreads;
        for (int i = 0; i < nThreads; ++i)
        {
            const int yStart = i * rowsPerThread;
            const int yEnd   = std::min(N, yStart + rowsPerThread);
            if (yStart >= yEnd) break;
            workers.emplace_back([=, &params, &rgb]()
            {
                BakeStripe(yStart, yEnd, params, sunDir,
                           tilePtr, objPtr, objCount, rgb.data());
            });
        }
        for (auto& w : workers) w.join();
    }

    if (!WriteTerrainLightRGB(mapId, rgb.data())) return false;
    ReloadTerrainLightFromSlot(mapId);
    return true;
}

}} // namespace MuEditor::CustomMap

#endif // _EDITOR
