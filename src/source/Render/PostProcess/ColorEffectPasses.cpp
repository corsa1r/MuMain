// ColorEffectPasses.cpp — see header. Shaders are GLSL 120 to match the
// existing compatibility context (same target SoftShadow / Bloom use).

#include "stdafx.h"
#include "ColorEffectPasses.h"
#include "PostProcessGL.h"

namespace PostProcess
{
    namespace
    {
        // Shared fullscreen vertex shader. Reads gl_MultiTexCoord0 (emitted by
        // DrawFullscreenQuad) as the sample UV.
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";
    }

    // ===== FullscreenPass base ==============================================
    bool FullscreenPass::EnsureResources(int /*w*/, int /*h*/)
    {
        if (m_program)
            return true;
        m_program = CompileProgram(kVS, Fragment());
        if (!m_program)
            return false;
        m_locScene = GL().GetUniformLocation(m_program, "uScene");
        OnProgramLinked();
        return true;
    }

    void FullscreenPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        gl.UseProgram(m_program);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_locScene, 0);

        SetParams(ctx);
        DrawFullscreenQuad();

        gl.UseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ===== Tone mapping (ACES) =============================================
    const char* ToneMapPass::Fragment() const
    {
        // Narkowicz ACES approximation. Operates on the LDR scene for
        // stylization; exposure scales before the curve so highlights roll off.
        return R"(
#version 120
uniform sampler2D uScene;
uniform float uExposure;
varying vec2 vUV;
vec3 aces(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb * uExposure;
    gl_FragColor = vec4(aces(c), 1.0);
}
)";
    }
    void ToneMapPass::OnProgramLinked()
    {
        m_locExposure = GL().GetUniformLocation(m_program, "uExposure");
    }
    void ToneMapPass::SetParams(const PassContext&)
    {
        GL().Uniform1f(m_locExposure, m_exposure);
    }

    // ===== Color grading ===================================================
    const char* ColorGradePass::Fragment() const
    {
        return R"(
#version 120
uniform sampler2D uScene;
uniform float uContrast;
uniform float uSaturation;
uniform float uBrightness;
uniform float uTemperature;
uniform float uShadows;     // gain for dark pixels (1 = neutral)
uniform float uMidtones;    // gain for mid pixels
uniform float uHighlights;  // gain for bright pixels
varying vec2 vUV;
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    c *= uBrightness;
    // Warm/cool shift: push red up & blue down for positive (warm) temperature.
    c.r += uTemperature * 0.10;
    c.b -= uTemperature * 0.10;

    // Per-zone tonal gain. Weight each control by where the pixel's luminance
    // sits: shadows fade out by mid-grey, highlights fade in past mid-grey,
    // midtones cover the middle. Weights sum to 1 so neutral (all=1) is a no-op.
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    float wShadow = smoothstep(0.5, 0.0, l);
    float wHigh   = smoothstep(0.5, 1.0, l);
    float wMid    = 1.0 - wShadow - wHigh;
    c *= (uShadows * wShadow + uMidtones * wMid + uHighlights * wHigh);

    // Contrast about mid-grey.
    c = (c - 0.5) * uContrast + 0.5;
    // Saturation about luminance.
    float l2 = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l2), c, uSaturation);
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";
    }
    void ColorGradePass::OnProgramLinked()
    {
        const GLProcs& gl = GL();
        m_locContrast    = gl.GetUniformLocation(m_program, "uContrast");
        m_locSaturation  = gl.GetUniformLocation(m_program, "uSaturation");
        m_locBrightness  = gl.GetUniformLocation(m_program, "uBrightness");
        m_locTemperature = gl.GetUniformLocation(m_program, "uTemperature");
        m_locShadows     = gl.GetUniformLocation(m_program, "uShadows");
        m_locMidtones    = gl.GetUniformLocation(m_program, "uMidtones");
        m_locHighlights  = gl.GetUniformLocation(m_program, "uHighlights");
    }
    void ColorGradePass::SetParams(const PassContext&)
    {
        const GLProcs& gl = GL();
        gl.Uniform1f(m_locContrast, m_contrast);
        gl.Uniform1f(m_locSaturation, m_saturation);
        gl.Uniform1f(m_locBrightness, m_brightness);
        gl.Uniform1f(m_locTemperature, m_temperature);
        gl.Uniform1f(m_locShadows, m_shadows);
        gl.Uniform1f(m_locMidtones, m_midtones);
        gl.Uniform1f(m_locHighlights, m_highlights);
    }

    // ===== FXAA ============================================================
    const char* FxaaPass::Fragment() const
    {
        // NVIDIA "simplified FXAA" (Timothy Lottes), GLSL 120 compatible.
        return R"(
#version 120
uniform sampler2D uScene;
uniform vec2 uTexel;
varying vec2 vUV;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main()
{
    vec3 rgbM  = texture2D(uScene, vUV).rgb;
    float lM   = luma(rgbM);
    float lNW  = luma(texture2D(uScene, vUV + vec2(-1.0, -1.0) * uTexel).rgb);
    float lNE  = luma(texture2D(uScene, vUV + vec2( 1.0, -1.0) * uTexel).rgb);
    float lSW  = luma(texture2D(uScene, vUV + vec2(-1.0,  1.0) * uTexel).rgb);
    float lSE  = luma(texture2D(uScene, vUV + vec2( 1.0,  1.0) * uTexel).rgb);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec2 dir;
    dir.x = -((lNW + lNE) - (lSW + lSE));
    dir.y =  ((lNW + lSW) - (lNE + lSE));

    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcp, -8.0, 8.0) * uTexel;

    vec3 rgbA = 0.5 * (texture2D(uScene, vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture2D(uScene, vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(uScene, vUV + dir * -0.5).rgb +
                                     texture2D(uScene, vUV + dir *  0.5).rgb);
    float lB = luma(rgbB);
    gl_FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
)";
    }
    void FxaaPass::OnProgramLinked()
    {
        m_locTexel = GL().GetUniformLocation(m_program, "uTexel");
    }
    void FxaaPass::SetParams(const PassContext& ctx)
    {
        GL().Uniform2f(m_locTexel,
                       1.0f / static_cast<float>(ctx.width),
                       1.0f / static_cast<float>(ctx.height));
    }

    // ===== Sharpen (CAS-style unsharp) =====================================
    const char* SharpenPass::Fragment() const
    {
        return R"(
#version 120
uniform sampler2D uScene;
uniform vec2 uTexel;
uniform float uStrength;
varying vec2 vUV;
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    vec3 n = texture2D(uScene, vUV + vec2(0.0, -1.0) * uTexel).rgb
           + texture2D(uScene, vUV + vec2(0.0,  1.0) * uTexel).rgb
           + texture2D(uScene, vUV + vec2(-1.0, 0.0) * uTexel).rgb
           + texture2D(uScene, vUV + vec2( 1.0, 0.0) * uTexel).rgb;
    vec3 sharp = c * (1.0 + 4.0 * uStrength) - n * uStrength;
    gl_FragColor = vec4(clamp(sharp, 0.0, 1.0), 1.0);
}
)";
    }
    void SharpenPass::OnProgramLinked()
    {
        const GLProcs& gl = GL();
        m_locTexel    = gl.GetUniformLocation(m_program, "uTexel");
        m_locStrength = gl.GetUniformLocation(m_program, "uStrength");
    }
    void SharpenPass::SetParams(const PassContext& ctx)
    {
        const GLProcs& gl = GL();
        gl.Uniform2f(m_locTexel,
                     1.0f / static_cast<float>(ctx.width),
                     1.0f / static_cast<float>(ctx.height));
        gl.Uniform1f(m_locStrength, m_strength);
    }

    // ===== Vignette ========================================================
    const char* VignettePass::Fragment() const
    {
        return R"(
#version 120
uniform sampler2D uScene;
uniform float uStrength;
uniform float uRadius;
varying vec2 vUV;
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    vec2 d = vUV - 0.5;
    float dist = length(d) * 1.41421356;   // 0 center .. ~1 corner
    float v = smoothstep(uRadius, 1.0, dist);
    c *= (1.0 - v * uStrength);
    gl_FragColor = vec4(c, 1.0);
}
)";
    }
    void VignettePass::OnProgramLinked()
    {
        const GLProcs& gl = GL();
        m_locStrength = gl.GetUniformLocation(m_program, "uStrength");
        m_locRadius   = gl.GetUniformLocation(m_program, "uRadius");
    }
    void VignettePass::SetParams(const PassContext&)
    {
        const GLProcs& gl = GL();
        gl.Uniform1f(m_locStrength, m_strength);
        gl.Uniform1f(m_locRadius, m_radius);
    }

    // ===== Film grain / dither =============================================
    const char* FilmGrainPass::Fragment() const
    {
        return R"(
#version 120
uniform sampler2D uScene;
uniform float uStrength;
uniform float uTime;
varying vec2 vUV;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    float n = hash(vUV + fract(uTime)) - 0.5;   // animated, zero-mean noise
    gl_FragColor = vec4(c + n * uStrength, 1.0);
}
)";
    }
    void FilmGrainPass::OnProgramLinked()
    {
        const GLProcs& gl = GL();
        m_locStrength = gl.GetUniformLocation(m_program, "uStrength");
        m_locTime     = gl.GetUniformLocation(m_program, "uTime");
    }
    void FilmGrainPass::SetParams(const PassContext& ctx)
    {
        // Advance time so the grain animates. Use a fallback step if the frame
        // delta is unknown (0) so the noise still moves.
        m_time += (ctx.deltaSeconds > 0.0f) ? ctx.deltaSeconds : 0.016f;
        if (m_time > 1000.0f) m_time -= 1000.0f;   // keep magnitude bounded

        const GLProcs& gl = GL();
        gl.Uniform1f(m_locStrength, m_strength);
        gl.Uniform1f(m_locTime, m_time);
    }
}
