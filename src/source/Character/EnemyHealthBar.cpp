//*****************************************************************************
// File: EnemyHealthBar.cpp
//*****************************************************************************

#include "stdafx.h"
#include "Character/EnemyHealthBar.h"

#include "Camera/CameraProjection.h"
#include "Engine/Object/ZzzCharacter.h"
#include "UI/Legacy/UIControls.h"

namespace
{
    constexpr float kBarWidth    = 80.f;
    constexpr float kBarHeight   = 6.6f;   // 40% less than original 11px
    constexpr float kBorderSize  = 1.f;
    constexpr float kHeadZOffset = 40.f;
    constexpr float kFontHeight  = 8.f;    // font height in reference units (REFERENCE_HEIGHT=480, scales up at higher res)
    constexpr float kNameGap     = 1.f;    // ~2-3 actual pixels gap between name bottom and bar top
    constexpr float kNameOffset  = kFontHeight + kNameGap;

    struct BarEntry
    {
        float   refX, refY;
        float   fillRatio;
        int     hpCurrent, hpMax;
        wchar_t name[MAX_MONSTER_NAME + 1];
    };

    bool ProjectEnemyHead(CHARACTER* c, float& outRefX, float& outRefY)
    {
        const OBJECT* o = &c->Object;
        vec3_t headPos;
        Vector(o->Position[0], o->Position[1],
               o->Position[2] + o->BoundingBoxMax[2] + kHeadZOffset, headPos);

        int screenX, screenY;
        CameraProjection::WorldToScreen(g_Camera, headPos, &screenX, &screenY);

        if (screenX <= 0 || screenX >= REFERENCE_WIDTH ||
            screenY <= 0 || screenY >= REFERENCE_HEIGHT)
            return false;

        outRefX = static_cast<float>(screenX);
        outRefY = static_cast<float>(screenY);
        return true;
    }
}

CEnemyHealthBar g_EnemyHealthBar;

void CEnemyHealthBar::RenderAll() const
{
    BarEntry entries[MAX_CHARACTERS_CLIENT];
    int count = 0;

    for (int i = 0; i < MAX_CHARACTERS_CLIENT; ++i)
    {
        CHARACTER* c = &CharactersClient[i];
        if (!c->Object.Live || !c->Object.Visible)
            continue;
        if (!IsMonster(c))
            continue;
        if (c->HealthStatus == 0.f)
            continue;

        float refX, refY;
        if (!ProjectEnemyHead(c, refX, refY))
            continue;

        float fillRatio;
        if (c->HpMax > 0)
            fillRatio = static_cast<float>(c->HpCurrent) / static_cast<float>(c->HpMax);
        else if (c->HealthStatus < 0.f)
            fillRatio = 1.f;
        else
            fillRatio = c->HealthStatus;

        fillRatio = std::max(0.f, std::min(1.f, fillRatio));

        BarEntry& e = entries[count++];
        e.refX      = refX;
        e.refY      = refY;
        e.fillRatio = fillRatio;
        e.hpCurrent = c->HpCurrent;
        e.hpMax     = c->HpMax;
        wcscpy_s(e.name, MAX_MONSTER_NAME + 1, c->ID);
    }

    if (count == 0)
        return;

    for (int i = 0; i < count; ++i)
    {
        const BarEntry& e = entries[i];
        const float left = e.refX - kBarWidth * 0.5f;
        const float top  = e.refY;

        // Bar geometry — solid, no blending
        DisableAlphaBlend();

        glColor3f(0.f, 0.f, 0.f);
        RenderColor(left - kBorderSize, top - kBorderSize,
                    kBarWidth + kBorderSize * 2.f, kBarHeight + kBorderSize * 2.f);

        glColor3f(0.15f, 0.f, 0.f);
        RenderColor(left, top, kBarWidth, kBarHeight);

        if (e.fillRatio > 0.f)
        {
            glColor3f(1.f, 0.f, 0.f);
            RenderColor(left, top, kBarWidth * e.fillRatio, kBarHeight);
        }

        // Text — SRC_ALPHA blend re-enables texturing for RenderBitmap
        EnableAlphaBlend3();
        glColor4f(1.f, 1.f, 1.f, 1.f);
        g_pRenderText->SetBgColor(0);

        // Monster name 2px above the bar
        g_pRenderText->SetTextColor(255, 255, 255, 255);
        g_pRenderText->RenderText(
            static_cast<int>(left),
            static_cast<int>(top - kNameOffset),
            e.name,
            static_cast<int>(kBarWidth),
            0,
            RT3_SORT_CENTER);

        // HP numbers inside the bar
        wchar_t text[32];
        if (e.hpMax > 0)
            mu_swprintf(text, L"%d/%d", e.hpCurrent, e.hpMax);
        else
            mu_swprintf(text, L"?/?");

        // Vertically center text over bar: offset = (barHeight - fontHeight) / 2
        const int hpTextY = static_cast<int>(top + (kBarHeight - kFontHeight) * 0.5f);
        g_pRenderText->SetTextColor(255, 255, 255, 255);
        g_pRenderText->RenderText(
            static_cast<int>(left),
            hpTextY,
            text,
            static_cast<int>(kBarWidth),
            0,
            RT3_SORT_CENTER);
    }

    g_pRenderText->SetTextColor(255, 255, 255, 255);
    g_pRenderText->SetBgColor(0);
    glColor3f(1.f, 1.f, 1.f);
}
