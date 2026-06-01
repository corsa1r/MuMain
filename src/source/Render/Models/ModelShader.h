// ============================================================================
//  ModelShader.h  —  per-pixel lighting + normal mapping for BMD models
// ----------------------------------------------------------------------------
//  WHY THIS EXISTS
//  The legacy BMD path bakes lighting into vertex colors on the CPU
//  (BMD::Transform: color = BodyLight * (dot(N,L)*0.8+0.4)). This module moves
//  that exact term to a per-pixel GLSL 1.20 program and adds a normal-map
//  perturbation + Blinn-Phong specular, so the upscaled diffuse textures show
//  real surface relief and glints.
//
//  It reuses the post-process GL plumbing (PostProcess::CompileProgram / GL()):
//  no separate proc loader. Lighting is done in EYE space so it is independent
//  of whether submitted vertices include BodyOrigin — and dot(N,L) is rotation
//  invariant, so with a flat normal + zero specular it reproduces the legacy
//  per-vertex result.
//
//  Gated behind [Graphics] PerPixelLighting (default off). When disabled or the
//  program fails to compile, Active() is false and ZzzBMD uses the legacy path
//  unchanged — guaranteed parity.
// ============================================================================
#pragma once

namespace ModelLighting
{
    // Compile the program and cache uniforms. Idempotent; lazily invoked by
    // Active() on a current GL context. Reads config (GameConfig).
    void Init();

    // Release the program. Safe to call without a prior Init().
    void Shutdown();

    // True when the feature is enabled (runtime) AND the program is valid.
    // Cheap after the first call (cached). Triggers lazy Init().
    bool Active();

    // Live-update the runtime toggle + tunables (from the per-map preset system
    // / editor panel, via PostProcess::Chain::ApplySettings). Takes effect on the
    // next frame; no rebuild. Lazily compiles the program if needed. sunColor is
    // the shared sun tint (God Rays Tint); it tints the directly-lit diffuse and
    // the specular (ambient stays neutral). White => neutral (parity).
    void SetParams(bool enabled, float normalStrength, float specStrength, float specPower,
                   const float sunColor[3]);

    // Begin a lit draw. bodyLight is a 3-float array in the submitted gl_Normal
    // (world) space. The light direction is the shared world-space sun
    // (BloodlustMU::g_SunDirection) so per-pixel diffuse + specular track the
    // same Sun Angle / Sun Height Z as the god rays and character shadows.
    // normalTex is the diffuse's sibling normal-map GL texture id (0 = none ->
    // flat-normal branch); Begin binds it to GL_TEXTURE1 when non-zero. alpha
    // multiplies output alpha (matches the legacy path).
    void Begin(const float bodyLight[3], unsigned int normalTex, float alpha);

    // Restore fixed-function (glUseProgram(0)) and active texture unit 0.
    void End();

    // Terrain variant: ADDITIVE relief. Keeps the tile's existing baked per-vertex
    // light (gl_Color) and texture exactly, and only adds normal-map relief +
    // sun-colored specular on top. normalTex = the tile's sibling normal map id
    // (0 => parity branch, renders identical to legacy). Uses the shared runtime
    // params (enabled/strength/spec/sunColor) and sun direction. Wrap a tile
    // quad's glBegin/glEnd with BeginTerrain/EndTerrain.
    void BeginTerrain(unsigned int normalTex);
    void EndTerrain();
}
