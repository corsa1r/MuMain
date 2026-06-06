// BloomPass.cpp — threshold bloom. See header for design intent.

#include "stdafx.h"
#include "BloomPass.h"
#include "PostProcessGL.h"

#include <windows.h>
#include <algorithm>

namespace PostProcess
{
    namespace
    {
        // Shared fullscreen VS (GLSL 120, fixed-function attributes — matches
        // the SoftShadow / passthrough convention so no VAO/VBO setup needed).
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        // Bright-pass: keep only the luminance ABOVE the threshold, preserving
        // hue. Pixels below threshold contribute nothing. The division by
        // luminance keeps the extracted color saturated rather than whitened.
        const char* kBrightFS = R"(
#version 120
uniform sampler2D uScene;
uniform float uThreshold;
varying vec2 vUV;
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float contrib = max(l - uThreshold, 0.0);
    float scale = contrib / max(l, 1e-4);
    gl_FragColor = vec4(c * scale, 1.0);
}
)";

        // Separable 9-tap Gaussian (sigma ~3) on RGB. uStep is (1/w,0) then
        // (0,1/h). Same kernel family as the proven SoftShadow blur.
        const char* kBlurFS = R"(
#version 120
uniform sampler2D uTex;
uniform vec2 uStep;
varying vec2 vUV;
void main()
{
    float w0 = 0.227027;
    float w1 = 0.194595;
    float w2 = 0.121622;
    float w3 = 0.054054;
    float w4 = 0.016216;
    vec3 c = texture2D(uTex, vUV).rgb * w0;
    c += texture2D(uTex, vUV + uStep * 1.0).rgb * w1;
    c += texture2D(uTex, vUV - uStep * 1.0).rgb * w1;
    c += texture2D(uTex, vUV + uStep * 2.0).rgb * w2;
    c += texture2D(uTex, vUV - uStep * 2.0).rgb * w2;
    c += texture2D(uTex, vUV + uStep * 3.0).rgb * w3;
    c += texture2D(uTex, vUV - uStep * 3.0).rgb * w3;
    c += texture2D(uTex, vUV + uStep * 4.0).rgb * w4;
    c += texture2D(uTex, vUV - uStep * 4.0).rgb * w4;
    gl_FragColor = vec4(c, 1.0);
}
)";

        // Composite: original scene + blurred bloom * intensity. Additive — the
        // classic glow look; never darkens, only adds light.
        const char* kCompFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uIntensity;
varying vec2 vUV;
void main()
{
    vec3 s = texture2D(uScene, vUV).rgb;
    vec3 b = texture2D(uBloom, vUV).rgb;
    gl_FragColor = vec4(s + b * uIntensity, 1.0);
}
)";
    }

    void BloomPass::Destroy()
    {
        const GLProcs& gl = GL();
        if (m_brightFBO) { gl.DeleteFramebuffers(1, &m_brightFBO); m_brightFBO = 0; }
        if (m_blurFBO)   { gl.DeleteFramebuffers(1, &m_blurFBO);   m_blurFBO = 0; }
        if (m_brightTex) { glDeleteTextures(1, &m_brightTex);      m_brightTex = 0; }
        if (m_blurTex)   { glDeleteTextures(1, &m_blurTex);        m_blurTex = 0; }
    }

    bool BloomPass::Create(int halfW, int halfH)
    {
        const GLProcs& gl = GL();
        Destroy();

        m_brightTex = CreateColorTexture(halfW, halfH);
        gl.GenFramebuffers(1, &m_brightFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_brightFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brightTex, 0);
        bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        m_blurTex = CreateColorTexture(halfW, halfH);
        gl.GenFramebuffers(1, &m_blurFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex, 0);
        ok = ok && (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok)
        {
            OutputDebugStringA("[Bloom] scratch FBO incomplete\n");
            Destroy();
            return false;
        }
        m_halfW = halfW;
        m_halfH = halfH;
        return true;
    }

    bool BloomPass::EnsureResources(int width, int height)
    {
        const int halfW = std::max(1, width / 2);
        const int halfH = std::max(1, height / 2);

        if (!m_brightProg)
        {
            m_brightProg = CompileProgram(kVS, kBrightFS);
            if (!m_brightProg) return false;
            m_brightLocScene     = GL().GetUniformLocation(m_brightProg, "uScene");
            m_brightLocThreshold = GL().GetUniformLocation(m_brightProg, "uThreshold");
        }
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
            m_compLocScene     = GL().GetUniformLocation(m_compProg, "uScene");
            m_compLocBloom     = GL().GetUniformLocation(m_compProg, "uBloom");
            m_compLocIntensity = GL().GetUniformLocation(m_compProg, "uIntensity");
        }

        if (m_brightFBO == 0 || halfW != m_halfW || halfH != m_halfH)
        {
            if (!Create(halfW, halfH))
                return false;
        }
        return true;
    }

    void BloomPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        // --- 1) Bright extract: full-res scene -> half-res bright -----------
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_brightFBO);
        glViewport(0, 0, m_halfW, m_halfH);
        gl.UseProgram(m_brightProg);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_brightLocScene, 0);
        gl.Uniform1f(m_brightLocThreshold, m_threshold);
        DrawFullscreenQuad();

        // --- 2) Horizontal blur: bright -> blur ----------------------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        glViewport(0, 0, m_halfW, m_halfH);
        gl.UseProgram(m_blurProg);
        glBindTexture(GL_TEXTURE_2D, m_brightTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, 1.0f / static_cast<float>(m_halfW), 0.0f);
        DrawFullscreenQuad();

        // --- 3) Vertical blur: blur -> bright (bloom now lives in brightTex) -
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_brightFBO);
        glViewport(0, 0, m_halfW, m_halfH);
        glBindTexture(GL_TEXTURE_2D, m_blurTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, 0.0f, 1.0f / static_cast<float>(m_halfH));
        DrawFullscreenQuad();

        // --- 4) Composite: scene + bloom -> destination (full res) ---------
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);
        gl.UseProgram(m_compProg);

        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_compLocScene, 0);

        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_brightTex);
        gl.Uniform1i(m_compLocBloom, 1);

        gl.Uniform1f(m_compLocIntensity, m_intensity);
        DrawFullscreenQuad();

        // Leave texture units tidy for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
