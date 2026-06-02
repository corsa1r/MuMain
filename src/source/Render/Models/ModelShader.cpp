// ModelShader.cpp — see header for design intent.

#include "stdafx.h"
#include "ModelShader.h"

#include <gl/glew.h>

#include "Render/PostProcess/PostProcessGL.h"
#include "Render/SunDirection.h"
#include "Data/GameConfig/GameConfig.h"

namespace ModelLighting
{
    namespace
    {
        bool   s_inited       = false;   // program compiled (latched once GL ready)
        bool   s_programValid = false;   // CompileProgram succeeded
        bool   s_runtimeEnabled = false; // live on/off (config default, then per-map/editor)
        GLuint s_prog        = 0;        // model program
        GLuint s_terrainProg = 0;        // terrain additive-relief program

        // Cached uniform locations (model program).
        GLint u_diffuse        = -1;
        GLint u_normalTex      = -1;
        GLint u_bodyLight      = -1;
        GLint u_lightDir       = -1;
        GLint u_hasNormalMap   = -1;
        GLint u_normalStrength = -1;
        GLint u_specStrength   = -1;
        GLint u_specPower      = -1;
        GLint u_alpha          = -1;
        GLint u_sunColor       = -1;

        // Cached uniform locations (terrain program).
        GLint t_diffuse        = -1;
        GLint t_normalTex      = -1;
        GLint t_lightDir       = -1;
        GLint t_hasNormalMap   = -1;
        GLint t_normalStrength = -1;
        GLint t_specStrength   = -1;
        GLint t_specPower      = -1;
        GLint t_sunColor       = -1;

        // Config tunables, cached at Init().
        float s_normalStrength = 1.0f;
        float s_specStrength   = 0.30f;
        float s_specPower      = 24.0f;
        float s_sunColor[3]    = { 1.0f, 1.0f, 1.0f };

        // --- Dynamic point lights (fed from the engine's AddTerrainLight sources) ---
        enum { MAX_LIGHTS = 24, MAX_COLLECT = 96 };
        struct PointLight { float pos[3]; float color[3]; float radius; };

        bool  s_dynLights       = false;   // feature on/off (live)
        float s_dynIntensity    = 1.0f;    // global scale
        float s_dynFlicker      = 0.5f;    // 0 = steady (smoothed), 1 = raw flicker
        bool  s_playerLight     = true;    // always-on light following the hero
        float s_playerRadius    = 700.0f;  // world units

        PointLight s_collect[MAX_COLLECT];     // lights registered during the frame
        int        s_collectCount = 0;
        PointLight s_active[MAX_LIGHTS];        // selected (nearest) set used for drawing
        int        s_activeCount  = 0;
        PointLight s_prevActive[MAX_LIGHTS];    // last frame's (smoothed) set, for temporal smoothing
        int        s_prevActiveCount = 0;
        float      s_activeEye[MAX_LIGHTS][3];     // active positions in eye space (per-frame)
        float      s_activeColScaled[MAX_LIGHTS][3]; // color * intensity
        float      s_activeRadInv[MAX_LIGHTS];     // 1 / radius
        bool       s_eyeDirty     = true;       // recompute eye-space lights this frame

        // Light uniform locations: [0]=model program, [1]=terrain program.
        GLint u_numLights[2]   = { -1, -1 };
        GLint u_lightPos[2]    = { -1, -1 };   // vec3 array (eye space)
        GLint u_lightColor[2]  = { -1, -1 };   // vec3 array (already * intensity)
        GLint u_lightRadInv[2] = { -1, -1 };   // 1/radius array

        const char* kVertexSrc =
            "#version 120\n"
            "varying vec3 vNormalEye;\n"
            "varying vec3 vLightEye;\n"
            "varying vec3 vEyePos;\n"
            "varying vec2 vUv;\n"
            "uniform vec3 uLightDir;\n"   // same space as gl_Normal (NormalTransform)
            "void main(){\n"
            "    vEyePos    = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
            "    vNormalEye = gl_NormalMatrix * gl_Normal;\n"
            "    vLightEye  = gl_NormalMatrix * uLightDir;\n"
            "    vUv        = gl_MultiTexCoord0.xy;\n"
            // ftransform() guarantees the clip position is BIT-IDENTICAL to the
            // fixed-function path. Equipment is drawn in multiple overlapping
            // passes (base + glow); if this pass used gl_ProjectionMatrix *
            // gl_ModelViewMatrix the FP result would differ infinitesimally from
            // the legacy passes and z-fight => armor flicker. Do NOT replace.
            "    gl_Position = ftransform();\n"
            "}\n";

        const char* kFragmentSrc =
            "#version 120\n"
            "varying vec3 vNormalEye;\n"
            "varying vec3 vLightEye;\n"
            "varying vec3 vEyePos;\n"
            "varying vec2 vUv;\n"
            "uniform sampler2D uDiffuse;\n"
            "uniform sampler2D uNormalTex;\n"
            "uniform vec3  uBodyLight;\n"
            "uniform int   uHasNormalMap;\n"
            "uniform float uNormalStrength;\n"
            "uniform float uSpecStrength;\n"
            "uniform float uSpecPower;\n"
            "uniform float uAlpha;\n"
            "uniform vec3  uSunColor;\n"
            // Dynamic point lights (eye space). Accumulate diffuse falloff.
            "uniform int   uNumLights;\n"
            "uniform vec3  uLightPos[24];\n"
            "uniform vec3  uLightColor[24];\n"
            "uniform float uLightRadInv[24];\n"
            "vec3 pointLights(vec3 N, vec3 P){\n"
            "    vec3 acc = vec3(0.0);\n"
            "    for(int i=0;i<24;i++){\n"
            "        if(i>=uNumLights) break;\n"
            "        vec3 d = uLightPos[i] - P;\n"
            "        float dist = length(d);\n"
            "        float a = max(1.0 - dist*uLightRadInv[i], 0.0); a*=a;\n"
            "        acc += uLightColor[i] * (max(dot(N, d/max(dist,0.0001)),0.0) * a);\n"
            "    }\n"
            "    return acc;\n"
            "}\n"
            // Schueler cotangent frame: derive TBN from screen-space derivatives,
            // so no per-vertex tangents are needed.
            "mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv){\n"
            "    vec3 dp1 = dFdx(p);\n"
            "    vec3 dp2 = dFdy(p);\n"
            "    vec2 duv1 = dFdx(uv);\n"
            "    vec2 duv2 = dFdy(uv);\n"
            "    vec3 dp2perp = cross(dp2, N);\n"
            "    vec3 dp1perp = cross(N, dp1);\n"
            "    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
            "    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
            "    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));\n"
            "    return mat3(T * invmax, B * invmax, N);\n"
            "}\n"
            "void main(){\n"
            "    vec4 texColor = texture2D(uDiffuse, vUv);\n"
            "    vec3 N = normalize(vNormalEye);\n"
            "    if (uHasNormalMap == 1){\n"
            "        vec3 nTex = texture2D(uNormalTex, vUv).xyz * 2.0 - 1.0;\n"
            "        nTex.xy *= uNormalStrength;\n"
            "        mat3 TBN = cotangentFrame(N, vEyePos, vUv);\n"
            "        N = normalize(TBN * nTex);\n"
            "    }\n"
            "    vec3 L = normalize(vLightEye);\n"
            "    float ndl = dot(N, L);\n"
            // Matches legacy: Luminosity = dot(N,L)*0.8 + 0.4, floored at 0.2.
            "    float lum = max(ndl * 0.8 + 0.4, 0.2);\n"
            // Tint only the directly-lit fraction toward the sun color; ambient
            // stays neutral (white) so shadowed areas don't warm-wash. uSunColor
            // == white => identical to the neutral look.
            "    vec3 lightTint = mix(vec3(1.0), uSunColor, max(ndl, 0.0));\n"
            "    vec3 diffuse = texColor.rgb * uBodyLight * lum * lightTint;\n"
            "    vec3 V = normalize(-vEyePos);\n"
            "    vec3 H = normalize(L + V);\n"
            "    float spec = pow(max(dot(N, H), 0.0), uSpecPower) * uSpecStrength;\n"
            "    spec *= max(ndl, 0.0);\n"   // no glint on back-facing surfaces
            // Specular reflects the light source -> tint it by the sun color.
            "    vec3 color = diffuse + uSunColor * spec;\n"
            // Dynamic point lights add on top, illuminating the albedo with the
            // same (normal-mapped) normal as the sun.
            "    color += texColor.rgb * pointLights(N, vEyePos);\n"
            "    gl_FragColor = vec4(color, texColor.a * uAlpha);\n"
            "}\n";

        // --- Terrain: SUN RE-LIGHT. base = tile * gl_Color uses the map's
        // lightmap (PrimaryTerrainLight) as albedo, then re-lights per-pixel with
        // the shared sun (half-lambert off the perturbed normal) + sun tint +
        // specular. Flat ground tracks sun elevation; bumps add relief. ---------
        const char* kTerrainVertexSrc =
            "#version 120\n"
            "varying vec3 vNormalEye;\n"
            "varying vec3 vLightEye;\n"
            "varying vec3 vEyePos;\n"
            "varying vec2 vUv;\n"
            "varying vec4 vColor;\n"
            "uniform vec3 uLightDir;\n"
            "void main(){\n"
            "    vEyePos    = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
            "    vNormalEye = gl_NormalMatrix * gl_Normal;\n"
            "    vLightEye  = gl_NormalMatrix * uLightDir;\n"
            "    vUv        = gl_MultiTexCoord0.xy;\n"
            "    vColor     = gl_Color;\n"   // rgb = baked PrimaryTerrainLight; a = layer-2 blend weight (1 on base)
            "    gl_Position = ftransform();\n"
            "}\n";

        const char* kTerrainFragmentSrc =
            "#version 120\n"
            "varying vec3 vNormalEye;\n"
            "varying vec3 vLightEye;\n"
            "varying vec3 vEyePos;\n"
            "varying vec2 vUv;\n"
            "varying vec4 vColor;\n"
            "uniform sampler2D uDiffuse;\n"
            "uniform sampler2D uNormalTex;\n"
            "uniform int   uHasNormalMap;\n"
            "uniform float uNormalStrength;\n"
            "uniform float uSpecStrength;\n"
            "uniform float uSpecPower;\n"
            "uniform vec3  uSunColor;\n"
            "uniform int   uNumLights;\n"
            "uniform vec3  uLightPos[24];\n"
            "uniform vec3  uLightColor[24];\n"
            "uniform float uLightRadInv[24];\n"
            "vec3 pointLights(vec3 N, vec3 P){\n"
            "    vec3 acc = vec3(0.0);\n"
            "    for(int i=0;i<24;i++){\n"
            "        if(i>=uNumLights) break;\n"
            "        vec3 d = uLightPos[i] - P;\n"
            "        float dist = length(d);\n"
            "        float a = max(1.0 - dist*uLightRadInv[i], 0.0); a*=a;\n"
            "        acc += uLightColor[i] * (max(dot(N, d/max(dist,0.0001)),0.0) * a);\n"
            "    }\n"
            "    return acc;\n"
            "}\n"
            "mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv){\n"
            "    vec3 dp1 = dFdx(p);\n"
            "    vec3 dp2 = dFdy(p);\n"
            "    vec2 duv1 = dFdx(uv);\n"
            "    vec2 duv2 = dFdy(uv);\n"
            "    vec3 dp2perp = cross(dp2, N);\n"
            "    vec3 dp1perp = cross(N, dp1);\n"
            "    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;\n"
            "    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;\n"
            "    float invmax = inversesqrt(max(dot(T,T), dot(B,B)));\n"
            "    return mat3(T * invmax, B * invmax, N);\n"
            "}\n"
            "void main(){\n"
            "    vec4 texColor = texture2D(uDiffuse, vUv);\n"
            "    vec3 base = texColor.rgb * vColor.rgb;\n"   // lightmap albedo
            // alpha = texture alpha * layer blend weight (a==1 on the base pass).
            "    float outA = texColor.a * vColor.a;\n"
            // No normal map (e.g. grass billboards): still re-light by the sun
            // using the macro normal so brightness/tint match the ground beneath,
            // just without bump relief.
            "    if (uHasNormalMap == 0){\n"
            "        vec3 Nm = normalize(vNormalEye);\n"
            "        float ndl0 = max(dot(Nm, normalize(vLightEye)), 0.0);\n"
            "        float lum0 = 0.4 + 0.6 * ndl0;\n"
            "        vec3 tint0 = mix(vec3(1.0), uSunColor, ndl0);\n"
            "        vec3 c0 = base * lum0 * tint0 + texColor.rgb * pointLights(Nm, vEyePos);\n"
            "        gl_FragColor = vec4(c0, outA);\n"
            "        return;\n"
            "    }\n"
            "    vec3 Nmacro = normalize(vNormalEye);\n"
            "    vec3 nTex = texture2D(uNormalTex, vUv).xyz * 2.0 - 1.0;\n"
            "    nTex.xy *= uNormalStrength;\n"
            "    mat3 TBN = cotangentFrame(Nmacro, vEyePos, vUv);\n"
            "    vec3 Nb = normalize(TBN * nTex);\n"
            "    vec3 L = normalize(vLightEye);\n"
            "    float ndl = max(dot(Nb, L), 0.0);\n"
            // Sun RE-LIGHT: flat ground brightens as the sun climbs overhead
            // (dot(up,sun) -> 1) and darkens at grazing angles, with bumps adding
            // micro relief via the perturbed normal. Ambient floor (0.4) keeps
            // low-sun ground from going pitch black; caps at 1.0 so the baked
            // lightmap (carried in base) is never blown out. Tinted by the sun.
            "    float lum = 0.4 + 0.6 * ndl;\n"
            "    vec3 lightTint = mix(vec3(1.0), uSunColor, ndl);\n"
            "    vec3 lit = base * lum * lightTint;\n"
            "    vec3 V = normalize(-vEyePos);\n"
            "    vec3 H = normalize(L + V);\n"
            // Ground specular kept subtler than models (x0.5) so it doesn't read wet.
            "    float spec = pow(max(dot(Nb, H), 0.0), uSpecPower) * uSpecStrength * 0.5 * ndl;\n"
            "    vec3 color = lit + uSunColor * spec;\n"
            "    color += texColor.rgb * pointLights(Nb, vEyePos);\n"
            "    gl_FragColor = vec4(color, outA);\n"
            "}\n";

        // Transform the active point lights into eye space once per frame (using
        // the camera modelview active during the first lit draw) and pre-scale
        // color by intensity / radius. Cheap; one glGetFloatv per frame.
        void EnsureEyeLights()
        {
            if (!s_eyeDirty) return;
            s_eyeDirty = false;
            float m[16];
            glGetFloatv(GL_MODELVIEW_MATRIX, m);
            for (int i = 0; i < s_activeCount; i++)
            {
                const float* p = s_active[i].pos;
                s_activeEye[i][0] = m[0]*p[0] + m[4]*p[1] + m[8]*p[2]  + m[12];
                s_activeEye[i][1] = m[1]*p[0] + m[5]*p[1] + m[9]*p[2]  + m[13];
                s_activeEye[i][2] = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
                for (int c = 0; c < 3; c++) s_activeColScaled[i][c] = s_active[i].color[c] * s_dynIntensity;
                float r = s_active[i].radius; if (r < 1.0f) r = 1.0f;
                s_activeRadInv[i] = 1.0f / r;
            }
        }

        // Push the active lights to one program (slot 0 = model, 1 = terrain).
        // When the feature is off, uNumLights = 0 -> the shader loop is skipped.
        void UploadLights(int slot)
        {
            const PostProcess::GLProcs& gl = PostProcess::GL();
            const int n = (s_dynLights ? s_activeCount : 0);
            if (u_numLights[slot] >= 0) gl.Uniform1i(u_numLights[slot], n);
            if (n > 0)
            {
                EnsureEyeLights();
                if (u_lightPos[slot]    >= 0) gl.Uniform3fv(u_lightPos[slot],    n, &s_activeEye[0][0]);
                if (u_lightColor[slot]  >= 0) gl.Uniform3fv(u_lightColor[slot],  n, &s_activeColScaled[0][0]);
                if (u_lightRadInv[slot] >= 0 && gl.Uniform1fv) gl.Uniform1fv(u_lightRadInv[slot], n, s_activeRadInv);
            }
        }
    }

    void Init()
    {
        if (s_inited) return;

        // Seed runtime state from config (the per-map preset system / editor
        // override these later via SetParams). Re-read each retry until latched.
        GameConfig& cfg = GameConfig::GetInstance();
        s_runtimeEnabled = cfg.GetPerPixelLighting();
        s_normalStrength = cfg.GetNormalMapStrength();
        s_specStrength   = cfg.GetSpecularStrength();
        s_specPower      = cfg.GetSpecularPower();

        // Resolve GL2 entry points (idempotent; independent of the post chain).
        // If the GL context/procs aren't ready yet, DON'T latch — retry next call.
        if (!PostProcess::Available())
            PostProcess::Load();
        if (!PostProcess::Available())
            return;

        s_inited = true;   // GL ready: latch (compile once, even if currently off)

        s_prog = PostProcess::CompileProgram(kVertexSrc, kFragmentSrc);
        if (!s_prog)
            return;

        const PostProcess::GLProcs& gl = PostProcess::GL();
        u_diffuse        = gl.GetUniformLocation(s_prog, "uDiffuse");
        u_normalTex      = gl.GetUniformLocation(s_prog, "uNormalTex");
        u_bodyLight      = gl.GetUniformLocation(s_prog, "uBodyLight");
        u_lightDir       = gl.GetUniformLocation(s_prog, "uLightDir");
        u_hasNormalMap   = gl.GetUniformLocation(s_prog, "uHasNormalMap");
        u_normalStrength = gl.GetUniformLocation(s_prog, "uNormalStrength");
        u_specStrength   = gl.GetUniformLocation(s_prog, "uSpecStrength");
        u_specPower      = gl.GetUniformLocation(s_prog, "uSpecPower");
        u_alpha          = gl.GetUniformLocation(s_prog, "uAlpha");
        u_sunColor       = gl.GetUniformLocation(s_prog, "uSunColor");
        u_numLights[0]   = gl.GetUniformLocation(s_prog, "uNumLights");
        u_lightPos[0]    = gl.GetUniformLocation(s_prog, "uLightPos");
        u_lightColor[0]  = gl.GetUniformLocation(s_prog, "uLightColor");
        u_lightRadInv[0] = gl.GetUniformLocation(s_prog, "uLightRadInv");

        // Terrain additive-relief program (shares the runtime params + sun).
        s_terrainProg = PostProcess::CompileProgram(kTerrainVertexSrc, kTerrainFragmentSrc);
        if (s_terrainProg)
        {
            t_diffuse        = gl.GetUniformLocation(s_terrainProg, "uDiffuse");
            t_normalTex      = gl.GetUniformLocation(s_terrainProg, "uNormalTex");
            t_lightDir       = gl.GetUniformLocation(s_terrainProg, "uLightDir");
            t_hasNormalMap   = gl.GetUniformLocation(s_terrainProg, "uHasNormalMap");
            t_normalStrength = gl.GetUniformLocation(s_terrainProg, "uNormalStrength");
            t_specStrength   = gl.GetUniformLocation(s_terrainProg, "uSpecStrength");
            t_specPower      = gl.GetUniformLocation(s_terrainProg, "uSpecPower");
            t_sunColor       = gl.GetUniformLocation(s_terrainProg, "uSunColor");
            u_numLights[1]   = gl.GetUniformLocation(s_terrainProg, "uNumLights");
            u_lightPos[1]    = gl.GetUniformLocation(s_terrainProg, "uLightPos");
            u_lightColor[1]  = gl.GetUniformLocation(s_terrainProg, "uLightColor");
            u_lightRadInv[1] = gl.GetUniformLocation(s_terrainProg, "uLightRadInv");
        }

        // Valid only if BOTH programs compiled (they share the same GLSL feature
        // set, so in practice they succeed/fail together).
        s_programValid = (s_prog != 0 && s_terrainProg != 0);
    }

    void Shutdown()
    {
        if (PostProcess::Available())
        {
            if (s_prog)        PostProcess::GL().DeleteProgram(s_prog);
            if (s_terrainProg) PostProcess::GL().DeleteProgram(s_terrainProg);
        }
        s_prog = 0;
        s_terrainProg = 0;
        s_programValid = false;
        s_inited = false;
    }

    bool Active()
    {
        if (!s_inited) Init();
        return s_programValid && s_runtimeEnabled;
    }

    void SetParams(bool enabled, float normalStrength, float specStrength, float specPower,
                   const float sunColor[3])
    {
        if (!s_inited) Init();   // ensure the program is compiled (config seeds first)
        s_runtimeEnabled = enabled;
        s_normalStrength = normalStrength;
        s_specStrength   = specStrength;
        s_specPower      = specPower;
        s_sunColor[0] = sunColor[0];
        s_sunColor[1] = sunColor[1];
        s_sunColor[2] = sunColor[2];
    }

    void Begin(const float bodyLight[3], unsigned int normalTex, float alpha)
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        gl.UseProgram(s_prog);

        // Bind the normal map to unit 1 (leave the active unit at 0 so the
        // already-bound diffuse on unit 0 stays current for the draw).
        if (normalTex && gl.ActiveTexture)
        {
            gl.ActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, (GLuint)normalTex);
            gl.ActiveTexture(GL_TEXTURE0);
        }

        // World-space direction TO the sun (shared with god rays + shadows), so
        // per-pixel diffuse/specular follow the Sun Angle / Sun Height Z sliders.
        const float sun[3] = { BloodlustMU::g_SunDirection.x,
                               BloodlustMU::g_SunDirection.y,
                               BloodlustMU::g_SunDirection.z };

        gl.Uniform1i(u_diffuse, 0);
        gl.Uniform1i(u_normalTex, 1);
        gl.Uniform3fv(u_bodyLight, 1, bodyLight);
        gl.Uniform3fv(u_lightDir, 1, sun);
        gl.Uniform1i(u_hasNormalMap, normalTex ? 1 : 0);
        gl.Uniform1f(u_normalStrength, s_normalStrength);
        gl.Uniform1f(u_specStrength, s_specStrength);
        gl.Uniform1f(u_specPower, s_specPower);
        gl.Uniform1f(u_alpha, alpha);
        gl.Uniform3fv(u_sunColor, 1, s_sunColor);
        UploadLights(0);
    }

    void End()
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        // Make sure later fixed-function draws sample from unit 0.
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        gl.UseProgram(0);
    }

    void BeginTerrain(unsigned int normalTex)
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        gl.UseProgram(s_terrainProg);

        if (normalTex && gl.ActiveTexture)
        {
            gl.ActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, (GLuint)normalTex);
            gl.ActiveTexture(GL_TEXTURE0);
        }

        const float sun[3] = { BloodlustMU::g_SunDirection.x,
                               BloodlustMU::g_SunDirection.y,
                               BloodlustMU::g_SunDirection.z };

        gl.Uniform1i(t_diffuse, 0);
        gl.Uniform1i(t_normalTex, 1);
        gl.Uniform3fv(t_lightDir, 1, sun);
        gl.Uniform1i(t_hasNormalMap, normalTex ? 1 : 0);
        gl.Uniform1f(t_normalStrength, s_normalStrength);
        gl.Uniform1f(t_specStrength, s_specStrength);
        gl.Uniform1f(t_specPower, s_specPower);
        gl.Uniform3fv(t_sunColor, 1, s_sunColor);
        UploadLights(1);
    }

    void EndTerrain() { End(); }   // same restore (program 0 + unit 0)

    // --- Dynamic point-light registry (fed by ZzzLodTerrain's AddTerrainLight) -
    void SetDynamicLights(bool enabled, float intensity, float flicker)
    {
        s_dynLights = enabled;
        s_dynIntensity = intensity;
        s_dynFlicker = flicker;
    }

    bool DynamicLightsActive()
    {
        if (!s_inited) Init();
        // Requires the per-pixel shaders to actually be running (they're what
        // render the point lights). When per-pixel lighting is off, fall back so
        // AddTerrainLight keeps its legacy terrain-vertex glow.
        return s_dynLights && s_runtimeEnabled && s_programValid;
    }

    void ClearLights()
    {
        s_collectCount = 0;
    }

    void AddLight(float x, float y, float z, float r, float g, float b, float radius)
    {
        if (!s_dynLights) return;
        // Merge into a co-located existing light (e.g. a skill that spams many
        // AddTerrainLight calls at one spot) so clustered sources collapse to one
        // slot and don't evict distant torches. Keep the brightest + largest.
        for (int i = 0; i < s_collectCount; i++)
        {
            float dx = s_collect[i].pos[0] - x;
            float dy = s_collect[i].pos[1] - y;
            float dz = s_collect[i].pos[2] - z;
            if (dx*dx + dy*dy + dz*dz < 60.f * 60.f)
            {
                if (r > s_collect[i].color[0]) s_collect[i].color[0] = r;
                if (g > s_collect[i].color[1]) s_collect[i].color[1] = g;
                if (b > s_collect[i].color[2]) s_collect[i].color[2] = b;
                if (radius > s_collect[i].radius) s_collect[i].radius = radius;
                return;
            }
        }
        if (s_collectCount >= MAX_COLLECT) return;
        PointLight& L = s_collect[s_collectCount++];
        L.pos[0] = x; L.pos[1] = y; L.pos[2] = z;
        L.color[0] = r; L.color[1] = g; L.color[2] = b;
        L.radius = radius;
    }

    void SetPlayerLight(bool enabled, float radius)
    {
        s_playerLight = enabled;
        s_playerRadius = radius;
    }

    // Register the always-on hero light (warm, ~constant). Pre-divides by the
    // global intensity so its net brightness stays ~absolute regardless of the
    // torch intensity scale.
    void AddPlayerLight(float x, float y, float z)
    {
        if (!s_dynLights || !s_playerLight) return;
        const float inv = (s_dynIntensity > 0.01f) ? (1.0f / s_dynIntensity) : 1.0f;
        AddLight(x, y, z, 1.0f * inv, 0.95f * inv, 0.85f * inv, s_playerRadius);
    }

    // Pick the nearest MAX_LIGHTS to camPos from the collected set (called once
    // per frame, before ClearLights resets the collector). Sets s_eyeDirty.
    void SelectActiveLights(const float camPos[3])
    {
        if (s_collectCount <= MAX_LIGHTS)
        {
            s_activeCount = s_collectCount;
            for (int i = 0; i < s_activeCount; i++) s_active[i] = s_collect[i];
        }
        else
        {
            bool used[MAX_COLLECT] = { false };
            s_activeCount = MAX_LIGHTS;
            for (int k = 0; k < MAX_LIGHTS; k++)
            {
                int best = -1; float bestD = 1e30f;
                for (int i = 0; i < s_collectCount; i++)
                {
                    if (used[i]) continue;
                    float dx = s_collect[i].pos[0] - camPos[0];
                    float dy = s_collect[i].pos[1] - camPos[1];
                    float dz = s_collect[i].pos[2] - camPos[2];
                    float d = dx*dx + dy*dy + dz*dz;
                    if (d < bestD) { bestD = d; best = i; }
                }
                if (best < 0) { s_activeCount = k; break; }
                used[best] = true; s_active[k] = s_collect[best];
            }
        }

        // Temporal flicker smoothing: ease each light's color toward its value
        // from last frame (matched by position), so the source rand() flicker is
        // damped. response a: flicker=1 -> raw (a=1), flicker=0 -> very steady.
        const float a = 0.06f + 0.94f * (s_dynFlicker < 0.f ? 0.f : (s_dynFlicker > 1.f ? 1.f : s_dynFlicker));
        if (a < 0.999f)
        {
            for (int i = 0; i < s_activeCount; i++)
            {
                int best = -1; float bestD = 60.f * 60.f;   // match radius (world units)
                for (int j = 0; j < s_prevActiveCount; j++)
                {
                    float dx = s_active[i].pos[0] - s_prevActive[j].pos[0];
                    float dy = s_active[i].pos[1] - s_prevActive[j].pos[1];
                    float dz = s_active[i].pos[2] - s_prevActive[j].pos[2];
                    float d = dx*dx + dy*dy + dz*dz;
                    if (d < bestD) { bestD = d; best = j; }
                }
                if (best >= 0)
                    for (int c = 0; c < 3; c++)
                        s_active[i].color[c] = s_prevActive[best].color[c]
                            + (s_active[i].color[c] - s_prevActive[best].color[c]) * a;
            }
        }
        // Save the (smoothed) set for next frame's matching.
        s_prevActiveCount = s_activeCount;
        for (int i = 0; i < s_activeCount; i++) s_prevActive[i] = s_active[i];

        s_eyeDirty = true;
    }
}
