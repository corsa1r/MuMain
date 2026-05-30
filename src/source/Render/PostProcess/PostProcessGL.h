// ============================================================================
//  PostProcessGL.h  —  shared GL entry points + helpers for the post chain
// ----------------------------------------------------------------------------
//  WHY THIS EXISTS
//  The post-process system needs the same family of "modern" GL entry points
//  that SoftShadow.cpp loads privately (FBOs, GLSL programs, multitexture).
//  Rather than have every pass re-load them, we centralise loading here and
//  expose a single proc table + a few helpers. This is the one place that
//  touches wglGetProcAddress for the post chain.
//
//  CONVENTION (matches SoftShadow): we do NOT call glewInit(); glew is used
//  only for its typedefs/enums. Entry points are resolved at runtime so the
//  module degrades gracefully (PostProcessChain reports !available) on a
//  context that lacks them, instead of failing to link.
//
//  DECOUPLING NOTE: nothing here knows about the game, the scene, or any
//  specific effect. It is pure GL plumbing shared by the chain and its passes.
// ============================================================================
#pragma once

#include <gl/glew.h>

namespace PostProcess
{
    // Resolved GL entry points used across the post chain. Pointers are null
    // until Load() succeeds. Passes call these instead of raw gl* so they work
    // without a global loader/glew init.
    struct GLProcs
    {
        // Framebuffer objects
        PFNGLGENFRAMEBUFFERSPROC         GenFramebuffers        = nullptr;
        PFNGLDELETEFRAMEBUFFERSPROC      DeleteFramebuffers     = nullptr;
        PFNGLBINDFRAMEBUFFERPROC         BindFramebuffer        = nullptr;
        PFNGLFRAMEBUFFERTEXTURE2DPROC    FramebufferTexture2D   = nullptr;
        PFNGLCHECKFRAMEBUFFERSTATUSPROC  CheckFramebufferStatus = nullptr;
        PFNGLBLITFRAMEBUFFERPROC         BlitFramebuffer        = nullptr;

        // Shader / program objects
        PFNGLCREATESHADERPROC            CreateShader     = nullptr;
        PFNGLDELETESHADERPROC            DeleteShader     = nullptr;
        PFNGLSHADERSOURCEPROC            ShaderSource     = nullptr;
        PFNGLCOMPILESHADERPROC           CompileShader    = nullptr;
        PFNGLGETSHADERIVPROC             GetShaderiv      = nullptr;
        PFNGLGETSHADERINFOLOGPROC        GetShaderInfoLog = nullptr;
        PFNGLCREATEPROGRAMPROC           CreateProgram    = nullptr;
        PFNGLDELETEPROGRAMPROC           DeleteProgram    = nullptr;
        PFNGLATTACHSHADERPROC            AttachShader     = nullptr;
        PFNGLLINKPROGRAMPROC             LinkProgram      = nullptr;
        PFNGLGETPROGRAMIVPROC            GetProgramiv     = nullptr;
        PFNGLGETPROGRAMINFOLOGPROC       GetProgramInfoLog = nullptr;
        PFNGLUSEPROGRAMPROC              UseProgram       = nullptr;
        PFNGLGETUNIFORMLOCATIONPROC      GetUniformLocation = nullptr;
        PFNGLUNIFORM1IPROC               Uniform1i        = nullptr;
        PFNGLUNIFORM1FPROC               Uniform1f        = nullptr;
        PFNGLUNIFORM2FPROC               Uniform2f        = nullptr;
        PFNGLUNIFORM4FPROC               Uniform4f        = nullptr;

        // Multitexture (passes that sample more than one input)
        PFNGLACTIVETEXTUREPROC           ActiveTexture    = nullptr;
    };

    // The shared, process-wide proc table. Valid only after Load() returns true.
    const GLProcs& GL();

    // Resolve all entry points. Idempotent; returns the cached result on repeat
    // calls. Must be called on a current GL context (i.e. after wglMakeCurrent).
    bool Load();

    // True once Load() has succeeded.
    bool Available();

    // ---- Helpers shared by the chain and every pass --------------------------

    // Compile a VS+FS pair into a linked program. Logs to OutputDebugString and
    // returns 0 on failure (callers fall back / disable the pass).
    GLuint CompileProgram(const char* vertexSrc, const char* fragmentSrc);

    // Create an RGBA8 color texture suitable as an FBO color attachment.
    GLuint CreateColorTexture(int width, int height);

    // Create a DEPTH_COMPONENT24 texture (NEAREST) usable as an FBO depth
    // attachment AND sampled later by depth-aware passes (fog, SSAO, ...).
    GLuint CreateDepthTexture(int width, int height);

    // Draw a clip-space [-1,1] quad emitting gl_MultiTexCoord0 in [0,1]. Passes
    // read it as the sample UV. Uses fixed-function attributes (GLSL 120 compat)
    // so we don't impose VAO/VBO setup on the legacy context. Saves/restores the
    // projection & modelview matrices it touches.
    void DrawFullscreenQuad();
}
