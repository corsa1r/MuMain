// ============================================================================
//  SunShadow.h  —  forward shadow mapping (sun), clean rebuild.
// ----------------------------------------------------------------------------
//  APPROACH (deliberately NOT the old screen-space reconstruction, which failed):
//  the shadow map is sampled INSIDE the geometry shaders (ModelLighting model +
//  terrain programs), where each fragment already has its true world position
//  (gl_Vertex) and N.L — so there is NO per-pixel world reconstruction to get
//  wrong, and bias is a simple slope term.
//
//  CASTER PASS (no fragile pre-pass re-issue): characters are COLLECTED during
//  the normal forward render (BMD::RenderBodyShadow -> PushCharacterVerts, real
//  world-space body verts with garbage guards), then FLUSHED once into a depth
//  FBO at frame end (BuildFromCollected). The shaders sample THAT map next frame
//  (one-frame lag — invisible for shadows, and it avoids re-issuing the complex
//  character renderer before its transforms exist).
//
//  Milestone 1 scope: characters cast + receive (self / mutual shadowing) only,
//  to validate the in-shader projection + sampling in isolation. Terrain/objects
//  come in later milestones.
// ============================================================================
#pragma once

namespace SunShadow
{
    // Live tunables. resolution = square map size; distance = ortho half-extent
    // (world units) around the camera focus; darkness 0..1; softness scales PCF.
    void SetParams(bool enabled, int resolution, float distance,
                   float darkness, float softness, float bias);
    bool Enabled();

    // ---- Caster collection (called from the forward render) -----------------
    bool CharacterCastWanted();                       // collect this frame?
    void PushCharacterVerts(const float* xyz, int n); // n triangle verts (world)

    // Static world objects (buildings/trees/props) as casters: true when
    // collecting this frame AND the object at 'origin' is within the shadow-map
    // footprint (cheap XY range cull so off-map geometry is skipped). Collected
    // verts go through PushCharacterVerts like characters.
    bool ObjectCastWanted(const float origin[3]);

    // Alpha-tested casters (fences/grilles/foliage whose texture has holes). The
    // mesh is drawn into the depth map WITH its texture + alpha test, so the
    // transparent texels punch through and the shadow keeps the cutout shape
    // instead of being a solid wall. posUV = 5 floats/vertex (x,y,z,u,v), nVerts
    // triangle verts; glTex = the diffuse GL texture id (its alpha is the mask).
    void PushTexturedCaster(unsigned int glTex, const float* posUV, int nVerts);

    // M1 isolation: only the entity at the camera focus (the player) casts, so
    // the debug map shows exactly one clean character. SetTarget each frame
    // before the world render; AcceptCaster gates by BodyOrigin distance.
    void SetTarget(const float p[3]);
    bool AcceptCaster(const float bodyOrigin[3]);

    // Build the depth map from everything collected this frame, around
    // 'cameraTarget' (world ground focus). Call once at frame end, AFTER the
    // forward render. Stores the world->light matrix for the shaders. No-op when
    // disabled. Resets the collection buffer for next frame.
    void BuildFromCollected(const float cameraTarget[3]);

    // ---- Consumed by the ModelLighting shaders ------------------------------
    bool         MapReady();        // a valid map exists to sample
    unsigned int DepthTexture();    // GL depth texture id
    const float* Matrix();          // 4x4 column-major world->light [0,1]
    int          Resolution();
    float        Darkness();
    float        Softness();
    float        Bias();

    void Shutdown();
    void DebugDraw();               // bottom-left corner depth preview (diagnostic)
}
