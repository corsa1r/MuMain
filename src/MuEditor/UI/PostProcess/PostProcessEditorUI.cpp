#include "stdafx.h"

#ifdef _EDITOR

#include "PostProcessEditorUI.h"
#include "imgui.h"

#include "Render/PostProcess/PostProcessChain.h"
#include "Render/PostProcess/PostProcessPreset.h"
#include "Render/Shadow/SunShadow.h"
#include "Render/Water/WaterReflection.h"
#include "Data/GameConfig/GameConfig.h"
#include "Network/ServerMapManifest.h"        // CurrentServerMapNumber (true map id)
#include "../MuEditor/Core/MuEditorCore.h"   // g_MuEditorCore.SetHoveringUI

#include <filesystem>
#include <string>
#include <cctype>

CPostProcessEditorUI& CPostProcessEditorUI::GetInstance()
{
    static CPostProcessEditorUI instance;
    return instance;
}

void CPostProcessEditorUI::LoadFromConfig()
{
    GameConfig& c = GameConfig::GetInstance();
    m_postProcessEnabled         = c.GetPostProcess();
    m_settings.ssao              = c.GetSSAO();
    m_settings.ssaoRadius        = c.GetSSAORadius();
    m_settings.ssaoStrength      = c.GetSSAOStrength();
    m_settings.ssaoPower         = c.GetSSAOPower();
    m_settings.depthOfField      = c.GetDepthOfField();
    m_settings.dofFocusDistance  = c.GetDofFocusDistance();
    m_settings.dofFocusRange     = c.GetDofFocusRange();
    m_settings.dofBlur           = c.GetDofBlur();
    m_settings.fog               = c.GetFog();
    m_settings.fogR              = c.GetFogColorR();
    m_settings.fogG              = c.GetFogColorG();
    m_settings.fogB              = c.GetFogColorB();
    m_settings.fogDensity        = c.GetFogDensity();
    m_settings.fogStart          = c.GetFogStart();
    m_settings.fogHeightStrength = c.GetFogHeightStrength();
    m_settings.fogHeightTop      = c.GetFogHeightTop();
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
    m_settings.msaa              = c.GetMSAA();
    m_settings.msaaSamples       = c.GetMSAASamples();
    m_settings.fxaa              = c.GetFXAA();
    m_settings.sharpen           = c.GetSharpen();
    m_settings.sharpenStrength   = c.GetSharpenStrength();
    m_settings.filmGrain         = c.GetFilmGrain();
    m_settings.filmGrainStrength  = c.GetFilmGrainStrength();
    m_settings.godRays           = c.GetGodRays();
    m_settings.godRaysLightX     = c.GetGodRaysLightX();
    m_settings.godRaysLightY     = c.GetGodRaysLightY();
    m_settings.godRaysSunZ       = c.GetGodRaysSunZ();
    m_settings.godRaysDensity    = c.GetGodRaysDensity();
    m_settings.godRaysWeight     = c.GetGodRaysWeight();
    m_settings.godRaysDecay      = c.GetGodRaysDecay();
    m_settings.godRaysThreshold  = c.GetGodRaysThreshold();
    m_settings.godRaysIntensity  = c.GetGodRaysIntensity();
    m_settings.godRaysSamples    = c.GetGodRaysSamples();
    m_settings.godRaysR          = c.GetGodRaysColorR();
    m_settings.godRaysG          = c.GetGodRaysColorG();
    m_settings.godRaysB          = c.GetGodRaysColorB();
    m_settings.lut               = c.GetLut();
    {
        const std::wstring wf = c.GetLutFile();   // narrow (ASCII filename)
        m_settings.lutFile.assign(wf.begin(), wf.end());
    }
    m_settings.perPixelLighting  = c.GetPerPixelLighting();
    m_settings.normalMapStrength = c.GetNormalMapStrength();
    m_settings.specularStrength  = c.GetSpecularStrength();
    m_settings.specularPower     = c.GetSpecularPower();
    m_settings.dynamicLights         = c.GetDynamicLights();
    m_settings.dynamicLightIntensity = c.GetDynamicLightIntensity();
    m_settings.dynamicLightFlicker   = c.GetDynamicLightFlicker();
    m_settings.dynamicLightCount     = c.GetDynamicLightCount();
    m_settings.playerLight       = c.GetPlayerLight();
    m_settings.playerLightRadius = c.GetPlayerLightRadius();
    m_settings.sunShadows          = c.GetSunShadows();
    m_settings.sunShadowResolution = c.GetSunShadowResolution();
    m_settings.sunShadowDistance   = c.GetSunShadowDistance();
    m_settings.sunShadowDarkness   = c.GetSunShadowDarkness();
    m_settings.sunShadowSoftness   = c.GetSunShadowSoftness();
    m_settings.sunShadowBias       = c.GetSunShadowBias();
    m_loaded = true;
}

void CPostProcessEditorUI::SaveToConfig()
{
    GameConfig& c = GameConfig::GetInstance();
    c.SetPostProcess(m_postProcessEnabled);
    c.SetSSAO(m_settings.ssao);
    c.SetSSAORadius(m_settings.ssaoRadius);
    c.SetSSAOStrength(m_settings.ssaoStrength);
    c.SetSSAOPower(m_settings.ssaoPower);
    c.SetDepthOfField(m_settings.depthOfField);
    c.SetDofFocusDistance(m_settings.dofFocusDistance);
    c.SetDofFocusRange(m_settings.dofFocusRange);
    c.SetDofBlur(m_settings.dofBlur);
    c.SetFog(m_settings.fog);
    c.SetFogColorR(m_settings.fogR);
    c.SetFogColorG(m_settings.fogG);
    c.SetFogColorB(m_settings.fogB);
    c.SetFogDensity(m_settings.fogDensity);
    c.SetFogStart(m_settings.fogStart);
    c.SetFogHeightStrength(m_settings.fogHeightStrength);
    c.SetFogHeightTop(m_settings.fogHeightTop);
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
    c.SetMSAA(m_settings.msaa);
    c.SetMSAASamples(m_settings.msaaSamples);
    c.SetFXAA(m_settings.fxaa);
    c.SetSharpen(m_settings.sharpen);
    c.SetSharpenStrength(m_settings.sharpenStrength);
    c.SetFilmGrain(m_settings.filmGrain);
    c.SetFilmGrainStrength(m_settings.filmGrainStrength);
    c.SetGodRays(m_settings.godRays);
    c.SetGodRaysLightX(m_settings.godRaysLightX);
    c.SetGodRaysLightY(m_settings.godRaysLightY);
    c.SetGodRaysSunZ(m_settings.godRaysSunZ);
    c.SetGodRaysDensity(m_settings.godRaysDensity);
    c.SetGodRaysWeight(m_settings.godRaysWeight);
    c.SetGodRaysDecay(m_settings.godRaysDecay);
    c.SetGodRaysThreshold(m_settings.godRaysThreshold);
    c.SetGodRaysIntensity(m_settings.godRaysIntensity);
    c.SetGodRaysSamples(m_settings.godRaysSamples);
    c.SetGodRaysColorR(m_settings.godRaysR);
    c.SetGodRaysColorG(m_settings.godRaysG);
    c.SetGodRaysColorB(m_settings.godRaysB);
    c.SetLut(m_settings.lut);
    {
        const std::string& f = m_settings.lutFile;   // widen (ASCII filename)
        c.SetLutFile(std::wstring(f.begin(), f.end()));
    }
    c.SetPerPixelLighting(m_settings.perPixelLighting);
    c.SetNormalMapStrength(m_settings.normalMapStrength);
    c.SetSpecularStrength(m_settings.specularStrength);
    c.SetSpecularPower(m_settings.specularPower);
    c.SetDynamicLights(m_settings.dynamicLights);
    c.SetDynamicLightIntensity(m_settings.dynamicLightIntensity);
    c.SetDynamicLightFlicker(m_settings.dynamicLightFlicker);
    c.SetDynamicLightCount(m_settings.dynamicLightCount);
    c.SetPlayerLight(m_settings.playerLight);
    c.SetPlayerLightRadius(m_settings.playerLightRadius);
    c.SetSunShadows(m_settings.sunShadows);
    c.SetSunShadowResolution(m_settings.sunShadowResolution);
    c.SetSunShadowDistance(m_settings.sunShadowDistance);
    c.SetSunShadowDarkness(m_settings.sunShadowDarkness);
    c.SetSunShadowSoftness(m_settings.sunShadowSoftness);
    c.SetSunShadowBias(m_settings.sunShadowBias);
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

    // Resync the sliders when the active map changed under us (the per-map
    // preset system applied a different look on map entry). Pull the settings
    // Presets actually applied so the panel mirrors what's on screen. Skipped
    // while Global Override is on (then every map shows the same global look,
    // which the user is editing directly).
    {
        const int aw = PostProcess::Presets::GetActiveWorld();
        if (aw != m_lastPanelWorld)
        {
            m_lastPanelWorld = aw;
            if (!PostProcess::Presets::GetGlobalOverride())
                m_settings = PostProcess::Presets::GetCurrent();
        }
    }

    // 'changed' aggregates every widget edit this frame; if anything moved we
    // re-apply the whole settings block to the live chain at the end.
    bool changed = false;

    changed |= ImGui::Checkbox("PostProcess (master)", &m_postProcessEnabled);
    ImGui::TextDisabled("Off = scene renders straight to backbuffer.");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Presets (per-map)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool ov = PostProcess::Presets::GetGlobalOverride();
        if (ImGui::Checkbox("Global Override (ignore per-map)", &ov))
        {
            PostProcess::Presets::SetGlobalOverride(ov);
            GameConfig::GetInstance().SetPPGlobalOverride(ov);
            // Re-apply so the switch takes effect immediately on this map.
            PostProcess::Presets::ApplyForWorld(BloodlustMU::ServerMapManifest::Instance().CurrentServerMapNumber());
        }

        const int world = BloodlustMU::ServerMapManifest::Instance().CurrentServerMapNumber();
        const bool has = PostProcess::Presets::HasMapPreset(world);
        ImGui::Text("Current map: %d   %s", world, has ? "[has preset]" : "[no preset]");

        if (ImGui::Button("Save as Map Preset"))
            PostProcess::Presets::SaveMapPreset(world, m_settings);
        ImGui::SameLine();
        if (ImGui::Button("Delete Map Preset"))
        {
            PostProcess::Presets::DeleteMapPreset(world);
            PostProcess::Presets::ApplyForWorld(world);   // revert to global base
        }
        ImGui::TextDisabled("Map preset = current sliders, auto-applied on entry.");
        ImGui::TextDisabled("'Save to config.ini' (below) sets the GLOBAL/fallback look.");
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Per-Pixel Lighting (models)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##ppl", &m_settings.perPixelLighting);
        changed |= ImGui::SliderFloat("Normal Strength##ppl", &m_settings.normalMapStrength, 0.0f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Specular Strength##ppl", &m_settings.specularStrength, 0.0f, 1.5f, "%.2f");
        changed |= ImGui::SliderFloat("Specular Power##ppl", &m_settings.specularPower, 1.0f, 128.0f, "%.0f");
        ImGui::TextDisabled("Per-pixel relief + glint on characters/monsters/objects.");
        ImGui::TextDisabled("Needs generated *_n normal maps (tools/upscale/gen_normals.ps1).");
        ImGui::TextDisabled("Also gated by the PostProcess master toggle above.");

        changed |= ImGui::Checkbox("Dynamic Lights##ppl", &m_settings.dynamicLights);
        changed |= ImGui::SliderFloat("Light Intensity##ppl", &m_settings.dynamicLightIntensity, 0.0f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("Flicker##ppl", &m_settings.dynamicLightFlicker, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderInt("Max Lights##ppl", &m_settings.dynamicLightCount, 1, 24);
        changed |= ImGui::Checkbox("Player Light##ppl", &m_settings.playerLight);
        changed |= ImGui::SliderFloat("Player Radius##ppl", &m_settings.playerLightRadius, 100.0f, 1500.0f, "%.0f");
        ImGui::TextDisabled("Torches/lanterns/lava/skills cast real per-pixel light.");
        ImGui::TextDisabled("Flicker: 0 = steady, 1 = raw. Requires Per-Pixel Lighting.");
        ImGui::TextDisabled("Player Light: always-on glow following your character.");
    }

    if (ImGui::CollapsingHeader("Sun Shadows", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Persisted via m_settings -> ApplyLive (ApplySettings -> SunShadow) and
        // saved by 'Save as Map Preset' / 'Save to config.ini'. 'changed' below
        // re-applies the whole block live.
        changed |= ImGui::Checkbox("Enabled##sunsh", &m_settings.sunShadows);
        const char* resItems[] = { "1024", "2048", "4096" };
        int ri = (m_settings.sunShadowResolution >= 4096) ? 2 : (m_settings.sunShadowResolution >= 2048) ? 1 : 0;
        if (ImGui::Combo("Resolution##sunsh", &ri, resItems, 3))
        { m_settings.sunShadowResolution = (ri == 2) ? 4096 : (ri == 1) ? 2048 : 1024; changed = true; }
        // Distance = ortho half-extent around the hero. Lower = sharper but
        // smaller coverage; higher = covers more buildings but blurrier.
        changed |= ImGui::SliderFloat("Distance##sunsh", &m_settings.sunShadowDistance, 150.0f, 4000.0f, "%.0f");
        changed |= ImGui::SliderFloat("Darkness##sunsh", &m_settings.sunShadowDarkness, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Softness##sunsh", &m_settings.sunShadowSoftness, 0.0f, 4.0f, "%.2f");
        changed |= ImGui::SliderFloat("Bias##sunsh", &m_settings.sunShadowBias, 0.0f, 6.0f, "%.2f");
        ImGui::TextDisabled("Characters/objects/terrain cast + receive. Needs Per-Pixel Lighting + master ON.");
        ImGui::TextDisabled("Sun = God Rays Sun Angle / Sun Height Z. Corner = the depth map.");
    }

    if (ImGui::CollapsingHeader("SSAO", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##ssao", &m_settings.ssao);
        changed |= ImGui::SliderFloat("Radius##ssao", &m_settings.ssaoRadius, 5.0f, 300.0f, "%.0f");
        changed |= ImGui::SliderFloat("Strength##ssao", &m_settings.ssaoStrength, 0.0f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Power##ssao", &m_settings.ssaoPower, 0.5f, 4.0f, "%.2f");
        ImGui::TextDisabled("Depth-based; darkens crevices/contact. Heavy.");
    }

    if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##dof", &m_settings.depthOfField);
        changed |= ImGui::SliderFloat("Focus Dist##dof", &m_settings.dofFocusDistance, 50.0f, 4000.0f, "%.0f");
        changed |= ImGui::SliderFloat("Focus Range##dof", &m_settings.dofFocusRange, 0.0f, 2000.0f, "%.0f");
        changed |= ImGui::SliderFloat("Blur##dof", &m_settings.dofBlur, 0.0f, 4.0f, "%.2f");
        ImGui::TextDisabled("Focus Dist = depth kept sharp (tune to the hero).");
    }

    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##fog", &m_settings.fog);
        float col[3] = { m_settings.fogR, m_settings.fogG, m_settings.fogB };
        if (ImGui::ColorEdit3("Color##fog", col))
        {
            m_settings.fogR = col[0]; m_settings.fogG = col[1]; m_settings.fogB = col[2];
            changed = true;
        }
        changed |= ImGui::SliderFloat("Density##fog", &m_settings.fogDensity, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Start##fog", &m_settings.fogStart, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Height Str##fog", &m_settings.fogHeightStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Height Top##fog", &m_settings.fogHeightTop, 0.0f, 1000.0f, "%.0f");
        ImGui::TextDisabled("Depth haze; Start=where it begins, Height=low mist.");
    }

    if (ImGui::CollapsingHeader("God Rays", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##god", &m_settings.godRays);
        changed |= ImGui::SliderFloat("Sun Angle##god", &m_settings.godRaysLightX, 0.0f, 360.0f, "%.0f deg");
        changed |= ImGui::SliderFloat("Sun Height Z##god", &m_settings.godRaysSunZ, 0.2f, 4.0f, "%.2f");
        float gcol[3] = { m_settings.godRaysR, m_settings.godRaysG, m_settings.godRaysB };
        if (ImGui::ColorEdit3("Tint##god", gcol))
        {
            m_settings.godRaysR = gcol[0]; m_settings.godRaysG = gcol[1]; m_settings.godRaysB = gcol[2];
            changed = true;
        }
        changed |= ImGui::SliderFloat("Intensity##god", &m_settings.godRaysIntensity, 0.0f, 3.0f, "%.2f");
        changed |= ImGui::SliderFloat("Shadow Len##god", &m_settings.godRaysDensity, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Shadow Dark##god", &m_settings.godRaysWeight, 0.0f, 5.0f, "%.2f");
        changed |= ImGui::SliderFloat("Decay##god", &m_settings.godRaysDecay, 0.8f, 1.0f, "%.3f");
        changed |= ImGui::SliderFloat("Occluder Hgt##god", &m_settings.godRaysThreshold, 0.0f, 400.0f, "%.0f");
        changed |= ImGui::SliderInt("Samples##god", &m_settings.godRaysSamples, 16, 128);
        ImGui::TextDisabled("Sun Angle = azimuth (0-360 deg), Sun Height Z = elevation.");
        ImGui::TextDisabled("Drives BOTH the god rays AND the character shadows (they");
        ImGui::TextDisabled("follow the sun). Shadow Len/Decay = ray length/fade;");
        ImGui::TextDisabled("Shadow Dark = darken; Intensity = beam; Occluder Hgt = min ht.");
    }

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

    if (ImGui::CollapsingHeader("MSAA", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##msaa", &m_settings.msaa);
        changed |= ImGui::SliderInt("Samples##msaa", &m_settings.msaaSamples, 2, 8);
        ImGui::TextDisabled("Smooths geometry edges only (not foliage cutouts).");
        ImGui::TextDisabled("Heavier than FXAA; changing it rebuilds targets.");
    }

    if (ImGui::CollapsingHeader("FXAA", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##fxaa", &m_settings.fxaa);
    }

    if (ImGui::CollapsingHeader("Sharpen", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##sharp", &m_settings.sharpen);
        changed |= ImGui::SliderFloat("Strength##sharp", &m_settings.sharpenStrength, 0.0f, 2.0f, "%.2f");
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

    if (ImGui::CollapsingHeader("LUT (.cube)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##lut", &m_settings.lut);

        // Dropdown of .cube files found in Data/PostProcess/. Scanned only while
        // the combo is open (BeginCombo returns true), so no per-frame disk IO.
        const char* preview = m_settings.lutFile.empty() ? "(none)" : m_settings.lutFile.c_str();
        if (ImGui::BeginCombo("File##lut", preview))
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator("Data\\PostProcess", ec))
            {
                if (ec) break;
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (ext != ".cube") continue;

                std::string name = entry.path().filename().string();
                const bool sel = (name == m_settings.lutFile);
                if (ImGui::Selectable(name.c_str(), sel))
                {
                    m_settings.lutFile = name;
                    changed = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Drop .cube files in Data/PostProcess/.");
    }

    // ---- Water Reflection (planar mirror) — part of the per-map preset --------
    // Drives m_settings so 'Save as Map Preset' persists it and map entry restores
    // it (ApplyLive -> Chain::ApplySettings distributes to the WaterReflection module).
    if (ImGui::CollapsingHeader("Water Reflection", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::Checkbox("Enabled##wr", &m_settings.waterReflect);
        changed |= ImGui::SliderFloat("Strength##wr", &m_settings.waterStrength, 0.0f, 1.0f, "%.2f");

        ImGui::Separator(); ImGui::TextDisabled("Plane");
        changed |= ImGui::SliderFloat("Plane Grow##wr", &m_settings.waterGrow, 0.0f, 12.0f, "%.0f tiles");
        ImGui::TextDisabled("Grows the flat plane outward (never shrinks); the");
        ImGui::TextDisabled("spill past the shore is hidden by depth occlusion.");
        float wtint[3] = { m_settings.waterTintR, m_settings.waterTintG, m_settings.waterTintB };
        if (ImGui::ColorEdit3("Tint##wr", wtint))
        {
            m_settings.waterTintR = wtint[0]; m_settings.waterTintG = wtint[1]; m_settings.waterTintB = wtint[2];
            changed = true;
        }
        changed |= ImGui::SliderFloat("Tint Opacity##wr", &m_settings.waterTintOpacity, 0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("Tint covers the base-water edge; 0 = off.");

        ImGui::Separator(); ImGui::TextDisabled("Reflection");
        changed |= ImGui::SliderFloat("Global Blur##wr", &m_settings.waterBlur, 0.0f, 12.0f, "%.1f px");
        ImGui::TextDisabled("Softens the WHOLE reflection (0 = crisp).");
        changed |= ImGui::SliderFloat("Clip Depth##wr", &m_settings.waterClipDepth, 0.0f, 800.0f, "%.0f");
        ImGui::TextDisabled("How far below the surface still reflects (higher = less cut).");
        {
            float g = (m_settings.waterHeightGate > 2000.0f) ? 2000.0f : m_settings.waterHeightGate;
            if (ImGui::SliderFloat("Height Gate##wr", &g, 20.0f, 2000.0f, "%.0f"))
            {
                m_settings.waterHeightGate = (g >= 2000.0f) ? 9999.0f : g;   // max = off
                changed = true;
            }
            ImGui::TextDisabled("Skip water tiles this far from the plane (cliffs). Max = off.");
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Save to config.ini"))
    {
        SaveToConfig();
        // Keep the live global base in sync so Global Override reflects edits
        // without a restart.
        PostProcess::Presets::SetGlobalBase(m_settings);
    }
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
