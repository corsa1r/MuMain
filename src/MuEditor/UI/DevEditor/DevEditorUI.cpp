#include "stdafx.h"

#ifdef _EDITOR

#include "DevEditorUI.h"
#include "imgui.h"
#include "Data/Translation/i18n.h"
#include "Camera/CameraManager.h"
#include "Camera/CameraMode.h"
#include "Camera/CameraConfig.h"
#include "Camera/OrbitalCamera.h"
#include "Camera/FreeFlyCamera.h"
#include "Camera/CameraMove.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Data/GameConfig/GameConfig.h"
#include "UI/Console/MuEditorConsoleUI.h"

// Map authoring
#include "CustomMap/CustomMapIO.h"
#include "CustomMap/SourceBank.h"
#include "Core/Globals/_define.h"          // TW_* flags, TERRAIN_SIZE
#include "Render/Terrain/ZzzLodTerrain.h"  // AddTerrainAttribute, SelectXF/YF
#include "Render/Textures/ZzzOpenglUtil.h" // EnableAlphaBlend / DisableTexture etc.
#include "Engine/Object/ZzzObject.h"       // CreateObject, ObjectBlock
#include "Engine/Object/w_ObjectInfo.h"    // OBJECT
#include "Render/Models/ZzzBMD.h"          // BMD, Models[]
#include "Render/Sprites/GlobalBitmap.h"   // CGlobalBitmap, BITMAP_t
#include <gl/GL.h>

extern CGlobalBitmap Bitmaps;

#include <cstdio>
#include <cctype>
#include <cmath>

// Cursor-on-terrain picking state set by the terrain renderer each frame.
extern bool SelectFlag;

// Engine edit-mode flag. When non-EDIT_NONE, MainScene runs an extra
// RenderTerrain(true) pass each frame that ray-casts the mouse against
// terrain tiles and updates SelectXF/SelectYF. We toggle this between
// EDIT_WALL and EDIT_NONE based on the paint-on-drag state so the brush
// always reads the current cursor tile, not a stale value from the last
// click-to-move.
extern int EditFlag;

// External C functions
extern "C" CameraManager& CameraManager_Instance();
extern "C" OrbitalCamera* GetOrbitalCameraInstance();

// External camera state
extern CameraState g_Camera;

// Forward declarations for camera accessors
extern "C" int GetCurrentCameraMode();
extern "C" float GetOrbitalCameraRadius();
extern "C" void GetOrbitalCameraAngles(float* outYaw, float* outPitch);

// CCameraMove wrapper
extern "C" CCameraMove* CCameraMove__GetInstancePtr();

// CHARACTER external
extern CHARACTER* Hero;

namespace
{
    // Horizontal FOV slider range (degrees). Matches the CameraConfig clamp used elsewhere.
    constexpr float MIN_HFOV = 10.0f;
    constexpr float MAX_HFOV = 150.0f;

    // Camera mode enum values as exposed by GetCurrentCameraMode()
    constexpr int CAMERA_MODE_DEFAULT = 0;
    constexpr int CAMERA_MODE_ORBITAL = 1;
    constexpr int CAMERA_MODE_FREEFLY = 2;

    // Login-scene render-distance slider range (world units)
    constexpr float LOGIN_DIST_MIN = 1000.0f;
    constexpr float LOGIN_DIST_MAX = 30000.0f;

    // Custom resolution input clamping (pixels)
    constexpr int CUSTOM_RES_MAX_WIDTH  = 3840;
    constexpr int CUSTOM_RES_MAX_HEIGHT = 2160;

    // World-to-tile scale (100 world units = 1 tile)
    constexpr float WORLD_TO_TILE_DIVISOR = 100.0f;

    // Modal popup ids — ImGui uses the string as both the lookup key and
    // the visible title bar text. The "###" suffix keeps the ID stable
    // while the leading text becomes the user-facing title (looked up via
    // EDITOR_TEXT at render time).
    constexpr const char* NEW_MAP_POPUP_ID  = "###dev_new_map_modal";
    constexpr const char* LOAD_MAP_POPUP_ID = "###dev_load_map_modal";

    // Custom-map slot id input bounds. Matches CustomMapIO's BYTE header field;
    // also keeps us clear of the 0..4 range whose .att files trip the classic
    // loader's per-world sentinel tile checks.
    constexpr int CUSTOM_MAP_ID_INPUT_MIN = 5;
    constexpr int CUSTOM_MAP_ID_INPUT_MAX = 254;

    // Brush options. Restricted to the low-byte attrs because the legacy
    // AddTerrainAttribute(int,int,BYTE) helper truncates anything wider.
    struct BrushAttribute
    {
        WORD        bits;
        const char* label;
    };
    constexpr BrushAttribute kBrushAttributes[] =
    {
        { TW_SAFEZONE, "TW_SAFEZONE  (safe zone)"   },
        { TW_NOMOVE,   "TW_NOMOVE    (blocked)"     },
        { TW_NOGROUND, "TW_NOGROUND  (void)"        },
        { TW_WATER,    "TW_WATER     (water)"       },
        { TW_ACTION,   "TW_ACTION    (action tile)" },
        { TW_HEIGHT,   "TW_HEIGHT    (height pin)"  },
        { TW_CAMERA_UP,"TW_CAMERA_UP (camera lift)" },
    };
    constexpr int kBrushAttributeCount =
        static_cast<int>(sizeof(kBrushAttributes) / sizeof(kBrushAttributes[0]));

    // Painter brush limits.
    constexpr int  BRUSH_RADIUS_MIN = 1;
    constexpr int  BRUSH_RADIUS_MAX = 16;

    // Attribute-overlay color table. One entry per displayable TW_* bit.
    // First matching bit wins per tile (no blending — keeps the visual
    // unambiguous when a tile carries multiple attributes).
    struct OverlayAttribute
    {
        WORD        bit;
        float       r, g, b;
        const char* label;
    };
    constexpr OverlayAttribute kOverlayAttrs[] =
    {
        { TW_SAFEZONE,  0.20f, 1.00f, 0.20f, "TW_SAFEZONE   (green)"    },
        { TW_NOMOVE,    1.00f, 0.20f, 0.20f, "TW_NOMOVE     (red)"      },
        { TW_NOGROUND,  0.10f, 0.10f, 0.10f, "TW_NOGROUND   (black)"    },
        { TW_WATER,     0.30f, 0.50f, 1.00f, "TW_WATER      (blue)"     },
        { TW_ACTION,    1.00f, 1.00f, 0.20f, "TW_ACTION     (yellow)"   },
        { TW_HEIGHT,    1.00f, 0.40f, 1.00f, "TW_HEIGHT     (magenta)"  },
        { TW_CAMERA_UP, 0.20f, 1.00f, 1.00f, "TW_CAMERA_UP  (cyan)"     },
    };
    constexpr int kOverlayAttrCount =
        static_cast<int>(sizeof(kOverlayAttrs) / sizeof(kOverlayAttrs[0]));

    // Overlay render tuning.
    constexpr int   OVERLAY_CULL_RADIUS = 40;     // tiles around hero
    constexpr float OVERLAY_ALPHA       = 0.45f;
    constexpr float OVERLAY_Z_OFFSET    = 5.0f;   // sit just above the terrain

    // Classic world picker — keep the dialog focused on the maps an authoring
    // workflow actually starts from. Folder index is 1-based (matches the
    // "World<n>" directory naming).
    struct ClassicWorld
    {
        int         folderIndex;
        const char* label;
    };
    // World name table, organized by WD_* enum order. Folder index is
    // the 1-based "World<N>" directory (WorldActive + 1). When a folder
    // ships but isn't in this table, the Sources combo falls back to
    // a bare "World<N>" label so private-server maps stay selectable.
    constexpr ClassicWorld kClassicWorlds[] =
    {
        // Continents (WD_0..WD_10)
        {  1, "World1   Lorencia"          },
        {  2, "World2   Dungeon"           },
        {  3, "World3   Devias"            },
        {  4, "World4   Noria"             },
        {  5, "World5   Lost Tower"        },
        {  7, "World7   Stadium / Arena"   },
        {  8, "World8   Atlans"            },
        {  9, "World9   Tarkan"            },
        { 10, "World10  Devil Square"      },
        { 11, "World11  Icarus / Heaven"   },

        // Event maps (Blood Castle 12..18, Chaos Castle 19..25)
        { 12, "World12  Blood Castle 1"    },
        { 13, "World13  Blood Castle 2"    },
        { 14, "World14  Blood Castle 3"    },
        { 15, "World15  Blood Castle 4"    },
        { 16, "World16  Blood Castle 5"    },
        { 17, "World17  Blood Castle 6"    },
        { 18, "World18  Blood Castle 7"    },
        { 19, "World19  Chaos Castle 1"    },

        // Hellas / Battle Castle
        { 25, "World25  Hellas"            },
        { 31, "World31  Battle Castle"     },
        { 32, "World32  Hunting Ground"    },

        // Season 3+ continents
        { 34, "World34  Aida"              },
        { 35, "World35  Crywolf 1st"       },
        { 36, "World36  Crywolf 2nd"       },
        { 38, "World38  Kanturu Ruins"     },
        { 39, "World39  Kanturu 2nd"       },
        { 40, "World40  Kanturu Relics"    },
        { 41, "World41  GM Area"           },

        // Cursed Temple Lv1..Lv6
        { 46, "World46  Cursed Temple 1"   },
        { 47, "World47  Cursed Temple 2"   },
        { 48, "World48  Cursed Temple 3"   },
        { 49, "World49  Cursed Temple 4"   },
        { 50, "World50  Cursed Temple 5"   },
        { 51, "World51  Cursed Temple 6"   },

        // Season 6+ continents and instances
        { 52, "World52  Elveland (S6 Home)"},
        { 53, "World53  Blood Castle ML"   },
        { 54, "World54  Chaos Castle ML"   },
        { 57, "World57  Swamp of Calmness" },
        { 58, "World58  Raklion / Ice City"},
        { 59, "World59  Raklion Boss"      },
        { 63, "World63  Santa Town"        },
        { 64, "World64  PK Field"          },
        { 65, "World65  Duel Arena"        },

        // Doppelganger 1..4
        { 66, "World66  Doppelganger 1"    },
        { 67, "World67  Doppelganger 2"    },
        { 68, "World68  Doppelganger 3"    },
        { 69, "World69  Doppelganger 4"    },

        // Empire Guardian 1..4
        { 70, "World70  Empire Guardian 1" },
        { 71, "World71  Empire Guardian 2" },
        { 72, "World72  Empire Guardian 3" },
        { 73, "World73  Empire Guardian 4" },

        // Later utility / Karutan
        { 74, "World74  New Login Scene"   },
        { 75, "World75  New Character Sel" },
        { 80, "World80  United Marketplace"},
        { 81, "World81  Karutan 1"         },
        { 82, "World82  Karutan 2"         },
    };
    constexpr int kClassicWorldCount =
        static_cast<int>(sizeof(kClassicWorlds) / sizeof(kClassicWorlds[0]));
}

CDevEditorUI& CDevEditorUI::GetInstance()
{
    static CDevEditorUI instance;
    return instance;
}

void CDevEditorUI::ApplyOrbitalOverrideToConfig(CameraConfig& cfg) const
{
    (void)cfg;
    // No Orbital config fields are exposed yet; this hook is here so the
    // extern-C dispatcher has somewhere to route once sliders are added.
}

void CDevEditorUI::ApplyDefaultOverrideToConfig(CameraConfig& cfg) const
{
    if (!m_DefaultOverride.enabled) return;
    // hFov / terrainCullRange intentionally NOT overridden: the Default camera
    // derives its horizontal extent and terrain cull shape from a hardcoded 2D
    // trapezoid (see WidthFar/WidthNear in ZzzLodTerrain.cpp), not from a
    // symmetric FOV or a terrainCullRange radius. Width multipliers (exposed
    // via DevEditor_GetDefaultTrapezoidMultipliers) are the real knobs there.
    cfg.nearPlane        = m_DefaultOverride.nearPlane;
    cfg.farPlane         = m_DefaultOverride.farPlane;
}

void CDevEditorUI::Render(bool* p_open)
{
    if (!p_open || !*p_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(EDITOR_TEXT("label_dev_editor_title"),
                      p_open,
                      ImGuiWindowFlags_MenuBar))
    {
        ImGui::End();
        // Painter input must still run when window is collapsed — the user
        // may have enabled paint-on-drag from a previous frame.
        HandlePaintBrushInput();
        return;
    }

    RenderFileMenuBar();
    RenderFileMenuModals();   // Same Begin/End scope as the menu callback.
    RenderOfflineAuthoringBanner();

    // Tab bar
    if (ImGui::BeginTabBar("DevEditorTabs"))
    {
        if (ImGui::BeginTabItem(EDITOR_TEXT("dev_tab_scenes")))
        {
            RenderScenesTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(EDITOR_TEXT("dev_tab_graphics")))
        {
            RenderGraphicsTab();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(EDITOR_TEXT("dev_tab_terrain_painter")))
        {
            RenderTerrainPainterTab();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
    HandlePaintBrushInput();
}

void CDevEditorUI::RenderScenesTab()
{
    extern EGameScene SceneFlag;
    auto& camMgr = CameraManager_Instance();
    int cameraMode = GetCurrentCameraMode();

    // In FreeFly mode we edit the spectated camera's config, otherwise the active one.
    ICamera* currentCamera = camMgr.GetActiveCamera();
    if (camMgr.GetCurrentMode() == CameraMode::FreeFly)
    {
        if (ICamera* spectated = camMgr.GetSpectatedCamera())
            currentCamera = spectated;
    }

    RenderCameraModeControls();
    RenderCameraSummaryLine(cameraMode);
    ImGui::Separator();

    if (SceneFlag == LOG_IN_SCENE && ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_login_scene")))
        RenderLoginSceneSection();

    if (SceneFlag == CHARACTER_SCENE && ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_character_scene")))
    {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", EDITOR_TEXT("dev_msg_nothing_here"));
        ImGui::Unindent();
    }

    if (SceneFlag == MAIN_SCENE && ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_game_scene")))
        RenderGameSceneSection(cameraMode, currentCamera);

    if (ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_debug")))
        RenderScenesDebugSection();
}

void CDevEditorUI::RenderCameraModeControls()
{
    auto& camMgr = CameraManager::Instance();
    const bool isFreeFly = (camMgr.GetCurrentMode() == CameraMode::FreeFly);

    if (!isFreeFly)
    {
        if (ImGui::Button(EDITOR_TEXT("dev_btn_switch_to_freefly"), ImVec2(250, 0)))
            camMgr.SetCameraMode(CameraMode::FreeFly);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", camMgr.GetActiveCamera()->GetName());
        return;
    }

    if (ImGui::Button(EDITOR_TEXT("dev_btn_switch_to_game_camera"), ImVec2(250, 0)))
    {
        ICamera* spectated = camMgr.GetSpectatedCamera();
        CameraMode target = CameraMode::Default;
        if (spectated && strcmp(spectated->GetName(), "Orbital") == 0)
            target = CameraMode::Orbital;
        camMgr.SetCameraMode(target);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_freefly"));

    if (ICamera* spectated = camMgr.GetSpectatedCamera())
    {
        ImGui::Text("%s: %s", EDITOR_TEXT("dev_label_spectating"), spectated->GetName());
        ImGui::SameLine();
        vec3_t snapPos, snapAngle;
        if (camMgr.GetSpectatedCameraState(snapPos, snapAngle))
        {
            if (ImGui::Button(EDITOR_TEXT("dev_btn_snap_to_spectated")))
            {
                auto* freeFly = static_cast<FreeFlyCamera*>(camMgr.GetActiveCamera());
                freeFly->SnapToPosition(snapPos, snapAngle[2], snapAngle[0]);
            }
        }
    }
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", EDITOR_TEXT("dev_label_freefly_help"));
}

void CDevEditorUI::RenderCameraSummaryLine(int cameraMode)
{
    const char* modeName;
    switch (cameraMode)
    {
        case CAMERA_MODE_DEFAULT: modeName = EDITOR_TEXT("dev_label_camera_default"); break;
        case CAMERA_MODE_ORBITAL: modeName = EDITOR_TEXT("dev_label_camera_orbital"); break;
        case CAMERA_MODE_FREEFLY: modeName = EDITOR_TEXT("dev_label_freefly");        break;
        default:                  modeName = EDITOR_TEXT("dev_label_camera_unknown"); break;
    }
    ImGui::Text("%s | %s: %.0f, %.0f, %.0f | %s: (%d, %d) | %s: %.1f %s: %.1f",
                modeName,
                EDITOR_TEXT("dev_label_pos"),
                g_Camera.Position[0], g_Camera.Position[1], g_Camera.Position[2],
                EDITOR_TEXT("dev_label_tile"),
                (int)(g_Camera.Position[0] / WORLD_TO_TILE_DIVISOR),
                (int)(g_Camera.Position[1] / WORLD_TO_TILE_DIVISOR),
                EDITOR_TEXT("dev_label_pitch"), g_Camera.Angle[0],
                EDITOR_TEXT("dev_label_yaw"),   g_Camera.Angle[2]);
}

void CDevEditorUI::RenderLoginSceneSection()
{
    ImGui::Indent();

    ImGui::PushItemWidth(150);
    ImGui::InputFloat(EDITOR_TEXT("dev_label_offset_x"), &g_LoginSceneOffsetX, 50.0f, 200.0f, "%.1f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_offset_y"), &g_LoginSceneOffsetY, 50.0f, 200.0f, "%.1f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_offset_z"), &g_LoginSceneOffsetZ, 50.0f, 200.0f, "%.1f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_pitch"), &g_LoginSceneAnglePitch, 1.0f, 5.0f, "%.1f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_yaw"), &g_LoginSceneAngleYaw, 1.0f, 5.0f, "%.1f");
    ImGui::PopItemWidth();

    if (ImGui::Button(EDITOR_TEXT("dev_btn_reset_offsets")))
    {
        g_LoginSceneOffsetX   = LoginSceneCameraDefaults::OFFSET_X;
        g_LoginSceneOffsetY   = LoginSceneCameraDefaults::OFFSET_Y;
        g_LoginSceneOffsetZ   = LoginSceneCameraDefaults::OFFSET_Z;
        g_LoginSceneAnglePitch = LoginSceneCameraDefaults::ANGLE_PITCH;
        g_LoginSceneAngleYaw   = LoginSceneCameraDefaults::ANGLE_YAW;
    }

    ImGui::Spacing();

    // Tour mode controls
    if (CCameraMove* cameraMove = CCameraMove__GetInstancePtr())
    {
        BOOL isTourMode = cameraMove->IsTourMode();
        BOOL isTourPaused = cameraMove->IsTourPaused();

        ImGui::Text("%s: %s%s",
                    EDITOR_TEXT("dev_label_tour"),
                    isTourMode ? EDITOR_TEXT("dev_label_active") : EDITOR_TEXT("dev_label_inactive"),
                    (isTourMode && isTourPaused) ? EDITOR_TEXT("dev_label_paused_suffix") : "");

        if (isTourMode)
        {
            if (isTourPaused)
            {
                if (ImGui::Button(EDITOR_TEXT("dev_btn_resume"))) cameraMove->PauseTour(FALSE);
            }
            else
            {
                if (ImGui::Button(EDITOR_TEXT("dev_btn_pause"))) cameraMove->PauseTour(TRUE);
            }
            ImGui::SameLine();
            if (ImGui::Button(EDITOR_TEXT("dev_btn_restart")) && Hero)
            {
                cameraMove->SetTourMode(FALSE, FALSE, 0);
                cameraMove->PlayCameraWalk(Hero->Object.Position, 1000);
                cameraMove->SetTourMode(TRUE, FALSE, 0);
            }
        }
        else if (ImGui::Button(EDITOR_TEXT("dev_btn_start_tour")) && Hero)
        {
            cameraMove->PlayCameraWalk(Hero->Object.Position, 1000);
            cameraMove->SetTourMode(TRUE, FALSE, 0);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s", EDITOR_TEXT("dev_label_render_distances"));
    ImGui::PushItemWidth(200);
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_terrain_viewfar"), &m_LoginTerrainDist, LOGIN_DIST_MIN, LOGIN_DIST_MAX, "%.0f");
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_object_distance"), &m_LoginObjectDist, LOGIN_DIST_MIN, LOGIN_DIST_MAX, "%.0f");
    ImGui::PopItemWidth();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_reset_distances")))
    {
        m_LoginTerrainDist = LoginSceneCameraDefaults::RENDER_TERRAIN_DIST;
        m_LoginObjectDist  = LoginSceneCameraDefaults::RENDER_OBJECT_DIST;
    }

    ImGui::Unindent();
}

void CDevEditorUI::RenderGameSceneSection(int cameraMode, ICamera* currentCamera)
{
    ImGui::Indent();

    if (currentCamera)
    {
        const CameraConfig& cfg = currentCamera->GetConfig();
        ImGui::Text("%s: %.0f  %s: %.0f  %s: %.0f  %s: %.0f",
                    EDITOR_TEXT("dev_label_near"),    cfg.nearPlane,
                    EDITOR_TEXT("dev_label_far"),     cfg.farPlane,
                    EDITOR_TEXT("dev_label_viewfar"), g_Camera.ViewFar,
                    EDITOR_TEXT("dev_label_projfar"), g_Camera.ViewFar * RENDER_DISTANCE_MULTIPLIER);
    }

    // Route panel by the currently-focused camera name rather than camera mode,
    // so that when FreeFly is spectating Default/Orbital the override panel for
    // the spectated camera stays visible and editable.
    const char* focusedName = currentCamera ? currentCamera->GetName() : nullptr;
    const bool focusingDefault = focusedName && strcmp(focusedName, "Default") == 0;
    const bool focusingOrbital = focusedName && strcmp(focusedName, "Orbital") == 0;

    if (focusingDefault)
        RenderDefaultCameraOverridePanel();
    else if (focusingOrbital)
        RenderOrbitalCameraOverridePanel();
    else
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                           EDITOR_TEXT("dev_msg_switch_to_default_orbital"));

    if (focusingOrbital)
    {
        ImGui::Spacing();
        float radius = GetOrbitalCameraRadius();
        float orbitalYaw = 0.0f, orbitalPitch = 0.0f;
        GetOrbitalCameraAngles(&orbitalYaw, &orbitalPitch);
        ImGui::Text("%s: %s=%.0f  %s=%.1f  %s=%.1f",
                    EDITOR_TEXT("dev_label_camera_orbital"),
                    EDITOR_TEXT("dev_label_zoom"),  radius,
                    EDITOR_TEXT("dev_label_yaw"),   orbitalYaw,
                    EDITOR_TEXT("dev_label_pitch"), orbitalPitch);
    }

    ImGui::Unindent();
}

void CDevEditorUI::RenderDefaultCameraOverridePanel()
{
    DevEditorDefaultCameraOverride& ov = m_DefaultOverride;

    // PushID disambiguates widgets that share labels with the Orbital panel
    // (formerly the ##def suffix on each label) so translated labels stay clean.
    ImGui::PushID("def");

    ImGui::Checkbox(EDITOR_TEXT("dev_chk_override_default_camera"), &ov.enabled);
    if (!ov.enabled)
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                           EDITOR_TEXT("dev_msg_default_camera_help"));
        ImGui::PopID();
        return;
    }

    ImGui::PushItemWidth(200);

    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_view_frustum"));
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_far_plane"), &ov.farPlane, 500.0f, 20000.0f, "%.0f");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_camera_offset"));
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_offset_x"), &ov.offsetX, -2000.0f, 2000.0f, "%.0f");
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_offset_y"), &ov.offsetY, -2000.0f, 2000.0f, "%.0f");
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_offset_z"), &ov.offsetZ, -1000.0f, 1000.0f, "%.0f");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_culling_trapezoid_width"));
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_bottom_near_mul"), &ov.widthNearMul, 0.25f, 4.0f, "%.2f");
    ImGui::SliderFloat(EDITOR_TEXT("dev_label_top_far_mul"),     &ov.widthFarMul,  0.25f, 4.0f, "%.2f");

    ImGui::Spacing();
    extern bool FogEnable;
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_fog"));
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_override_fog"), &ov.fogOverride);
    if (ov.fogOverride)
    {
        ImGui::SameLine();
        ImGui::Checkbox(EDITOR_TEXT("dev_chk_fog_on"), &ov.fogOn);
    }
    ImGui::SameLine();
    ImGui::TextColored(FogEnable ? ImVec4(0.5f,1.0f,0.5f,1.0f) : ImVec4(1.0f,0.5f,0.5f,1.0f),
                       "%s", FogEnable ? EDITOR_TEXT("dev_label_on") : EDITOR_TEXT("dev_label_off"));
    float startDisp = ov.fogStartPct * 100.0f, endDisp = ov.fogEndPct * 100.0f;
    if (ImGui::SliderFloat(EDITOR_TEXT("dev_label_fog_start_pct"), &startDisp, 0.0f, 200.0f, "%.0f%%")) ov.fogStartPct = startDisp / 100.0f;
    if (ImGui::SliderFloat(EDITOR_TEXT("dev_label_fog_end_pct"),   &endDisp,   0.0f, 200.0f, "%.0f%%")) ov.fogEndPct   = endDisp   / 100.0f;
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f),
                       "%s: %.0f - %.0f (%s=%.0f)",
                       EDITOR_TEXT("dev_label_fog"),
                       g_Camera.ViewFar * ov.fogStartPct, g_Camera.ViewFar * ov.fogEndPct,
                       EDITOR_TEXT("dev_label_viewfar"), g_Camera.ViewFar);

    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_reset_camera_defaults")))
    {
        const CameraConfig cfg = CameraConfig::ForMainSceneDefaultCamera();
        ov.nearPlane = cfg.nearPlane;
        ov.farPlane  = cfg.farPlane;
        ov.offsetX = ov.offsetY = ov.offsetZ = 0.0f;
        ov.widthNearMul = ov.widthFarMul = 1.0f;
        ov.fogStartPct = 1.00f;
        ov.fogEndPct   = 1.25f;
    }

    ImGui::PopID();
}

void CDevEditorUI::RenderOrbitalCameraOverridePanel()
{
    DevEditorOrbitalCameraOverride& ov = m_OrbitalOverride;

    // Seed the trapezoid from the natural view pyramid when the override is
    // first enabled. This way enabling at "defaults" == identical hull to
    // override-off, and user sees zero visible change until they touch a slider.
    auto seedFromNaturalPyramid = [&ov]()
    {
        if (ICamera* cam = CameraManager::Instance().GetActiveCamera())
        {
            const CameraConfig& cfg = cam->GetConfig();
            extern unsigned int WindowWidth, WindowHeight;
            const float aspect = (float)WindowWidth / (float)WindowHeight;
            const float vFov = HFovToVFov(cfg.hFov, aspect);
            const float tanHalf = tanf(vFov * 0.5f * Q_PI / 180.0f);
            ov.farDist   = cfg.terrainCullRange;
            ov.farWidth  = 2.0f * tanHalf * cfg.terrainCullRange * aspect;
            ov.nearDist  = 0.0f;
            ov.nearWidth = 800.0f;  // covers camera footprint so static 3D objects don't pop
        }
    };

    ImGui::PushID("orb");

    static bool s_wasEnabled = false;
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_override_orbital_camera"), &ov.enabled);
    if (ov.enabled && !s_wasEnabled) seedFromNaturalPyramid();
    s_wasEnabled = ov.enabled;

    if (!ov.enabled)
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                           EDITOR_TEXT("dev_msg_orbital_camera_help"));
        ImGui::PopID();
        return;
    }

    ImGui::PushItemWidth(200);

    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_culling_trapezoid"));
    // InputFloat: type any value directly, or use the +/- buttons (step = fine, Ctrl+click = coarse).
    ImGui::InputFloat(EDITOR_TEXT("dev_label_far_distance"),       &ov.farDist,   100.0f, 500.0f, "%.0f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_top_far_width"),      &ov.farWidth,  100.0f, 500.0f, "%.0f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_near_distance"),      &ov.nearDist,   50.0f, 250.0f, "%.0f");
    ImGui::InputFloat(EDITOR_TEXT("dev_label_bottom_near_width"),  &ov.nearWidth,  50.0f, 250.0f, "%.0f");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                       EDITOR_TEXT("dev_msg_view_aligned"));

    ImGui::Spacing();
    extern bool FogEnable;
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "%s", EDITOR_TEXT("dev_label_fog"));
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_fog_on"), &ov.fogOn);
    ImGui::SameLine();
    ImGui::TextColored(FogEnable ? ImVec4(0.5f,1.0f,0.5f,1.0f) : ImVec4(1.0f,0.5f,0.5f,1.0f),
                       "%s", FogEnable ? EDITOR_TEXT("dev_label_on") : EDITOR_TEXT("dev_label_off"));
    float startDisp = ov.fogStartPct * 100.0f, endDisp = ov.fogEndPct * 100.0f;
    if (ImGui::InputFloat(EDITOR_TEXT("dev_label_fog_start_pct"), &startDisp, 5.0f, 25.0f, "%.0f%%")) ov.fogStartPct = startDisp / 100.0f;
    if (ImGui::InputFloat(EDITOR_TEXT("dev_label_fog_end_pct"),   &endDisp,   5.0f, 25.0f, "%.0f%%")) ov.fogEndPct   = endDisp   / 100.0f;
    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f),
                       "%s: %.0f - %.0f (%s=%.0f)",
                       EDITOR_TEXT("dev_label_fog"),
                       g_Camera.ViewFar * ov.fogStartPct, g_Camera.ViewFar * ov.fogEndPct,
                       EDITOR_TEXT("dev_label_viewfar"), g_Camera.ViewFar);

    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_reset_natural_pyramid")))
    {
        seedFromNaturalPyramid();
        ov.fogStartPct = 1.00f;
        ov.fogEndPct   = 1.25f;
    }

    ImGui::PopID();
}

void CDevEditorUI::RenderScenesDebugSection()
{
    ImGui::Indent();

    // Debug Visualization — wireframes overlaid on the scene
    ImGui::Text("%s", EDITOR_TEXT("dev_label_debug_visualization"));
    ImGui::Columns(2, nullptr, false);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_character_pick_boxes"), &m_ShowCharacterPickBoxes);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_item_pick_boxes"),      &m_ShowItemPickBoxes);
    ImGui::NextColumn();
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_item_cull_sphere"),     &m_ShowItemCullSphere);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_tile_grid"),            &m_ShowTileGrid);
    ImGui::Columns(1);

    ImGui::PushItemWidth(150);
    ImGui::InputFloat(EDITOR_TEXT("dev_label_item_cull_radius"), &m_CullRadiusItem, 10.0f, 50.0f, "%.1f");
    if (m_CullRadiusItem < 0.0f) m_CullRadiusItem = 0.0f;
    ImGui::PopItemWidth();

    ImGui::Spacing();

    if (OrbitalCamera* orbitalCam = GetOrbitalCameraInstance())
    {
        vec3_t target = {0, 0, 0};
        orbitalCam->GetTargetPosition(target);
        ImGui::Text("%s: %.0f, %.0f, %.0f  %s: (%d, %d)",
                    EDITOR_TEXT("dev_label_orbital_target"),
                    target[0], target[1], target[2],
                    EDITOR_TEXT("dev_label_tile"),
                    (int)(target[0] / WORLD_TO_TILE_DIVISOR),
                    (int)(target[1] / WORLD_TO_TILE_DIVISOR));
    }

    // Rendering — toggles for what gets drawn each frame
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("%s", EDITOR_TEXT("dev_label_rendering"));

    ImGui::Columns(2, nullptr, false);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_terrain"), &m_RenderTerrain);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_static_objects"), &m_RenderStaticObjects);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_effects"), &m_RenderEffects);
    ImGui::NextColumn();
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_dropped_items"), &m_RenderDroppedItems);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_weather"), &m_RenderWeatherEffects);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_item_labels"), &m_RenderItemLabels);
    ImGui::Columns(1);

    if (ImGui::Button(EDITOR_TEXT("dev_btn_all_on")))
    {
        m_RenderTerrain = m_RenderStaticObjects = m_RenderEffects = true;
        m_RenderDroppedItems = m_RenderWeatherEffects = m_RenderItemLabels = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_all_off")))
    {
        m_RenderTerrain = m_RenderStaticObjects = m_RenderEffects = false;
        m_RenderDroppedItems = m_RenderWeatherEffects = m_RenderItemLabels = false;
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", EDITOR_TEXT("dev_label_todo_not_working"));
    ImGui::BeginDisabled();
    ImGui::Columns(2, nullptr, false);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_shaders"), &m_RenderShaders);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_skill_effects"), &m_RenderSkillEffects);
    ImGui::NextColumn();
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_equipped_items"), &m_RenderEquippedItems);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_ui"), &m_RenderUI);
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", EDITOR_TEXT("dev_label_todo_not_implemented"));
    ImGui::Columns(3, nullptr, false);
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_hero"), &m_RenderHero);
    ImGui::NextColumn();
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_npcs"), &m_RenderNPCs);
    ImGui::NextColumn();
    ImGui::Checkbox(EDITOR_TEXT("dev_chk_monsters"), &m_RenderMonsters);
    ImGui::Columns(1);
    ImGui::EndDisabled();

    ImGui::Unindent();
}

// Shared helper: applies a new window size. Used by preset buttons, custom-size apply,
void CDevEditorUI::RenderGraphicsTab()
{
    ImGui::Text("%s", EDITOR_TEXT("dev_label_graphics_debug_info"));
    ImGui::Separator();

    RenderGraphicsDebugInfo();
}

void CDevEditorUI::RenderGraphicsDebugInfo()
{
    extern unsigned int WindowWidth, WindowHeight;
    extern BOOL g_bUseWindowMode;
    extern HWND g_hWnd;
    extern int OpenglWindowWidth, OpenglWindowHeight;
    extern float g_fScreenRate_x, g_fScreenRate_y;

    ImGui::Text("%s: %u x %u", EDITOR_TEXT("dev_label_current_resolution"), WindowWidth, WindowHeight);
    ImGui::Text("%s: %d x %d", EDITOR_TEXT("dev_label_opengl_viewport"), OpenglWindowWidth, OpenglWindowHeight);
    ImGui::Text("%s: %.2f x %.2f", EDITOR_TEXT("dev_label_screen_rate"), g_fScreenRate_x, g_fScreenRate_y);
    ImGui::Text("%s: %s", EDITOR_TEXT("dev_label_window_mode"),
                g_bUseWindowMode ? EDITOR_TEXT("dev_label_windowed") : EDITOR_TEXT("dev_label_fullscreen"));

    int clientWidth = 0, clientHeight = 0;
    float calculatedScaleX = 0, calculatedScaleY = 0;
    if (g_hWnd)
    {
        RECT clientRect;
        GetClientRect(g_hWnd, &clientRect);
        clientWidth  = clientRect.right  - clientRect.left;
        clientHeight = clientRect.bottom - clientRect.top;
        ImGui::Text("%s: %d x %d", EDITOR_TEXT("dev_label_actual_window_client"), clientWidth, clientHeight);

        calculatedScaleX = (float)clientWidth  / (float)REFERENCE_WIDTH;
        calculatedScaleY = (float)clientHeight / (float)REFERENCE_HEIGHT;
        ImGui::Text("%s: %.2f x %.2f", EDITOR_TEXT("dev_label_calculated_scale"), calculatedScaleX, calculatedScaleY);
    }

    if (WindowWidth != OpenglWindowWidth || WindowHeight != OpenglWindowHeight)
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", EDITOR_TEXT("dev_warn_window_size_mismatch"));

    ImGui::Spacing();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_copy_debug_info"), ImVec2(250, 0)))
    {
        char debugInfo[1024];
        sprintf_s(debugInfo,
            "=== Graphics Debug Info ===\n"
            "Current Resolution: %u x %u\n"
            "OpenGL Viewport: %d x %d\n"
            "Screen Rate: %.2f x %.2f\n"
            "Window Mode: %s\n"
            "Actual Window Client: %d x %d\n"
            "Calculated Scale from Client: %.2f x %.2f\n"
            "Mismatch: %s\n"
            "UI Reference System: %d x %d\n"
            "Expected UI Scale: WindowWidth/%d = %.2f, WindowHeight/%d = %.2f\n"
            "Aspect Ratio: %.3f\n",
            WindowWidth, WindowHeight,
            OpenglWindowWidth, OpenglWindowHeight,
            g_fScreenRate_x, g_fScreenRate_y,
            g_bUseWindowMode ? "Windowed" : "Fullscreen",
            clientWidth, clientHeight,
            calculatedScaleX, calculatedScaleY,
            (WindowWidth != OpenglWindowWidth || WindowHeight != OpenglWindowHeight) ? "YES" : "NO",
            REFERENCE_WIDTH, REFERENCE_HEIGHT,
            REFERENCE_WIDTH, (float)WindowWidth / (float)REFERENCE_WIDTH,
            REFERENCE_HEIGHT, (float)WindowHeight / (float)REFERENCE_HEIGHT,
            (float)WindowWidth / (float)WindowHeight
        );
        ImGui::SetClipboardText(debugInfo);
        g_MuEditorConsoleUI.LogEditor(EDITOR_TEXT("dev_log_debug_info_copied"));
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", EDITOR_TEXT("dev_label_paste_hint"));
}

// --- Map authoring: File menu + Terrain Painter -----------------------------
// IO lives in MuEditor::CustomMap; these methods are UI glue only.
// Modal popups are opened from the menu callback and rendered each frame in
// the same Begin/End scope so ImGui can manage their lifetime.

void CDevEditorUI::RenderFileMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu(EDITOR_TEXT("dev_menu_file")))
    {
        // Only flag the request here; the actual OpenPopup runs at window
        // scope inside RenderFileMenuModals so it lands on the same ID
        // stack that BeginPopupModal uses.
        if (ImGui::MenuItem(EDITOR_TEXT("dev_menu_file_new_map")))
        {
            m_RequestOpenNewMap = true;
        }

        const bool canSave = (m_CurrentCustomMapId >= 0);
        if (ImGui::MenuItem(EDITOR_TEXT("dev_menu_file_save_map"), nullptr, false, canSave))
        {
            MuEditor::CustomMap::SaveCustomMap(m_CurrentCustomMapId);
        }
        if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", EDITOR_TEXT("dev_tip_save_needs_slot"));
        }

        if (ImGui::MenuItem(EDITOR_TEXT("dev_menu_file_load_map")))
        {
            m_RequestOpenLoadMap = true;
        }

        ImGui::EndMenu();
    }

    // Active-slot indicator on the right side of the menu bar.
    if (m_CurrentCustomMapId >= 0)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  [%s %d]",
            EDITOR_TEXT("dev_label_custom_slot"), m_CurrentCustomMapId);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "%s", buf);
    }

    ImGui::EndMenuBar();
}

void CDevEditorUI::RenderOfflineAuthoringBanner()
{
    if (!IsOfflineAuthoring()) return;

    // Yellow-on-dark band immediately under the menu bar. Borders + padding
    // make it impossible to miss while still leaving the tab bar usable.
    const ImVec4 bgColor   = ImVec4(0.35f, 0.25f, 0.00f, 1.00f);
    const ImVec4 textColor = ImVec4(1.00f, 0.85f, 0.20f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bgColor);
    ImGui::BeginChild("OfflineAuthoringBanner",
                      ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.6f),
                      true);
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::TextWrapped("%s", EDITOR_TEXT("dev_offline_banner_title"));
    ImGui::TextWrapped("%s", EDITOR_TEXT("dev_offline_banner_body"));
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void CDevEditorUI::RenderFileMenuModals()
{
    // Honor deferred open requests at window scope so the popup IDs match
    // the BeginPopupModal scope below.
    if (m_RequestOpenNewMap)
    {
        ImGui::OpenPopup(NEW_MAP_POPUP_ID);
        m_RequestOpenNewMap = false;
    }
    if (m_RequestOpenLoadMap)
    {
        ImGui::OpenPopup(LOAD_MAP_POPUP_ID);
        m_RequestOpenLoadMap = false;
    }

    RenderNewMapModal();
    RenderLoadMapModal();
}

void CDevEditorUI::RenderNewMapModal()
{
    // ImGui::BeginPopupModal: "<visible title>###<stable id>".
    // The localized title is composed at render time so language changes
    // re-flow into the title bar.
    char popupLabel[128];
    std::snprintf(popupLabel, sizeof(popupLabel), "%s%s",
                  EDITOR_TEXT("dev_new_map_title"), NEW_MAP_POPUP_ID);

    if (!ImGui::BeginPopupModal(popupLabel, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::Text("%s", EDITOR_TEXT("dev_new_map_help"));
    ImGui::InputInt(EDITOR_TEXT("dev_new_map_slot_label"), &m_NewMapIdInput);
    if (m_NewMapIdInput < CUSTOM_MAP_ID_INPUT_MIN) m_NewMapIdInput = CUSTOM_MAP_ID_INPUT_MIN;
    if (m_NewMapIdInput > CUSTOM_MAP_ID_INPUT_MAX) m_NewMapIdInput = CUSTOM_MAP_ID_INPUT_MAX;

    ImGui::TextDisabled("Data\\World\\Custom\\World%d\\EncTerrain%d.{att,obj}",
                        m_NewMapIdInput + 1, m_NewMapIdInput + 1);

    if (ImGui::Button(EDITOR_TEXT("dev_btn_create"), ImVec2(120, 0)))
    {
        const int newId = m_NewMapIdInput;
        if (MuEditor::CustomMap::CreateNewCustomMap(newId) &&
            MuEditor::CustomMap::LoadCustomMap(newId))
        {
            m_CurrentCustomMapId = newId;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_cancel"), ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void CDevEditorUI::RenderLoadMapModal()
{
    char popupLabel[128];
    std::snprintf(popupLabel, sizeof(popupLabel), "%s%s",
                  EDITOR_TEXT("dev_load_map_title"), LOAD_MAP_POPUP_ID);

    if (!ImGui::BeginPopupModal(popupLabel, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    ImGui::Text("%s", EDITOR_TEXT("dev_load_map_classic_header"));
    // Scrollable region — the full classic-world list is too long to
    // grow the modal vertically without clipping the rest of the
    // dialog. 12-row cap keeps it comfortable on common resolutions.
    const float classicListHeight = ImGui::GetTextLineHeightWithSpacing() * 12.0f;
    ImGui::BeginChild("##classicworlds", ImVec2(280, classicListHeight), true);
    for (int i = 0; i < kClassicWorldCount; ++i)
    {
        const ClassicWorld& w = kClassicWorlds[i];
        if (w.folderIndex < 1) continue;
        ImGui::PushID(w.folderIndex);
        if (ImGui::Button(w.label, ImVec2(-1, 0)))
        {
            if (MuEditor::CustomMap::LoadClassicMap(w.folderIndex))
            {
                // Classic load: drop slot binding so Save Map can't clobber
                // shipping assets via the custom path.
                m_CurrentCustomMapId = -1;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::Text("%s", EDITOR_TEXT("dev_load_map_custom_header"));
    ImGui::Indent();
    const std::vector<int> customIds = MuEditor::CustomMap::ListCustomMapIds();
    if (customIds.empty())
    {
        ImGui::TextDisabled("%s", EDITOR_TEXT("dev_load_map_no_custom"));
    }
    for (int mapId : customIds)
    {
        ImGui::PushID(mapId);
        char label[64];
        std::snprintf(label, sizeof(label), "World%d  (slot %d)",
                      mapId + 1, mapId);
        if (ImGui::Button(label, ImVec2(260, 0)))
        {
            if (MuEditor::CustomMap::LoadCustomMap(mapId))
            {
                m_CurrentCustomMapId = mapId;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::PopID();
    }
    ImGui::Unindent();

    ImGui::Separator();
    if (ImGui::Button(EDITOR_TEXT("dev_btn_close"), ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

namespace
{
    // Resolves the tile coordinate (0..255) that the brush should affect this
    // frame. Cursor pick wins when valid; falls back to the hero's tile so
    // the "Paint at hero" button works without a terrain pick.
    struct BrushTarget
    {
        bool valid;
        int  tileX;
        int  tileY;
    };

    BrushTarget ResolveCursorTile()
    {
        if (!SelectFlag) return { false, 0, 0 };
        const int x = static_cast<int>(SelectXF);
        const int y = static_cast<int>(SelectYF);
        if (x < 0 || x >= TERRAIN_SIZE || y < 0 || y >= TERRAIN_SIZE)
            return { false, 0, 0 };
        return { true, x, y };
    }

    BrushTarget ResolveHeroTile()
    {
        if (!Hero) return { false, 0, 0 };
        const int x = static_cast<int>(Hero->Object.Position[0] / TERRAIN_SCALE);
        const int y = static_cast<int>(Hero->Object.Position[1] / TERRAIN_SCALE);
        if (x < 0 || x >= TERRAIN_SIZE || y < 0 || y >= TERRAIN_SIZE)
            return { false, 0, 0 };
        return { true, x, y };
    }

    void ApplyBrushAt(int tileX, int tileY, BYTE attr, int radius, bool subtract)
    {
        // AddTerrainAttributeRange already clamps internally to the grid via
        // the (x+i, y+j) addressing; we just center on the target tile.
        const int origin = radius - 1;            // 1 -> 0, 2 -> 1, ...
        const int extent = radius * 2 - 1;        // 1 -> 1, 2 -> 3, 3 -> 5
        const BYTE addFlag = subtract ? 0 : 1;
        AddTerrainAttributeRange(tileX - origin, tileY - origin,
                                 extent, extent, attr, addFlag);
    }
}

int CDevEditorUI::ResolveCurrentBrushMode() const
{
    if (m_BrushPaintOnDrag)     return 1;
    if (m_PlaceOnClickEnabled)  return 2;
    if (m_DeleteOnClickEnabled) return 3;
    if (m_PaintTextureOnDrag)   return 4;
    return 0;
}

void CDevEditorUI::RenderActiveToolHeader()
{
    // Big colored banner that tells the user, at a glance, which tool
    // is active and what its current key selection is. The previous UI
    // buried this in a small radio mid-tab; promoting it here removes
    // the "which mode am I in again?" question.
    const int mode = ResolveCurrentBrushMode();

    struct ToolPresentation { const char* label; ImVec4 color; };
    const ToolPresentation P[] = {
        { "OFF — pick a tool below", ImVec4(0.40f, 0.40f, 0.40f, 1.0f) },
        { "PAINT ATTRIBUTE",          ImVec4(0.90f, 0.55f, 0.10f, 1.0f) },
        { "PLACE OBJECT",             ImVec4(0.20f, 0.75f, 0.30f, 1.0f) },
        { "DELETE OBJECT",            ImVec4(0.90f, 0.25f, 0.25f, 1.0f) },
        { "PAINT TEXTURE",            ImVec4(0.30f, 0.60f, 0.95f, 1.0f) },
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.13f, 1.0f));
    ImGui::BeginChild("##toolheader",
                      ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2.3f),
                      true);
    ImGui::PushStyleColor(ImGuiCol_Text, P[mode].color);
    ImGui::Text("%s", P[mode].label);
    ImGui::PopStyleColor();

    // Selection summary line — what this tool will actually do if you
    // click right now. Each mode reports its own one-line state.
    switch (mode)
    {
        case 1:
            ImGui::Text("Attr: %s   Mode: %s   Radius: %d",
                kBrushAttributes[m_BrushAttrIndex].label,
                m_BrushSubtractMode ? "Subtract" : "Add",
                m_BrushRadius);
            break;
        case 2:
            if (m_PlaceSourceWorld >= 0)
                ImGui::Text("Source: World%d   Slot: %d   Scale: %.2f",
                    m_PlaceSourceWorld, m_PlaceLocalType, m_PlaceScale);
            else
                ImGui::Text("No source bank selected.");
            break;
        case 3:
            ImGui::Text("Hit radius: %.0f world units", m_DeleteRadius);
            break;
        case 4:
        {
            const char* layerName = (m_TextureBrushLayer == 0) ? "Eraser"
                                  : (m_TextureBrushLayer == 1) ? "Base"
                                  :                              "Overlay";
            ImGui::Text("Tile: %d   Layer: %s   Radius: %d",
                m_TextureBrushIndex, layerName, m_BrushRadius);
            break;
        }
        default:
            ImGui::TextDisabled("Click a brush below to activate it.");
            break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void CDevEditorUI::RenderAttributePainterPanel()
{
    if (m_BrushAttrIndex < 0 || m_BrushAttrIndex >= kBrushAttributeCount)
        m_BrushAttrIndex = 0;

    if (ImGui::BeginCombo(EDITOR_TEXT("dev_painter_attr"),
                          kBrushAttributes[m_BrushAttrIndex].label))
    {
        for (int i = 0; i < kBrushAttributeCount; ++i)
        {
            const bool selected = (i == m_BrushAttrIndex);
            if (ImGui::Selectable(kBrushAttributes[i].label, selected))
                m_BrushAttrIndex = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    int mode = m_BrushSubtractMode ? 1 : 0;
    ImGui::RadioButton(EDITOR_TEXT("dev_painter_mode_add"), &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton(EDITOR_TEXT("dev_painter_mode_sub"), &mode, 1);
    m_BrushSubtractMode = (mode == 1);

    ImGui::SliderInt(EDITOR_TEXT("dev_painter_radius"),
                     &m_BrushRadius, BRUSH_RADIUS_MIN, BRUSH_RADIUS_MAX);

    ImGui::Spacing();
    const BrushTarget cursor = ResolveCursorTile();
    const BrushTarget hero   = ResolveHeroTile();
    const BYTE attr = static_cast<BYTE>(kBrushAttributes[m_BrushAttrIndex].bits);
    if (ImGui::Button(EDITOR_TEXT("dev_painter_apply_at_hero"),
                      ImVec2(220, 0)) && hero.valid)
    {
        ApplyBrushAt(hero.tileX, hero.tileY, attr,
                     m_BrushRadius, m_BrushSubtractMode);
    }
    ImGui::SameLine();
    if (ImGui::Button(EDITOR_TEXT("dev_painter_apply_at_cursor"),
                      ImVec2(220, 0)) && cursor.valid)
    {
        ApplyBrushAt(cursor.tileX, cursor.tileY, attr,
                     m_BrushRadius, m_BrushSubtractMode);
    }
}

void CDevEditorUI::RenderDisplayOptionsPanel()
{
    ImGui::Checkbox(EDITOR_TEXT("dev_painter_show_overlay"),
                    &m_ShowAttrOverlay);
    if (m_ShowAttrOverlay)
    {
        ImGui::Indent();
        for (int i = 0; i < kOverlayAttrCount; ++i)
        {
            const OverlayAttribute& a = kOverlayAttrs[i];
            const ImVec4 swatch(a.r, a.g, a.b, 1.0f);
            ImGui::ColorButton("##swatch", swatch,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                ImVec2(16, 16));
            ImGui::SameLine();
            bool on = (m_OverlayAttrMask & a.bit) != 0;
            ImGui::PushID(i);
            if (ImGui::Checkbox(a.label, &on))
            {
                if (on) m_OverlayAttrMask |=  a.bit;
                else    m_OverlayAttrMask &= ~a.bit;
            }
            ImGui::PopID();
        }
        ImGui::Unindent();
    }
}

void CDevEditorUI::RenderCursorStatusFooter()
{
    const BrushTarget cursor = ResolveCursorTile();
    const BrushTarget hero   = ResolveHeroTile();

    ImGui::Separator();
    if (cursor.valid)
        ImGui::Text("%s: (%d, %d)   ",
                    EDITOR_TEXT("dev_painter_cursor_tile"),
                    cursor.tileX, cursor.tileY);
    else
        ImGui::TextDisabled("%s   ",
                            EDITOR_TEXT("dev_painter_cursor_none"));
    ImGui::SameLine();
    if (hero.valid)
        ImGui::Text("%s: (%d, %d)",
                    EDITOR_TEXT("dev_painter_hero_tile"),
                    hero.tileX, hero.tileY);
}

void CDevEditorUI::RenderTerrainPainterTab()
{
    // Header: shows current tool + its key selection. Always visible.
    RenderActiveToolHeader();

    // Tool palette — the primary control on this tab. Each radio is
    // mutually exclusive; the active tool's settings render below.
    int curMode = ResolveCurrentBrushMode();
    int newMode = curMode;
    ImGui::TextDisabled("%s", EDITOR_TEXT("dev_brush_mode_header"));
    ImGui::RadioButton(EDITOR_TEXT("dev_brush_mode_off"),     &newMode, 0); ImGui::SameLine();
    ImGui::RadioButton(EDITOR_TEXT("dev_brush_mode_paint"),   &newMode, 1); ImGui::SameLine();
    ImGui::RadioButton(EDITOR_TEXT("dev_brush_mode_place"),   &newMode, 2); ImGui::SameLine();
    ImGui::RadioButton(EDITOR_TEXT("dev_brush_mode_delete"),  &newMode, 3); ImGui::SameLine();
    ImGui::RadioButton(EDITOR_TEXT("dev_brush_mode_texture"), &newMode, 4);
    if (newMode != curMode)
    {
        SetExclusiveBrushMode(newMode);
        curMode = newMode;
    }

    RenderUndoControls();

    ImGui::Separator();

    // Tool-specific settings — render ONLY the active tool's panel so
    // the user isn't staring at controls that don't apply right now.
    // The Off case shows a one-liner hint; everything else surfaces the
    // panel for the active brush.
    switch (curMode)
    {
        case 0:
            ImGui::TextWrapped("%s", EDITOR_TEXT("dev_painter_intro"));
            ImGui::TextDisabled("Pick a brush above to start editing.");
            break;

        case 1:
            RenderAttributePainterPanel();
            break;

        case 2:
            // Place mode needs Sources — they're co-located here so the
            // workflow "add a source → pick a slot → click" is linear.
            if (ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_sources"),
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                RenderSourcesPanel();
                ImGui::Unindent();
            }
            ImGui::Separator();
            RenderPlaceObjectPanel();
            break;

        case 3:
            RenderDeleteObjectPanel();
            break;

        case 4:
            RenderTexturePainterPanel();
            break;
    }

    // Always-available auxiliary sections — display options + sources.
    // Source banks stay accessible from any mode so the user can prep
    // them without first switching to Place. Display options are visual
    // aids unrelated to which brush is active.
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Display options"))
    {
        ImGui::Indent();
        RenderDisplayOptionsPanel();
        ImGui::Unindent();
    }
    if (curMode != 2)  // already shown in-line above when in Place mode
    {
        if (ImGui::CollapsingHeader(EDITOR_TEXT("dev_section_sources")))
        {
            ImGui::Indent();
            RenderSourcesPanel();
            ImGui::Unindent();
        }
    }

    RenderCursorStatusFooter();
}

void CDevEditorUI::HandlePaintBrushInput()
{
    // EDIT_WALL drives MainScene's per-frame RenderTerrain(true) pass
    // that ray-casts the mouse and updates SelectXF/YF. Any of the three
    // editor brush modes needs that pick — without it the cursor readout
    // is stale and place/delete would always hit the same tile.
    EditFlag = IsPaintingTerrain() ? EDIT_WALL : EDIT_NONE;

    // Paint mode early-out runs the attribute brush; place/delete are
    // independent handlers below.
    if (m_BrushPaintOnDrag)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureMouse &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const BrushTarget cursor = ResolveCursorTile();
            if (cursor.valid)
            {
                const BYTE attr =
                    static_cast<BYTE>(kBrushAttributes[m_BrushAttrIndex].bits);
                ApplyBrushAt(cursor.tileX, cursor.tileY, attr,
                             m_BrushRadius, m_BrushSubtractMode);
            }
        }
    }

    HandlePlaceObjectInput();
    HandleDeleteObjectInput();
    HandlePaintTextureInput();
}

void CDevEditorUI::SetExclusiveBrushMode(int mode)
{
    // Modes: 0 = none, 1 = paint attr, 2 = place, 3 = delete, 4 = paint texture.
    m_BrushPaintOnDrag     = (mode == 1);
    m_PlaceOnClickEnabled  = (mode == 2);
    m_DeleteOnClickEnabled = (mode == 3);
    m_PaintTextureOnDrag   = (mode == 4);

    // Leaving place mode must hide the ghost preview immediately,
    // otherwise the half-transparent OBJECT lingers in ObjectBlock
    // until the next HandlePlaceObjectInput frame runs. Same reasoning
    // applies to the delete-hover preview when leaving delete mode.
    if (!m_PlaceOnClickEnabled)  HidePlacementPreview();
    if (!m_DeleteOnClickEnabled) ClearDeleteHoverPreview();
}

namespace
{
    // Maps SelectXF/YF (tile-corner ints) to the tile-center world XY
    // we want to place objects at. RequestTerrainHeight gives the Z so
    // we ground-snap automatically — same scheme as cross-world import.
    bool ResolveCursorWorldPosition(vec3_t outPos)
    {
        if (!SelectFlag) return false;
        const int x = static_cast<int>(SelectXF);
        const int y = static_cast<int>(SelectYF);
        if (x < 0 || x >= TERRAIN_SIZE || y < 0 || y >= TERRAIN_SIZE)
            return false;
        outPos[0] = (static_cast<float>(x) + 0.5f) * TERRAIN_SCALE;
        outPos[1] = (static_cast<float>(y) + 0.5f) * TERRAIN_SCALE;
        outPos[2] = RequestTerrainHeight(outPos[0], outPos[1]);
        return true;
    }
}

void CDevEditorUI::RenderSourcesPanel()
{
    ImGui::TextWrapped("%s", EDITOR_TEXT("dev_sources_intro"));

    // Dropdown is populated by scanning Data\Object<N>\ directories at
    // editor startup — that's the actual ground truth for what side-
    // loadable banks ship in this build. kClassicWorlds is used only
    // as a name lookup (so "World3" displays as "World3  Devias"); any
    // folder we don't have a friendly name for still appears as
    // "World<N>" so private-server / custom maps remain selectable.
    auto labelForFolderIndex = [](int folder, char* buf, size_t bufN)
    {
        for (int i = 0; i < kClassicWorldCount; ++i)
        {
            if (kClassicWorlds[i].folderIndex == folder)
            {
                std::snprintf(buf, bufN, "%s", kClassicWorlds[i].label);
                return;
            }
        }
        std::snprintf(buf, bufN, "World%d", folder);
    };

    const auto& available =
        MuEditor::CustomMap::EnumerateAvailableSourceWorlds();

    char currentLabel[64] = "(select source)";
    if (m_AddSourceWorldInput > 0)
        labelForFolderIndex(m_AddSourceWorldInput, currentLabel, sizeof(currentLabel));

    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo(EDITOR_TEXT("dev_sources_world_index"), currentLabel))
    {
        for (int folder : available)
        {
            char label[64];
            labelForFolderIndex(folder, label, sizeof(label));
            const bool sel = (folder == m_AddSourceWorldInput);
            ImGui::PushID(folder);
            if (ImGui::Selectable(label, sel))
                m_AddSourceWorldInput = folder;
            if (sel) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(EDITOR_TEXT("dev_sources_add")))
    {
        const int off =
            MuEditor::CustomMap::LoadSourceBank(m_AddSourceWorldInput);
        if (off >= 0 && m_PlaceSourceWorld < 0)
            m_PlaceSourceWorld = m_AddSourceWorldInput;
    }

    // Loaded list with remove buttons.
    auto banks = MuEditor::CustomMap::GetLoadedSourceBanks();
    if (banks.empty())
    {
        ImGui::TextDisabled("%s", EDITOR_TEXT("dev_sources_none"));
        return;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("%s", EDITOR_TEXT("dev_sources_loaded_header"));
    for (const auto& b : banks)
    {
        ImGui::PushID(b.worldFolderIndex);
        ImGui::Text("World%d  (slots %d..%d)",
                    b.worldFolderIndex,
                    b.baseOffset,
                    b.baseOffset + 159);
        ImGui::SameLine();
        if (ImGui::SmallButton(EDITOR_TEXT("dev_sources_remove")))
        {
            MuEditor::CustomMap::UnloadSourceBank(b.worldFolderIndex);
            if (m_PlaceSourceWorld == b.worldFolderIndex)
                m_PlaceSourceWorld = -1;
        }
        ImGui::PopID();
    }
}

void CDevEditorUI::RenderPlaceObjectPanel()
{
    auto banks = MuEditor::CustomMap::GetLoadedSourceBanks();
    if (banks.empty())
    {
        ImGui::TextDisabled("%s", EDITOR_TEXT("dev_place_need_source"));
        return;
    }

    // Source picker.
    const char* curLabel = "(none)";
    char curBuf[32];
    if (m_PlaceSourceWorld >= 0)
    {
        std::snprintf(curBuf, sizeof(curBuf),
                      "World%d", m_PlaceSourceWorld);
        curLabel = curBuf;
    }
    if (ImGui::BeginCombo(EDITOR_TEXT("dev_place_active_source"), curLabel))
    {
        for (const auto& b : banks)
        {
            char label[32];
            std::snprintf(label, sizeof(label), "World%d",
                          b.worldFolderIndex);
            const bool sel = (b.worldFolderIndex == m_PlaceSourceWorld);
            if (ImGui::Selectable(label, sel))
                m_PlaceSourceWorld = b.worldFolderIndex;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (m_PlaceSourceWorld < 0) return;

    // Slot picker grid — 8 columns x 20 rows, scrollable. Empty bank
    // slots (NumMeshs == 0) are greyed-out and unclickable; the
    // selected cell is highlighted; clicking a cell drives the cursor
    // ghost preview via m_PlaceLocalType. A filter box above narrows
    // the grid to cells whose BMD name or texture filenames match the
    // substring (case-insensitive) — type "tree", ".tga", "stone", etc.
    //
    // Phase 3 plan: cell *content* will be replaced with FBO-rendered
    // thumbnails; surrounding layout stays as-is.
    constexpr int   SLOT_COUNT     = 160;
    constexpr int   GRID_COLUMNS   = 8;
    constexpr float GRID_CELL_SIZE = 36.0f;
    constexpr float GRID_HEIGHT    = 320.0f;
    constexpr int SLOT_MAX = SLOT_COUNT - 1;
    if (m_PlaceLocalType < 0)       m_PlaceLocalType = 0;
    if (m_PlaceLocalType > SLOT_MAX) m_PlaceLocalType = SLOT_MAX;

    const int baseOffset =
        MuEditor::CustomMap::GetSourceBankBaseOffset(m_PlaceSourceWorld);

    // Case-insensitive substring match. Used to filter the grid against
    // BMD::Name + every Textures[i].FileName for the slot. Stops on
    // first hit since we just need a boolean.
    auto containsInsensitive = [](const char* hay, const char* needle) -> bool
    {
        if (!hay || !needle || !*needle) return true;
        for (; *hay; ++hay)
        {
            const char* h = hay;
            const char* n = needle;
            while (*h && *n &&
                   std::tolower(static_cast<unsigned char>(*h)) ==
                   std::tolower(static_cast<unsigned char>(*n)))
            { ++h; ++n; }
            if (!*n) return true;
        }
        return false;
    };

    auto slotMatchesFilter = [&](int localSlot) -> bool
    {
        if (!m_PlaceFilter[0])     return true;
        if (baseOffset < 0)        return true;
        const BMD& m = Models[baseOffset + localSlot];
        if (m.NumMeshs == 0)       return false;
        if (containsInsensitive(m.Name, m_PlaceFilter)) return true;
        if (m.Textures != nullptr)
        {
            for (int t = 0; t < m.NumMeshs; ++t)
            {
                if (containsInsensitive(m.Textures[t].FileName, m_PlaceFilter))
                    return true;
            }
        }
        return false;
    };

    ImGui::TextDisabled("%s: %d", EDITOR_TEXT("dev_place_slot"),
                        m_PlaceLocalType);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##slotfilter",
                             EDITOR_TEXT("dev_place_filter_hint"),
                             m_PlaceFilter, sizeof(m_PlaceFilter));
    ImGui::SameLine();
    if (ImGui::SmallButton(EDITOR_TEXT("dev_place_filter_clear")))
        m_PlaceFilter[0] = '\0';

    ImGui::BeginChild("##slotgrid", ImVec2(0, GRID_HEIGHT), true);
    int displayed = 0;       // tracks column wrap for filtered grid
    for (int i = 0; i < SLOT_COUNT; ++i)
    {
        if (!slotMatchesFilter(i)) continue;

        if ((displayed % GRID_COLUMNS) != 0) ImGui::SameLine();
        ++displayed;

        const bool empty =
            (baseOffset >= 0) && (Models[baseOffset + i].NumMeshs == 0);
        const bool selected = (i == m_PlaceLocalType);

        if (empty)    ImGui::BeginDisabled();
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.20f, 0.55f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(0.30f, 0.65f, 0.95f, 1.0f));
        }

        char label[8];
        std::snprintf(label, sizeof(label), "%d", i);
        ImGui::PushID(i);
        if (ImGui::Button(label, ImVec2(GRID_CELL_SIZE, GRID_CELL_SIZE)))
            m_PlaceLocalType = i;

        // Hover tooltip — BMD metadata so the cell number isn't the
        // only identifier. We deliberately skip BMD::Name because the
        // shipping BMDs store it in a non-UTF-8 encoding (CP949 / EUC-KR
        // — the original Korean asset filenames), which ImGui renders
        // as garbled glyphs. Instead we show the loaded file path,
        // which is deterministic: source banks always use the uniform
        // Object<n>.bmd naming under Data\Object<world>\. Texture names
        // are clean ASCII (engine convention) so they pass through.
        if (!empty && baseOffset >= 0 && ImGui::IsItemHovered())
        {
            const BMD& m = Models[baseOffset + i];
            ImGui::BeginTooltip();
            ImGui::Text("Slot %d", i);
            // n+1 because LoadSourceBank loads with i+1 as the file index.
            ImGui::Text("File: Data\\Object%d\\Object%02d.bmd",
                        m_PlaceSourceWorld, i + 1);
            ImGui::Text("Meshes: %d   Bones: %d   Actions: %d",
                        m.NumMeshs, m.NumBones, m.NumActions);
            if (m.Textures != nullptr && m.NumMeshs > 0)
            {
                ImGui::Separator();
                ImGui::TextDisabled("Textures:");
                constexpr int MAX_TEX_LINES = 8;
                const int shown =
                    (m.NumMeshs > MAX_TEX_LINES) ? MAX_TEX_LINES : m.NumMeshs;
                for (int t = 0; t < shown; ++t)
                {
                    if (m.Textures[t].FileName[0])
                        ImGui::BulletText("%s", m.Textures[t].FileName);
                }
                if (m.NumMeshs > MAX_TEX_LINES)
                    ImGui::TextDisabled("... +%d more",
                                        m.NumMeshs - MAX_TEX_LINES);
            }
            ImGui::EndTooltip();
        }

        ImGui::PopID();

        if (selected)
        {
            ImGui::PopStyleColor();
            ImGui::PopStyleColor();
        }
        if (empty) ImGui::EndDisabled();
    }
    if (displayed == 0)
        ImGui::TextDisabled("%s", EDITOR_TEXT("dev_place_no_matches"));
    ImGui::EndChild();

    ImGui::SliderFloat(EDITOR_TEXT("dev_place_scale"),
                       &m_PlaceScale, 0.25f, 4.0f, "%.2f");
    ImGui::SliderFloat(EDITOR_TEXT("dev_place_angle"),
                       &m_PlaceAngleZ, 0.0f, 360.0f, "%.0f deg");

    ImGui::TextDisabled("%s", EDITOR_TEXT("dev_place_click_help"));
}

void CDevEditorUI::RenderDeleteObjectPanel()
{
    ImGui::SliderFloat(EDITOR_TEXT("dev_delete_radius"),
                       &m_DeleteRadius, 25.0f, 500.0f, "%.0f");
    ImGui::TextDisabled("%s", EDITOR_TEXT("dev_delete_click_help"));
}

void CDevEditorUI::HidePlacementPreview()
{
    if (m_PlacementPreview != nullptr)
    {
        m_PlacementPreview->Live = false;
        m_PlacementPreview = nullptr;
    }
}

void CDevEditorUI::HandlePlaceObjectInput()
{
    // Mode-off / no source / mouse-over-UI / cursor-not-on-terrain all
    // require the preview to go away. We short-circuit by hiding then
    // returning so we don't accidentally commit on a stale frame.
    if (!m_PlaceOnClickEnabled || m_PlaceSourceWorld < 0)
    {
        HidePlacementPreview();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        HidePlacementPreview();
        return;
    }

    vec3_t pos;
    if (!ResolveCursorWorldPosition(pos))
    {
        HidePlacementPreview();
        return;
    }

    const int baseOffset =
        MuEditor::CustomMap::GetSourceBankBaseOffset(m_PlaceSourceWorld);
    if (baseOffset < 0)
    {
        HidePlacementPreview();
        return;
    }
    const int absoluteType = baseOffset + m_PlaceLocalType;
    vec3_t angle = { 0.0f, 0.0f, m_PlaceAngleZ };

    // Spawn the preview lazily on the first valid frame. Subsequent
    // frames just update fields on the same OBJECT — much cheaper than
    // CreateObject + DeleteObject every frame, and the spatial-hash
    // block placement stays stable enough for the renderer's frustum
    // tests to still find it (it iterates all blocks).
    constexpr float PREVIEW_ALPHA = 0.5f;
    if (m_PlacementPreview == nullptr)
    {
        m_PlacementPreview = CreateObject(absoluteType, pos, angle, m_PlaceScale);
    }

    if (m_PlacementPreview != nullptr)
    {
        m_PlacementPreview->Live        = true;
        m_PlacementPreview->Type        = static_cast<short>(absoluteType);
        m_PlacementPreview->Position[0] = pos[0];
        m_PlacementPreview->Position[1] = pos[1];
        m_PlacementPreview->Position[2] = pos[2];
        m_PlacementPreview->Angle[0]    = 0.0f;
        m_PlacementPreview->Angle[1]    = 0.0f;
        m_PlacementPreview->Angle[2]    = m_PlaceAngleZ;
        m_PlacementPreview->Scale       = m_PlaceScale;
        m_PlacementPreview->Alpha       = PREVIEW_ALPHA;
        m_PlacementPreview->AlphaTarget = PREVIEW_ALPHA;
    }

    // Click → commit. Bump alpha to 1 so the renderer's lerp doesn't
    // fade it out, push to undo, and forget the pointer. Next frame
    // the if-null path spawns a fresh preview at the new cursor pos.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        m_PlacementPreview != nullptr)
    {
        m_PlacementPreview->Alpha       = 1.0f;
        m_PlacementPreview->AlphaTarget = 1.0f;

        DevEditorUndoAction action;
        action.kind = DevEditorUndoAction::Kind::PlaceObject;
        action.obj  = m_PlacementPreview;
        PushUndo(action);

        m_PlacementPreview = nullptr;
    }
}

void CDevEditorUI::ClearDeleteHoverPreview()
{
    // Restore full alpha on the object we were fading, then drop the
    // pointer. AlphaTarget is what the engine lerps toward each frame;
    // setting both keeps the model from any transient transparency on
    // the next render.
    if (m_DeleteHoverTarget != nullptr)
    {
        m_DeleteHoverTarget->Alpha = 1.0f;
        m_DeleteHoverTarget->AlphaTarget = 1.0f;
        m_DeleteHoverTarget = nullptr;
    }
}

void CDevEditorUI::HandleDeleteObjectInput()
{
    // Mode off or mouse over UI → restore the previously-faded object
    // and drop the hover target. Cursor off-terrain also clears.
    if (!m_DeleteOnClickEnabled)
    {
        ClearDeleteHoverPreview();
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
    {
        ClearDeleteHoverPreview();
        return;
    }

    vec3_t cursorPos;
    if (!ResolveCursorWorldPosition(cursorPos))
    {
        ClearDeleteHoverPreview();
        return;
    }

    // Find-nearest runs every frame so we can preview the target.
    // Skip dead nodes — MarkAllObjectsDead may have hidden classic-
    // world decor; we don't want delete-click to resurrect then re-hide.
    //
    // Hybrid picker:
    //   1. Distance is ALWAYS measured from cursor to OBJECT.Position —
    //      that field is set at CreateObject time and reliable for every
    //      live object.
    //   2. AABB containment (BoundingBoxMin/Max) is a *bonus* — it gets
    //      promoted to "preferred" when valid and the cursor sits inside
    //      it. But the AABB is filled in by the render pipeline; off-
    //      screen objects (or ones that haven't rendered yet this frame)
    //      may have a zero or stale AABB. Using AABB-center for distance
    //      would silently exclude them. So distance uses Position; AABB
    //      only affects the containment-vs-fallback ordering.
    constexpr int OBJECT_BLOCK_COUNT = 256;
    OBJECT*  nearest      = nullptr;
    float    nearestDist2 = m_DeleteRadius * m_DeleteRadius;
    bool     foundContained = false;
    for (int b = 0; b < OBJECT_BLOCK_COUNT; ++b)
    {
        for (OBJECT* o = ObjectBlock[b].Head; o != nullptr; o = o->Next)
        {
            if (!o->Live) continue;

            const float dx = o->Position[0] - cursorPos[0];
            const float dy = o->Position[1] - cursorPos[1];
            const float d2 = dx * dx + dy * dy;

            const bool aabbValid =
                o->BoundingBoxMax[0] > o->BoundingBoxMin[0] &&
                o->BoundingBoxMax[1] > o->BoundingBoxMin[1];
            const bool contains = aabbValid &&
                cursorPos[0] >= o->BoundingBoxMin[0] &&
                cursorPos[0] <= o->BoundingBoxMax[0] &&
                cursorPos[1] >= o->BoundingBoxMin[1] &&
                cursorPos[1] <= o->BoundingBoxMax[1];

            if (contains)
            {
                // Containment wins outright over any non-contained
                // candidate. Among multiple overlapping containments,
                // pick the one whose Position is closest.
                if (!foundContained || d2 < nearestDist2)
                {
                    nearest = o;
                    nearestDist2 = d2;
                    foundContained = true;
                }
            }
            else if (!foundContained)
            {
                if (d2 < nearestDist2)
                {
                    nearest = o;
                    nearestDist2 = d2;
                }
            }
        }
    }
    // Hover target changed — restore the previous object's alpha so it
    // doesn't get left half-transparent when the cursor moves on.
    if (m_DeleteHoverTarget != nullptr && m_DeleteHoverTarget != nearest)
    {
        m_DeleteHoverTarget->Alpha = 1.0f;
        m_DeleteHoverTarget->AlphaTarget = 1.0f;
    }
    m_DeleteHoverTarget = nearest;

    // Fade the current target every frame. The engine's per-frame
    // alpha lerp would otherwise pull AlphaTarget back to whatever
    // value the engine wants (usually 1.0), so we hold both fields at
    // 0.5 continuously while hovered.
    if (nearest != nullptr)
    {
        constexpr float HOVER_ALPHA = 0.5f;
        nearest->Alpha = HOVER_ALPHA;
        nearest->AlphaTarget = HOVER_ALPHA;
    }

    // Click commits the previewed target — restore full alpha before
    // flipping Live to false, so undo brings the object back fully
    // opaque (without an explicit restore in PerformUndo).
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && nearest != nullptr)
    {
        nearest->Alpha = 1.0f;
        nearest->AlphaTarget = 1.0f;
        nearest->Live = false;

        DevEditorUndoAction action;
        action.kind = DevEditorUndoAction::Kind::DeleteObject;
        action.obj  = nearest;
        PushUndo(action);
        m_DeleteHoverTarget = nullptr;  // hidden now, drop the ref
    }
}

void CDevEditorUI::RenderTexturePainterPanel()
{
    // Texture brush palette — 30 BITMAP_MAPTILE slots loaded from
    // whichever world's tile bitmaps were copied into the slot at
    // Create / Load time. Click a thumbnail to pick the brush; click
    // on the world (with brush mode = Paint Texture) to write that
    // index into TerrainMappingLayer1[tile]. Save persists via .map.
    constexpr int   TILE_SLOT_COUNT      = 30;
    constexpr int   TILE_GRID_COLUMNS    = 5;
    constexpr float TILE_THUMB_SIZE      = 56.0f;
    constexpr float TILE_GRID_HEIGHT     = 280.0f;

    if (m_TextureBrushIndex < 0)
        m_TextureBrushIndex = 0;
    if (m_TextureBrushIndex >= TILE_SLOT_COUNT)
        m_TextureBrushIndex = TILE_SLOT_COUNT - 1;

    ImGui::TextDisabled("%s: %d",
                        EDITOR_TEXT("dev_texture_selected"),
                        m_TextureBrushIndex);

    ImGui::BeginChild("##texgrid",
                      ImVec2(0, TILE_GRID_HEIGHT), true);
    for (int i = 0; i < TILE_SLOT_COUNT; ++i)
    {
        if ((i % TILE_GRID_COLUMNS) != 0) ImGui::SameLine();

        // FindTexture (not GetTexture) returns nullptr for unloaded
        // slots — GetTexture substitutes a static error sentinel whose
        // FileName leaks into the hover tooltip ("CGlobalBitmap::Get-
        // Texture Error!!!"). For an empty slot we want a clean miss.
        BITMAP_t* bmp = Bitmaps.FindTexture(BITMAP_MAPTILE + i);
        const bool hasTex = (bmp != nullptr && bmp->TextureNumber != 0);
        const bool selected = (i == m_TextureBrushIndex);

        ImGui::PushID(i);

        // Highlight the selected swatch with a colored border that
        // ImageButton's frame padding turns into a visible ring.
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.20f, 0.55f, 0.85f, 1.0f));

        if (hasTex)
        {
            // ImTextureID is uint64_t in this ImGui build (modern API);
            // a static_cast from the GLuint texture name is the right
            // bridge — the OpenGL backend uses the integer directly.
            const ImTextureID texId =
                static_cast<ImTextureID>(bmp->TextureNumber);
            if (ImGui::ImageButton("##swatch", texId,
                    ImVec2(TILE_THUMB_SIZE, TILE_THUMB_SIZE)))
            {
                m_TextureBrushIndex = i;
            }
        }
        else
        {
            // Slot exists in the engine bank but no bitmap loaded —
            // still selectable so the user can paint with "empty"
            // (which renders blank / uses Layer2 only).
            char label[8];
            std::snprintf(label, sizeof(label), "%d", i);
            if (ImGui::Button(label,
                    ImVec2(TILE_THUMB_SIZE, TILE_THUMB_SIZE)))
            {
                m_TextureBrushIndex = i;
            }
        }

        if (selected) ImGui::PopStyleColor();

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text("Slot %d", i);
            if (bmp != nullptr && bmp->FileName[0] != L'\0')
            {
                // BITMAP_t::FileName is wchar_t — convert to UTF-8 for
                // ImGui::Text. Tile filenames are ASCII in practice
                // ("TileGrass01.jpg", "TileRock07.jpg"), so a narrow
                // pass-through is sufficient.
                char narrow[128];
                int j = 0;
                for (; bmp->FileName[j] != L'\0' &&
                       j + 1 < static_cast<int>(sizeof(narrow)); ++j)
                {
                    const wchar_t w = bmp->FileName[j];
                    narrow[j] = (w >= 0x20 && w < 0x7F)
                              ? static_cast<char>(w) : '?';
                }
                narrow[j] = '\0';
                ImGui::TextDisabled("%s", narrow);
            }
            ImGui::EndTooltip();
        }

        ImGui::PopID();
    }
    ImGui::EndChild();

    // Reuse the attribute brush radius — one knob across painter modes.
    ImGui::SliderInt(EDITOR_TEXT("dev_painter_radius"),
                     &m_BrushRadius, 1, 16);

    // Brush layer selector + soft toggle. Base = sets Layer1 (the
    // underlying texture). Overlay = sets Layer2 with a per-tile alpha
    // — that's what produces smooth path-on-grass transitions. Eraser
    // fades the overlay alpha back to 0 to expose the base.
    const char* layerNames[] = {
        EDITOR_TEXT("dev_texture_layer_eraser"),
        EDITOR_TEXT("dev_texture_layer_base"),
        EDITOR_TEXT("dev_texture_layer_overlay"),
    };
    ImGui::Combo(EDITOR_TEXT("dev_texture_layer"),
                 &m_TextureBrushLayer, layerNames, 3);

    if (m_TextureBrushLayer != 1)
    {
        ImGui::Checkbox(EDITOR_TEXT("dev_texture_soft_brush"),
                        &m_TextureBrushSoft);
    }
    if (m_TextureBrushLayer == 2)
    {
        ImGui::SliderFloat(EDITOR_TEXT("dev_texture_strength"),
                           &m_TextureBrushStrength, 0.05f, 1.0f, "%.2f");
    }

    ImGui::TextDisabled("%s", EDITOR_TEXT("dev_texture_click_help"));
}

void CDevEditorUI::HandlePaintTextureInput()
{
    if (!m_PaintTextureOnDrag) return;

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

    const BrushTarget cursor = ResolveCursorTile();
    if (!cursor.valid) return;

    const int radius = m_BrushRadius < 1 ? 1 : m_BrushRadius;
    const float frad = static_cast<float>(radius);
    const int origin = radius - 1;
    const int extent = radius * 2 - 1;
    const unsigned char brush =
        static_cast<unsigned char>(m_TextureBrushIndex);

    // Per-tile write — Layer1/Layer2/Alpha all keyed on the same tile
    // index. A corner-grid pass for Alpha (one wider in +x/+y) is
    // technically more "symmetric" with how the engine samples vertex
    // alphas, but in practice it produces visible stair-step rings at
    // the brush boundary — the per-tile write leans on the engine's
    // own vertex interpolation between adjacent painted/unpainted
    // tiles to smooth things out, which looks cleaner.
    for (int dy = 0; dy < extent; ++dy)
    {
        const int y = cursor.tileY - origin + dy;
        if (y < 0 || y >= TERRAIN_SIZE) continue;
        for (int dx = 0; dx < extent; ++dx)
        {
            const int x = cursor.tileX - origin + dx;
            if (x < 0 || x >= TERRAIN_SIZE) continue;

            const int offX = dx - origin;
            const int offY = dy - origin;
            const float dist =
                std::sqrt(static_cast<float>(offX * offX + offY * offY));

            float falloff = 1.0f;
            if (m_TextureBrushSoft)
            {
                if (dist >= frad) continue;
                falloff = 1.0f - (dist / frad);
            }

            const int tile = x + y * TERRAIN_SIZE;
            switch (m_TextureBrushLayer)
            {
                case 1:  // Base — solid Layer1 set
                    TerrainMappingLayer1[tile] = brush;
                    break;

                case 2:  // Overlay — Layer2 set + Alpha contributes
                {
                    TerrainMappingLayer2[tile] = brush;
                    const float a = m_TextureBrushStrength * falloff;
                    if (a > TerrainMappingAlpha[tile])
                        TerrainMappingAlpha[tile] = a;
                    break;
                }

                case 0:  // Eraser — fade Alpha back toward 0
                default:
                {
                    TerrainMappingAlpha[tile] *= (1.0f - falloff);
                    if (TerrainMappingAlpha[tile] < 0.001f)
                        TerrainMappingAlpha[tile] = 0.0f;
                    break;
                }
            }
        }
    }
}

void CDevEditorUI::PushUndo(DevEditorUndoAction action)
{
    // Cap stack size — keep the most recent N. 32 is enough that a
    // mistake several clicks back is still recoverable, while bounding
    // pointer-set growth across long authoring sessions.
    constexpr size_t UNDO_STACK_MAX = 32;
    if (m_UndoStack.size() >= UNDO_STACK_MAX)
        m_UndoStack.erase(m_UndoStack.begin());
    m_UndoStack.push_back(action);
}

void CDevEditorUI::PerformUndo()
{
    if (m_UndoStack.empty()) return;
    const DevEditorUndoAction a = m_UndoStack.back();
    m_UndoStack.pop_back();

    // OBJECT* may have been MarkAllObjectsDead'd by a map swap since
    // the action happened — its memory is still valid (we never free
    // those nodes; see ClearAllObjects deliberately-no-free comment),
    // so flipping Live is always safe even if the slot's contextually
    // stale. Worst case: the un-deleted object reappears in the wrong
    // map context; the user can re-delete it.
    if (a.obj == nullptr) return;

    switch (a.kind)
    {
        case DevEditorUndoAction::Kind::PlaceObject:
            // Was placed — hide it.
            a.obj->Live = false;
            break;
        case DevEditorUndoAction::Kind::DeleteObject:
            // Was deleted — restore.
            a.obj->Live = true;
            break;
    }
}

void CDevEditorUI::RenderUndoControls()
{
    const bool canUndo = !m_UndoStack.empty();
    if (!canUndo) ImGui::BeginDisabled();
    if (ImGui::Button(EDITOR_TEXT("dev_undo_last")))
        PerformUndo();
    if (!canUndo) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s: %d",
                        EDITOR_TEXT("dev_undo_stack_depth"),
                        static_cast<int>(m_UndoStack.size()));
}

// Accessors for external use
extern "C"
{
    // Camera info accessors
    int GetCurrentCameraMode()
    {
        auto& manager = CameraManager_Instance();
        return static_cast<int>(manager.GetCurrentMode());
    }

    float GetOrbitalCameraRadius()
    {
        auto& manager = CameraManager_Instance();
        if (static_cast<int>(manager.GetCurrentMode()) == 1)  // CameraMode::Orbital
        {
            auto* orbital = GetOrbitalCameraInstance();
            if (orbital)
            {
                return orbital->GetRadius();
            }
        }
        return 800.0f;  // Default
    }

    // Note: GetOrbitalCameraAngles is implemented in CameraManager.cpp

    // Per-camera CameraConfig overrides — only apply in MAIN_SCENE.
    // Cameras pass their own GetName() ("Default" / "Orbital") so each maintains
    // an independent override state in the DevEditor UI.
    bool DevEditor_IsCameraOverrideEnabled(const char* cameraName)
    {
        extern EGameScene SceneFlag;
        if (SceneFlag != MAIN_SCENE || !cameraName) return false;
        if (strcmp(cameraName, "Default") == 0) return g_DevEditorUI.GetDefaultOverride().enabled;
        if (strcmp(cameraName, "Orbital") == 0) return g_DevEditorUI.GetOrbitalOverride().enabled;
        return false;
    }

    void DevEditor_ApplyCameraOverride(const char* cameraName, CameraConfig* cfg)
    {
        extern EGameScene SceneFlag;
        if (SceneFlag != MAIN_SCENE || !cfg || !cameraName) return;
        if (strcmp(cameraName, "Default") == 0) g_DevEditorUI.ApplyDefaultOverrideToConfig(*cfg);
        else if (strcmp(cameraName, "Orbital") == 0) g_DevEditorUI.ApplyOrbitalOverrideToConfig(*cfg);
    }

    bool DevEditor_IsCameraFogOverrideEnabled(const char* cameraName)
    {
        extern EGameScene SceneFlag;
        if (SceneFlag != MAIN_SCENE || !cameraName) return false;
        if (strcmp(cameraName, "Default") == 0)
        {
            const auto& ov = g_DevEditorUI.GetDefaultOverride();
            return ov.enabled && ov.fogOverride;
        }
        if (strcmp(cameraName, "Orbital") == 0)
        {
            // Orbital has a single Fog toggle that always applies when the
            // override is enabled (no separate fogOverride gate).
            const auto& ov = g_DevEditorUI.GetOrbitalOverride();
            return ov.enabled;
        }
        return false;
    }

    bool DevEditor_GetCameraFogOverrideValue(const char* cameraName)
    {
        if (cameraName)
        {
            if (strcmp(cameraName, "Default") == 0) return g_DevEditorUI.GetDefaultOverride().fogOn;
            if (strcmp(cameraName, "Orbital") == 0) return g_DevEditorUI.GetOrbitalOverride().fogOn;
        }
        return true;
    }

    void DevEditor_GetCameraFogRange(const char* cameraName, float viewFar, float* outStart, float* outEnd)
    {
        float startPct = 1.00f, endPct = 1.25f;
        if (cameraName)
        {
            if (strcmp(cameraName, "Default") == 0)
            {
                startPct = g_DevEditorUI.GetDefaultOverride().fogStartPct;
                endPct   = g_DevEditorUI.GetDefaultOverride().fogEndPct;
            }
            else if (strcmp(cameraName, "Orbital") == 0)
            {
                const auto& ov = g_DevEditorUI.GetOrbitalOverride();
                startPct = ov.fogStartPct;
                endPct   = ov.fogEndPct;
                // Scale fog off the trapezoid's Far distance when override is on,
                // so sliding "Far distance" also pushes fog out to match.
                if (ov.enabled) viewFar = ov.farDist;
            }
        }
        if (outStart) *outStart = viewFar * startPct;
        if (outEnd)   *outEnd   = viewFar * endPct;
    }

    // Default-camera-only accessors (position offset + 2D trapezoid width multipliers).
    // Return identity/zero when override disabled or not in MAIN_SCENE.
    void DevEditor_GetDefaultCameraOffset(float* outX, float* outY, float* outZ)
    {
        extern EGameScene SceneFlag;
        const auto& ov = g_DevEditorUI.GetDefaultOverride();
        const bool active = (SceneFlag == MAIN_SCENE) && ov.enabled;
        if (outX) *outX = active ? ov.offsetX : 0.0f;
        if (outY) *outY = active ? ov.offsetY : 0.0f;
        if (outZ) *outZ = active ? ov.offsetZ : 0.0f;
    }

    // Orbital 2D culling: returns `true` when a custom view-aligned trapezoid
    // should replace the view-cone pyramid. Values are in absolute world units
    // in camera-local space (forward = view -Z, lateral = view X).
    bool DevEditor_GetOrbitalHullTrapezoid(float* outFarDist, float* outFarWidth,
                                           float* outNearDist, float* outNearWidth)
    {
        extern EGameScene SceneFlag;
        const auto& ov = g_DevEditorUI.GetOrbitalOverride();
        const bool active = (SceneFlag == MAIN_SCENE) && ov.enabled;
        if (!active) return false;
        if (outFarDist)   *outFarDist   = ov.farDist;
        if (outFarWidth)  *outFarWidth  = ov.farWidth;
        if (outNearDist)  *outNearDist  = ov.nearDist;
        if (outNearWidth) *outNearWidth = ov.nearWidth;
        return true;
    }

    void DevEditor_GetDefaultTrapezoidMultipliers(float* outNearMul, float* outFarMul)
    {
        extern EGameScene SceneFlag;
        const auto& ov = g_DevEditorUI.GetDefaultOverride();
        const bool active = (SceneFlag == MAIN_SCENE) && ov.enabled;
        if (outNearMul) *outNearMul = active ? ov.widthNearMul : 1.0f;
        if (outFarMul)  *outFarMul  = active ? ov.widthFarMul  : 1.0f;
    }



    // Render toggle accessors
    bool DevEditor_ShouldRenderTerrain()
    {
        return g_DevEditorUI.ShouldRenderTerrain();
    }

    bool DevEditor_ShouldRenderStaticObjects()
    {
        return g_DevEditorUI.ShouldRenderStaticObjects();
    }

    bool DevEditor_ShouldRenderEffects()
    {
        return g_DevEditorUI.ShouldRenderEffects();
    }

    bool DevEditor_ShouldRenderDroppedItems()
    {
        return g_DevEditorUI.ShouldRenderDroppedItems();
    }

    bool DevEditor_ShouldRenderItemLabels()
    {
        return g_DevEditorUI.ShouldRenderItemLabels();
    }

    bool DevEditor_ShouldRenderEquippedItems()
    {
        return g_DevEditorUI.ShouldRenderEquippedItems();
    }

    bool DevEditor_ShouldRenderWeatherEffects()
    {
        return g_DevEditorUI.ShouldRenderWeatherEffects();
    }

    bool DevEditor_ShouldRenderUI()
    {
        return g_DevEditorUI.ShouldRenderUI();
    }

    // Not implemented (always return true)
    bool DevEditor_ShouldRenderHero()
    {
        return g_DevEditorUI.ShouldRenderHero();
    }

    bool DevEditor_ShouldRenderNPCs()
    {
        return g_DevEditorUI.ShouldRenderNPCs();
    }

    bool DevEditor_ShouldRenderMonsters()
    {
        return g_DevEditorUI.ShouldRenderMonsters();
    }

    // New untested toggles
    bool DevEditor_ShouldRenderShaders()
    {
        return g_DevEditorUI.ShouldRenderShaders();
    }

    bool DevEditor_ShouldRenderSkillEffects()
    {
        return g_DevEditorUI.ShouldRenderSkillEffects();
    }
}

#endif // _EDITOR

// C linkage wrappers for external C code (always available, return false when _EDITOR not defined)
extern "C" {
    bool DevEditor_ShouldShowCharacterPickBoxes()
    {
#ifdef _EDITOR
        return g_DevEditorUI.ShouldShowCharacterPickBoxes();
#else
        return false;
#endif
    }

    bool DevEditor_ShouldShowTileGrid()
    {
#ifdef _EDITOR
        return g_DevEditorUI.ShouldShowTileGrid();
#else
        return false;
#endif
    }

    bool DevEditor_ShouldShowItemCullSphere()
    {
#ifdef _EDITOR
        return g_DevEditorUI.ShouldShowItemCullSphere();
#else
        return false;
#endif
    }

    bool DevEditor_ShouldShowItemPickBoxes()
    {
#ifdef _EDITOR
        return g_DevEditorUI.ShouldShowItemPickBoxes();
#else
        return false;
#endif
    }

    float DevEditor_GetCullRadiusItem()
    {
#ifdef _EDITOR
        return g_DevEditorUI.GetCullRadiusItem();
#else
        return 100.0f;
#endif
    }

    float DevEditor_GetLoginTerrainDist()
    {
#ifdef _EDITOR
        return g_DevEditorUI.GetLoginTerrainDist();
#else
        return 10000.0f;
#endif
    }

    float DevEditor_GetLoginObjectDist()
    {
#ifdef _EDITOR
        return g_DevEditorUI.GetLoginObjectDist();
#else
        return 10000.0f;
#endif
    }

    // Offline authoring mode: the network layer queries this to decide
    // whether to silence outbound movement packets and inbound Hero
    // position corrections while a custom map slot is bound.
    bool DevEditor_IsOfflineAuthoring()
    {
#ifdef _EDITOR
        return g_DevEditorUI.IsOfflineAuthoring();
#else
        return false;
#endif
    }

    // When true, the engine's click-to-move / attack handlers must skip
    // any LMB-driven action this frame — the editor's paint brush is
    // claiming the click. Used in ZzzInterface.cpp Attack() and the main
    // movement handler to swallow LMB before they consume it.
    bool DevEditor_IsPaintingTerrain()
    {
#ifdef _EDITOR
        return g_DevEditorUI.IsPaintingTerrain();
#else
        return false;
#endif
    }

    // Red wireframe box around the OBJECT the Delete brush is currently
    // hovering. Sized to the OBJECT's transformed AABB so it actually
    // wraps the visible model — the engine maintains BoundingBoxMin/Max
    // in world space, updated each frame by the regular render pass.
    // The box is inflated by HIGHLIGHT_PAD so the outline doesn't
    // z-fight with the model's own surface.
    void DevEditor_RenderDeleteHoverHighlight()
    {
#ifdef _EDITOR
        OBJECT* target = g_DevEditorUI.GetDeleteHoverTarget();
        if (target == nullptr) return;
        if (!target->Live) return;        // safety: don't draw on dead nodes

        constexpr float HIGHLIGHT_PAD = 8.0f;
        vec3_t origin = {
            target->BoundingBoxMin[0] - HIGHLIGHT_PAD,
            target->BoundingBoxMin[1] - HIGHLIGHT_PAD,
            target->BoundingBoxMin[2] - HIGHLIGHT_PAD,
        };
        const float sx =
            (target->BoundingBoxMax[0] - target->BoundingBoxMin[0]) +
            HIGHLIGHT_PAD * 2.0f;
        const float sy =
            (target->BoundingBoxMax[1] - target->BoundingBoxMin[1]) +
            HIGHLIGHT_PAD * 2.0f;
        const float sz =
            (target->BoundingBoxMax[2] - target->BoundingBoxMin[2]) +
            HIGHLIGHT_PAD * 2.0f;

        // Degenerate AABB → fall back to a fixed footprint so the user
        // still sees *something*. Shouldn't happen for live objects but
        // some have empty meshes that never get a transformed box.
        if (sx < 1.0f || sy < 1.0f || sz < 1.0f)
        {
            constexpr float FALLBACK = 100.0f;
            RenderDebugBox(target->Position,
                           FALLBACK, FALLBACK, FALLBACK * 1.5f,
                           1.0f, 0.2f, 0.2f);
            return;
        }

        RenderDebugBox(origin, sx, sy, sz, 1.0f, 0.2f, 0.2f);
#endif
    }

    // Tile-attribute overlay: draws translucent colored quads on each
    // walkable tile whose attributes match the user's overlay mask. Called
    // from MainScene's render loop right after RenderTerrain(false) so the
    // quads land on the visible terrain surface.
    //
    // Culling: a square window around the Hero's tile keeps the per-frame
    // cost bounded — full-grid iteration would be 65536 tiles per frame.
    void DevEditor_RenderTileAttributeOverlay()
    {
#ifdef _EDITOR
        if (!g_DevEditorUI.ShouldRenderTileAttributeOverlay()) return;
        if (Hero == nullptr) return;

        const int mask = g_DevEditorUI.GetOverlayAttributeMask();
        if (mask == 0) return;

        const int hx = static_cast<int>(Hero->Object.Position[0] / TERRAIN_SCALE);
        const int hy = static_cast<int>(Hero->Object.Position[1] / TERRAIN_SCALE);

        auto clamp = [](int v, int lo, int hi)
        {
            return v < lo ? lo : (v > hi ? hi : v);
        };
        const int x0 = clamp(hx - OVERLAY_CULL_RADIUS, 0, TERRAIN_SIZE - 2);
        const int x1 = clamp(hx + OVERLAY_CULL_RADIUS, 0, TERRAIN_SIZE - 2);
        const int y0 = clamp(hy - OVERLAY_CULL_RADIUS, 0, TERRAIN_SIZE - 2);
        const int y1 = clamp(hy + OVERLAY_CULL_RADIUS, 0, TERRAIN_SIZE - 2);

        DisableDepthTest();
        EnableAlphaBlend();
        DisableTexture();

        for (int y = y0; y <= y1; ++y)
        {
            for (int x = x0; x <= x1; ++x)
            {
                const WORD attr =
                    TerrainWall[x + y * TERRAIN_SIZE] &
                    static_cast<WORD>(mask);
                if (attr == 0) continue;

                const OverlayAttribute* hit = nullptr;
                for (int i = 0; i < kOverlayAttrCount; ++i)
                {
                    if (attr & kOverlayAttrs[i].bit)
                    {
                        hit = &kOverlayAttrs[i];
                        break;
                    }
                }
                if (hit == nullptr) continue;

                const float sx = static_cast<float>(x) * TERRAIN_SCALE;
                const float sy = static_cast<float>(y) * TERRAIN_SCALE;
                const float zA = BackTerrainHeight[
                    TERRAIN_INDEX_REPEAT(x,     y)]     + OVERLAY_Z_OFFSET;
                const float zB = BackTerrainHeight[
                    TERRAIN_INDEX_REPEAT(x + 1, y)]     + OVERLAY_Z_OFFSET;
                const float zC = BackTerrainHeight[
                    TERRAIN_INDEX_REPEAT(x + 1, y + 1)] + OVERLAY_Z_OFFSET;
                const float zD = BackTerrainHeight[
                    TERRAIN_INDEX_REPEAT(x,     y + 1)] + OVERLAY_Z_OFFSET;

                glBegin(GL_TRIANGLE_FAN);
                glColor4f(hit->r, hit->g, hit->b, OVERLAY_ALPHA);
                glVertex3f(sx,                    sy,                    zA);
                glVertex3f(sx + TERRAIN_SCALE,    sy,                    zB);
                glVertex3f(sx + TERRAIN_SCALE,    sy + TERRAIN_SCALE,    zC);
                glVertex3f(sx,                    sy + TERRAIN_SCALE,    zD);
                glEnd();
            }
        }

        DisableAlphaBlend();
#endif // _EDITOR
    }

}
