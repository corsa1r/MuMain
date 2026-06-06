// SoftShadow.cpp — see header for high-level design.

#include "stdafx.h"
#include "SoftShadow.h"

#include <gl/glew.h>
#include <windows.h>
#include <cstdio>

namespace
{
    // ── Entry points loaded at Init() ───────────────────────────────────
    // We deliberately don't call glewInit() — glew32.lib isn't shipped in
    // this repo's dependencies/lib. Loading just what we need keeps the new
    // surface tiny.

    PFNGLGENFRAMEBUFFERSPROC         pglGenFramebuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC      pglDeleteFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC         pglBindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC    pglFramebufferTexture2D = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC  pglCheckFramebufferStatus = nullptr;
    PFNGLGENRENDERBUFFERSPROC        pglGenRenderbuffers = nullptr;
    PFNGLDELETERENDERBUFFERSPROC     pglDeleteRenderbuffers = nullptr;
    PFNGLBINDRENDERBUFFERPROC        pglBindRenderbuffer = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC     pglRenderbufferStorage = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC pglFramebufferRenderbuffer = nullptr;
    PFNGLBLITFRAMEBUFFERPROC         pglBlitFramebuffer = nullptr;

    PFNGLCREATESHADERPROC            pglCreateShader = nullptr;
    PFNGLDELETESHADERPROC            pglDeleteShader = nullptr;
    PFNGLSHADERSOURCEPROC            pglShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC           pglCompileShader = nullptr;
    PFNGLGETSHADERIVPROC             pglGetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC        pglGetShaderInfoLog = nullptr;

    PFNGLCREATEPROGRAMPROC           pglCreateProgram = nullptr;
    PFNGLDELETEPROGRAMPROC           pglDeleteProgram = nullptr;
    PFNGLATTACHSHADERPROC            pglAttachShader = nullptr;
    PFNGLLINKPROGRAMPROC             pglLinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC            pglGetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC       pglGetProgramInfoLog = nullptr;
    PFNGLUSEPROGRAMPROC              pglUseProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC      pglGetUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC               pglUniform1i = nullptr;
    PFNGLUNIFORM2FPROC               pglUniform2f = nullptr;
    PFNGLUNIFORM1FPROC               pglUniform1f = nullptr;

    PFNGLACTIVETEXTUREPROC           pglActiveTexture = nullptr;

    template <typename Fn>
    bool LoadProc(Fn& out, const char* name)
    {
        out = reinterpret_cast<Fn>(wglGetProcAddress(name));
        return out != nullptr;
    }

    bool LoadEntrypoints()
    {
        bool ok = true;
        ok &= LoadProc(pglGenFramebuffers,         "glGenFramebuffers");
        ok &= LoadProc(pglDeleteFramebuffers,      "glDeleteFramebuffers");
        ok &= LoadProc(pglBindFramebuffer,         "glBindFramebuffer");
        ok &= LoadProc(pglFramebufferTexture2D,    "glFramebufferTexture2D");
        ok &= LoadProc(pglCheckFramebufferStatus,  "glCheckFramebufferStatus");
        ok &= LoadProc(pglGenRenderbuffers,        "glGenRenderbuffers");
        ok &= LoadProc(pglDeleteRenderbuffers,     "glDeleteRenderbuffers");
        ok &= LoadProc(pglBindRenderbuffer,        "glBindRenderbuffer");
        ok &= LoadProc(pglRenderbufferStorage,     "glRenderbufferStorage");
        ok &= LoadProc(pglFramebufferRenderbuffer, "glFramebufferRenderbuffer");
        ok &= LoadProc(pglBlitFramebuffer,         "glBlitFramebuffer");

        ok &= LoadProc(pglCreateShader,            "glCreateShader");
        ok &= LoadProc(pglDeleteShader,            "glDeleteShader");
        ok &= LoadProc(pglShaderSource,            "glShaderSource");
        ok &= LoadProc(pglCompileShader,           "glCompileShader");
        ok &= LoadProc(pglGetShaderiv,             "glGetShaderiv");
        ok &= LoadProc(pglGetShaderInfoLog,        "glGetShaderInfoLog");

        ok &= LoadProc(pglCreateProgram,           "glCreateProgram");
        ok &= LoadProc(pglDeleteProgram,           "glDeleteProgram");
        ok &= LoadProc(pglAttachShader,            "glAttachShader");
        ok &= LoadProc(pglLinkProgram,             "glLinkProgram");
        ok &= LoadProc(pglGetProgramiv,            "glGetProgramiv");
        ok &= LoadProc(pglGetProgramInfoLog,       "glGetProgramInfoLog");
        ok &= LoadProc(pglUseProgram,              "glUseProgram");
        ok &= LoadProc(pglGetUniformLocation,      "glGetUniformLocation");
        ok &= LoadProc(pglUniform1i,               "glUniform1i");
        ok &= LoadProc(pglUniform2f,               "glUniform2f");
        ok &= LoadProc(pglUniform1f,               "glUniform1f");

        ok &= LoadProc(pglActiveTexture,           "glActiveTexture");
        return ok;
    }

    // ── State ────────────────────────────────────────────────────────────
    bool s_available = false;

    int  s_width = 0;
    int  s_height = 0;

    GLuint s_shadowFBO     = 0;
    GLuint s_shadowColor   = 0;
    GLuint s_shadowDepth   = 0;  // depth texture for shadow FBO (per-pixel ground Z)
    GLuint s_pingFBO       = 0;
    GLuint s_pingColor     = 0;
    GLuint s_sceneDepth    = 0;  // depth texture blit target for scene depth
    GLuint s_sceneDepthFBO = 0;  // FBO wrapping s_sceneDepth as DEPTH_ATTACHMENT

    GLuint s_blurProgram   = 0;
    GLint  s_blurLocSampler = -1;
    GLint  s_blurLocStep    = -1;  // vec2 — texel size along blur axis

    GLuint s_compositeProgram = 0;
    GLint  s_compositeLocColor  = -1;
    GLint  s_compositeLocShadowZ = -1;
    GLint  s_compositeLocSceneZ  = -1;

    // Stack of previously-bound framebuffers so BeginShadowDraw/EndShadowDraw
    // can nest safely. In practice the depth never exceeds 1, but storing it
    // explicitly avoids subtle issues if something else binds an FBO.
    GLint     s_savedFBO = 0;
    GLint     s_savedBlendSrc = GL_ONE;
    GLint     s_savedBlendDst = GL_ZERO;
    GLboolean s_savedBlendEnabled = GL_FALSE;
    GLboolean s_savedDepthMask = GL_TRUE;
    GLboolean s_savedColorMask[4] = {1, 1, 1, 1};

    // ── Shader sources ──────────────────────────────────────────────────
    // Targeting #version 120 — keeps fixed-function attribute compatibility
    // (gl_Vertex / gl_MultiTexCoord0) so the host doesn't have to switch to
    // VAOs/VBOs. Available on every desktop GL since 2007.
    const char* kBlurVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

    // Separable Gaussian, 11-tap, sigma ~= 4.0. uStep is (1/w, 0) for
    // horizontal pass or (0, 1/h) for vertical.
    const char* kBlurFS = R"(
#version 120
uniform sampler2D uTex;
uniform vec2 uStep;
varying vec2 vUV;

void main()
{
    // Weights derived from a Gaussian with sigma = 4, normalized.
    float w0 = 0.197;
    float w1 = 0.175;
    float w2 = 0.121;
    float w3 = 0.066;
    float w4 = 0.028;
    float w5 = 0.009;

    vec4 c = texture2D(uTex, vUV) * w0;
    c += texture2D(uTex, vUV + uStep * 1.0) * w1;
    c += texture2D(uTex, vUV - uStep * 1.0) * w1;
    c += texture2D(uTex, vUV + uStep * 2.0) * w2;
    c += texture2D(uTex, vUV - uStep * 2.0) * w2;
    c += texture2D(uTex, vUV + uStep * 3.0) * w3;
    c += texture2D(uTex, vUV - uStep * 3.0) * w3;
    c += texture2D(uTex, vUV + uStep * 4.0) * w4;
    c += texture2D(uTex, vUV - uStep * 4.0) * w4;
    c += texture2D(uTex, vUV + uStep * 5.0) * w5;
    c += texture2D(uTex, vUV - uStep * 5.0) * w5;
    gl_FragColor = c;
}
)";

    // Composite shader: stamps the blurred shadow over the back buffer, but
    // discards pixels where the scene depth (back buffer, includes character
    // bodies) is in front of the shadow depth (ground plane Z). That prevents
    // the shadow from tinting the body that cast it.
    const char* kCompositeVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

    const char* kCompositeFS = R"(
#version 120
uniform sampler2D uShadowColor;
uniform sampler2D uShadowDepth;
uniform sampler2D uSceneDepth;
varying vec2 vUV;

void main()
{
    vec4 sc = texture2D(uShadowColor, vUV);
    if (sc.a < 0.001) discard;
    float shadowD = texture2D(uShadowDepth, vUV).r;
    float sceneD  = texture2D(uSceneDepth,  vUV).r;
    // If no shadow geometry was drawn here, shadowD will still be 1.0 — skip
    // the occlusion check so we don't accidentally discard the blurred halo
    // that extends beyond the original silhouette.
    if (shadowD < 0.9999 && sceneD < shadowD - 0.001) discard;
    gl_FragColor = sc;
}
)";

    // ── Helpers ─────────────────────────────────────────────────────────
    GLuint CompileShader(GLenum stage, const char* src)
    {
        GLuint sh = pglCreateShader(stage);
        pglShaderSource(sh, 1, &src, nullptr);
        pglCompileShader(sh);
        GLint ok = 0;
        pglGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024]{};
            pglGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            OutputDebugStringA("[SoftShadow] shader compile failed:\n");
            OutputDebugStringA(log);
            pglDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    GLuint LinkProgram(const char* vsSrc, const char* fsSrc)
    {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSrc);
        if (!vs || !fs) return 0;

        GLuint prog = pglCreateProgram();
        pglAttachShader(prog, vs);
        pglAttachShader(prog, fs);
        pglLinkProgram(prog);

        GLint ok = 0;
        pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024]{};
            pglGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            OutputDebugStringA("[SoftShadow] program link failed:\n");
            OutputDebugStringA(log);
            pglDeleteProgram(prog);
            prog = 0;
        }
        pglDeleteShader(vs);
        pglDeleteShader(fs);
        return prog;
    }

    void DestroyTargets()
    {
        if (s_shadowFBO)     { pglDeleteFramebuffers(1, &s_shadowFBO);  s_shadowFBO = 0; }
        if (s_pingFBO)       { pglDeleteFramebuffers(1, &s_pingFBO);    s_pingFBO = 0; }
        if (s_sceneDepthFBO) { pglDeleteFramebuffers(1, &s_sceneDepthFBO); s_sceneDepthFBO = 0; }
        if (s_shadowColor)   { glDeleteTextures(1, &s_shadowColor);     s_shadowColor = 0; }
        if (s_shadowDepth)   { glDeleteTextures(1, &s_shadowDepth);     s_shadowDepth = 0; }
        if (s_pingColor)     { glDeleteTextures(1, &s_pingColor);       s_pingColor = 0; }
        if (s_sceneDepth)    { glDeleteTextures(1, &s_sceneDepth);      s_sceneDepth = 0; }
    }

    GLuint CreateDepthTexture(int w, int h)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        // NEAREST so the per-pixel depth comparison isn't softened by a filter.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    }

    bool CreateTargets(int w, int h)
    {
        DestroyTargets();

        // Shadow accumulation: RGBA8 color + depth texture. Depth is needed so
        // the composite pass can compare shadow-ground Z against scene Z and
        // discard pixels where a body is in front of its own shadow.
        glGenTextures(1, &s_shadowColor);
        glBindTexture(GL_TEXTURE_2D, s_shadowColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        s_shadowDepth = CreateDepthTexture(w, h);

        pglGenFramebuffers(1, &s_shadowFBO);
        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_shadowColor, 0);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, s_shadowDepth, 0);
        if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            OutputDebugStringA("[SoftShadow] shadow FBO incomplete\n");
            pglBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        // Ping-pong color target for blur.
        glGenTextures(1, &s_pingColor);
        glBindTexture(GL_TEXTURE_2D, s_pingColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        pglGenFramebuffers(1, &s_pingFBO);
        pglBindFramebuffer(GL_FRAMEBUFFER, s_pingFBO);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_pingColor, 0);
        if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            OutputDebugStringA("[SoftShadow] ping FBO incomplete\n");
            pglBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        // Scene-depth target: receives a blit of the back buffer's depth at
        // composite time. Wrapped in its own FBO so glBlitFramebuffer has a
        // depth-attached destination.
        s_sceneDepth = CreateDepthTexture(w, h);
        pglGenFramebuffers(1, &s_sceneDepthFBO);
        pglBindFramebuffer(GL_FRAMEBUFFER, s_sceneDepthFBO);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_sceneDepth, 0);
        // No color attachment — explicitly tell GL we're depth-only or some
        // drivers will mark the FBO incomplete.
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            OutputDebugStringA("[SoftShadow] scene-depth FBO incomplete\n");
            pglBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void DrawFullscreenQuad()
    {
        // Identity-space quad in clip coords (-1..1). The blur shader reads
        // gl_MultiTexCoord0 for UV so we also emit per-vertex texcoords.
        // Fixed-function attribute setup is fine under GLSL 120.
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

        glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glEnd();

        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    }
}

// ── Public API ──────────────────────────────────────────────────────────
namespace SoftShadow
{
    bool Init()
    {
        if (!LoadEntrypoints())
        {
            OutputDebugStringA("[SoftShadow] failed to load required GL entry points\n");
            s_available = false;
            return false;
        }
        s_blurProgram = LinkProgram(kBlurVS, kBlurFS);
        s_compositeProgram = LinkProgram(kCompositeVS, kCompositeFS);
        if (!s_blurProgram || !s_compositeProgram)
        {
            s_available = false;
            return false;
        }
        s_blurLocSampler = pglGetUniformLocation(s_blurProgram, "uTex");
        s_blurLocStep    = pglGetUniformLocation(s_blurProgram, "uStep");

        s_compositeLocColor    = pglGetUniformLocation(s_compositeProgram, "uShadowColor");
        s_compositeLocShadowZ  = pglGetUniformLocation(s_compositeProgram, "uShadowDepth");
        s_compositeLocSceneZ   = pglGetUniformLocation(s_compositeProgram, "uSceneDepth");

        s_available = true;
        return true;
    }

    void Shutdown()
    {
        if (s_blurProgram)      { pglDeleteProgram(s_blurProgram);      s_blurProgram = 0; }
        if (s_compositeProgram) { pglDeleteProgram(s_compositeProgram); s_compositeProgram = 0; }
        DestroyTargets();
        s_available = false;
    }

    void Resize(int width, int height)
    {
        if (!s_available) return;
        if (width <= 0 || height <= 0) return;
        if (width == s_width && height == s_height && s_shadowFBO != 0) return;
        if (!CreateTargets(width, height))
        {
            // Allocation failed — drop back to legacy path.
            s_available = false;
            return;
        }
        s_width = width;
        s_height = height;
    }

    bool IsAvailable() { return s_available && s_shadowFBO != 0; }

    void BeginFrame()
    {
        if (!IsAvailable()) return;

        // Capture-agnostic restore: remember whichever framebuffer the scene is
        // currently rendering into and rebind it after we clear the shadow FBO.
        // Normally that is 0 (the backbuffer); when the post-process off-screen
        // capture is active it is the scene RTV. Hard-coding 0 here would yank
        // subsequent scene draws back to the backbuffer and break capture.
        GLint prevFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        // Force masks fully open so the clear actually touches every channel.
        // Caller code elsewhere may have left colorMask or depthMask off,
        // which would silently skip the clear.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        pglBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
    }

    void BeginShadowDraw()
    {
        if (!IsAvailable()) return;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_savedFBO);
        glGetIntegerv(GL_BLEND_SRC, &s_savedBlendSrc);
        glGetIntegerv(GL_BLEND_DST, &s_savedBlendDst);
        s_savedBlendEnabled = glIsEnabled(GL_BLEND);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &s_savedDepthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, s_savedColorMask);

        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        // Need depth writes so the FBO depth attachment captures the ground
        // Z of each shadow pixel — the composite pass relies on it.
        glDepthMask(GL_TRUE);
    }

    void EndShadowDraw()
    {
        if (!IsAvailable()) return;
        pglBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s_savedFBO));
        s_savedFBO = 0;

        // Restore caller's blend, depth-mask, color-mask state. The body
        // pass that interleaves with shadow draws relies on these being
        // exactly as they were before we hijacked the framebuffer.
        glBlendFunc(s_savedBlendSrc, s_savedBlendDst);
        if (s_savedBlendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glDepthMask(s_savedDepthMask);
        glColorMask(s_savedColorMask[0], s_savedColorMask[1], s_savedColorMask[2], s_savedColorMask[3]);
    }

    void Composite()
    {
        if (!IsAvailable()) return;

        // Remember the framebuffer the scene rendered into so both the scene-
        // depth blit (read source) and the final composite (draw target) follow
        // the scene instead of a hard-coded backbuffer. 0 normally; the post-
        // process scene RTV when off-screen capture is active. This is the only
        // change needed for SoftShadow to coexist with the post-process chain —
        // the blur+composite algorithm itself is untouched.
        GLint prevSceneFBO = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevSceneFBO);

        // Snapshot the legacy GL state we touch, restore at the end.
        GLboolean prevBlend     = glIsEnabled(GL_BLEND);
        GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean prevCullFace  = glIsEnabled(GL_CULL_FACE);
        GLboolean prevAlphaTest = glIsEnabled(GL_ALPHA_TEST);
        GLboolean prevFog       = glIsEnabled(GL_FOG);
        GLboolean prevTex2D     = glIsEnabled(GL_TEXTURE_2D);
        GLboolean prevStencil   = glIsEnabled(GL_STENCIL_TEST);
        GLint     prevTexBind   = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexBind);
        GLint     prevViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        GLint     prevBlendSrc = GL_ONE, prevBlendDst = GL_ZERO;
        glGetIntegerv(GL_BLEND_SRC, &prevBlendSrc);
        glGetIntegerv(GL_BLEND_DST, &prevBlendDst);
        GLfloat   prevColor[4] = {1, 1, 1, 1};
        glGetFloatv(GL_CURRENT_COLOR, prevColor);
        GLboolean prevColorMask[4] = {1, 1, 1, 1};
        glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

        // Force a known-good state.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_ALPHA_TEST);
        glDisable(GL_FOG);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_TEXTURE_2D);

        // ── Pass 1: horizontal blur, shadowColor → pingFBO ────────────────
        pglBindFramebuffer(GL_FRAMEBUFFER, s_pingFBO);
        glViewport(0, 0, s_width, s_height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_BLEND);

        pglUseProgram(s_blurProgram);
        pglActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_shadowColor);
        pglUniform1i(s_blurLocSampler, 0);
        pglUniform2f(s_blurLocStep, 1.0f / static_cast<float>(s_width), 0.0f);
        DrawFullscreenQuad();

        // ── Pass 2: vertical blur, pingColor → shadowFBO ─────────────────
        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        glViewport(0, 0, s_width, s_height);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);  // stencil left alone

        glBindTexture(GL_TEXTURE_2D, s_pingColor);
        pglUniform1i(s_blurLocSampler, 0);
        pglUniform2f(s_blurLocStep, 0.0f, 1.0f / static_cast<float>(s_height));
        DrawFullscreenQuad();

        // ── Capture scene depth ──────────────────────────────────────────
        // Blit the default framebuffer's depth into our scene-depth texture
        // so the composite shader can read it as a sampler2D.
        pglBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevSceneFBO));
        pglBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_sceneDepthFBO);
        pglBlitFramebuffer(0, 0, s_width, s_height,
                           0, 0, s_width, s_height,
                           GL_DEPTH_BUFFER_BIT, GL_NEAREST);

        // ── Composite: shadowColor + depth tests → scene target ──────────
        // (the backbuffer normally, or the post-process scene RTV when active)
        pglBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevSceneFBO));
        glViewport(0, 0, s_width, s_height);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        pglUseProgram(s_compositeProgram);

        pglActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_shadowColor);
        pglUniform1i(s_compositeLocColor, 0);

        pglActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_shadowDepth);
        pglUniform1i(s_compositeLocShadowZ, 1);

        pglActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, s_sceneDepth);
        pglUniform1i(s_compositeLocSceneZ, 2);

        DrawFullscreenQuad();

        // Unbind extra texture units we touched and return to unit 0 so we
        // don't leave foreign state behind for the next legacy draw.
        glBindTexture(GL_TEXTURE_2D, 0);
        pglActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        pglActiveTexture(GL_TEXTURE0);
        pglUseProgram(0);

        // Restore.
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexBind));
        if (!prevTex2D)   glDisable(GL_TEXTURE_2D);
        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (prevCullFace) glEnable(GL_CULL_FACE);
        if (prevAlphaTest) glEnable(GL_ALPHA_TEST);
        if (prevFog)      glEnable(GL_FOG);
        if (prevStencil)  glEnable(GL_STENCIL_TEST);
        if (!prevBlend)   glDisable(GL_BLEND);
        glBlendFunc(prevBlendSrc, prevBlendDst);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glColor4fv(prevColor);
        glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
    }
}
