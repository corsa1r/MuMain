// FogPass.cpp — depth-based atmospheric fog. See header.

#include "stdafx.h"
#include "FogPass.h"
#include "PostProcessGL.h"

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
varying vec2 vUV;

// World units over which height fog ramps from none (at uHeightTop) to full
// (at uHeightTop - this). MU terrain Z spans ~hundreds, so 300 is a sane band.
const float kHeightBand = 300.0;

float linDepth(float d)
{
    float z = d * 2.0 - 1.0;                       // window -> NDC
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main()
{
    vec3 scene = texture2D(uScene, vUV).rgb;

    // No usable projection -> pass through unchanged.
    if (uNear <= 0.0 || uFar <= 0.0) { gl_FragColor = vec4(scene, 1.0); return; }

    float d = texture2D(uDepth, vUV).r;

    // Sky / no geometry -> full fog so the horizon reads as haze.
    if (d >= 0.99999) { gl_FragColor = vec4(mix(scene, uColor.rgb, uDensity), 1.0); return; }

    float linZ = linDepth(d);                 // positive view distance

    // Distance fog: exp2 falloff past a start distance (fraction of far plane).
    float startDist = uStart * uFar;
    float fogDist = max(linZ - startDist, 0.0);
    float k = (uDensity * 3.0) / max(uFar, 1.0);   // scale density to scene range
    float distFog = 1.0 - exp(-(fogDist * k) * (fogDist * k));

    // Height fog (optional): denser the further a pixel sits BELOW uHeightTop
    // in WORLD space. Reconstruct view-space position from depth, transform to
    // world via the inverse view matrix, and key off world Z (MU's up-axis).
    // This pools mist in low terrain regardless of camera angle — unlike the
    // old screen-space version that just washed the bottom of the frame.
    float heightFog = 0.0;
    if (uHeightStrength > 0.0)
    {
        vec3 viewPos = vec3((vUV.x * 2.0 - 1.0) * uTanX * linZ,
                            (vUV.y * 2.0 - 1.0) * uTanY * linZ,
                            -linZ);
        vec4 worldPos = uInvView * vec4(viewPos, 1.0);
        float below = clamp((uHeightTop - worldPos.z) / kHeightBand, 0.0, 1.0);
        heightFog = clamp(below * uHeightStrength, 0.0, 1.0);
    }

    float fog = clamp(distFog + heightFog * (1.0 - distFog), 0.0, 1.0);
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

        DrawFullscreenQuad();

        // Tidy units for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
