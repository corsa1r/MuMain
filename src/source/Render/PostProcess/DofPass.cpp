// DofPass.cpp — depth of field. See header for the 3-stage shape.

#include "stdafx.h"
#include "DofPass.h"
#include "PostProcessGL.h"

#include <windows.h>

namespace PostProcess
{
    namespace
    {
        // Shared fullscreen VS (GLSL 120, fixed-function attributes).
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        // Separable 9-tap Gaussian. uStep selects axis + radius (scaled by Blur).
        const char* kBlurFS = R"(
#version 120
uniform sampler2D uTex;
uniform vec2 uStep;
varying vec2 vUV;
void main()
{
    vec3 c  = texture2D(uTex, vUV).rgb * 0.227027;
    c += texture2D(uTex, vUV + uStep * 1.0).rgb * 0.194595;
    c += texture2D(uTex, vUV - uStep * 1.0).rgb * 0.194595;
    c += texture2D(uTex, vUV + uStep * 2.0).rgb * 0.121622;
    c += texture2D(uTex, vUV - uStep * 2.0).rgb * 0.121622;
    c += texture2D(uTex, vUV + uStep * 3.0).rgb * 0.054054;
    c += texture2D(uTex, vUV - uStep * 3.0).rgb * 0.054054;
    c += texture2D(uTex, vUV + uStep * 4.0).rgb * 0.016216;
    c += texture2D(uTex, vUV - uStep * 4.0).rgb * 0.016216;
    gl_FragColor = vec4(c, 1.0);
}
)";

        // Composite: circle-of-confusion from depth vs the focus plane, then blend
        // the sharp scene toward the blurred (half-res, bilinear-upsampled) image.
        const char* kCompFS = R"(
#version 120
uniform sampler2D uScene;   // full-res sharp
uniform sampler2D uBlur;    // half-res blurred
uniform sampler2D uDepth;
uniform float uNear;
uniform float uFar;
uniform float uFocus;       // view-space focus distance
uniform float uRange;       // sharp half-width
varying vec2 vUV;

float linDepth(float d)
{
    float z = d * 2.0 - 1.0;
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main()
{
    vec3 sharp = texture2D(uScene, vUV).rgb;
    // No projection -> pass-through.
    if (uNear <= 0.0 || uFar <= 0.0) { gl_FragColor = vec4(sharp, 1.0); return; }

    float linZ = linDepth(texture2D(uDepth, vUV).r);
    // CoC: 0 inside the sharp band [focus-range, focus+range], ramping to 1 over
    // one more 'range' beyond it (near AND far).
    float coc = clamp((abs(linZ - uFocus) - uRange) / max(uRange, 1.0), 0.0, 1.0);
    coc = coc * coc * (3.0 - 2.0 * coc);   // smoothstep ease

    vec3 blur = texture2D(uBlur, vUV).rgb;
    gl_FragColor = vec4(mix(sharp, blur, coc), 1.0);
}
)";
    }

    void DofPass::Destroy()
    {
        const GLProcs& gl = GL();
        if (m_fboA) { gl.DeleteFramebuffers(1, &m_fboA); m_fboA = 0; }
        if (m_fboB) { gl.DeleteFramebuffers(1, &m_fboB); m_fboB = 0; }
        if (m_texA) { glDeleteTextures(1, &m_texA); m_texA = 0; }
        if (m_texB) { glDeleteTextures(1, &m_texB); m_texB = 0; }
    }

    bool DofPass::Create(int w, int h)
    {
        const GLProcs& gl = GL();
        Destroy();

        m_hw = (w > 1) ? w / 2 : 1;
        m_hh = (h > 1) ? h / 2 : 1;

        m_texA = CreateColorTexture(m_hw, m_hh);
        gl.GenFramebuffers(1, &m_fboA);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fboA);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texA, 0);
        bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        m_texB = CreateColorTexture(m_hw, m_hh);
        gl.GenFramebuffers(1, &m_fboB);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fboB);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texB, 0);
        ok = ok && (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok) { OutputDebugStringA("[DoF] scratch FBO incomplete\n"); Destroy(); return false; }
        m_w = w;
        m_h = h;
        return true;
    }

    bool DofPass::EnsureResources(int width, int height)
    {
        if (!m_blurProg)
        {
            m_blurProg = CompileProgram(kVS, kBlurFS);
            if (!m_blurProg) return false;
            m_blurLocTex  = GL().GetUniformLocation(m_blurProg, "uTex");
            m_blurLocStep = GL().GetUniformLocation(m_blurProg, "uStep");
        }
        if (!m_compProg)
        {
            m_compProg = CompileProgram(kVS, kCompFS);
            if (!m_compProg) return false;
            const GLProcs& gl = GL();
            m_compScene = gl.GetUniformLocation(m_compProg, "uScene");
            m_compBlur  = gl.GetUniformLocation(m_compProg, "uBlur");
            m_compDepth = gl.GetUniformLocation(m_compProg, "uDepth");
            m_compNear  = gl.GetUniformLocation(m_compProg, "uNear");
            m_compFar   = gl.GetUniformLocation(m_compProg, "uFar");
            m_compFocus = gl.GetUniformLocation(m_compProg, "uFocus");
            m_compRange = gl.GetUniformLocation(m_compProg, "uRange");
        }
        if (m_fboA == 0 || width != m_w || height != m_h)
        {
            if (!Create(width, height)) return false;
        }
        return true;
    }

    void DofPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        // Half-res texel * blur scale -> Gaussian step (bokeh size).
        const float sx = (m_blur / static_cast<float>(m_hw));
        const float sy = (m_blur / static_cast<float>(m_hh));

        gl.UseProgram(m_blurProg);

        // --- 1) Blur X: full-res scene -> half-res texA (downsample + blur) ----
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fboA);
        glViewport(0, 0, m_hw, m_hh);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, sx, 0.0f);
        DrawFullscreenQuad();

        // --- 2) Blur Y: texA -> texB ------------------------------------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_fboB);
        glViewport(0, 0, m_hw, m_hh);
        glBindTexture(GL_TEXTURE_2D, m_texA);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, 0.0f, sy);
        DrawFullscreenQuad();

        // --- 3) Composite: mix(sharp, blurred) by CoC -> dest -----------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);
        gl.UseProgram(m_compProg);

        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_compScene, 0);
        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texB);
        gl.Uniform1i(m_compBlur, 1);
        gl.ActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceDepthTex);
        gl.Uniform1i(m_compDepth, 2);

        gl.Uniform1f(m_compNear, ctx.nearZ);
        gl.Uniform1f(m_compFar, ctx.farZ);
        gl.Uniform1f(m_compFocus, m_focus);
        gl.Uniform1f(m_compRange, m_range);
        DrawFullscreenQuad();

        // Leave texture units tidy for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
