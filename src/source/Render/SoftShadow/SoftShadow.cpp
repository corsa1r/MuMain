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

    GLuint s_shadowFBO    = 0;
    GLuint s_shadowColor  = 0;
    GLuint s_shadowStencil = 0;  // stencil renderbuffer
    GLuint s_pingFBO      = 0;
    GLuint s_pingColor    = 0;

    GLuint s_blurProgram   = 0;
    GLint  s_blurLocSampler = -1;
    GLint  s_blurLocStep    = -1;  // vec2 — texel size along blur axis

    // Stack of previously-bound framebuffers so BeginShadowDraw/EndShadowDraw
    // can nest safely. In practice the depth never exceeds 1, but storing it
    // explicitly avoids subtle issues if something else binds an FBO.
    GLint     s_savedFBO = 0;
    GLint     s_savedBlendSrc = GL_ONE;
    GLint     s_savedBlendDst = GL_ZERO;
    GLboolean s_savedBlendEnabled = GL_FALSE;
    GLint     s_savedStencilMask = 0xFF;
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

    GLuint BuildBlurProgram()
    {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, kBlurVS);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kBlurFS);
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
        if (s_shadowColor)   { glDeleteTextures(1, &s_shadowColor);     s_shadowColor = 0; }
        if (s_pingColor)     { glDeleteTextures(1, &s_pingColor);       s_pingColor = 0; }
        if (s_shadowStencil) { pglDeleteRenderbuffers(1, &s_shadowStencil); s_shadowStencil = 0; }
    }

    bool CreateTargets(int w, int h)
    {
        DestroyTargets();

        // Shadow accumulation: RGBA8 color + 8-bit stencil renderbuffer.
        glGenTextures(1, &s_shadowColor);
        glBindTexture(GL_TEXTURE_2D, s_shadowColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        pglGenRenderbuffers(1, &s_shadowStencil);
        pglBindRenderbuffer(GL_RENDERBUFFER, s_shadowStencil);
        pglRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, w, h);

        pglGenFramebuffers(1, &s_shadowFBO);
        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_shadowColor, 0);
        pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_shadowStencil);
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
        s_blurProgram = BuildBlurProgram();
        if (!s_blurProgram)
        {
            s_available = false;
            return false;
        }
        s_blurLocSampler = pglGetUniformLocation(s_blurProgram, "uTex");
        s_blurLocStep    = pglGetUniformLocation(s_blurProgram, "uStep");
        s_available = true;
        return true;
    }

    void Shutdown()
    {
        if (s_blurProgram) { pglDeleteProgram(s_blurProgram); s_blurProgram = 0; }
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
        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        // Force masks fully open so the clear actually touches every
        // channel/bit. Caller code elsewhere may have left colorMask or
        // stencilMask half-disabled, which would silently skip the clear
        // and leave the FBO with whatever garbage initial contents it had.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glStencilMask(0xFF);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void BeginShadowDraw()
    {
        if (!IsAvailable()) return;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s_savedFBO);
        glGetIntegerv(GL_BLEND_SRC, &s_savedBlendSrc);
        glGetIntegerv(GL_BLEND_DST, &s_savedBlendDst);
        s_savedBlendEnabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_STENCIL_WRITEMASK, &s_savedStencilMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, s_savedColorMask);

        pglBindFramebuffer(GL_FRAMEBUFFER, s_shadowFBO);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void EndShadowDraw()
    {
        if (!IsAvailable()) return;
        pglBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(s_savedFBO));
        s_savedFBO = 0;

        // Restore caller's blend, stencil-mask, color-mask state. The body
        // pass that interleaves with shadow draws relies on these being
        // exactly as they were before we hijacked the framebuffer.
        glBlendFunc(s_savedBlendSrc, s_savedBlendDst);
        if (s_savedBlendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glStencilMask(static_cast<GLuint>(s_savedStencilMask));
        glColorMask(s_savedColorMask[0], s_savedColorMask[1], s_savedColorMask[2], s_savedColorMask[3]);
    }

    void Composite()
    {
        if (!IsAvailable()) return;

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

        // ── Composite: shadowColor → back buffer (alpha blend) ───────────
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, s_width, s_height);
        pglUseProgram(0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, s_shadowColor);
        // Fixed-function fullscreen quad (no shader) — the texture already
        // contains pre-multiplied dark color, so just stamp it.
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glEnd();
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();

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
