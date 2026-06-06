// WaterReflection.cpp — planar water reflection RTT. See header.

#include "stdafx.h"
#include "Render/Water/WaterReflection.h"

#include <gl/glew.h>
#include <cmath>
#include <vector>
#include <cstring>

#include "Core/Globals/_define.h"           // TERRAIN_SIZE, TERRAIN_SCALE
#include "Render/PostProcess/PostProcessGL.h"
#include "Render/Terrain/ZzzLodTerrain.h"   // RenderTerrain, terrain data, IsWaterTileXY
#include "Engine/Object/ZzzObject.h"        // RenderObjects
#include "World/MapInfra/MapManager.h"      // gMapManager, WD_7ATLANSE

extern CHARACTER* Hero;
extern int CachTexture;            // BindTexture's cached id (ZzzOpenglUtil.cpp)

namespace WaterReflection
{
    namespace
    {
        GLuint s_fbo = 0, s_colorTex = 0, s_depthTex = 0;
        int    s_w = 0, s_h = 0;       // reflection texture size (half scene)
        bool   s_enabled  = true;      // M1: on for validation
        bool   s_rendering = false;
        bool   s_valid    = false;
        float  s_waterH   = 0.0f;
        float  s_strength = 0.45f;     // reflection blend over the water texture
        float  s_clipDepth = 90.0f;    // clip geometry below (waterH - this) out of reflection
        float  s_heightGate = 9999.0f; // skip water tiles farther than this from the plane
                                       // (cuts water texture painted UP cliffs); large = off
        float  s_edgeRadius = 0.0f;    // "Plane Grow": grow-only dilation margin in tiles (0 = none)
        // Water tint laid under the reflection (covers the blocky base-water edge and
        // colors the dissolved shore extension so the surface reads as organic water).
        float  s_tintR = 0.10f, s_tintG = 0.18f, s_tintB = 0.26f;
        float  s_tintOpacity = 0.30f;
        bool   s_showDebug = true;     // bottom-left reflection preview overlay

        // Projective lookup matrix: bias * proj * view (MAIN camera). A water
        // fragment samples the reflection texture at its OWN screen position; for
        // a planar mirror that yields the correct reflected color.
        float  s_texMtx[16];
        bool   s_haveTexMtx = false;

        // DOMINANT water level for this map (mode of the water tiles' heights). The
        // mirror plane reflects across it; the mesh sits on the terrain.
        float  s_waterLevel = 0.0f;
        bool   s_haveLevel  = false;

        // --- Dedicated water-surface MESH (built once per map on load) ----------
        // The water surface is a real prebuilt mesh shaped to the water region, not
        // a per-frame decal on the tile grid. Each corner carries a smooth coverage
        // alpha (box-blurred water mask -> rounded, organic outline). Drawn each
        // frame with a tint pass + a projective-reflection pass.
        struct WQuad { float p[12]; float cx, cy; };
        std::vector<WQuad> s_mesh;
        bool s_meshDirty = true;        // rebuild requested (map change / edge params)
        int  s_meshWorld = -1;          // world the mesh was built for

        // column-major o = a * b
        inline void Mat4Mul(float* o, const float* a, const float* b)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1]
                                 + a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
        }

        // --- Reflection blur (feathers hard silhouette edges) -------------------
        // The reflection FBO has no sky, so reflected cliffs/terrain meet the empty
        // clear-color along HARD silhouette edges -> visible zig-zag cutoffs on the
        // water. A gentle separable Gaussian softens those edges (and reads more
        // like water) while the base stays full-res, so detail is preserved.
        GLuint s_blurFbo = 0, s_blurTex = 0;
        GLuint s_blurProg = 0;
        int    s_blurLocTex = -1, s_blurLocStep = -1;
        float  s_blurRadius = 1.5f;     // global softness in texels (rounded edges do the edges)

        const char* kBlurVS =
            "#version 120\n"
            "varying vec2 vUV;\n"
            "void main(){ gl_Position = gl_Vertex; vUV = gl_MultiTexCoord0.xy; }\n";

        const char* kBlurFS =
            "#version 120\n"
            "uniform sampler2D uTex;\n"
            "uniform vec2 uStep;\n"
            "varying vec2 vUV;\n"
            "void main(){\n"
            "    vec4 c  = texture2D(uTex, vUV) * 0.227027;\n"
            "    c += texture2D(uTex, vUV + uStep * 1.0) * 0.194595;\n"
            "    c += texture2D(uTex, vUV - uStep * 1.0) * 0.194595;\n"
            "    c += texture2D(uTex, vUV + uStep * 2.0) * 0.121622;\n"
            "    c += texture2D(uTex, vUV - uStep * 2.0) * 0.121622;\n"
            "    c += texture2D(uTex, vUV + uStep * 3.0) * 0.054054;\n"
            "    c += texture2D(uTex, vUV - uStep * 3.0) * 0.054054;\n"
            "    c += texture2D(uTex, vUV + uStep * 4.0) * 0.016216;\n"
            "    c += texture2D(uTex, vUV - uStep * 4.0) * 0.016216;\n"
            "    gl_FragColor = c;\n"
            "}\n";

        bool EnsureFbo(int w, int h)
        {
            if (!PostProcess::Available()) return false;
            if (s_fbo && w == s_w && h == s_h) return true;

            const PostProcess::GLProcs& gl = PostProcess::GL();
            if (s_fbo) { gl.DeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
            if (s_colorTex) { glDeleteTextures(1, &s_colorTex); s_colorTex = 0; }
            if (s_depthTex) { glDeleteTextures(1, &s_depthTex); s_depthTex = 0; }
            if (s_blurFbo) { gl.DeleteFramebuffers(1, &s_blurFbo); s_blurFbo = 0; }
            if (s_blurTex) { glDeleteTextures(1, &s_blurTex); s_blurTex = 0; }

            s_colorTex = PostProcess::CreateColorTexture(w, h);
            s_depthTex = PostProcess::CreateDepthTexture(w, h);
            if (!s_colorTex || !s_depthTex) return false;
            gl.GenFramebuffers(1, &s_fbo);
            gl.BindFramebuffer(GL_FRAMEBUFFER, s_fbo);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_colorTex, 0);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, s_depthTex, 0);
            const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            if (!ok) { OutputDebugStringA("[WaterReflection] FBO incomplete\n"); return false; }

            // Scratch target for the separable blur (same size, color only).
            s_blurTex = PostProcess::CreateColorTexture(w, h);
            if (s_blurTex)
            {
                gl.GenFramebuffers(1, &s_blurFbo);
                gl.BindFramebuffer(GL_FRAMEBUFFER, s_blurFbo);
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_blurTex, 0);
                if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                { gl.DeleteFramebuffers(1, &s_blurFbo); s_blurFbo = 0; glDeleteTextures(1, &s_blurTex); s_blurTex = 0; }
                gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            s_w = w; s_h = h;
            return true;
        }

        // Separable Gaussian over s_colorTex, in place (H -> scratch, V -> back).
        void BlurReflection()
        {
            const PostProcess::GLProcs& gl = PostProcess::GL();
            if (!s_blurProg)
            {
                s_blurProg = PostProcess::CompileProgram(kBlurVS, kBlurFS);
                if (s_blurProg)
                {
                    s_blurLocTex  = gl.GetUniformLocation(s_blurProg, "uTex");
                    s_blurLocStep = gl.GetUniformLocation(s_blurProg, "uStep");
                }
            }
            if (!s_blurProg || !s_blurFbo || s_blurRadius <= 0.0f) return;

            glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE); glEnable(GL_TEXTURE_2D);
            gl.UseProgram(s_blurProg);
            const float sx = s_blurRadius / (float)s_w;
            const float sy = s_blurRadius / (float)s_h;

            // Horizontal: s_colorTex -> s_blurTex
            gl.BindFramebuffer(GL_FRAMEBUFFER, s_blurFbo);
            glViewport(0, 0, s_w, s_h);
            gl.ActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_colorTex);
            gl.Uniform1i(s_blurLocTex, 0);
            gl.Uniform2f(s_blurLocStep, sx, 0.0f);
            PostProcess::DrawFullscreenQuad();

            // Vertical: s_blurTex -> s_colorTex (back into s_fbo)
            gl.BindFramebuffer(GL_FRAMEBUFFER, s_fbo);
            glViewport(0, 0, s_w, s_h);
            glBindTexture(GL_TEXTURE_2D, s_blurTex);
            gl.Uniform1i(s_blurLocTex, 0);
            gl.Uniform2f(s_blurLocStep, 0.0f, sy);
            PostProcess::DrawFullscreenQuad();

            gl.UseProgram(0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glPopAttrib();
        }
    } // namespace

    void  SetEnabled(bool on) { s_enabled = on; }
    bool  Enabled()           { return s_enabled; }
    void  SetWaterHeight(float h) { s_waterH = h; }
    void  SetStrength(float s) { s_strength = s; }
    void  SetBlur(float texels) { s_blurRadius = texels; }
    void  SetClipDepth(float d) { s_clipDepth = d; }
    void  SetHeightGate(float g) { if (g != s_heightGate) s_meshDirty = true; s_heightGate = g; }
    void  SetEdgeRadius(float t) { if (t != s_edgeRadius) s_meshDirty = true; s_edgeRadius = t; }
    void  SetTint(float r, float g, float b) { s_tintR = r; s_tintG = g; s_tintB = b; }
    void  SetTintOpacity(float o) { s_tintOpacity = o; }
    void  SetDebugOverlay(bool on) { s_showDebug = on; }
    bool  Rendering()         { return s_rendering; }
    unsigned int Texture()    { return s_colorTex; }
    bool  Ready()             { return s_valid && s_colorTex != 0; }

    float GetStrength()       { return s_strength; }
    float GetBlur()           { return s_blurRadius; }
    float GetClipDepth()      { return s_clipDepth; }
    float GetHeightGate()     { return s_heightGate; }
    float GetEdgeRadius()     { return s_edgeRadius; }
    void  GetTint(float* rgb) { rgb[0] = s_tintR; rgb[1] = s_tintG; rgb[2] = s_tintB; }
    float GetTintOpacity()    { return s_tintOpacity; }
    float GetMeasuredLevel()  { return s_waterLevel; }
    bool  DebugOverlayEnabled() { return s_showDebug; }

    // Maps that are entirely "underwater" (Atlans) have no above-water world to
    // mirror, so a surface reflection is meaningless and only adds artifacts.
    inline bool SkipForMap()
    {
        return gMapManager.WorldActive == WD_7ATLANSE;
    }

    namespace
    {
        // Build the per-map water-surface mesh: smooth (box-blurred) coverage of the
        // water tiles -> rounded organic outline; per-corner alpha = smoothstep of
        // the coverage; vertices sit on the terrain (matches the game's water).
        void BuildMeshForMap()
        {
            s_meshDirty = false;
            s_mesh.clear();
            s_haveLevel = false;
            s_meshWorld = gMapManager.WorldActive;
            if (SkipForMap()) return;

            const int N = TERRAIN_SIZE;                 // 256

            // 1) dominant water level = the most-populated height level among the
            // map's water tiles (so the flat plane sits on the main water body).
            int   waterTiles = 0;
            int   binKey[128]; int binCnt[128]; int nbin = 0;
            const float kBin = 25.0f;
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                {
                    if (!IsWaterTileXY(x, y)) continue;
                    ++waterTiles;
                    const float hgt = BackTerrainHeight[TERRAIN_INDEX(x, y)];
                    const int b = (int)std::floor(hgt / kBin);
                    int j = 0; for (; j < nbin; ++j) if (binKey[j] == b) break;
                    if (j == nbin) { if (nbin < 128) { binKey[nbin] = b; binCnt[nbin] = 0; nbin = j + 1; } else continue; }
                    ++binCnt[j];
                }
            if (waterTiles == 0) return;

            int best = 0; for (int j = 1; j < nbin; ++j) if (binCnt[j] > binCnt[best]) best = j;
            double lsum = 0; int lcnt = 0;
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                    if (IsWaterTileXY(x, y))
                    {
                        const float hgt = BackTerrainHeight[TERRAIN_INDEX(x, y)];
                        if ((int)std::floor(hgt / kBin) == binKey[best]) { lsum += hgt; ++lcnt; }
                    }
            s_waterLevel = lcnt ? (float)(lsum / lcnt) : 0.f;
            s_haveLevel  = true;

            // 2) Emit a FLAT, SOLID quad per cell that is water OR within `dilate`
            // tiles of water. The plane is a real flat surface at the water level
            // (+lift); it only ever GROWS outward (dilation), never shrinks. The
            // spill onto higher ground is hidden by DEPTH OCCLUSION, so the visible
            // waterline follows the real terrain contour (the shoreline / cliff base).
            const int  dilate = (int)(s_edgeRadius + 0.5f);   // grow-only margin (tiles)
            const bool gate   = (s_heightGate < 5000.0f);
            const float lvl   = s_waterLevel + 10.0f;
            s_mesh.reserve(8192);
            for (int y = 0; y < N - 1; ++y)
                for (int x = 0; x < N - 1; ++x)
                {
                    bool nearWater = false;
                    for (int dy = -dilate; dy <= dilate + 1 && !nearWater; ++dy)
                        for (int dx = -dilate; dx <= dilate + 1 && !nearWater; ++dx)
                            if (IsWaterTileXY(x + dx, y + dy)) nearWater = true;
                    if (!nearWater) continue;

                    const float h0 = BackTerrainHeight[TERRAIN_INDEX(x,     y)];
                    const float h1 = BackTerrainHeight[TERRAIN_INDEX(x + 1, y)];
                    const float h2 = BackTerrainHeight[TERRAIN_INDEX(x + 1, y + 1)];
                    const float h3 = BackTerrainHeight[TERRAIN_INDEX(x,     y + 1)];
                    if (gate && std::fabs((h0 + h1 + h2 + h3) * 0.25f - s_waterLevel) > s_heightGate)
                        continue;

                    const float wx = x * TERRAIN_SCALE, wy = y * TERRAIN_SCALE, sc = TERRAIN_SCALE;
                    WQuad q;
                    q.p[0] = wx;      q.p[1] = wy;       q.p[2]  = lvl;
                    q.p[3] = wx + sc; q.p[4] = wy;       q.p[5]  = lvl;
                    q.p[6] = wx + sc; q.p[7] = wy + sc;  q.p[8]  = lvl;
                    q.p[9] = wx;      q.p[10] = wy + sc; q.p[11] = lvl;
                    q.cx = wx + sc * 0.5f; q.cy = wy + sc * 0.5f;
                    s_mesh.push_back(q);
                }
        }
    } // namespace

    void Build()
    {
        s_valid = false;

        // Rebuild the surface mesh on map change (or when edge params changed).
        if (gMapManager.WorldActive != s_meshWorld) s_meshDirty = true;

        if (!s_enabled || !PostProcess::Available() || Hero == nullptr) return;
        if (SkipForMap()) { if (!s_mesh.empty()) s_mesh.clear(); s_meshWorld = gMapManager.WorldActive; return; }
        if (s_meshDirty) BuildMeshForMap();
        if (s_mesh.empty() || !s_haveLevel) return;

        // Capture the live camera matrices (set by SetupMainSceneViewport) and the
        // scene viewport, BEFORE we touch any GL state.
        GLfloat proj[16], view[16];
        glGetFloatv(GL_PROJECTION_MATRIX, proj);
        glGetFloatv(GL_MODELVIEW_MATRIX, view);
        GLint savedFbo = 0, savedVp[4] = { 0,0,0,0 };
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &savedFbo);
        glGetIntegerv(GL_VIEWPORT, savedVp);

        // Projective lookup matrix for the water overlay: bias * proj * view.
        {
            static const float bias[16] = {
                0.5f, 0.f,  0.f,  0.f,
                0.f,  0.5f, 0.f,  0.f,
                0.f,  0.f,  0.5f, 0.f,
                0.5f, 0.5f, 0.5f, 1.f };
            float pv[16];
            Mat4Mul(pv, proj, view);
            Mat4Mul(s_texMtx, bias, pv);
            s_haveTexMtx = true;
        }

        // Full-resolution reflection: half-res blurred badly on large open water
        // (old maps). Vertex cost is unchanged; only fill-rate grows.
        const int rw = (savedVp[2] > 1) ? savedVp[2] : 1;
        const int rh = (savedVp[3] > 1) ? savedVp[3] : 1;
        if (!EnsureFbo(rw, rh)) return;

        // Mirror plane = the map's dominant water level (baked at mesh build).
        const float h = s_waterLevel;
        s_waterH = h;

        const PostProcess::GLProcs& gl = PostProcess::GL();

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                     GL_VIEWPORT_BIT | GL_POLYGON_BIT | GL_TRANSFORM_BIT | GL_CURRENT_BIT);

        gl.BindFramebuffer(GL_FRAMEBUFFER, s_fbo);
        glViewport(0, 0, s_w, s_h);
        glClearColor(0.10f, 0.16f, 0.22f, 1.0f);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Reflected camera: modelview = view * reflect(z=h). Column-major reflect.
        const float reflect[16] = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, -1.f, 0.f,
            0.f, 0.f, 2.f * h, 1.f
        };
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadMatrixf(proj);
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(view); glMultMatrixf(reflect);

        // Clip DEEP submerged geometry (tree roots / pillars / cliff undersides)
        // out of the reflection, but keep near-surface geometry: a tight clip
        // (h-20) sliced the bank right at the waterline, leaving hard triangular
        // cutoffs where a tile reflected the empty clear-color instead. A deeper
        // plane (h-90) keeps the shore continuous and only removes the deep stuff.
        // World-space plane (glClipPlane * inverse modelview) => keeps world z >= h-clipDepth.
        const double clipEqn[4] = { 0.0, 0.0, 1.0, -((double)h - (double)s_clipDepth) };
        glClipPlane(GL_CLIP_PLANE0, clipEqn);
        glEnable(GL_CLIP_PLANE0);

        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
        // The mirror matrix has a negative determinant => it REVERSES triangle
        // winding. Drawing both faces (cull off) shows solids inside-out and the
        // undersides of terrain = the "looking underground" artifact. Instead flip
        // the front-face to CW and cull backfaces, so the reflection renders the
        // proper TOP surfaces (bridge deck, walls, ground) mirrored. Engine winding
        // is the GL default CCW, so mirrored fronts are CW.
        glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CW);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glColor4f(1.f, 1.f, 1.f, 1.f);

        s_rendering = true;                 // suppress sun-shadow caster collection
        RenderTerrain(false);
        RenderObjects();
        s_rendering = false;

        glDisable(GL_CLIP_PLANE0);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
        glPopAttrib();
        if (gl.UseProgram)    gl.UseProgram(0);
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);

        // Feather the hard silhouette edges (no sky in the FBO => reflected terrain
        // meets clear-color along sharp zig-zags). Runs in the reflection FBO space.
        BlurReflection();

        gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)savedFbo);
        glViewport(savedVp[0], savedVp[1], savedVp[2], savedVp[3]);
        CachTexture = 0x6FFFFFFF;            // raw binds above desync BindTexture's cache

        s_valid = true;
    }

    // Draw the prebuilt flat water plane: a solid tint pass + a projective-reflection
    // pass. The plane is opaque (no coverage fade); its visible edge is the terrain
    // contour via DEPTH OCCLUSION. Called after the terrain (end of RenderTerrain).
    void DrawSurface()
    {
        if (s_rendering) return;
        if (!s_valid || !s_haveTexMtx || s_colorTex == 0 || s_mesh.empty()) return;
        if (!PostProcess::Available()) return;
        const PostProcess::GLProcs& gl = PostProcess::GL();

        // Cull to a generous radius around the hero (skip far water on big maps).
        const float hx = Hero ? Hero->Object.Position[0] : 0.f;
        const float hy = Hero ? Hero->Object.Position[1] : 0.f;
        const float kCull2 = 5000.0f * 5000.0f;
        const size_t n = s_mesh.size();

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                     GL_CURRENT_BIT | GL_TEXTURE_BIT);
        if (gl.UseProgram) gl.UseProgram(0);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        // Pass 1 — flat water tint (solid).
        if (s_tintOpacity > 0.001f)
        {
            glDisable(GL_TEXTURE_2D);
            glColor4f(s_tintR, s_tintG, s_tintB, s_tintOpacity);
            glBegin(GL_QUADS);
            for (size_t i = 0; i < n; ++i)
            {
                const WQuad& q = s_mesh[i];
                const float dx = q.cx - hx, dy = q.cy - hy;
                if (dx * dx + dy * dy > kCull2) continue;
                glVertex3f(q.p[0], q.p[1], q.p[2]);   glVertex3f(q.p[3], q.p[4], q.p[5]);
                glVertex3f(q.p[6], q.p[7], q.p[8]);   glVertex3f(q.p[9], q.p[10], q.p[11]);
            }
            glEnd();
        }

        // Pass 2 — reflection (projective planar mirror, solid).
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, s_colorTex);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1.f, 1.f, 1.f, s_strength);
        const float* T = s_texMtx;
        glBegin(GL_QUADS);
        for (size_t i = 0; i < n; ++i)
        {
            const WQuad& q = s_mesh[i];
            const float dx = q.cx - hx, dy = q.cy - hy;
            if (dx * dx + dy * dy > kCull2) continue;
            for (int k = 0; k < 4; ++k)
            {
                const float x = q.p[k * 3 + 0], y = q.p[k * 3 + 1], z = q.p[k * 3 + 2];
                const float s = T[0] * x + T[4] * y + T[8]  * z + T[12];
                const float t = T[1] * x + T[5] * y + T[9]  * z + T[13];
                const float r = T[2] * x + T[6] * y + T[10] * z + T[14];
                const float w = T[3] * x + T[7] * y + T[11] * z + T[15];
                glTexCoord4f(s, t, r, w);
                glVertex3f(x, y, z);
            }
        }
        glEnd();

        glDepthMask(GL_TRUE);
        glBindTexture(GL_TEXTURE_2D, 0);
        glPopAttrib();
        CachTexture = 0x6FFFFFFF;            // invalidate BindTexture's cache
    }

    void DebugDraw()
    {
        if (!s_showDebug) return;
        if (!s_valid || s_colorTex == 0 || !PostProcess::Available()) return;
        const PostProcess::GLProcs& gl = PostProcess::GL();

        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_TRANSFORM_BIT);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        // Bottom-left preview panel.
        glViewport(0, 0, 420, 236);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        if (gl.UseProgram) gl.UseProgram(0);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_colorTex);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glBegin(GL_QUADS);
            glTexCoord2f(0.f, 0.f); glVertex2f(-1.f, -1.f);
            glTexCoord2f(1.f, 0.f); glVertex2f( 1.f, -1.f);
            glTexCoord2f(1.f, 1.f); glVertex2f( 1.f,  1.f);
            glTexCoord2f(0.f, 1.f); glVertex2f(-1.f,  1.f);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        glPopAttrib();
    }

    void Shutdown()
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        if (s_fbo && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &s_fbo);
        if (s_blurFbo && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &s_blurFbo);
        if (s_colorTex) glDeleteTextures(1, &s_colorTex);
        if (s_depthTex) glDeleteTextures(1, &s_depthTex);
        if (s_blurTex) glDeleteTextures(1, &s_blurTex);
        if (s_blurProg && gl.DeleteProgram) gl.DeleteProgram(s_blurProg);
        s_fbo = s_colorTex = s_depthTex = s_blurFbo = s_blurTex = 0;
        s_blurProg = 0;
        s_valid = false;
    }
}
