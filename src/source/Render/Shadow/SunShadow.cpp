// SunShadow.cpp — forward shadow mapping (sun). See header.

#include "stdafx.h"
#include "Render/Shadow/SunShadow.h"
#include "Render/PostProcess/PostProcessGL.h"
#include "Render/SunDirection.h"

#include <gl/glew.h>
#include <windows.h>
#include <cmath>
#include <vector>
#include <cstdio>

namespace SunShadow
{
    namespace
    {
        // ---- Tunables -------------------------------------------------------
        bool  s_enabled  = false;
        int   s_res      = 2048;
        float s_distance = 1400.0f;
        float s_darkness = 0.5f;
        float s_softness = 1.0f;
        float s_bias     = 1.5f;

        // ---- GL objects -----------------------------------------------------
        GLuint s_fbo      = 0;
        GLuint s_depthTex = 0;
        int    s_builtRes = 0;
        bool   s_ready    = false;           // a valid map exists to sample

        float  s_matrix[16]  = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // world->light [0,1]
        float  s_lightView[16];
        float  s_lightProj[16];

        // ---- Collected caster verts (filled during the forward render) ------
        std::vector<float> s_verts;          // xyz triplets, world space (opaque)
        float s_target[3] = { 0,0,0 };       // camera focus (player) for M1 gate

        // Alpha-tested casters: interleaved x,y,z,u,v, grouped into per-texture
        // draw ranges so the cutout (fence holes) survives in the depth map.
        struct TexBatch { unsigned int tex; int first; int count; };
        std::vector<float>    s_texVerts;
        std::vector<TexBatch> s_texBatches;

        GLint  s_savedFbo = 0;
        GLint  s_savedVp[4] = {0,0,0,0};

        // ---- Small column-major mat4 / vec3 helpers -------------------------
        void v3norm(float v[3])
        {
            float l = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
            if (l > 1e-8f) { v[0]/=l; v[1]/=l; v[2]/=l; }
        }
        void v3cross(const float a[3], const float b[3], float o[3])
        { o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0]; }
        float v3dot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

        void mat4mul(const float A[16], const float B[16], float C[16])
        {
            for (int col=0; col<4; ++col)
                for (int row=0; row<4; ++row)
                {
                    float s = 0.0f;
                    for (int k=0; k<4; ++k) s += A[k*4+row]*B[col*4+k];
                    C[col*4+row] = s;
                }
        }
        void lookAt(const float eye[3], const float c[3], const float up[3], float m[16])
        {
            float f[3] = { c[0]-eye[0], c[1]-eye[1], c[2]-eye[2] };
            v3norm(f);
            float s[3]; v3cross(f, up, s); v3norm(s);
            float u[3]; v3cross(s, f, u);
            m[0]=s[0]; m[1]=u[0]; m[2]=-f[0]; m[3]=0.0f;
            m[4]=s[1]; m[5]=u[1]; m[6]=-f[1]; m[7]=0.0f;
            m[8]=s[2]; m[9]=u[2]; m[10]=-f[2]; m[11]=0.0f;
            m[12]=-v3dot(s,eye); m[13]=-v3dot(u,eye); m[14]=v3dot(f,eye); m[15]=1.0f;
        }
        void ortho(float l, float r, float b, float t, float n, float fr, float m[16])
        {
            for (int i=0;i<16;++i) m[i]=0.0f;
            m[0]=2.0f/(r-l); m[5]=2.0f/(t-b); m[10]=-2.0f/(fr-n);
            m[12]=-(r+l)/(r-l); m[13]=-(t+b)/(t-b); m[14]=-(fr+n)/(fr-n); m[15]=1.0f;
        }

        bool EnsureFbo(int res)
        {
            if (!PostProcess::Available()) return false;
            if (s_fbo != 0 && s_builtRes == res) return true;
            const PostProcess::GLProcs& gl = PostProcess::GL();
            if (s_fbo)      { gl.DeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
            if (s_depthTex) { glDeleteTextures(1, &s_depthTex); s_depthTex = 0; }

            s_depthTex = PostProcess::CreateDepthTexture(res, res);
            if (!s_depthTex) return false;
            gl.GenFramebuffers(1, &s_fbo);
            gl.BindFramebuffer(GL_FRAMEBUFFER, s_fbo);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_depthTex, 0);
            glDrawBuffer(GL_NONE);
            glReadBuffer(GL_NONE);
            bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            if (!ok)
            {
                OutputDebugStringA("[SunShadow] depth FBO incomplete\n");
                if (s_fbo)      { gl.DeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
                if (s_depthTex) { glDeleteTextures(1, &s_depthTex); s_depthTex = 0; }
                return false;
            }
            s_builtRes = res;
            return true;
        }

        // ---- debug preview --------------------------------------------------
        GLuint s_dbgProg = 0;
        GLint  s_dbgLoc  = -1;
    } // namespace

    void SetParams(bool enabled, int resolution, float distance,
                   float darkness, float softness, float bias)
    {
        s_enabled  = enabled;
        s_res      = (resolution >= 4096) ? 4096 : (resolution >= 2048) ? 2048 : 1024;
        s_distance = (distance > 200.0f) ? distance : 200.0f;
        s_darkness = (darkness < 0.0f) ? 0.0f : (darkness > 1.0f ? 1.0f : darkness);
        s_softness = softness;
        s_bias     = bias;
        if (!enabled) s_ready = false;
    }
    bool Enabled() { return s_enabled; }

    bool CharacterCastWanted() { return s_enabled && PostProcess::Available(); }

    void SetTarget(const float p[3])
    {
        s_target[0] = p[0]; s_target[1] = p[1]; s_target[2] = p[2];
    }
    bool AcceptCaster(const float bodyOrigin[3])
    {
        // M1: only the entity at the camera focus (the player). Items/props/mobs
        // sit hundreds of units away and are excluded so the map shows one clean
        // character. (Later milestones drop this gate.)
        const float dx = bodyOrigin[0] - s_target[0];
        const float dy = bodyOrigin[1] - s_target[1];
        return (dx*dx + dy*dy) < (250.0f * 250.0f);
    }

    bool ObjectCastWanted(const float origin[3])
    {
        if (!s_enabled || !PostProcess::Available()) return false;
        const float dx = origin[0] - s_target[0];
        const float dy = origin[1] - s_target[1];
        // map half-extent (ortho fit ~= s_distance) plus a margin for an
        // object whose body reaches into the footprint from just outside it.
        const float r = s_distance + 700.0f;
        return (dx*dx + dy*dy) < (r*r);
    }

    void PushCharacterVerts(const float* xyz, int n)
    {
        if (!s_enabled || n <= 0 || !xyz) return;
        if (s_verts.size() > 2000000u * 3u) return;     // hard cap
        s_verts.insert(s_verts.end(), xyz, xyz + (size_t)n * 3);
    }

    void PushTexturedCaster(unsigned int glTex, const float* posUV, int nVerts)
    {
        if (!s_enabled || nVerts <= 0 || !posUV || glTex == 0) return;
        if (s_texVerts.size() > 2000000u * 5u) return;  // hard cap
        const int first = (int)(s_texVerts.size() / 5);
        s_texVerts.insert(s_texVerts.end(), posUV, posUV + (size_t)nVerts * 5);
        s_texBatches.push_back({ glTex, first, nVerts });
    }

    void BuildFromCollected(const float cameraTarget[3])
    {
        if (!s_enabled || !PostProcess::Available()) { s_verts.clear(); s_texVerts.clear(); s_texBatches.clear(); return; }
        if (!EnsureFbo(s_res))                        { s_verts.clear(); s_texVerts.clear(); s_texBatches.clear(); return; }
        const PostProcess::GLProcs& gl = PostProcess::GL();

        // ---- Light ortho camera from the shared world sun -------------------
        float sun[3] = { BloodlustMU::g_SunDirection.x, BloodlustMU::g_SunDirection.y,
                         BloodlustMU::g_SunDirection.z };
        v3norm(sun);
        if (std::fabs(sun[0])+std::fabs(sun[1])+std::fabs(sun[2]) < 1e-4f)
        { sun[0]=0; sun[1]=0; sun[2]=1; }

        const float R = s_distance;
        const float D = R * 2.5f;
        float eye[3] = { cameraTarget[0]+sun[0]*D, cameraTarget[1]+sun[1]*D, cameraTarget[2]+sun[2]*D };
        float ctr[3] = { cameraTarget[0], cameraTarget[1], cameraTarget[2] };
        float up[3]  = { 0,0,1 };
        if (std::fabs(sun[2]) > 0.985f) { up[0]=0; up[1]=1; up[2]=0; }

        lookAt(eye, ctr, up, s_lightView);
        ortho(-R, R, -R, R, R*0.5f, R*4.5f, s_lightProj);
        float pv[16]; mat4mul(s_lightProj, s_lightView, pv);
        static const float B[16] = { 0.5f,0,0,0, 0,0.5f,0,0, 0,0,0.5f,0, 0.5f,0.5f,0.5f,1 };
        mat4mul(B, pv, s_matrix);


        // ---- Render collected caster verts into the depth map ---------------
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_savedFbo);
        glGetIntegerv(GL_VIEWPORT, s_savedVp);
        // Save ALL the fixed-function render state this frame-end pass touches so
        // it is fully state-neutral. The menu cursor (SceneManager RenderCursor)
        // is drawn AFTER this and is alpha-tested + blended — leaking our
        // GL_ALPHA_TEST/GL_BLEND/etc. disables rendered it as a white square.
        // (FBO binding + matrices aren't on the attrib stack, restored manually.)
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_POLYGON_BIT);

        gl.BindFramebuffer(GL_FRAMEBUFFER, s_fbo);
        glViewport(0, 0, s_builtRes, s_builtRes);

        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadMatrixf(s_lightProj);
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadMatrixf(s_lightView);

        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_TEXTURE_2D);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.1f, 2.0f);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        if (!s_verts.empty())
        {
            glEnableClientState(GL_VERTEX_ARRAY);
            glVertexPointer(3, GL_FLOAT, 0, s_verts.data());
            glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(s_verts.size()/3));
            glDisableClientState(GL_VERTEX_ARRAY);
        }

        // Alpha-tested casters (fences/grilles/foliage): draw textured with the
        // alpha test so transparent texels write NO depth -> the shadow keeps the
        // holes. Color mask stays off (depth-only); GL_MODULATE * white means the
        // fragment alpha == texture alpha, which is what the alpha test reads.
        if (!s_texVerts.empty() && !s_texBatches.empty())
        {
            glColor4f(1.f, 1.f, 1.f, 1.f);
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.5f);
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            const GLsizei stride = 5 * sizeof(float);
            const float* base = s_texVerts.data();
            glVertexPointer(3, GL_FLOAT, stride, base);
            glTexCoordPointer(2, GL_FLOAT, stride, base + 3);
            for (size_t bi = 0; bi < s_texBatches.size(); ++bi)
            {
                const TexBatch& b = s_texBatches[bi];
                glBindTexture(GL_TEXTURE_2D, (GLuint)b.tex);
                glDrawArrays(GL_TRIANGLES, b.first, b.count);
            }
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_TEXTURE_2D);
        }

        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();

        gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)s_savedFbo);
        glViewport(s_savedVp[0], s_savedVp[1], s_savedVp[2], s_savedVp[3]);
        glPopAttrib();   // restore enables/blend/alpha-test/depth/polygon-offset
        // glPushAttrib restores NEITHER the bound shader program NOR the active
        // texture-unit selector. On the menu scenes the cursor (and ImGui) are
        // drawn right after this frame-end pass; a leaked program or non-zero
        // active unit makes the fixed-function cursor quad render as a white
        // square. Force the fixed-function texturing path back to clean.
        if (gl.UseProgram)    gl.UseProgram(0);
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        glEnable(GL_TEXTURE_2D);

        s_ready = true;
        s_verts.clear();        // fresh collection next frame
        s_texVerts.clear();
        s_texBatches.clear();
    }

    bool         MapReady()     { return s_ready && s_depthTex != 0; }
    unsigned int DepthTexture() { return s_depthTex; }
    const float* Matrix()       { return s_matrix; }
    int          Resolution()   { return s_builtRes; }
    float        Darkness()     { return s_darkness; }
    float        Softness()     { return s_softness; }
    float        Bias()         { return s_bias; }

    void Shutdown()
    {
        if (PostProcess::Available())
        {
            const PostProcess::GLProcs& gl = PostProcess::GL();
            if (s_fbo) { gl.DeleteFramebuffers(1, &s_fbo); s_fbo = 0; }
        }
        if (s_depthTex) { glDeleteTextures(1, &s_depthTex); s_depthTex = 0; }
        s_builtRes = 0; s_ready = false; s_verts.clear();
        s_texVerts.clear(); s_texBatches.clear();
    }

    void DebugDraw()
    {
        if (!s_enabled || s_depthTex == 0 || !PostProcess::Available()) return;
        const PostProcess::GLProcs& gl = PostProcess::GL();
        if (s_dbgProg == 0)
        {
            const char* vs = "#version 120\nvarying vec2 vUV;\n"
                "void main(){ gl_Position = gl_Vertex; vUV = gl_MultiTexCoord0.xy; }";
            // Flat black/white SILHOUETTE: geometry -> white, empty sky -> black.
            // No depth-colour gradient, so the actual caster SHAPE is unmistakable
            // (a character should read as a clean human-ish white blob).
            const char* fs = "#version 120\nuniform sampler2D uTex; varying vec2 vUV;\n"
                "void main(){ float d = texture2D(uTex, vUV).r;\n"
                "  if (d >= 0.9999) gl_FragColor = vec4(0.0,0.0,0.0,1.0);\n"
                "  else gl_FragColor = vec4(1.0,1.0,1.0,1.0); }";
            s_dbgProg = PostProcess::CompileProgram(vs, fs);
            if (s_dbgProg) s_dbgLoc = gl.GetUniformLocation(s_dbgProg, "uTex");
        }
        if (!s_dbgProg) return;
        GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
        // State-neutral: the menu cursor (drawn after, via SceneManager
        // RenderCursor) is alpha-tested + blended, so restore every enable we
        // flip. (Program binding isn't on the attrib stack -> UseProgram(0) below.)
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 420, 420);
        glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);
        gl.UseProgram(s_dbgProg);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_depthTex);
        if (s_dbgLoc >= 0) gl.Uniform1i(s_dbgLoc, 0);
        PostProcess::DrawFullscreenQuad();
        gl.UseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glViewport(vp[0], vp[1], vp[2], vp[3]);
        glPopAttrib();   // restore depth/blend/cull/texture enables
    }
}
