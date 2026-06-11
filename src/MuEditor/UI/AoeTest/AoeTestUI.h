#pragma once

#ifdef _EDITOR

// ============================================================================
//  CAoeEffectUI  —  editor-only panel for previewing skills as ground AoE hazards
// ----------------------------------------------------------------------------
//  Drives the client-only /aoe test (SpawnAoeEffect): spawns a pulsing telegraph
//  at the player's feet and repeatedly casts a chosen skill between two hidden
//  dummy actors inside the radius, so we can eyeball every skill and handpick
//  the ones that read well as boss/elite hazards.
//
//  Controls: AOE type (element tint), range (tiles), duration (sec), intensity
//  (casts/sec), plus a scrollable, filterable grid of every loaded skill as a
//  "(id) Name" button — click to cast it in the zone.
//
//  Self-contained visibility like CPostProcessEditorUI: the toolbar's "AOE Test"
//  button toggles it; CMuEditorCore::Render() calls Render() each frame.
// ============================================================================

class CAoeEffectUI
{
public:
    static CAoeEffectUI& GetInstance();

    void Render();              // draw window (no-op if hidden)

    bool IsVisible() const { return m_visible; }
    void SetVisible(bool v) { m_visible = v; }
    void Toggle()          { m_visible = !m_visible; }

private:
    CAoeEffectUI() = default;
    ~CAoeEffectUI() = default;

    int   m_type      = 1;      // 1=fire 2=physical 3=ice 4=lightning (telegraph tint)
    int   m_range     = 3;      // radius in tiles (1-10)
    float m_duration  = 5.0f;   // zone lifetime in seconds
    float m_castDelay = 1.0f;   // telegraph-only warning before the skill starts (seconds)
    int   m_intensity = 2;      // casts per second (1-10)
    int   m_targetMode = 2;     // 1=on a player, 2=random tile near elite, 3=on self (server placement)
    int   m_lastSkill = 0;      // last skill cast from the grid (shown in the copyable /aoe command)
    char  m_filter[64] = "";    // case-insensitive name filter for the skill grid
    bool  m_visible   = false;
};

#define g_AoeEffectUI CAoeEffectUI::GetInstance()

#endif // _EDITOR
