#pragma once

#ifdef _EDITOR

// ============================================================================
//  CPostProcessEditorUI  —  live ImGui panel for the post-process chain
// ----------------------------------------------------------------------------
//  Editor-only. Mirrors every [Graphics] effect knob as a checkbox / slider.
//  Edits apply to the running chain IMMEDIATELY (live preview) via
//  PostProcess::Chain::ApplySettings; a "Save to config.ini" button persists
//  the current values through GameConfig so they survive a restart.
//
//  Singleton, matching the other editor panels (g_DevEditorUI etc.). Owns its
//  own visibility; the toolbar's "Post FX" button toggles it and
//  CMuEditorCore::Render() calls Render() each frame while the editor is open.
//
//  Layering: reads/writes GameConfig (the INI store) and pushes a neutral
//  PostProcess::Settings to the chain — it never reaches into a pass directly.
// ============================================================================

#include "Render/PostProcess/PostProcessSettings.h"

class CPostProcessEditorUI
{
public:
    static CPostProcessEditorUI& GetInstance();

    void Render();              // draw window (no-op if hidden); live-applies edits

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }
    void Toggle()          { m_visible = !m_visible; }

private:
    CPostProcessEditorUI() = default;
    ~CPostProcessEditorUI() = default;

    void LoadFromConfig();   // GameConfig -> m_settings (+ master flag)
    void SaveToConfig();     // m_settings -> GameConfig (write-through to INI)
    void ApplyLive();        // m_settings -> running chain

    PostProcess::Settings m_settings;   // current working values
    bool m_postProcessEnabled = false;  // master [Graphics] PostProcess
    bool m_visible = false;
    bool m_loaded  = false;             // lazy: pull from config on first Render
    int  m_lastPanelWorld = -1000;      // resync sliders when the active map changes
};

#define g_PostProcessEditorUI CPostProcessEditorUI::GetInstance()

#endif // _EDITOR
