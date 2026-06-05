// ModelShader.cpp — see header for design intent.

#include "stdafx.h"
#include "ModelShader.h"

#include <gl/glew.h>

#include "Render/PostProcess/PostProcessGL.h"
#include "Render/SunDirection.h"
#include "Render/Shadow/SunShadow.h"
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
        // Sun shadow (model program).
        GLint u_shadowMap      = -1;
        GLint u_shadowMatrix   = -1;
        GLint u_shadowOn       = -1;
        GLint u_shadowDarkness = -1;
        GLint u_shadowTexel    = -1;
        GLint u_shadowSoftness = -1;
        GLint u_shadowBias     = -1;

        // Cached uniform locations (terrain program).
        GLint t_diffuse        = -1;
        GLint t_normalTex      = -1;
        GLint t_lightDir       = -1;
        GLint t_hasNormalMap   = -1;
        GLint t_normalStrength = -1;
        GLint t_specStrength   = -1;
        GLint t_specPower      = -1;
        GLint t_sunColor       = -1;
        // Sun shadow (terrain program).
        GLint t_shadowMap      = -1;
        GLint t_shadowMatrix   = -1;
        GLint t_shadowOn       = -1;
        GLint t_shadowDarkness = -1;
        GLint t_shadowTexel    = -1;
        GLint t_shadowSoftness = -1;
        GLint t_shadowBias     = -1;
        GLint t_reLight        = -1;

        // Config tunables, cached at Init().
        float s_normalStrength = 1.0f;
        float s_specStrength   = 0.30f;
        float s_specPower      = 24.0f;
        float s_sunColor[3]    = { 1.0f, 1.0f, 1.0f };

        // Per-draw: does the MODEL program sample the sun shadow map? Toggled by
        // the caller (true around static world objects, false around characters).
        bool  s_receiveModelShadow = false;

        // Terrain: do the full per-pixel sun re-light (Per-Pixel Lighting on) vs
        // shadow-only (PPL off, shader runs solely so the ground can catch sun
        // shadows — keep the exact legacy look, just multiply the shadow term).
        bool  s_groundRelight = true;

        // --- Dynamic point lights (fed from the engine's AddTerrainLight sources) ---
        enum { MAX_LIGHTS = 24, MAX_COLLECT = 96 };
        // seenTick = GetTickCount() when this source was FIRST seen (carried across
        // frames by position match). Lights present a while (torches/lanterns) are
        // "persistent" and outrank brand-new transient skill flames when the budget
        // is exceeded, so an AoE skill (e.g. Inferno) can't evict the town torches.
        struct PointLight { float pos[3]; float color[3]; float radius; unsigned int seenTick; };

        bool  s_dynLights       = false;   // feature on/off (live)
        float s_dynIntensity    = 1.0f;    // global scale
        float s_dynFlicker      = 0.5f;    // 0 = steady (smoothed), 1 = raw flicker
        int   s_maxLights       = MAX_LIGHTS; // configurable cap on active lights (1..MAX_LIGHTS)
        bool  s_playerLight     = true;    // always-on light following the hero
        float s_playerRadius    = 700.0f;  // world units

        PointLight s_collect[MAX_COLLECT];     // lights registered during the frame
        int        s_collectCount = 0;
        PointLight s_active[MAX_LIGHTS];        // selected (nearest) set used for drawing
        int        s_activeCount  = 0;
        PointLight s_prevActive[MAX_LIGHTS];    // last frame's (smoothed) set, for temporal smoothing
        int        s_prevActiveCount = 0;
        PointLight s_prevCollect[MAX_COLLECT];  // last frame's full collected set (for persistence aging)
        int        s_prevCollectCount = 0;
        float      s_activeEye[MAX_LIGHTS][3];     // active positions in eye space (per-frame)
        float      s_activeColScaled[MAX_LIGHTS][3]; // color * intensity
        float      s_activeRadInv[MAX_LIGHTS];     // 1 / radius
        bool       s_eyeDirty     = true;       // recompute eye-space lights this frame

        // Light uniform locations: [0]=model program, [1]=terrain program.
        GLint u_numLights[2]   = { -1, -1 };
        GLint u_lightPos[2]    = { -1, -1 };   // vec3 array (eye space)
        GLint u_lightColor[2]  = { -1, -1 };   // vec3 array (already * intensity)
        GLint u_lightRadInv[2] = { -1, -1 };   // 1/radius array
        // Point-light cone shadows ([i][0]=model program, [i][1]=terrain program).
        GLint u_lsMap[8][2] = {};           // 8 sampler locations per program (set in Init)
        GLint u_lsMat[2]    = { -1, -1 };   // uLSMat[8] array base location per program
        GLint u_lslot[2]    = { -1, -1 };   // uLSlot[24] per-light cone-slot index
        // Per active light: which cone-shadow SLOT shadows it (-1 = none). Built in
        // SelectActiveLights from the stable slot assignment, uploaded each draw.
        int   s_activeShadowSlot[MAX_LIGHTS] = {};

        const char* kVertexSrc =
            "#version 120\n"
            "varying vec3 vNormalEye;\n"
            "varying vec3 vLightEye;\n"
            "varying vec3 vEyePos;\n"
            "varying vec2 vUv;\n"
            "varying vec3 vWorldPos;\n"   // gl_Vertex is WORLD space for models
            "uniform vec3 uLightDir;\n"   // same space as gl_Normal (NormalTransform)
            "void main(){\n"
            "    vEyePos    = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
            "    vWorldPos  = gl_Vertex.xyz;\n"
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
            "varying vec3 vWorldPos;\n"
            "uniform sampler2D uDiffuse;\n"
            "uniform sampler2D uNormalTex;\n"
            "uniform vec3  uBodyLight;\n"
            "uniform int   uHasNormalMap;\n"
            "uniform float uNormalStrength;\n"
            "uniform float uSpecStrength;\n"
            "uniform float uSpecPower;\n"
            "uniform float uAlpha;\n"
            "uniform vec3  uSunColor;\n"
            // Sun shadow map (forward sample: vWorldPos -> light space, no
            // screen-space reconstruction).
            "uniform sampler2D uShadowMap;\n"
            "uniform mat4  uShadowMatrix;\n"
            "uniform int   uShadowOn;\n"
            "uniform float uShadowDarkness;\n"
            "uniform float uShadowTexel;\n"
            "uniform float uShadowSoftness;\n"
            "uniform float uShadowBias;\n"
            "float sunShadow(float ndl){\n"
            "    if (uShadowOn == 0) return 1.0;\n"
            "    vec4 sc = uShadowMatrix * vec4(vWorldPos, 1.0);\n"   // ortho -> w==1
            "    if (sc.x<0.0||sc.x>1.0||sc.y<0.0||sc.y>1.0||sc.z>1.0) return 1.0;\n"
            "    float bias = clamp(uShadowBias*0.0008*(1.0-ndl)+0.0004, 0.0003, 0.004);\n"
            "    float r = uShadowTexel * max(uShadowSoftness, 0.001);\n"
            "    float sh = 0.0;\n"
            "    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)\n"
            "        sh += (sc.z - bias > texture2D(uShadowMap, sc.xy+vec2(float(x),float(y))*r).r) ? 1.0 : 0.0;\n"
            "    return 1.0 - (sh/9.0)*uShadowDarkness;\n"
            "}\n"
            // Dynamic point lights (eye space). Accumulate diffuse falloff.
            "uniform int   uNumLights;\n"
            "uniform vec3  uLightPos[24];\n"
            "uniform vec3  uLightColor[24];\n"
            "uniform float uLightRadInv[24];\n"
            // Dynamic point-light CONE shadows: the nearest N caster lights each
            // have a perspective depth map; sample it (perspective divide -> [0,1])
            // to shadow that light where a static caster blocks it. vWorldPos =
            // fragment world position. Samplers are unrolled (GLSL120 can't index
            // them by a variable); matrices are an indexable array.
            "uniform sampler2D uLSMap0;\nuniform sampler2D uLSMap1;\nuniform sampler2D uLSMap2;\nuniform sampler2D uLSMap3;\n"
            "uniform sampler2D uLSMap4;\nuniform sampler2D uLSMap5;\nuniform sampler2D uLSMap6;\nuniform sampler2D uLSMap7;\n"
            "uniform mat4  uLSMat[8];\n"
            "uniform int   uLSlot[24];\n"   // per active light: cone-map slot (-1 = none)
            "float lsS(sampler2D s, mat4 m, vec3 wp){\n"
            "    vec4 sc = m * vec4(wp, 1.0);\n"
            "    if (sc.w <= 0.0001) return 1.0;\n"
            "    vec3 pc = sc.xyz / sc.w;\n"
            "    if (pc.x<0.0||pc.x>1.0||pc.y<0.0||pc.y>1.0||pc.z>1.0||pc.z<0.0) return 1.0;\n"
            // Same darkness as the sun shadow so light shadows aren't pitch-black.
            "    float sh = (pc.z - 0.0025 > texture2D(s, pc.xy).r) ? 1.0 : 0.0;\n"
            "    return 1.0 - sh * uShadowDarkness;\n"
            "}\n"
            "float lsPick(int i, vec3 wp){\n"
            "    if (i==0) return lsS(uLSMap0, uLSMat[0], wp);\n"
            "    if (i==1) return lsS(uLSMap1, uLSMat[1], wp);\n"
            "    if (i==2) return lsS(uLSMap2, uLSMat[2], wp);\n"
            "    if (i==3) return lsS(uLSMap3, uLSMat[3], wp);\n"
            "    if (i==4) return lsS(uLSMap4, uLSMat[4], wp);\n"
            "    if (i==5) return lsS(uLSMap5, uLSMat[5], wp);\n"
            "    if (i==6) return lsS(uLSMap6, uLSMat[6], wp);\n"
            "    return lsS(uLSMap7, uLSMat[7], wp);\n"
            "}\n"
            "vec3 pointLights(vec3 N, vec3 P){\n"
            "    vec3 acc = vec3(0.0);\n"
            "    for(int i=0;i<24;i++){\n"
            "        if(i>=uNumLights) break;\n"
            "        vec3 d = uLightPos[i] - P;\n"
            "        float dist = length(d);\n"
            "        float a = max(1.0 - dist*uLightRadInv[i], 0.0); a*=a;\n"
            "        int sls = uLSlot[i];\n"
            "        float lsh = (sls >= 0) ? lsPick(sls, vWorldPos) : 1.0;\n"
            "        acc += uLightColor[i] * (max(dot(N, d/max(dist,0.0001)),0.0) * a * lsh);\n"
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
            // Sun shadow darkens the SUN contribution (diffuse+spec); point lights
            // below are unaffected so torches still light shadowed areas.
            "    color *= sunShadow(ndl);\n"
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
            "varying vec3 vWorldPos;\n"
            "uniform vec3 uLightDir;\n"
            "void main(){\n"
            "    vEyePos    = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
            "    vWorldPos  = gl_Vertex.xyz;\n"
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
            "varying vec3 vWorldPos;\n"
            // 1 = full per-pixel sun re-light (Per-Pixel Lighting on). 0 =
            // shadow-only: keep the EXACT legacy ground color and only multiply
            // the shadow term, so a custom map with no normal maps / PPL off
            // still receives shadows without its terrain look changing.
            "uniform int   uReLight;\n"
            "uniform sampler2D uShadowMap;\n"
            "uniform mat4  uShadowMatrix;\n"
            "uniform int   uShadowOn;\n"
            "uniform float uShadowDarkness;\n"
            "uniform float uShadowTexel;\n"
            "uniform float uShadowSoftness;\n"
            "uniform float uShadowBias;\n"
            "float sunShadow(float ndl){\n"
            "    if (uShadowOn == 0) return 1.0;\n"
            "    vec4 sc = uShadowMatrix * vec4(vWorldPos, 1.0);\n"
            "    if (sc.x<0.0||sc.x>1.0||sc.y<0.0||sc.y>1.0||sc.z>1.0) return 1.0;\n"
            "    float bias = clamp(uShadowBias*0.0008*(1.0-ndl)+0.0004, 0.0003, 0.004);\n"
            "    float r = uShadowTexel * max(uShadowSoftness, 0.001);\n"
            "    float sh = 0.0;\n"
            "    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++)\n"
            "        sh += (sc.z - bias > texture2D(uShadowMap, sc.xy+vec2(float(x),float(y))*r).r) ? 1.0 : 0.0;\n"
            "    return 1.0 - (sh/9.0)*uShadowDarkness;\n"
            "}\n"
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
            // Dynamic point-light CONE shadows: the nearest N caster lights each
            // have a perspective depth map; sample it (perspective divide -> [0,1])
            // to shadow that light where a static caster blocks it. vWorldPos =
            // fragment world position. Samplers are unrolled (GLSL120 can't index
            // them by a variable); matrices are an indexable array.
            "uniform sampler2D uLSMap0;\nuniform sampler2D uLSMap1;\nuniform sampler2D uLSMap2;\nuniform sampler2D uLSMap3;\n"
            "uniform sampler2D uLSMap4;\nuniform sampler2D uLSMap5;\nuniform sampler2D uLSMap6;\nuniform sampler2D uLSMap7;\n"
            "uniform mat4  uLSMat[8];\n"
            "uniform int   uLSlot[24];\n"   // per active light: cone-map slot (-1 = none)
            "float lsS(sampler2D s, mat4 m, vec3 wp){\n"
            "    vec4 sc = m * vec4(wp, 1.0);\n"
            "    if (sc.w <= 0.0001) return 1.0;\n"
            "    vec3 pc = sc.xyz / sc.w;\n"
            "    if (pc.x<0.0||pc.x>1.0||pc.y<0.0||pc.y>1.0||pc.z>1.0||pc.z<0.0) return 1.0;\n"
            // Same darkness as the sun shadow so light shadows aren't pitch-black.
            "    float sh = (pc.z - 0.0025 > texture2D(s, pc.xy).r) ? 1.0 : 0.0;\n"
            "    return 1.0 - sh * uShadowDarkness;\n"
            "}\n"
            "float lsPick(int i, vec3 wp){\n"
            "    if (i==0) return lsS(uLSMap0, uLSMat[0], wp);\n"
            "    if (i==1) return lsS(uLSMap1, uLSMat[1], wp);\n"
            "    if (i==2) return lsS(uLSMap2, uLSMat[2], wp);\n"
            "    if (i==3) return lsS(uLSMap3, uLSMat[3], wp);\n"
            "    if (i==4) return lsS(uLSMap4, uLSMat[4], wp);\n"
            "    if (i==5) return lsS(uLSMap5, uLSMat[5], wp);\n"
            "    if (i==6) return lsS(uLSMap6, uLSMat[6], wp);\n"
            "    return lsS(uLSMap7, uLSMat[7], wp);\n"
            "}\n"
            "vec3 pointLights(vec3 N, vec3 P){\n"
            "    vec3 acc = vec3(0.0);\n"
            "    for(int i=0;i<24;i++){\n"
            "        if(i>=uNumLights) break;\n"
            "        vec3 d = uLightPos[i] - P;\n"
            "        float dist = length(d);\n"
            "        float a = max(1.0 - dist*uLightRadInv[i], 0.0); a*=a;\n"
            "        int sls = uLSlot[i];\n"
            "        float lsh = (sls >= 0) ? lsPick(sls, vWorldPos) : 1.0;\n"
            "        acc += uLightColor[i] * (max(dot(N, d/max(dist,0.0001)),0.0) * a * lsh);\n"
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
            // Shadow-only mode (PPL off, wrapped solely to receive shadows):
            // output the unchanged legacy ground color times the shadow term so
            // nothing else about the terrain look shifts. No re-light, no spec,
            // no per-pixel point lights.
            "    if (uReLight == 0){\n"
            "        vec3 Nm = normalize(vNormalEye);\n"
            "        float ndlS = max(dot(Nm, normalize(vLightEye)), 0.0);\n"
            "        gl_FragColor = vec4(base * sunShadow(ndlS), outA);\n"
            "        return;\n"
            "    }\n"
            // No normal map (e.g. grass billboards): still re-light by the sun
            // using the macro normal so brightness/tint match the ground beneath,
            // just without bump relief.
            "    if (uHasNormalMap == 0){\n"
            "        vec3 Nm = normalize(vNormalEye);\n"
            "        float ndl0 = max(dot(Nm, normalize(vLightEye)), 0.0);\n"
            "        float lum0 = 0.4 + 0.6 * ndl0;\n"
            "        vec3 tint0 = mix(vec3(1.0), uSunColor, ndl0);\n"
            "        vec3 c0 = base * lum0 * tint0 * sunShadow(ndl0);\n"
            "        c0 += texColor.rgb * pointLights(Nm, vEyePos);\n"
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
            "    color *= sunShadow(ndl);\n"
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

        // Bind the point-light CONE shadow maps (8 fixed SLOTS, units 3..10) + their
        // world->light matrices + the per-active-light slot index (uLSlot), so the
        // shader's pointLights() shadows each light with its OWN stable cone map. The
        // slot assignment is stable across frames (ModelLighting keeps a torch in the
        // same slot), so the one-frame-lagged map is never mis-paired -> no popping.
        // No-op (all uLSlot already -1 from the per-frame build) when off / not ready.
        void BindLightShadows(int slot)
        {
            const PostProcess::GLProcs& gl = PostProcess::GL();
            const bool on = s_dynLights && SunShadow::LightShadowReady();

            // Upload the per-light slot map every draw (it's -1 everywhere when off,
            // so the shader applies no light shadows).
            if (u_lslot[slot] >= 0 && gl.Uniform1iv)
                gl.Uniform1iv(u_lslot[slot], MAX_LIGHTS, s_activeShadowSlot);
            if (!on) return;

            // Bind all 8 slot maps to distinct units 3..10 (every sampler needs a
            // valid, distinct unit; unreferenced slots are simply never sampled).
            if (gl.ActiveTexture)
            {
                for (int i = 0; i < 8; ++i)
                {
                    gl.ActiveTexture(GL_TEXTURE3 + i);
                    glBindTexture(GL_TEXTURE_2D, (GLuint)SunShadow::LightShadowTexture(i));
                }
                gl.ActiveTexture(GL_TEXTURE0);
            }
            for (int i = 0; i < 8; ++i)
                if (u_lsMap[i][slot] >= 0) gl.Uniform1i(u_lsMap[i][slot], 3 + i);
            if (u_lsMat[slot] >= 0 && gl.UniformMatrix4fv)
                gl.UniformMatrix4fv(u_lsMat[slot], 8, GL_FALSE, SunShadow::LightShadowMatrices());
        }
    }

    // Bind the sun shadow map (unit 2) + its world->light matrix and params to
    // whichever program is current. Leaves the active texture unit at 0 so the
    // diffuse draw is unaffected. When shadows are off / not ready, just sets
    // uShadowOn=0 (the shader early-outs to fully lit).
    void BindSunShadow(GLint locMap, GLint locMat, GLint locOn, GLint locDark,
                       GLint locTexel, GLint locSoft, GLint locBias)
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        const bool on = SunShadow::Enabled() && SunShadow::MapReady();
        if (locOn >= 0) gl.Uniform1i(locOn, on ? 1 : 0);
        if (!on) return;
        if (gl.ActiveTexture)
        {
            gl.ActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, (GLuint)SunShadow::DepthTexture());
            gl.ActiveTexture(GL_TEXTURE0);
        }
        if (locMap >= 0) gl.Uniform1i(locMap, 2);
        if (locMat >= 0 && gl.UniformMatrix4fv)
            gl.UniformMatrix4fv(locMat, 1, GL_FALSE, SunShadow::Matrix());
        if (locDark >= 0) gl.Uniform1f(locDark, SunShadow::Darkness());
        const int res = SunShadow::Resolution();
        if (locTexel >= 0) gl.Uniform1f(locTexel, res > 0 ? 1.0f/(float)res : 0.0005f);
        if (locSoft >= 0) gl.Uniform1f(locSoft, SunShadow::Softness());
        if (locBias >= 0) gl.Uniform1f(locBias, SunShadow::Bias());
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
        u_shadowMap      = gl.GetUniformLocation(s_prog, "uShadowMap");
        u_shadowMatrix   = gl.GetUniformLocation(s_prog, "uShadowMatrix");
        u_shadowOn       = gl.GetUniformLocation(s_prog, "uShadowOn");
        u_shadowDarkness = gl.GetUniformLocation(s_prog, "uShadowDarkness");
        u_shadowTexel    = gl.GetUniformLocation(s_prog, "uShadowTexel");
        u_shadowSoftness = gl.GetUniformLocation(s_prog, "uShadowSoftness");
        u_shadowBias     = gl.GetUniformLocation(s_prog, "uShadowBias");
        u_numLights[0]   = gl.GetUniformLocation(s_prog, "uNumLights");
        u_lightPos[0]    = gl.GetUniformLocation(s_prog, "uLightPos");
        u_lightColor[0]  = gl.GetUniformLocation(s_prog, "uLightColor");
        u_lightRadInv[0] = gl.GetUniformLocation(s_prog, "uLightRadInv");
        {
            static const char* lsN[8] = { "uLSMap0","uLSMap1","uLSMap2","uLSMap3",
                                          "uLSMap4","uLSMap5","uLSMap6","uLSMap7" };
            for (int i = 0; i < 8; ++i) u_lsMap[i][0] = gl.GetUniformLocation(s_prog, lsN[i]);
        }
        u_lsMat[0]       = gl.GetUniformLocation(s_prog, "uLSMat");
        u_lslot[0]       = gl.GetUniformLocation(s_prog, "uLSlot");

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
            t_shadowMap      = gl.GetUniformLocation(s_terrainProg, "uShadowMap");
            t_shadowMatrix   = gl.GetUniformLocation(s_terrainProg, "uShadowMatrix");
            t_shadowOn       = gl.GetUniformLocation(s_terrainProg, "uShadowOn");
            t_shadowDarkness = gl.GetUniformLocation(s_terrainProg, "uShadowDarkness");
            t_shadowTexel    = gl.GetUniformLocation(s_terrainProg, "uShadowTexel");
            t_shadowSoftness = gl.GetUniformLocation(s_terrainProg, "uShadowSoftness");
            t_shadowBias     = gl.GetUniformLocation(s_terrainProg, "uShadowBias");
            t_reLight        = gl.GetUniformLocation(s_terrainProg, "uReLight");
            u_numLights[1]   = gl.GetUniformLocation(s_terrainProg, "uNumLights");
            u_lightPos[1]    = gl.GetUniformLocation(s_terrainProg, "uLightPos");
            u_lightColor[1]  = gl.GetUniformLocation(s_terrainProg, "uLightColor");
            u_lightRadInv[1] = gl.GetUniformLocation(s_terrainProg, "uLightRadInv");
            {
                static const char* lsN[8] = { "uLSMap0","uLSMap1","uLSMap2","uLSMap3",
                                              "uLSMap4","uLSMap5","uLSMap6","uLSMap7" };
                for (int i = 0; i < 8; ++i) u_lsMap[i][1] = gl.GetUniformLocation(s_terrainProg, lsN[i]);
            }
            u_lsMat[1]       = gl.GetUniformLocation(s_terrainProg, "uLSMat");
            u_lslot[1]       = gl.GetUniformLocation(s_terrainProg, "uLSlot");
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
        // Models always CAST (collected into the map). RECEIVING is per-draw:
        // ON for static world objects (so building/tree walls catch the shadows
        // other casters throw), OFF for characters/monsters (the one-frame-latent
        // map + low-poly meshes self-acne/flicker). Caller toggles via
        // SetReceiveShadow() around RenderObjects vs RenderCharactersClient.
        if (s_receiveModelShadow)
            BindSunShadow(u_shadowMap, u_shadowMatrix, u_shadowOn, u_shadowDarkness,
                          u_shadowTexel, u_shadowSoftness, u_shadowBias);
        else if (u_shadowOn >= 0)
            gl.Uniform1i(u_shadowOn, 0);
        UploadLights(0);
        BindLightShadows(0);
    }

    void End()
    {
        const PostProcess::GLProcs& gl = PostProcess::GL();
        // Make sure later fixed-function draws sample from unit 0.
        if (gl.ActiveTexture) gl.ActiveTexture(GL_TEXTURE0);
        gl.UseProgram(0);
    }

    void SetReceiveShadow(bool on) { s_receiveModelShadow = on; }
    void SetGroundRelight(bool on) { s_groundRelight = on; }

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
        if (t_reLight >= 0) gl.Uniform1i(t_reLight, s_groundRelight ? 1 : 0);
        BindSunShadow(t_shadowMap, t_shadowMatrix, t_shadowOn, t_shadowDarkness,
                      t_shadowTexel, t_shadowSoftness, t_shadowBias);
        UploadLights(1);
        BindLightShadows(1);
    }

    void EndTerrain() { End(); }   // same restore (program 0 + unit 0)

    // --- Dynamic point-light registry (fed by ZzzLodTerrain's AddTerrainLight) -
    void SetDynamicLights(bool enabled, float intensity, float flicker)
    {
        s_dynLights = enabled;
        s_dynIntensity = intensity;
        s_dynFlicker = flicker;
    }

    void SetMaxDynamicLights(int n)
    {
        s_maxLights = (n < 1) ? 1 : (n > MAX_LIGHTS ? MAX_LIGHTS : n);
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
        L.seenTick = 0;   // assigned (aged) in SelectActiveLights via position match
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
        // --- Persistence aging --------------------------------------------------
        // Carry each collected light's "first seen" tick across frames by matching
        // its position to last frame's set. A torch sits at a fixed spot so its age
        // grows every frame; a transient skill flame (Inferno etc.) is new or moves,
        // so it stays young. Used below to keep world torches lit when a skill spams
        // the area with flame lights and blows past the light budget.
        const unsigned int nowTick = GetTickCount();
        const float        kMatch2 = 50.0f * 50.0f;       // "same source" position tolerance
        const unsigned int kPersistMs   = 1500;           // seen this long -> world light
        const float        kTransientPenalty = 4.0f;      // treat new flames as 2x farther
        for (int i = 0; i < s_collectCount; i++)
        {
            unsigned int seen = nowTick;                  // default: brand new this frame
            for (int j = 0; j < s_prevCollectCount; j++)
            {
                const float dx = s_collect[i].pos[0] - s_prevCollect[j].pos[0];
                const float dy = s_collect[i].pos[1] - s_prevCollect[j].pos[1];
                const float dz = s_collect[i].pos[2] - s_prevCollect[j].pos[2];
                if (dx*dx + dy*dy + dz*dz < kMatch2) { seen = s_prevCollect[j].seenTick; break; }
            }
            s_collect[i].seenTick = seen;
        }

        // Configurable cap: keep only the nearest s_maxLights (1..MAX_LIGHTS).
        const int cap = (s_maxLights < 1) ? 1 : (s_maxLights > MAX_LIGHTS ? MAX_LIGHTS : s_maxLights);
        if (s_collectCount <= cap)
        {
            s_activeCount = s_collectCount;
            for (int i = 0; i < s_activeCount; i++) s_active[i] = s_collect[i];
        }
        else
        {
            // Over budget: select by a persistence-weighted distance. World lights
            // (persistent) use their true distance; transient skill flames are
            // pushed back (counted as ~2x farther), so a town torch within ~2x a
            // flame's distance keeps its slot instead of being evicted. The hero
            // light, being nearest (distance ~0), always survives either way.
            bool used[MAX_COLLECT] = { false };
            s_activeCount = cap;
            for (int k = 0; k < cap; k++)
            {
                int best = -1; float bestD = 1e30f;
                for (int i = 0; i < s_collectCount; i++)
                {
                    if (used[i]) continue;
                    float dx = s_collect[i].pos[0] - camPos[0];
                    float dy = s_collect[i].pos[1] - camPos[1];
                    float dz = s_collect[i].pos[2] - camPos[2];
                    float d = dx*dx + dy*dy + dz*dz;
                    if ((nowTick - s_collect[i].seenTick) < kPersistMs) d *= kTransientPenalty;
                    if (d < bestD) { bestD = d; best = i; }
                }
                if (best < 0) { s_activeCount = k; break; }
                used[best] = true; s_active[k] = s_collect[best];
            }
        }

        // Order the active set so the SHADOW-CASTING world lights come FIRST:
        // persistent lights (torches/lanterns, seen >= kPersistMs) before transient
        // skill flames, nearest-first within each group. The cone-shadow casters are
        // the front prefix of s_active (the shader pairs cone map i with active light
        // i), so this keeps the town torches casting even when an AoE skill (Inferno)
        // surrounds you with NEARER flame lights. Illumination is order-independent,
        // so the flames still light you up -- they just don't steal the caster slots.
        // (selection sort; s_activeCount <= 24, negligible.)
        for (int i = 0; i < s_activeCount; ++i)
        {
            int   best = i;
            int   bestT = 2; float bestD = 1e30f;
            for (int j = i; j < s_activeCount; ++j)
            {
                const int   t  = ((nowTick - s_active[j].seenTick) < kPersistMs) ? 1 : 0;   // 0=persistent first
                const float dx = s_active[j].pos[0] - camPos[0];
                const float dy = s_active[j].pos[1] - camPos[1];
                const float dz = s_active[j].pos[2] - camPos[2];
                const float d  = dx*dx + dy*dy + dz*dz;
                if (t < bestT || (t == bestT && d < bestD)) { bestT = t; bestD = d; best = j; }
            }
            if (best != i) { PointLight t = s_active[i]; s_active[i] = s_active[best]; s_active[best] = t; }
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

        // Build the dynamic-light CONE shadow casters with STABLE SLOT identity.
        // Each persistent world light (torch/lantern) is pinned to a fixed cone-map
        // slot across frames (matched by position). The shader samples each light's
        // OWN slot via uLSlot[], so a torch's shadow is never mis-paired when the
        // active-light order churns or due to the one-frame map latency -- that is
        // what was causing shadows to pop on/off while walking. Transient skill
        // flames (Inferno) are never casters. Slot count bounded by the Max Dynamic
        // Lights slider and the cone-map cap.
        const int NS = SunShadow::LightShadowCap();        // 8 cone-map slots
        SunShadow::SetLightShadowParams(NS, 512);
        for (int i = 0; i < MAX_LIGHTS; ++i) s_activeShadowSlot[i] = -1;
        {
            static float s_slotPos[8][3] = {};
            static float s_slotRad[8]    = {};
            static bool  s_slotUsed[8]   = {};
            static bool  s_slotReady[8]  = {};   // slot's map was built last frame (sampleable now)

            int want = s_maxLights;                 // == the Max Dynamic Lights slider
            if (want > NS)            want = NS;    // bounded by available cone slots
            if (want > s_activeCount) want = s_activeCount;
            if (!s_dynLights)         want = 0;

            // Distance gate WITH HYSTERESIS: enter casting within the footprint,
            // keep casting until clearly beyond it (no per-step edge flicker).
            const float d0      = SunShadow::Distance();
            const float inD2    = d0 * d0;
            const float outD2   = (d0 * 1.25f) * (d0 * 1.25f);
            const float matchR2 = 64.0f * 64.0f;    // "same torch" position tolerance

            // Candidate casters = persistent active lights within the OUTER footprint,
            // nearest-first (s_active is persistent-first then nearest, so we stop at
            // the first transient or first one beyond the outer radius).
            struct Cand { int idx; float pos[3]; float rad; float d2; bool taken; };
            Cand cand[MAX_LIGHTS]; int candN = 0;
            for (int i = 0; i < s_activeCount && candN < MAX_LIGHTS; ++i)
            {
                if ((nowTick - s_active[i].seenTick) < kPersistMs) break;  // transient -> stop
                const float dx = s_active[i].pos[0] - camPos[0];
                const float dy = s_active[i].pos[1] - camPos[1];
                const float dz = s_active[i].pos[2] - camPos[2];
                const float d2 = dx*dx + dy*dy + dz*dz;
                if (d2 > outD2) break;                                     // farther ones too
                Cand& c = cand[candN++];
                c.idx = i; c.pos[0] = s_active[i].pos[0]; c.pos[1] = s_active[i].pos[1];
                c.pos[2] = s_active[i].pos[2]; c.rad = s_active[i].radius; c.d2 = d2; c.taken = false;
            }
            if (want <= 0) candN = 0;

            // Step A -- keep slots whose torch is still present (match by position,
            // already filtered to outD2 = hysteresis). A still-occupied slot that was
            // built last frame is sampled THIS frame (uLSlot points at it). Otherwise
            // free the slot.
            for (int s = 0; s < NS; ++s)
            {
                if (!s_slotUsed[s]) continue;
                int best = -1; float bestE = matchR2;
                for (int k = 0; k < candN; ++k)
                {
                    if (cand[k].taken) continue;
                    const float ex = cand[k].pos[0] - s_slotPos[s][0];
                    const float ey = cand[k].pos[1] - s_slotPos[s][1];
                    const float ez = cand[k].pos[2] - s_slotPos[s][2];
                    const float e2 = ex*ex + ey*ey + ez*ez;
                    if (e2 < bestE) { bestE = e2; best = k; }
                }
                if (best >= 0)
                {
                    cand[best].taken = true;
                    s_slotPos[s][0] = cand[best].pos[0];
                    s_slotPos[s][1] = cand[best].pos[1];
                    s_slotPos[s][2] = cand[best].pos[2];
                    s_slotRad[s]    = cand[best].rad;
                    if (s_slotReady[s] && cand[best].idx < MAX_LIGHTS)
                        s_activeShadowSlot[cand[best].idx] = s;
                }
                else { s_slotUsed[s] = false; s_slotReady[s] = false; }
            }

            // Step B -- assign NEW torches (within the INNER gate) to free slots,
            // nearest-first, up to 'want'. Their map builds at frame end, so the
            // shadow appears next frame (s_slotReady stays false now -> uLSlot = -1).
            for (;;)
            {
                int usedNow = 0; for (int s = 0; s < NS; ++s) if (s_slotUsed[s]) ++usedNow;
                if (usedNow >= want) break;
                int best = -1; float bestD = inD2;
                for (int k = 0; k < candN; ++k)
                    if (!cand[k].taken && cand[k].d2 <= bestD) { bestD = cand[k].d2; best = k; }
                if (best < 0) break;
                int freeSlot = -1; for (int s = 0; s < NS; ++s) if (!s_slotUsed[s]) { freeSlot = s; break; }
                if (freeSlot < 0) break;
                cand[best].taken       = true;
                s_slotUsed[freeSlot]   = true;
                s_slotReady[freeSlot]  = false;
                s_slotPos[freeSlot][0] = cand[best].pos[0];
                s_slotPos[freeSlot][1] = cand[best].pos[1];
                s_slotPos[freeSlot][2] = cand[best].pos[2];
                s_slotRad[freeSlot]    = cand[best].rad;
            }

            // Enforce the slider cap if it shrank: free the FARTHEST used slots.
            for (;;)
            {
                int usedNow = 0; for (int s = 0; s < NS; ++s) if (s_slotUsed[s]) ++usedNow;
                if (usedNow <= want) break;
                int farSlot = -1; float farD = -1.0f;
                for (int s = 0; s < NS; ++s)
                {
                    if (!s_slotUsed[s]) continue;
                    const float dx = s_slotPos[s][0]-camPos[0], dy = s_slotPos[s][1]-camPos[1], dz = s_slotPos[s][2]-camPos[2];
                    const float d2 = dx*dx + dy*dy + dz*dz;
                    if (d2 > farD) { farD = d2; farSlot = s; }
                }
                if (farSlot < 0) break;
                s_slotUsed[farSlot] = false; s_slotReady[farSlot] = false;
                for (int i = 0; i < MAX_LIGHTS; ++i) if (s_activeShadowSlot[i] == farSlot) s_activeShadowSlot[i] = -1;
            }

            // Feed the 8 slots to SunShadow (it builds the occupied ones at frame end).
            float slotPos[8 * 3]; float slotRad[8]; int slotUsed[8];
            for (int s = 0; s < NS; ++s)
            {
                slotUsed[s]      = s_slotUsed[s] ? 1 : 0;
                slotPos[s*3+0]   = s_slotPos[s][0];
                slotPos[s*3+1]   = s_slotPos[s][1];
                slotPos[s*3+2]   = s_slotPos[s][2];
                slotRad[s]       = s_slotRad[s];
            }
            SunShadow::SetLightCasters(slotPos, slotRad, slotUsed, NS);

            // Advance readiness: occupied slots build their map this frame and become
            // sampleable next frame.
            for (int s = 0; s < NS; ++s) s_slotReady[s] = s_slotUsed[s];
        }

        // Snapshot this frame's full collected set (with aged seenTicks) so next
        // frame can carry persistence forward by position match.
        s_prevCollectCount = s_collectCount;
        for (int i = 0; i < s_collectCount; i++) s_prevCollect[i] = s_collect[i];

        s_eyeDirty = true;
    }
}
