// GodRaysPass.cpp — depth-marched volumetric light shafts. See header for intent.

#include "stdafx.h"
#include "GodRaysPass.h"
#include "PostProcessGL.h"

#include <windows.h>
#include <algorithm>

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

        // --- Stage 1: directional depth-march -> per-pixel LIT FRACTION --------
        // For each pixel we march toward the sun and, at each step, reconstruct the
        // sample's WORLD position. A sample shadows the pixel only if it is a real
        // raised object (rises > uThreshold above the pixel) AND tall enough to
        // reach it at the sun's elevation: a column of height h casts a shadow
        // h/uSlope long, so it covers the pixel when h >= uSlope * horizontalDist.
        // This makes shadow length proportional to object height (short objects ->
        // short shadows = a high "top-angle" sun, not a horizon smear) and matches
        // the engine's own shear (≈ 0.5*height, i.e. slope ≈ 2). World-space test,
        // so it is stable under camera motion and needs no global ground estimate.
        const char* kMarchFS = R"(
#version 120
uniform sampler2D uDepth;
uniform vec4  uSunWorld;     // sun WORLD direction (xyz); projected to screen via uInvView
uniform float uThreshold;   // occluder min height (world units above the pixel)
uniform mat4  uInvView;
uniform vec4  uProj;        // near, far, tanHalfFovX, tanHalfFovY
uniform float uHasInv;      // 1 if depth reconstruction is usable
uniform float uDensity;     // march reach toward the sun (screen units) = max shadow length
uniform float uDecay;       // along-march weight falloff
uniform int   uSamples;     // march steps
uniform vec2  uSunDir;      // auto sun screen direction (matched to character shadows)
uniform float uHasSunDir;   // 1 if uSunDir is valid
uniform float uSlope;       // sun elevation: shadow SCREEN-length per world-height unit (smaller = steeper/shorter)
varying vec2 vUV;

vec3 worldPosAt(vec2 uv)
{
    float dd = texture2D(uDepth, uv).r;
    float n = uProj.x, f = uProj.y;
    float ndc = dd * 2.0 - 1.0;
    float linZ = (2.0 * n * f) / (f + n - ndc * (f - n));
    vec3 vp;
    vp.x = (uv.x * 2.0 - 1.0) * uProj.z * linZ;
    vp.y = (uv.y * 2.0 - 1.0) * uProj.w * linZ;
    vp.z = -linZ;
    return (uInvView * vec4(vp, 1.0)).xyz;
}

const int kMaxSamples = 128;
void main()
{
    if (uHasInv < 0.5) { gl_FragColor = vec4(1.0); return; }   // no recon -> fully lit

    float dP = texture2D(uDepth, vUV).r;
    if (dP >= 0.9999) { gl_FragColor = vec4(1.0); return; }     // sky -> fully lit
    vec3 wpP = worldPosAt(vUV);

    int n = uSamples;
    if (n < 1) n = 1;
    if (n > kMaxSamples) n = kMaxSamples;

    // March toward the sun: project the shared world sun's HORIZONTAL direction to
    // screen via the inverse-view basis (uInvView columns are the camera axes in
    // world). Shadows fall opposite — the SAME world direction the character-shadow
    // shear uses, so the two line up by construction. Z (elevation) doesn't change
    // the on-screen direction, only the character-shadow length.
    vec3 sh = vec3(uSunWorld.x, uSunWorld.y, 0.0);
    vec2 viewSun = vec2(dot(uInvView[0].xyz, sh), dot(uInvView[1].xyz, sh));
    vec2 dir = (length(viewSun) > 1e-4) ? normalize(viewSun) : vec2(0.0, 1.0);
    float Ld = length(dir);
    vec2 stepv = (Ld > 1e-4 ? dir / Ld : vec2(0.0, 1.0)) * (max(uDensity, 0.05) * 0.4 / float(n));

    float lit = 0.0, total = 0.0, w = 1.0;
    vec2 tc = vUV;
    for (int i = 0; i < kMaxSamples; ++i)
    {
        if (i >= n) break;
        tc += stepv;
        float ddi   = texture2D(uDepth, tc).r;
        float isSky = step(0.9999, ddi);                  // sky never occludes
        float hAbove = worldPosAt(tc).z - wpP.z;          // sample height above the pixel (world up = Z)
        // Shadowed if a real object (rises > uThreshold above the pixel) lies
        // toward the sun within the march reach. Decay fades the shadow with
        // distance; Shadow Len (uDensity) sets how far the march reaches.
        float occ = (1.0 - isSky) * step(uThreshold, hAbove);
        lit   += (1.0 - occ) * w;
        total += w;
        w     *= uDecay;
    }
    float frac = (total > 0.0) ? lit / total : 1.0;

    // Fade shadows toward the screen border so occluders scrolling in/out at the
    // edges don't pop as you walk (no off-screen depth to sample).
    float edge = min(min(vUV.x, 1.0 - vUV.x), min(vUV.y, 1.0 - vUV.y));
    frac = mix(1.0, frac, smoothstep(0.0, 0.07, edge));

    gl_FragColor = vec4(vec3(frac), 1.0);
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

        // --- Stage 3: composite -> scene with shadow-rays + lit beams ----------
        const char* kCompFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler2D uRays;     // lit-fraction mask (grayscale): 1 = lit, 0 = shadowed
uniform vec4 uColor;         // rgb = tint, a = intensity (beam brightening)
uniform float uShadowDark;   // shadow-ray darkening (0..1) — contrast without washing
varying vec2 vUV;
void main()
{
    vec3 s = texture2D(uScene, vUV).rgb;
    float shaft  = texture2D(uRays, vUV).r;
    float shadow = 1.0 - shaft;
    // Shadows darken the scene (multiplicative) AND lit areas brighten (additive),
    // so shadow-rays read at low intensity instead of needing a full-frame wash.
    vec3 outc = s * (1.0 - uShadowDark * shadow) + uColor.rgb * (uColor.a * shaft);
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
            m_occLocDepth   = GL().GetUniformLocation(m_occProg, "uDepth");
            m_occLocSunWorld = GL().GetUniformLocation(m_occProg, "uSunWorld");
            m_occLocThresh  = GL().GetUniformLocation(m_occProg, "uThreshold");
            m_occLocInvView = GL().GetUniformLocation(m_occProg, "uInvView");
            m_occLocProj    = GL().GetUniformLocation(m_occProg, "uProj");
            m_occLocHasInv  = GL().GetUniformLocation(m_occProg, "uHasInv");
            m_occLocDensity = GL().GetUniformLocation(m_occProg, "uDensity");
            m_occLocDecay   = GL().GetUniformLocation(m_occProg, "uDecay");
            m_occLocSamples = GL().GetUniformLocation(m_occProg, "uSamples");
            m_occLocSunDir    = GL().GetUniformLocation(m_occProg, "uSunDir");
            m_occLocHasSunDir = GL().GetUniformLocation(m_occProg, "uHasSunDir");
            m_occLocSlope     = GL().GetUniformLocation(m_occProg, "uSlope");
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
        gl.Uniform4f(m_occLocSunWorld, m_lightX, m_lightY, m_sunZ, 0.0f);
        // m_threshold is the occluder height (world units). Auto-migrate configs
        // saved while it was the old 0..1 cutoff: anything <= 1.5 -> default 100.
        const float occH = (m_threshold <= 1.5f) ? 100.0f : m_threshold;
        gl.Uniform1f(m_occLocThresh, occH);
        gl.UniformMatrix4fv(m_occLocInvView, 1, GL_FALSE, ctx.invView);
        gl.Uniform4f(m_occLocProj, ctx.nearZ, ctx.farZ, ctx.tanHalfFovX, ctx.tanHalfFovY);
        gl.Uniform1f(m_occLocHasInv, (ctx.hasInvView && ctx.nearZ > 0.0f) ? 1.0f : 0.0f);
        gl.Uniform1f(m_occLocDensity, m_density);
        gl.Uniform1f(m_occLocDecay, m_decay);
        gl.Uniform1i(m_occLocSamples, m_samples);
        gl.Uniform2f(m_occLocSunDir, ctx.sunDirX, ctx.sunDirY);
        gl.Uniform1f(m_occLocHasSunDir, ctx.sunDirValid ? 1.0f : 0.0f);
        // 'Sun Height' (m_lightY, 0..1) drives the sun ELEVATION: higher = steeper
        // sun = shorter shadows. Maps to shadow SCREEN-length per world-height unit:
        // Y=0 -> 0.0020 (long/horizon), Y=1 -> 0.0002 (short/steep). Tuned for MU's scale.
        gl.Uniform1f(m_occLocSlope, 0.0020f - 0.0018f * m_lightY);
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
        gl.Uniform1f(m_compLocShadowDark, m_weight);   // 'Weight' repurposed as shadow darkness
        DrawFullscreenQuad();

        // Leave texture units tidy for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
