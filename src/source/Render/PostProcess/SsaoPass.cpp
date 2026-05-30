// SsaoPass.cpp — depth-based SSAO. See header for the 3-stage shape.

#include "stdafx.h"
#include "SsaoPass.h"
#include "PostProcessGL.h"

#include <windows.h>
#include <algorithm>

namespace PostProcess
{
    namespace
    {
        // Shared fullscreen VS (GLSL 120, fixed-function attributes — same as
        // every other pass). Emits gl_MultiTexCoord0 as the sample UV.
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        // AO compute. Reconstructs view-space position from the depth buffer and
        // a per-pixel normal from screen-space derivatives, then samples a
        // hash-rotated 2D disk; each neighbour's view position forms a vector
        // from the surface — occlusion is the normal-aligned, range-attenuated
        // sum. Output is the AO factor in all channels (1 = lit, 0 = occluded).
        const char* kAoFS = R"(
#version 120
uniform sampler2D uDepth;
uniform vec2  uTexel;
uniform float uNear;
uniform float uFar;
uniform float uTanX;
uniform float uTanY;
uniform float uRadius;     // world units
uniform float uStrength;
uniform float uPower;
uniform float uTime;
varying vec2 vUV;

float linDepth(float d)
{
    float z = d * 2.0 - 1.0;                       // window -> NDC
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

vec3 viewPos(vec2 uv)
{
    float lz = linDepth(texture2D(uDepth, uv).r);  // positive distance
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * uTanX * lz, ndc.y * uTanY * lz, -lz);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

void main()
{
    // Disabled / no projection -> pass-through (fully lit).
    if (uNear <= 0.0 || uFar <= 0.0) { gl_FragColor = vec4(1.0); return; }

    float dC = texture2D(uDepth, vUV).r;
    if (dC >= 0.99999) { gl_FragColor = vec4(1.0); return; }  // sky / no geometry

    vec3 P = viewPos(vUV);
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));

    // Per-pixel rotation must be TEMPORALLY STABLE: depend only on pixel
    // position, never on time. Animating it (the old `+ fract(uTime)`) made the
    // AO noise change every frame, which reads as crawling "film grain" in dark
    // areas. Static rotation + the blur pass yields stable, clean AO.
    float ang = hash(vUV) * 6.2831853;
    float ca = cos(ang), sa = sin(ang);

    // 12 fixed disk directions over two rings; rotated per-pixel by 'ang'.
    const int N_DIR = 12;
    vec2 dirs[12];
    dirs[0]  = vec2( 1.0,  0.0); dirs[1]  = vec2( 0.5,  0.866);
    dirs[2]  = vec2(-0.5,  0.866); dirs[3] = vec2(-1.0,  0.0);
    dirs[4]  = vec2(-0.5, -0.866); dirs[5] = vec2( 0.5, -0.866);
    dirs[6]  = vec2( 0.707, 0.707); dirs[7] = vec2(-0.707, 0.707);
    dirs[8]  = vec2(-0.707,-0.707); dirs[9] = vec2( 0.707,-0.707);
    dirs[10] = vec2( 0.0,  1.0); dirs[11] = vec2( 0.0, -1.0);

    // World radius -> screen UV radius at this depth (clamped so near pixels
    // don't sample the whole screen). P.z is negative; use its magnitude.
    float linZ = -P.z;
    float radiusUV = clamp(uRadius / max(linZ, 1.0), 0.002, 0.12);

    float occ = 0.0;
    for (int i = 0; i < N_DIR; ++i)
    {
        vec2 d = dirs[i];
        vec2 r = vec2(d.x * ca - d.y * sa, d.x * sa + d.y * ca);
        // two taps per direction (inner + outer ring) for a fuller hemisphere
        for (int k = 1; k <= 2; ++k)
        {
            vec2 uv = vUV + r * radiusUV * (float(k) / 2.0);
            vec3 S = viewPos(uv);
            vec3 diff = S - P;
            float dist = length(diff);
            if (dist < 1e-4) continue;
            float nd = max(dot(N, diff / dist), 0.0);
            // Range check: ignore occluders farther than the world radius so
            // background geometry doesn't halo foreground edges.
            float range = 1.0 - smoothstep(uRadius, uRadius * 2.0, dist);
            occ += nd * range;
        }
    }
    occ /= float(N_DIR * 2);

    float ao = 1.0 - clamp(occ * uStrength, 0.0, 1.0);
    ao = pow(ao, uPower);
    gl_FragColor = vec4(vec3(ao), 1.0);
}
)";

        // Separable box-ish blur to smooth the rotation noise. uStep selects
        // the axis. 5 taps is enough for the per-pixel hash noise.
        const char* kBlurFS = R"(
#version 120
uniform sampler2D uTex;
uniform vec2 uStep;
varying vec2 vUV;
void main()
{
    float s = texture2D(uTex, vUV).r * 0.4;
    s += texture2D(uTex, vUV + uStep).r * 0.24;
    s += texture2D(uTex, vUV - uStep).r * 0.24;
    s += texture2D(uTex, vUV + uStep * 2.0).r * 0.06;
    s += texture2D(uTex, vUV - uStep * 2.0).r * 0.06;
    gl_FragColor = vec4(vec3(s), 1.0);
}
)";

        // Composite: darken the scene by the (blurred) AO factor.
        const char* kCompFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler2D uAO;
varying vec2 vUV;
void main()
{
    vec3 c = texture2D(uScene, vUV).rgb;
    float ao = texture2D(uAO, vUV).r;
    gl_FragColor = vec4(c * ao, 1.0);
}
)";
    }

    void SsaoPass::Destroy()
    {
        const GLProcs& gl = GL();
        if (m_aoFBO)   { gl.DeleteFramebuffers(1, &m_aoFBO);   m_aoFBO = 0; }
        if (m_blurFBO) { gl.DeleteFramebuffers(1, &m_blurFBO); m_blurFBO = 0; }
        if (m_aoTex)   { glDeleteTextures(1, &m_aoTex);   m_aoTex = 0; }
        if (m_blurTex) { glDeleteTextures(1, &m_blurTex); m_blurTex = 0; }
    }

    bool SsaoPass::Create(int w, int h)
    {
        const GLProcs& gl = GL();
        Destroy();

        m_aoTex = CreateColorTexture(w, h);
        gl.GenFramebuffers(1, &m_aoFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_aoFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_aoTex, 0);
        bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

        m_blurTex = CreateColorTexture(w, h);
        gl.GenFramebuffers(1, &m_blurFBO);
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_blurTex, 0);
        ok = ok && (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!ok)
        {
            OutputDebugStringA("[SSAO] scratch FBO incomplete\n");
            Destroy();
            return false;
        }
        m_w = w;
        m_h = h;
        return true;
    }

    bool SsaoPass::EnsureResources(int width, int height)
    {
        if (!m_aoProg)
        {
            m_aoProg = CompileProgram(kVS, kAoFS);
            if (!m_aoProg) return false;
            const GLProcs& gl = GL();
            m_aoLocDepth    = gl.GetUniformLocation(m_aoProg, "uDepth");
            m_aoLocTexel    = gl.GetUniformLocation(m_aoProg, "uTexel");
            m_aoLocNear     = gl.GetUniformLocation(m_aoProg, "uNear");
            m_aoLocFar      = gl.GetUniformLocation(m_aoProg, "uFar");
            m_aoLocTanX     = gl.GetUniformLocation(m_aoProg, "uTanX");
            m_aoLocTanY     = gl.GetUniformLocation(m_aoProg, "uTanY");
            m_aoLocRadius   = gl.GetUniformLocation(m_aoProg, "uRadius");
            m_aoLocStrength = gl.GetUniformLocation(m_aoProg, "uStrength");
            m_aoLocPower    = gl.GetUniformLocation(m_aoProg, "uPower");
            m_aoLocTime     = gl.GetUniformLocation(m_aoProg, "uTime");
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
            m_compLocScene = GL().GetUniformLocation(m_compProg, "uScene");
            m_compLocAO    = GL().GetUniformLocation(m_compProg, "uAO");
        }

        if (m_aoFBO == 0 || width != m_w || height != m_h)
        {
            if (!Create(width, height))
                return false;
        }
        return true;
    }

    void SsaoPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        m_time += (ctx.deltaSeconds > 0.0f) ? ctx.deltaSeconds : 0.016f;
        if (m_time > 1000.0f) m_time -= 1000.0f;

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        const float texelX = 1.0f / static_cast<float>(ctx.width);
        const float texelY = 1.0f / static_cast<float>(ctx.height);

        // --- 1) AO compute: depth -> aoFBO --------------------------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, m_aoFBO);
        glViewport(0, 0, m_w, m_h);
        gl.UseProgram(m_aoProg);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceDepthTex);
        gl.Uniform1i(m_aoLocDepth, 0);
        gl.Uniform2f(m_aoLocTexel, texelX, texelY);
        gl.Uniform1f(m_aoLocNear, ctx.nearZ);
        gl.Uniform1f(m_aoLocFar, ctx.farZ);
        gl.Uniform1f(m_aoLocTanX, ctx.tanHalfFovX);
        gl.Uniform1f(m_aoLocTanY, ctx.tanHalfFovY);
        gl.Uniform1f(m_aoLocRadius, m_radius);
        gl.Uniform1f(m_aoLocStrength, m_strength);
        gl.Uniform1f(m_aoLocPower, m_power);
        gl.Uniform1f(m_aoLocTime, m_time);
        DrawFullscreenQuad();

        // --- 2) Blur: aoTex -> blurTex (H) -> aoTex (V) --------------------
        gl.UseProgram(m_blurProg);

        gl.BindFramebuffer(GL_FRAMEBUFFER, m_blurFBO);
        glViewport(0, 0, m_w, m_h);
        glBindTexture(GL_TEXTURE_2D, m_aoTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, texelX, 0.0f);
        DrawFullscreenQuad();

        gl.BindFramebuffer(GL_FRAMEBUFFER, m_aoFBO);
        glViewport(0, 0, m_w, m_h);
        glBindTexture(GL_TEXTURE_2D, m_blurTex);
        gl.Uniform1i(m_blurLocTex, 0);
        gl.Uniform2f(m_blurLocStep, 0.0f, texelY);
        DrawFullscreenQuad();

        // --- 3) Composite: scene * ao -> dest -----------------------------
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);
        gl.UseProgram(m_compProg);

        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_compLocScene, 0);

        gl.ActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_aoTex);
        gl.Uniform1i(m_compLocAO, 1);

        DrawFullscreenQuad();

        // Leave units tidy for the next pass / legacy draws.
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
