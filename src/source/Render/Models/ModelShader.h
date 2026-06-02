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

    // Terrain variant: SUN RE-LIGHT. Uses the tile's lightmap (gl_Color) as
    // albedo and re-lights it per-pixel with the shared sun (half-lambert off the
    // perturbed normal) + sun tint + specular, so flat ground tracks sun
    // elevation and bumps add relief. normalTex = the tile's sibling normal map
    // id. Uses the shared runtime params (enabled/strength/spec/sunColor) and sun
    // direction. Wrap a tile quad's glBegin/glEnd with BeginTerrain/EndTerrain.
    void BeginTerrain(unsigned int normalTex);
    void EndTerrain();

    // --- Dynamic point lights -------------------------------------------------
    // Fed from the engine's existing AddTerrainLight sources (torches, lanterns,
    // candles, bonfires, lava, skills, auras). They accumulate per-pixel on top
    // of the sun in the same shaders, using the same (normal-mapped) normal.
    //
    // SetDynamicLights: feature on/off + global intensity (from config/editor).
    // ClearLights:      reset the per-frame collector (call at frame start).
    // AddLight:         register one light this frame (world pos, color, radius).
    // SelectActiveLights: pick the nearest N to camPos for this frame's draws
    //                   (call once per frame, before ClearLights).
    void SetDynamicLights(bool enabled, float intensity, float flicker);
    void SetPlayerLight(bool enabled, float radius);   // always-on hero light
    bool DynamicLightsActive();   // feature on AND program valid
    void ClearLights();
    void AddLight(float x, float y, float z, float r, float g, float b, float radius);
    void AddPlayerLight(float x, float y, float z);     // register the hero light this frame
    void SelectActiveLights(const float camPos[3]);
}
