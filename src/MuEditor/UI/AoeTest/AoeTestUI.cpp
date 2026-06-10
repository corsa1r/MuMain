#include "stdafx.h"

#ifdef _EDITOR

#include "AoeTestUI.h"
#include "Data/GameData/SkillData/SkillFieldMetadata.h" // SKILL_ATTRIBUTE (with its deps resolved)
#include "Core/Globals/_struct.h"
#include "Core/Globals/_define.h"                       // MAX_SKILLS
#include "imgui.h"

#include <string>
#include <cctype>
#include <cstdio>

// Engine globals / entry points (declared here to avoid pulling heavy headers).
extern SKILL_ATTRIBUTE* SkillAttribute;   // loaded skill table (ZzzInfomation.cpp), MAX_SKILLS entries
void SpawnAoeEffect(int element, int radiusTiles, int ability, float durationSec, int intensity, float castDelaySec); // ZzzEffect.cpp
void ClearAoeEffects();                                                                                               // ZzzEffect.cpp

CAoeEffectUI& CAoeEffectUI::GetInstance()
{
    static CAoeEffectUI s;
    return s;
}

void CAoeEffectUI::Render()
{
    if (!m_visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(460, 620), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AOE Effect", &m_visible))
    {
        ImGui::End();
        return;
    }

    // ---- controls -------------------------------------------------------
    const char* types[] = { "1  Fire", "2  Physical", "3  Ice", "4  Lightning" };
    int typeIdx = m_type - 1;
    if (typeIdx < 0 || typeIdx > 3) typeIdx = 0;
    if (ImGui::Combo("AOE Type", &typeIdx, types, IM_ARRAYSIZE(types)))
        m_type = typeIdx + 1;

    ImGui::SliderInt("Range (tiles)", &m_range, 1, 10);
    ImGui::SliderFloat("Duration (sec)", &m_duration, 1.0f, 30.0f, "%.0f");
    ImGui::SliderFloat("Cast Delay (sec)", &m_castDelay, 0.0f, 5.0f, "%.1f");
    ImGui::SliderInt("Intensity (casts/sec)", &m_intensity, 1, 10);
    ImGui::TextDisabled("Telegraph shows immediately; skill starts after Cast Delay.");

    // Live chat command for the current settings (skill = the last one you cast). Copy it to save favourites.
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "/aoe %d %d %d %g %d %g",
        m_type, m_range, m_lastSkill, (double)m_duration, m_intensity, (double)m_castDelay);
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.65f, 0.90f, 1.0f, 1.0f), "%s", cmd);
    ImGui::SameLine();
    if (ImGui::Button("Copy"))
        ImGui::SetClipboardText(cmd);

    ImGui::Separator();
    if (ImGui::Button("Telegraph only"))
        SpawnAoeEffect(m_type, m_range, 0, m_duration, m_intensity, m_castDelay);
    ImGui::SameLine();
    if (ImGui::Button("Clear all"))
        ClearAoeEffects();
    ImGui::SameLine();
    ImGui::TextDisabled("(click a skill below to cast it)");

    ImGui::InputTextWithHint("##aoefilter", "filter skills by name...", m_filter, sizeof(m_filter));
    ImGui::Separator();

    // ---- scrollable skill grid -----------------------------------------
    ImGui::BeginChild("aoeskills", ImVec2(0, 0), true);

    if (SkillAttribute == nullptr)
    {
        ImGui::TextDisabled("Skill table not loaded yet.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    std::string flt = m_filter;
    for (char& ch : flt) ch = (char)tolower((unsigned char)ch);

    const float btnW = 205.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    int perRow = (int)(availW / (btnW + 8.0f));
    if (perRow < 1) perRow = 1;

    int shown = 0;
    for (int i = 1; i < MAX_SKILLS; ++i)
    {
        if (SkillAttribute[i].Name[0] == 0)
            continue;

        char nameBuf[128] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, SkillAttribute[i].Name, -1, nameBuf, sizeof(nameBuf), NULL, NULL);
        std::string name = nameBuf;

        if (!flt.empty())
        {
            std::string lname = name;
            for (char& ch : lname) ch = (char)tolower((unsigned char)ch);
            if (lname.find(flt) == std::string::npos)
                continue;
        }

        char label[160];
        snprintf(label, sizeof(label), "(%d) %s", i, name.c_str());

        if ((shown % perRow) != 0)
            ImGui::SameLine();
        if (ImGui::Button(label, ImVec2(btnW, 0)))
        {
            m_lastSkill = i;
            SpawnAoeEffect(m_type, m_range, i, m_duration, m_intensity, m_castDelay);
        }
        ++shown;
    }

    if (shown == 0)
        ImGui::TextDisabled("No skills match the filter.");

    ImGui::EndChild();
    ImGui::End();
}

#endif // _EDITOR
