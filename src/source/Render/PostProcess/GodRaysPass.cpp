// GodRaysPass.cpp — depth-marched volumetric light shafts. See header for intent.

#include "stdafx.h"
#include "GodRaysPass.h"
#include "PostProcessGL.h"
#include "Render/Shadow/SunShadow.h"
#include "Render/SunDirection.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

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

        // --- Stage 1: VOLUMETRIC in-scatter via the SUN SHADOW MAP -------------
        // For each pixel we reconstruct its world position (ray end) and march the
        // VIEW RAY from the camera toward it in world space. At each step we project
        // the sample into the sun shadow map and test sun visibility (lit vs in a
        // caster's shadow volume). The average visibility along the ray is the
        // in-scattered light: looking through a sunlit gap accumulates bright fog
        // (a shaft); looking through a building's shadow stays dark. Because it
        // samples the SAME depth map the world shadows use, the shafts line up with
        // the real shadows and respect OFF-screen occluders (unlike a screen-space
        // march). A forward-scatter phase makes shafts read brightest toward the sun.
        const char* kMarchFS = R"(
#version 120
uniform sampler2D uDepth;
uniform sampler2D uShadowMap;  // sun depth map
uniform mat4  uShadowMat;      // world -> light clip [0,1]
uniform mat4  uInvView;        // view -> world
uniform vec4  uProj;           // near, far, tanHalfFovX, tanHalfFovY
uniform float uHasInv;         // 1 if depth reconstruction is usable
uniform float uHasShadow;      // 1 if the sun shadow map is valid
uniform int   uSamples;        // march steps
uniform float uMarchLen;       // max march distance (world units) = shaft reach
uniform float uShadowBias;     // light-clip-space depth bias
uniform vec3  uSunDirW;        // world direction TOWARD the sun (normalized)
uniform float uPhase;          // forward-scatter strength (0 = none .. 1 = full)
varying vec2 vUV;

vec3 worldPosAt(vec2 uv, float dd)
{
    float n = uProj.x, f = uProj.y;
    float ndc = dd * 2.0 - 1.0;
    float linZ = (2.0 * n * f) / (f + n - ndc * (f - n));
    vec3 vp;
    vp.x = (uv.x * 2.0 - 1.0) * uProj.z * linZ;
    vp.y = (uv.y * 2.0 - 1.0) * uProj.w * linZ;
    vp.z = -linZ;
    return (uInvView * vec4(vp, 1.0)).xyz;
}

// 1 = this world point sees the sun, 0 = inside a caster's shadow. Outside the
// shadow-map footprint there is no occluder data -> treat as lit (no false shaft).
float sunVis(vec3 wp)
{
    vec4 sc = uShadowMat * vec4(wp, 1.0);   // ortho -> w == 1
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;
    float d = texture2D(uShadowMap, sc.xy).r;
    return (sc.z - uShadowBias > d) ? 0.0 : 1.0;
}

float hash(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

const int kMaxSamples = 128;
void main()
{
    if (uHasInv < 0.5 || uHasShadow < 0.5) { gl_FragColor = vec4(0.0); return; }

    float dP = texture2D(uDepth, vUV).r;
    vec3 camPos = (uInvView * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 wpEnd  = worldPosAt(vUV, dP);
    vec3 ray    = wpEnd - camPos;
    float rayLen = length(ray);
    vec3  rdir  = (rayLen > 1e-4) ? ray / rayLen : vec3(0.0, 0.0, 1.0);
    // Cap the march: beyond the shadow-map footprint every sample reads lit, so
    // marching farther only adds flat haze. Sky pixels get the full cap.
    float marchLen = min(rayLen, uMarchLen);

    int n = uSamples; if (n < 1) n = 1; if (n > kMaxSamples) n = kMaxSamples;
    float stepLen = marchLen / float(n);
    float j = hash(vUV);   // per-pixel dither to break stepping bands

    float inscatter = 0.0;
    for (int i = 0; i < kMaxSamples; ++i)
    {
        if (i >= n) break;
        vec3 sp = camPos + rdir * (stepLen * (float(i) + j));
        inscatter += sunVis(sp);
    }
    // Two outputs from the march:
    //  .r = additive in-scatter: LIT DISTANCE along the ray (sum of lit steps *
    //       step length) * thickness, NOT averaged — a long sunlit ray is bright,
    //       a short/shadowed one dim. This is the bright shaft (beam) term.
    //  .g = shadow fraction: the AVERAGE occlusion along the ray (distance-
    //       independent), used by the composite to DARKEN shadowed bands. The two
    //       together read as crisp crepuscular rays (bright beams + dark bands).
    float litSteps   = inscatter;                       // sum of sunVis over n steps
    float shadowFrac = 1.0 - litSteps / float(n);       // 0 (lit) .. 1 (fully shadowed)

    float shaft = clamp(litSteps * stepLen * 0.0007, 0.0, 1.5);
    // Forward-scatter phase, FLOORED so shafts persist looking away from the sun.
    float cosA  = dot(rdir, uSunDirW);
    shaft *= mix(0.5, 1.0, 0.5 + 0.5 * cosA);

    gl_FragColor = vec4(shaft, shadowFrac, 0.0, 1.0);
}
)";

        // --- Stage 2: 3x3 box blur (soften the half-res mask) -----------------
        const char* kBlurFS = R"(
#version 120
uniform sampler2D uTex;
uniform vec2 uTexel;
varying vec2 vUV;
void main()
{
    vec3 s = texture2D(uTex, vUV).rgb * 4.0;
    s += texture2D(uTex, vUV + vec2( uTexel.x, 0.0)).rgb * 2.0;
    s += texture2D(uTex, vUV + vec2(-uTexel.x, 0.0)).rgb * 2.0;
    s += texture2D(uTex, vUV + vec2(0.0,  uTexel.y)).rgb * 2.0;
    s += texture2D(uTex, vUV + vec2(0.0, -uTexel.y)).rgb * 2.0;
    s += texture2D(uTex, vUV + vec2( uTexel.x,  uTexel.y)).rgb;
    s += texture2D(uTex, vUV + vec2(-uTexel.x, -uTexel.y)).rgb;
    s += texture2D(uTex, vUV + vec2( uTexel.x, -uTexel.y)).rgb;
    s += texture2D(uTex, vUV + vec2(-uTexel.x,  uTexel.y)).rgb;
    gl_FragColor = vec4(s / 16.0, 1.0);
}
)";

        // --- Stage 3: ADDITIVE composite -> scene + in-scattered shafts --------
        const char* kCompFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler2D uRays;     // .r = in-scatter (bright shafts), .g = shadow fraction
uniform vec4 uColor;         // rgb = ray tint, a = intensity
uniform float uShadowDark;   // shadow-band darkening amount (0..5)
varying vec2 vUV;
void main()
{
    vec3 s = texture2D(uScene, vUV).rgb;
    vec2 g = texture2D(uRays, vUV).rg;
    float shaft  = g.r;
    float shadow = g.g;
    // Crepuscular composite: DARKEN shadowed bands (multiplicative) AND ADD
    // sun-tinted in-scattered light (additive). The contrast between the two is
    // what makes the beams read; the 2.5 gain keeps Intensity in a usable range.
    vec3 outc = s * (1.0 - clamp(uShadowDark * shadow, 0.0, 1.0))
                  + uColor.rgb * (uColor.a * shaft * 2.5);
    gl_FragColor = vec4(outc, 1.0);
}
)";
    }

    void GodRaysPass::Destroy()
    {
        const GLProcs& gl = GL();
        if (m_maskFBO) { gl.DeleteFramebuffers(1, &m_maskFBO); m_maskFBO = 0; }
        if (m_blurFBO) { gl.DeleteFramebuffers(1, &m_blurFBO); m_blurFBO = 0; }
        if (m_maskTex) { glDeleteTextures(1, &m_maskTex);      m_maskTex = 0; }
        if (m_blurTex) { glDeleteTextures(1, &m_blurTex);      m_blurTex = 0; }
    }

    bool GodRaysPass::Create(int halfW, int halfH)
    {
        const GLProcs& gl = GL();
        Destroy();

        m_maskTex = CreateColorTexture(halfW, halfH);
        gl.GenFramebuffers(1, &m_maskFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_maskFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_maskTex, 0);
        bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        m_blurTex = CreateColorTexture(halfW, halfH);
        gl.GenFramebuffers(1, &m_blurFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex, 0);
        ok = ok && (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok)
        {
            OutputDebugStringA("[GodRays] scratch FBO incomplete\n");
            Destroy();
            return false;
        }
        m_halfW = halfW;
        m_halfH = halfH;
        return true;
    }

    bool GodRaysPass::EnsureResources(int width, int height)
    {
        const int halfW = std::max(1, width / 2);
        const int halfH = std::max(1, height / 2);

        if (!m_occProg)
        {
            m_occProg = CompileProgram(kVS, kMarchFS);
            if (!m_occProg) return false;
            m_occLocDepth      = GL().GetUniformLocation(m_occProg, "uDepth");
            m_occLocShadowMap  = GL().GetUniformLocation(m_occProg, "uShadowMap");
            m_occLocShadowMat  = GL().GetUniformLocation(m_occProg, "uShadowMat");
            m_occLocInvView    = GL().GetUniformLocation(m_occProg, "uInvView");
            m_occLocProj       = GL().GetUniformLocation(m_occProg, "uProj");
            m_occLocHasInv     = GL().GetUniformLocation(m_occProg, "uHasInv");
            m_occLocHasShadow  = GL().GetUniformLocation(m_occProg, "uHasShadow");
            m_occLocSamples    = GL().GetUniformLocation(m_occProg, "uSamples");
            m_occLocMarchLen   = GL().GetUniformLocation(m_occProg, "uMarchLen");
            m_occLocShadowBias = GL().GetUniformLocation(m_occProg, "uShadowBias");
            m_occLocSunDirW    = GL().GetUniformLocation(m_occProg, "uSunDirW");
            m_occLocPhase      = GL().GetUniformLocation(m_occProg, "uPhase");
        }
        if (!m_blurProg)
        {
            m_blurProg = CompileProgram(kVS, kBlurFS);
            if (!m_blurProg) return false;
            m_blurLocTex   = GL().GetUniformLocation(m_blurProg, "uTex");
            m_blurLocTexel = GL().GetUniformLocation(m_blurProg, "uTexel");
        }
        if (!m_compProg)
        {
            m_compProg = CompileProgram(kVS, kCompFS);
            if (!m_compProg) return false;
            m_compLocScene = GL().GetUniformLocation(m_compProg, "uScene");
            m_compLocRays  = GL().GetUniformLocation(m_compProg, "uRays");
            m_compLocColor = GL().GetUniformLocation(m_compProg, "uColor");
            m_compLocShadowDark = GL().GetUniformLocation(m_compProg, "uShadowDark");
        }

        if (m_maskFBO == 0 || halfW != m_halfW || halfH != m_halfH)
        {
            if (!Create(halfW, halfH))
                return false;
        }
        return true;
    }

    void GodRaysPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        // --- 1) Directional depth-march -> light-fraction mask (half-res) -----
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_maskFBO);
        glViewport(0, 0, m_halfW, m_halfH);
        gl.UseProgram(m_occProg);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceDepthTex);
        gl.Uniform1i(m_occLocDepth, 0);
        // Sun shadow map on unit 1 — the SAME depth map the world shadows sample,
        // so the shafts line up with the real shadows and see off-screen occluders.
        const bool shadowReady = SunShadow::MapReady();
        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowReady ? (GLuint)SunShadow::DepthTexture() : 0u);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.Uniform1i(m_occLocShadowMap, 1);
        if (m_occLocShadowMat >= 0)
            gl.UniformMatrix4fv(m_occLocShadowMat, 1, GL_FALSE, SunShadow::Matrix());
        gl.UniformMatrix4fv(m_occLocInvView, 1, GL_FALSE, ctx.invView);
        gl.Uniform4f(m_occLocProj, ctx.nearZ, ctx.farZ, ctx.tanHalfFovX, ctx.tanHalfFovY);
        gl.Uniform1f(m_occLocHasInv, (ctx.hasInvView && ctx.nearZ > 0.0f) ? 1.0f : 0.0f);
        gl.Uniform1f(m_occLocHasShadow, shadowReady ? 1.0f : 0.0f);
        gl.Uniform1i(m_occLocSamples, m_samples);
        // Density -> shaft reach: how far (world units) the view ray is marched.
        gl.Uniform1f(m_occLocMarchLen, std::max(m_density, 0.05f) * 5000.0f);
        gl.Uniform1f(m_occLocShadowBias, 0.0008f);
        // World direction toward the sun (the shared sun that built the shadow map).
        float sd[3] = { BloodlustMU::g_SunDirection.x,
                        BloodlustMU::g_SunDirection.y,
                        BloodlustMU::g_SunDirection.z };
        const float sl = std::sqrt(sd[0]*sd[0] + sd[1]*sd[1] + sd[2]*sd[2]);
        if (sl > 1e-4f) { sd[0]/=sl; sd[1]/=sl; sd[2]/=sl; }
        gl.Uniform3fv(m_occLocSunDirW, 1, sd);
        DrawFullscreenQuad();

        // --- 2) Soften the mask (half-res box blur) ---------------------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        glViewport(0, 0, m_halfW, m_halfH);
        gl.UseProgram(m_blurProg);
        glBindTexture(GL_TEXTURE_2D, m_maskTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocTexel, 1.0f / static_cast<float>(m_halfW),
                                     1.0f / static_cast<float>(m_halfH));
        DrawFullscreenQuad();

        // --- 3) Composite: scene + shafts -> destination (full res) -----------
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);
        gl.UseProgram(m_compProg);

        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_compLocScene, 0);

        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_blurTex);
        gl.Uniform1i(m_compLocRays, 1);

        gl.Uniform4f(m_compLocColor, m_r, m_g, m_b, m_intensity);
        gl.Uniform1f(m_compLocShadowDark, m_weight);   // 'Shadow Dark' slider (0..5)
        DrawFullscreenQuad();

        // Leave texture units tidy for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
