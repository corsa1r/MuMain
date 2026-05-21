//*****************************************************************************
// File: EnemyHealthBar.cpp
//*****************************************************************************

#include "stdafx.h"
#include "Character/EnemyHealthBar.h"

#include "Camera/CameraProjection.h"
#include "Engine/Object/ZzzCharacter.h"
#include "GameLogic/Combat/PrimeStatusStore.h"
#include "UI/Legacy/UIControls.h"

namespace
{
    constexpr float kBarWidth     = 80.f;
    constexpr float kBarHeight    = 6.6f;
    constexpr float kBorderSize   = 1.f;
    constexpr float kHeadZOffset  = 40.f;
    constexpr float kFontHeight   = 8.f;
    constexpr float kNameGap      = 1.f;
    constexpr float kNameOffset   = kFontHeight + kNameGap;
    constexpr float kSquareSize   = 7.f;
    constexpr float kSquareGap    = 2.f;

    struct BarEntry
    {
        float            refX, refY;
        float            fillRatio;
        int              hpCurrent, hpMax;
        wchar_t          name[MAX_MONSTER_NAME + 1];
        PrimeElementMask primeMask;
    };

    struct ElementDef
    {
        EPrimeElement elem;
        float r, g, b;
    };

    constexpr ElementDef kElementDefs[] = {
        { EPrimeElement::Fire,      1.000f, 0.471f, 0.000f },
        { EPrimeElement::Ice,       0.392f, 0.784f, 1.000f },
        { EPrimeElement::Lightning, 1.000f, 0.863f, 0.000f },
        { EPrimeElement::Physical,  0.784f, 0.784f, 0.784f },
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
        e.primeMask = GameLogic::PrimeStatus::GetMask(static_cast<uint16_t>(c->Key));
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

        // Monster name above the bar — always white, never modified
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

        const int hpTextY = static_cast<int>(top + (kBarHeight - kFontHeight) * 0.5f);
        g_pRenderText->SetTextColor(255, 255, 255, 255);
        g_pRenderText->RenderText(
            static_cast<int>(left),
            hpTextY,
            text,
            static_cast<int>(kBarWidth),
            0,
            RT3_SORT_CENTER);

        // Element squares — small colored tiles to the right of the bar, vertically centred
        if (e.primeMask != 0)
        {
            float iconX = left + kBarWidth + kSquareGap;
            const float iconY = top + (kBarHeight - kSquareSize) * 0.5f;

            DisableAlphaBlend();

            for (const auto& def : kElementDefs)
            {
                if (!(e.primeMask & ElementBit(def.elem)))
                    continue;
                glColor3f(def.r, def.g, def.b);
                RenderColor(iconX, iconY, kSquareSize, kSquareSize);
                iconX += kSquareSize + kSquareGap;
            }

            EnableAlphaBlend3();
        }
    }

    g_pRenderText->SetTextColor(255, 255, 255, 255);
    g_pRenderText->SetBgColor(0);
    glColor3f(1.f, 1.f, 1.f);
}
