#pragma once

#ifdef _EDITOR

#include <vector>

#include "Camera/CameraMove.h"

// Forward decl: a single point we can undo. Lives outside CDevEditorUI so
// the helper free functions in DevEditorUI.cpp can build/push them.
struct DevEditorUndoAction
{
    enum class Kind { PlaceObject, DeleteObject };
    Kind     kind;
    class OBJECT* obj;   // pointer into ObjectBlock; valid while editor session lives
};

struct CameraConfig;

// Per-camera override state used by the DevEditor Game Scene panel.
// Default and Orbital cameras use fundamentally different projection/culling
// paths, so each gets its own set of fields. Common fields are duplicated
// rather than unified to keep the UI surface honest (each slider ties to a
// specific hook in the correct camera).
struct DevEditorDefaultCameraOverride
{
    bool  enabled          = false;
    // CameraConfig values that actually have an effect for this camera:
    //   nearPlane → g_Camera.ViewNear → gluPerspective near clip
    //   farPlane  → g_Camera.ViewFar  → gluPerspective far clip + fog scale
    // terrainCullRange is intentionally omitted: the Default camera's terrain
    // culling uses a hardcoded 2D trapezoid (see ZzzLodTerrain.cpp legacy path),
    // not CameraConfig.terrainCullRange. Use the width multipliers below instead.
    float nearPlane        = 500.0f;
    float farPlane         = 3000.0f;
    // Camera position offset from the default (hero-relative) placement.
    // Applied at the end of DefaultCamera::CalculateCameraPosition.
    float offsetX          = 0.0f;
    float offsetY          = 0.0f;
    float offsetZ          = 0.0f;
    // Multipliers applied to the hardcoded 2D culling trapezoid widths
    // (WidthNear = "bottom" of the trapezoid, WidthFar = "top").
    float widthNearMul     = 1.0f;
    float widthFarMul      = 1.0f;
    // Fog
    bool  fogOverride      = false;
    bool  fogOn            = true;
    float fogStartPct      = 1.00f;
    float fogEndPct        = 1.25f;
};

// Orbital override: tunes the 2D terrain-cull hull and fog. Does NOT touch the
// 3D render frustum (FOV / near / far) — those are deliberately left on the
// original Orbital path. The 2D hull naturally starts as a pyramid (apex at
// the camera, 4 far corners at terrainCullRange). When nearDepthFrac > 0 it
// becomes a real trapezoid with a non-zero near edge.
struct DevEditorOrbitalCameraOverride
{
    bool  enabled          = false;
    // View-aligned trapezoid in absolute world units. Corners are built in
    // camera-local coordinates (forward along view -Z, lateral along view X)
    // then transformed via the camera matrix, so the shape tracks yaw AND
    // pitch — the trapezoid always matches what the camera is actually looking at.
    float farDist      = 5000.0f;  // distance from camera to the far edge
    float farWidth     = 8000.0f;  // total width at the far edge
    float nearDist     = 0.0f;     // distance from camera to the near edge (0 = at camera)
    float nearWidth    = 800.0f;   // total width at the near edge. Non-zero is needed to
                                   // include tiles at the camera footprint — the apex pyramid
                                   // (nearWidth=0) leaves gaps where tile-center tests fail,
                                   // and static 3D objects anchored to those tiles pop in/out.
                                   // 800 covers the tile footprint under and around the camera.
    // Fog: single toggle, defaults to on (matches Orbital's vanilla behavior).
    // Applied whenever the Orbital override is enabled.
    bool  fogOn            = true;
    float fogStartPct      = 1.00f;
    float fogEndPct        = 1.25f;
};

class CDevEditorUI
{
public:
    static CDevEditorUI& GetInstance();

    void Render(bool* p_open);

    // Per-camera override accessors. Default drives the real tuning panel;
    // Orbital's is scaffolding with no sliders attached yet.
    const DevEditorDefaultCameraOverride& GetDefaultOverride() const { return m_DefaultOverride; }
    const DevEditorOrbitalCameraOverride& GetOrbitalOverride() const { return m_OrbitalOverride; }

    // Applies the enabled Default override onto `cfg` in-place (far / near / terrainCull only).
    // Trapezoid width multipliers and position offset are read separately.
    void ApplyDefaultOverrideToConfig(CameraConfig& cfg) const;
    // Orbital: no-op today (struct has no config-driving fields). Kept so the
    // extern-C dispatcher has a target and won't need refactoring later.
    void ApplyOrbitalOverrideToConfig(CameraConfig& cfg) const;

    // Offline authoring mode: gameplay-affecting client/server bridges are
    // muted whenever a custom slot is bound. Outbound movement packets stop
    // going to the server, and inbound Hero position corrections from the
    // server are dropped — so we can paint terrain attributes and walk the
    // new layout without the live world fighting back. Triggered/cleared by
    // the File menu (New / Load Custom set it; Load Classic clears it).
    bool IsOfflineAuthoring() const { return m_CurrentCustomMapId >= 0; }

    // Any editor brush is consuming left-clicks this frame — paint,
    // place, or delete. The engine's click-to-move / attack gates swallow
    // LMB while this is true. (Function name is historic — predates the
    // place + delete modes; semantics now cover all three.)
    bool IsPaintingTerrain() const
    {
        return m_BrushPaintOnDrag
            || m_PlaceOnClickEnabled
            || m_DeleteOnClickEnabled
            || m_PaintTextureOnDrag;
    }

    // Render toggle accessors
    bool ShouldRenderTerrain() const { return m_RenderTerrain; }
    bool ShouldRenderStaticObjects() const { return m_RenderStaticObjects; }
    bool ShouldRenderEffects() const { return m_RenderEffects; }
    bool ShouldRenderDroppedItems() const { return m_RenderDroppedItems; }
    bool ShouldRenderItemLabels() const { return m_RenderItemLabels; }
    bool ShouldRenderEquippedItems() const { return m_RenderEquippedItems; }
    bool ShouldRenderWeatherEffects() const { return m_RenderWeatherEffects; }
    bool ShouldRenderUI() const { return m_RenderUI; }
    // Not implemented (for future):
    bool ShouldRenderHero() const { return m_RenderHero; }
    bool ShouldRenderNPCs() const { return m_RenderNPCs; }
    bool ShouldRenderMonsters() const { return m_RenderMonsters; }
    bool ShouldRenderShaders() const { return m_RenderShaders; }
    bool ShouldRenderSkillEffects() const { return m_RenderSkillEffects; }


    // Debug visualization accessors
    bool ShouldShowCharacterPickBoxes() const { return m_ShowCharacterPickBoxes; }
    bool ShouldShowItemPickBoxes() const { return m_ShowItemPickBoxes; }
    bool ShouldShowItemCullSphere() const { return m_ShowItemCullSphere; }
    bool ShouldShowTileGrid() const { return m_ShowTileGrid; }

    // Culling radius accessors
    float GetCullRadiusItem() const { return m_CullRadiusItem; }


private:
    CDevEditorUI() = default;
    ~CDevEditorUI() = default;

    void RenderScenesTab();
    void RenderGraphicsTab();

    // RenderScenesTab sections
    void RenderCameraModeControls();
    void RenderCameraSummaryLine(int cameraMode);
    void RenderLoginSceneSection();
    void RenderGameSceneSection(int cameraMode, class ICamera* currentCamera);
    void RenderDefaultCameraOverridePanel();
    void RenderOrbitalCameraOverridePanel();
    void RenderScenesDebugSection();

    // RenderGraphicsTab section
    void RenderGraphicsDebugInfo();

    // Map authoring (File menu + Terrain Painter tab). The IO itself
    // lives in MuEditor::CustomMap; these methods are pure UI glue.
    void RenderFileMenuBar();
    void RenderFileMenuModals();
    void RenderOfflineAuthoringBanner();
    void RenderSourcesPanel();
    void RenderPlaceObjectPanel();
    void RenderDeleteObjectPanel();
    void HandlePlaceObjectInput();
    void HandleDeleteObjectInput();
    void HandlePaintTextureInput();
    void RenderTexturePainterPanel();
    void HidePlacementPreview();    // call when mode exits or source vanishes
    void ClearDeleteHoverPreview(); // restores alpha on the current hover, nulls it
    void PushUndo(DevEditorUndoAction action);
    void PerformUndo();
    void RenderUndoControls();
    // Used by the mode radio buttons — keeps paint / place / delete
    // mutually exclusive so a single LMB click never triggers two of them.
    void SetExclusiveBrushMode(int mode);
    void RenderNewMapModal();
    void RenderLoadMapModal();
    void RenderTerrainPainterTab();
    void HandlePaintBrushInput();

    // Currently-authored custom-map slot. -1 = no slot bound; Save Map is
    // disabled until either a New Map is created or a custom Load picks a
    // slot. Loading a classic world deliberately resets this back to -1
    // (we don't want one-click "Save Map" overwriting shipping assets).
    int  m_CurrentCustomMapId = -1;

    // Modal trigger state. The menu callback only sets these flags; the
    // actual ImGui::OpenPopup() call happens at window scope after the menu
    // bar ends, so the popup ID matches what BeginPopupModal looks up.
    // OpenPopup-from-inside-BeginMenu registers the id under the menu's ID
    // stack, which BeginPopupModal at window scope can't find.
    bool m_RequestOpenNewMap  = false;
    bool m_RequestOpenLoadMap = false;
    int  m_NewMapIdInput      = 64;

    // Terrain Painter brush state.
    // m_BrushAttr is a single TW_* low-byte attribute (AddTerrainAttribute
    // takes BYTE, so the high-byte attrs TW_NOATTACKZONE / TW_ATT1..7 are
    // out of scope for this brush — would need a WORD-flavored helper).
    int  m_BrushAttrIndex     = 1;   // index into kBrushAttributes[]
    bool m_BrushSubtractMode  = false;
    int  m_BrushRadius        = 1;
    bool m_BrushPaintOnDrag   = false;

    // Source-bank picker state (Sources panel). The input is the
    // 1-based World folder index of the bank to side-load on "Add".
    int  m_AddSourceWorldInput  = 33;       // Aida-ish default

    // Object placement state (Place panel).
    // m_PlaceSourceWorld: which currently-loaded source's bank to draw
    //   from. -1 = none chosen (or no banks loaded).
    // m_PlaceLocalType: 0..159 — the slot within the source bank.
    // Scale/AngleZ shared by every placed object until the user changes
    // them; angle is in degrees, 0 = forward, rotation around Z (yaw).
    int   m_PlaceSourceWorld    = -1;
    int   m_PlaceLocalType      = 0;
    float m_PlaceScale          = 1.0f;
    float m_PlaceAngleZ         = 0.0f;
    bool  m_PlaceOnClickEnabled = false;
    // Case-insensitive substring filter for the slot picker grid.
    // Matches against BMD::Name and Textures[].FileName so a user can
    // type e.g. "tree" or ".tga" to narrow the 160-cell grid down to
    // visually-related models. Empty = show all.
    char  m_PlaceFilter[64]     = "";

    // Delete tool: click on the world; nearest live OBJECT within
    // m_DeleteRadius (world units) gets its Live flipped to false.
    // m_DeleteHoverTarget is recomputed every frame in delete mode and
    // surfaces the find-nearest result so the engine can draw a red
    // highlight box on it (avoids click-and-pray deletions).
    bool  m_DeleteOnClickEnabled = false;
    float m_DeleteRadius         = 150.0f;
    class OBJECT* m_DeleteHoverTarget = nullptr;
public:
    OBJECT* GetDeleteHoverTarget() const { return m_DeleteHoverTarget; }
private:

    // Texture painter: writes TerrainMappingLayer1[tile] = the selected
    // BITMAP_MAPTILE offset (0..29). The .map file already round-trips
    // this; no save plumbing changes needed. Brush radius reuses
    // m_BrushRadius from the attribute painter so a single value
    // controls every painter mode.
    bool  m_PaintTextureOnDrag   = false;
    int   m_TextureBrushIndex    = 0;     // 0..29 = BITMAP_MAPTILE + N
    // 0 = Eraser (fades TerrainMappingAlpha back to 0, revealing Layer1)
    // 1 = Base    (sets TerrainMappingLayer1; alpha stays / resets to 0)
    // 2 = Overlay (sets TerrainMappingLayer2 + writes Alpha; the soft
    //              brush below produces the smooth-blended look)
    int   m_TextureBrushLayer    = 1;
    bool  m_TextureBrushSoft     = true;
    float m_TextureBrushStrength = 1.0f;  // max alpha for Overlay mode

    // Ghost preview: while place mode is active, we instantiate an OBJECT
    // via CreateObject and continuously update its position/rotation/etc.
    // to follow the cursor at 50% alpha. On left-click we "commit" by
    // bumping its alpha to 1.0 and forgetting the pointer; the next
    // frame spawns a fresh preview. On mode-exit / source-unload we
    // flip Live=false to hide it.
    class OBJECT* m_PlacementPreview = nullptr;

    // Bounded undo stack — small enough that a casual misclick is
    // recoverable; cap keeps the OBJECT* pointer set from growing
    // forever in a long authoring session.
    std::vector<DevEditorUndoAction> m_UndoStack;

    // Tile attribute overlay — translucent colored quads on each tile
    // whose attribute matches one of the bits in m_OverlayAttrMask.
    // Render hook is DevEditor_RenderTileAttributeOverlay (called from
    // MainScene after the terrain pass). Defaults: SAFEZONE | NOMOVE |
    // NOGROUND, which is the everyday "is this tile broken?" set.
    bool m_ShowAttrOverlay    = false;
    int  m_OverlayAttrMask    = TW_SAFEZONE | TW_NOMOVE | TW_NOGROUND;

public:
    bool ShouldRenderTileAttributeOverlay() const { return m_ShowAttrOverlay; }
    int  GetOverlayAttributeMask() const          { return m_OverlayAttrMask; }
private:

    DevEditorDefaultCameraOverride m_DefaultOverride;
    DevEditorOrbitalCameraOverride m_OrbitalOverride;


    // Render toggle flags
    // Working toggles
    bool m_RenderTerrain = true;          // ✓ WORKING - Ground tiles
    bool m_RenderStaticObjects = true;    // ✓ WORKING - Trees, stones, walls
    bool m_RenderEffects = true;          // ✓ WORKING - Particles, magic
    bool m_RenderDroppedItems = true;     // ✓ WORKING - Ground loot
    bool m_RenderWeatherEffects = true;   // ✓ WORKING - Rain, snow, leaves, mist
    bool m_RenderItemLabels = true;       // ✓ WORKING - Item text boxes on ground

    // Not working (too complex - tested and failed)
    bool m_RenderShaders = true;          // ✗ NOT WORKING - Too low level
    bool m_RenderSkillEffects = true;     // ✗ NOT WORKING - Too complex
    bool m_RenderEquippedItems = true;    // ✗ NOT WORKING - Equipment on characters (too complex)
    bool m_RenderUI = true;               // ✗ NOT WORKING - Interface (needs extensive grouping)

    // Not implemented (too complex)
    bool m_RenderHero = true;             // NOT IMPLEMENTED
    bool m_RenderNPCs = true;             // NOT IMPLEMENTED
    bool m_RenderMonsters = true;         // NOT IMPLEMENTED

    // Debug visualization flags
    bool m_ShowCharacterPickBoxes = false;  // Wireframe of OBB used by SelectCharacter
    bool m_ShowItemPickBoxes = false;       // Wireframe of OBB used by SelectItem
    bool m_ShowItemCullSphere = false;      // Wireframe of frustum-cull sphere for items
    bool m_ShowTileGrid = false;

    // Runtime adjustable culling radii
    float m_CullRadiusItem = 100.0f;

    // Login scene render distances (adjustable via sliders).
    // Defaults live in LoginSceneCameraDefaults (CameraMove.h) so the non-editor
    // fallback in ZzzObject.cpp uses the same source of truth.
    float m_LoginTerrainDist = LoginSceneCameraDefaults::RENDER_TERRAIN_DIST;
    float m_LoginObjectDist  = LoginSceneCameraDefaults::RENDER_OBJECT_DIST;

public:
    // Login scene accessors
    float GetLoginTerrainDist() const { return m_LoginTerrainDist; }
    float GetLoginObjectDist() const { return m_LoginObjectDist; }
};

#define g_DevEditorUI CDevEditorUI::GetInstance()

#endif // _EDITOR
