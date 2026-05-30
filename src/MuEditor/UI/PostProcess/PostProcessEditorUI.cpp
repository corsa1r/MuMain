#include "stdafx.h"

#ifdef _EDITOR

#include "PostProcessEditorUI.h"
#include "imgui.h"

#include "Render/PostProcess/PostProcessChain.h"
#include "Data/GameConfig/GameConfig.h"
#include "../MuEditor/Core/MuEditorCore.h"   // g_MuEditorCore.SetHoveringUI

CPostProcessEditorUI& CPostProcessEditorUI::GetInstance()
{
    static CPostProcessEditorUI instance;
    return instance;
}

void CPostProcessEditorUI::LoadFromConfig()
{
    GameConfig& c = GameConfig::GetInstance();
    m_postProcessEnabled         = c.GetPostProcess();
    m_settings.bloom             = c.GetBloom();
    m_settings.bloomStrength     = c.GetBloomStrength();
    m_settings.bloomThreshold    = c.GetBloomThreshold();
    m_settings.toneMap           = c.GetToneMap();
    m_settings.exposure          = c.GetExposure();
    m_settings.colorGrade        = c.GetColorGrade();
    m_settings.contrast          = c.GetContrast();
    m_settings.saturation        = c.GetSaturation();
    m_settings.brightness        = c.GetBrightness();
    m_settings.temperature       = c.GetTemperature();
    m_settings.gradeShadows      = c.GetGradeShadows();
    m_settings.gradeMidtones     = c.GetGradeMidtones();
    m_settings.gradeHighlights   = c.GetGradeHighlights();
    m_settings.vignette          = c.GetVignette();
    m_settings.vignetteStrength   = c.GetVignetteStrength();
    m_settings.vignetteRadius    = c.GetVignetteRadius();
    m_settings.fxaa              = c.GetFXAA();
    m_settings.sharpen           = c.GetSharpen();
    m_settings.sharpenStrength   = c.GetSharpenStrength();
    m_settings.filmGrain         = c.GetFilmGrain();
    m_settings.filmGrainStrength  = c.GetFilmGrainStrength();
    m_loaded = true;
}

void CPostProcessEditorUI::SaveToConfig()
{
    GameConfig& c = GameConfig::GetInstance();
    c.SetPostProcess(m_postProcessEnabled);
    c.SetBloom(m_settings.bloom);
    c.SetBloomStrength(m_settings.bloomStrength);
    c.SetBloomThreshold(m_settings.bloomThreshold);
    c.SetToneMap(m_settings.toneMap);
    c.SetExposure(m_settings.exposure);
    c.SetColorGrade(m_settings.colorGrade);
    c.SetContrast(m_settings.contrast);
    c.SetSaturation(m_settings.saturation);
    c.SetBrightness(m_settings.brightness);
    c.SetTemperature(m_settings.temperature);
    c.SetGradeShadows(m_settings.gradeShadows);
    c.SetGradeMidtones(m_settings.gradeMidtones);
    c.SetGradeHighlights(m_settings.gradeHighlights);
    c.SetVignette(m_settings.vignette);
    c.SetVignetteStrength(m_settings.vignetteStrength);
    c.SetVignetteRadius(m_settings.vignetteRadius);
    c.SetFXAA(m_settings.fxaa);
    c.SetSharpen(m_settings.sharpen);
    c.SetSharpenStrength(m_settings.sharpenStrength);
    c.SetFilmGrain(m_settings.filmGrain);
    c.SetFilmGrainStrength(m_settings.filmGrainStrength);
}

void CPostProcessEditorUI::ApplyLive()
{
    // Master switch governs whether the chain captures at all; the per-effect
    // settings drive the individual passes. Applying both each change keeps the
    // on-screen result in lockstep with the widgets.
    PostProcess::Chain::SetEnabled(m_postProcessEnabled);
    PostProcess::Chain::ApplySettings(m_settings);
}

void CPostProcessEditorUI::Render()
{
    if (!m_loaded)
        LoadFromConfig();   // GameConfig is ready by the time the editor renders

    if (!m_visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(360, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Post-Process", &m_visible))
    {
        ImGui::End();
        return;
    }

    // Block game input while the cursor is over this panel (same convention as
    // the other editor windows).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                               ImGuiHoveredFlags_ChildWindows))
    {
        g_MuEditorCore.SetHoveringUI(true);
    }

    // 'changed' aggregates every widget edit this frame; if anything moved we
    // re-apply the whole settings block to the live chain at the end.
    bool changed = false;

    changed |= ImGui::Checkbox("PostProcess (master)", &m_postProcessEnabled);
    ImGui::TextDisabled("Off = scene renders straight to backbuffer.");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##bloom", &m_settings.bloom);
        changed |= ImGui::SliderInt("Strength##bloom", &m_settings.bloomStrength, 0, 8);
        changed |= ImGui::SliderFloat("Threshold##bloom", &m_settings.bloomThreshold, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Tone Mapping", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##tone", &m_settings.toneMap);
        changed |= ImGui::SliderFloat("Exposure##tone", &m_settings.exposure, 0.1f, 3.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Color Grading", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##grade", &m_settings.colorGrade);
        changed |= ImGui::SliderFloat("Contrast##grade", &m_settings.contrast, 0.5f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Saturation##grade", &m_settings.saturation, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Brightness##grade", &m_settings.brightness, 0.5f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("Temperature##grade", &m_settings.temperature, -1.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Tonal gain (1 = neutral)");
        changed |= ImGui::SliderFloat("Shadows##grade", &m_settings.gradeShadows, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Midtones##grade", &m_settings.gradeMidtones, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Highlights##grade", &m_settings.gradeHighlights, 0.0f, 2.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##fxaa", &m_settings.fxaa);
    }

    if (ImGui::CollapsingHeader("Sharpen", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##sharp", &m_settings.sharpen);
        changed |= ImGui::SliderFloat("Strength##sharp", &m_settings.sharpenStrength, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Vignette", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##vig", &m_settings.vignette);
        changed |= ImGui::SliderFloat("Strength##vig", &m_settings.vignetteStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Radius##vig", &m_settings.vignetteRadius, 0.0f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Film Grain", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##grain", &m_settings.filmGrain);
        changed |= ImGui::SliderFloat("Strength##grain", &m_settings.filmGrainStrength, 0.0f, 0.3f, "%.3f");
    }

    ImGui::Separator();

    if (ImGui::Button("Save to config.ini"))
        SaveToConfig();
    ImGui::SameLine();
    if (ImGui::Button("Reload from config"))
    {
        LoadFromConfig();
        changed = true;   // re-apply the freshly loaded values
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset defaults"))
    {
        m_settings = PostProcess::Settings{};   // struct defaults
        changed = true;
    }

    ImGui::TextDisabled("Edits preview live. 'Save' persists to config.ini.");

    ImGui::End();

    if (changed)
        ApplyLive();
}

#endif // _EDITOR
