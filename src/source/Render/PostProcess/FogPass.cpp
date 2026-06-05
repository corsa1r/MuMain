// FogPass.cpp — depth-based atmospheric fog. See header.

#include "stdafx.h"
#include "FogPass.h"
#include "PostProcessGL.h"
#include "Render/Shadow/SunShadow.h"

namespace PostProcess
{
    namespace
    {
        // Fullscreen VS — GLSL 120, fixed-function attributes (matches every
        // other pass). Emits gl_MultiTexCoord0 as the sample UV.
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        // Distance + optional height fog. Reconstructs linear view depth (and,
        // for height fog, view-space Y) from the depth buffer using the camera
        // projection params, then mixes uColor in by a 0..1 fog factor.
        const char* kFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform float uNear;
uniform float uFar;
uniform float uTanX;
uniform float uTanY;
uniform vec4  uColor;          // rgb = fog color (a unused); vec4 so it pairs
                               // with Uniform4f (the proc table has no Uniform3f)
uniform float uDensity;        // overall thickness
uniform float uStart;          // 0..1 of far before fog begins
uniform float uHeightStrength; // 0 = height fog off
uniform float uHeightTop;      // WORLD Z below which height fog grows (MU up=Z)
uniform mat4  uInvView;        // inverse scene view matrix (view -> world)
// Sun shadow map: makes the fog LIGHT-AWARE — glow where the sun reaches, pool
// dark in shadow — so the haze reads as a lit volume the god-ray shafts sit in.
uniform sampler2D uShadowMap;
uniform mat4  uShadowMat;      // world -> light clip [0,1]
uniform float uHasShadow;      // 1 if the sun shadow map is valid
uniform float uSunMul;         // fog brightness multiplier in sunlight (>1)
uniform float uShadowMul;      // fog brightness multiplier in shadow (<1)
varying vec2 vUV;

// World units over which height fog ramps from none (at uHeightTop) to full
// (at uHeightTop - this). MU terrain Z spans ~hundreds, so 300 is a sane band.
const float kHeightBand = 300.0;

float linDepth(float d)
{
    float z = d * 2.0 - 1.0;                       // window -> NDC
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

// Soft sun visibility at a world point (1 lit, 0 shadow). A small 2x2 PCF keeps
// the fog's light/shadow transition gentle (hard shadow edges look wrong on haze).
float sunVisAt(vec3 wp)
{
    if (uHasShadow < 0.5) return 1.0;
    vec4 sc = uShadowMat * vec4(wp, 1.0);          // ortho -> w == 1
    if (sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0 || sc.z > 1.0) return 1.0;
    float bias = 0.0015;
    float t = 0.0015;   // PCF tap offset in light-space UV
    float s = 0.0;
    s += (sc.z - bias > texture2D(uShadowMap, sc.xy + vec2(-t,-t)).r) ? 0.0 : 1.0;
    s += (sc.z - bias > texture2D(uShadowMap, sc.xy + vec2( t,-t)).r) ? 0.0 : 1.0;
    s += (sc.z - bias > texture2D(uShadowMap, sc.xy + vec2(-t, t)).r) ? 0.0 : 1.0;
    s += (sc.z - bias > texture2D(uShadowMap, sc.xy + vec2( t, t)).r) ? 0.0 : 1.0;
    return s * 0.25;
}

void main()
{
    vec3 scene = texture2D(uScene, vUV).rgb;

    // No usable projection -> pass through unchanged.
    if (uNear <= 0.0 || uFar <= 0.0) { gl_FragColor = vec4(scene, 1.0); return; }

    float d = texture2D(uDepth, vUV).r;
    bool  isSky = (d >= 0.99999);

    float linZ = isSky ? uFar : linDepth(d);       // positive view distance

    // World position of this pixel (always — used for height fog AND sun sampling).
    vec3 viewPos = vec3((vUV.x * 2.0 - 1.0) * uTanX * linZ,
                        (vUV.y * 2.0 - 1.0) * uTanY * linZ,
                        -linZ);
    vec3 worldPos = (uInvView * vec4(viewPos, 1.0)).xyz;

    // Distance fog: exp2 falloff past a start distance (fraction of far plane).
    float startDist = uStart * uFar;
    float fogDist = max(linZ - startDist, 0.0);
    float k = (uDensity * 3.0) / max(uFar, 1.0);   // scale density to scene range
    float distFog = 1.0 - exp(-(fogDist * k) * (fogDist * k));

    // Height fog: EXPONENTIAL pooling — density grows the further a pixel sits
    // below uHeightTop in WORLD space (MU up = Z), denser near the ground for a
    // real ground-mist look. Camera-stable (keys off world Z, not screen Y).
    float heightFog = 0.0;
    if (uHeightStrength > 0.0)
    {
        float belowDist = max(uHeightTop - worldPos.z, 0.0);
        heightFog = clamp(uHeightStrength * (1.0 - exp(-belowDist / kHeightBand)), 0.0, 1.0);
    }

    float fog = isSky ? uDensity
                      : clamp(distFog + heightFog * (1.0 - distFog), 0.0, 1.0);

    // LIGHT-AWARE fog color: brighten the haze where the sun reaches this point,
    // darken it in shadow. The fog now feels like a sunlit volume — bright banks
    // in the light, dark pooled mist in the shade — and stays coherent with the
    // god-ray shafts (same sun shadow map). Sky uses full sun (open to the sky).
    // NOTE: per-pixel sun-reactivity (brighten fog in sun / darken in shadow) was
    // tried and REMOVED — sampling the shadow map at each surface re-projected the
    // ground shadows onto the fog, DOUBLING every shadow. The fog is the
    // atmospheric MEDIUM (height + distance); the GOD RAYS provide the sunlit
    // in-scatter volumetrically (the real light-aware part) with no double shadows.
    gl_FragColor = vec4(mix(scene, uColor.rgb, fog), 1.0);
}
)";
    }

    bool FogPass::EnsureResources(int /*w*/, int /*h*/)
    {
        if (m_program)
            return true;
        m_program = CompileProgram(kVS, kFS);
        if (!m_program)
            return false;
        const GLProcs& gl = GL();
        m_locScene          = gl.GetUniformLocation(m_program, "uScene");
        m_locDepth          = gl.GetUniformLocation(m_program, "uDepth");
        m_locNear           = gl.GetUniformLocation(m_program, "uNear");
        m_locFar            = gl.GetUniformLocation(m_program, "uFar");
        m_locTanX           = gl.GetUniformLocation(m_program, "uTanX");
        m_locTanY           = gl.GetUniformLocation(m_program, "uTanY");
        m_locColor          = gl.GetUniformLocation(m_program, "uColor");
        m_locDensity        = gl.GetUniformLocation(m_program, "uDensity");
        m_locStart          = gl.GetUniformLocation(m_program, "uStart");
        m_locHeightStrength = gl.GetUniformLocation(m_program, "uHeightStrength");
        m_locHeightTop      = gl.GetUniformLocation(m_program, "uHeightTop");
        m_locInvView        = gl.GetUniformLocation(m_program, "uInvView");
        m_locShadowMap      = gl.GetUniformLocation(m_program, "uShadowMap");
        m_locShadowMat      = gl.GetUniformLocation(m_program, "uShadowMat");
        m_locHasShadow      = gl.GetUniformLocation(m_program, "uHasShadow");
        m_locSunMul         = gl.GetUniformLocation(m_program, "uSunMul");
        m_locShadowMul      = gl.GetUniformLocation(m_program, "uShadowMul");
        return true;
    }

    void FogPass::Execute(const PassContext& ctx)
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

        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceDepthTex);
        gl.Uniform1i(m_locDepth, 1);

        gl.Uniform1f(m_locNear, ctx.nearZ);
        gl.Uniform1f(m_locFar, ctx.farZ);
        gl.Uniform1f(m_locTanX, ctx.tanHalfFovX);
        gl.Uniform1f(m_locTanY, ctx.tanHalfFovY);
        gl.Uniform4f(m_locColor, m_r, m_g, m_b, 1.0f);   // vec4 uColor (a unused)
        gl.Uniform1f(m_locDensity, m_density);
        gl.Uniform1f(m_locStart, m_start);
        gl.Uniform1f(m_locHeightStrength, m_heightStrength);
        gl.Uniform1f(m_locHeightTop, m_heightTop);
        // Inverse view (column-major, no transpose) for world-Z reconstruction.
        if (m_locInvView >= 0 && gl.UniformMatrix4fv)
            gl.UniformMatrix4fv(m_locInvView, 1, GL_FALSE, ctx.invView);

        // Sun shadow map on unit 2 (same depth map the world shadows + god rays
        // use) so the fog can glow in sunlight and pool dark in shadow.
        const bool shadowReady = SunShadow::MapReady();
        gl.ActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, shadowReady ? (GLuint)SunShadow::DepthTexture() : 0u);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.Uniform1i(m_locShadowMap, 2);
        if (m_locShadowMat >= 0 && gl.UniformMatrix4fv)
            gl.UniformMatrix4fv(m_locShadowMat, 1, GL_FALSE, SunShadow::Matrix());
        gl.Uniform1f(m_locHasShadow, shadowReady ? 1.0f : 0.0f);
        // Fog brightness in sun vs shadow. Hardcoded for M1 (tunable later): lit
        // haze brightens, shadowed haze darkens for a moody, sun-reactive volume.
        gl.Uniform1f(m_locSunMul, 1.6f);
        gl.Uniform1f(m_locShadowMul, 0.5f);

        DrawFullscreenQuad();

        // Tidy units for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
