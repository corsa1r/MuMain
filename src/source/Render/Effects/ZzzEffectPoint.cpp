///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <algorithm>
#include "Camera/CameraProjection.h"
#include "Render/Textures/ZzzOpenglUtil.h"
#include "Render/Models/ZzzBMD.h"
#include "Engine/Object/ZzzInfomation.h"
#include "Engine/Object/ZzzObject.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Render/Terrain/ZzzLodTerrain.h"
#include "Render/Textures/ZzzTexture.h"
#include "Engine/AI/ZzzAI.h"
#include "ZzzEffect.h"
#include "Audio/DSPlaySound.h"
#include "Network/Server/WSclient.h"
#include "Platform/Windows/Winmain.h"
#include "UI/Legacy/UIControls.h"
#include "UI/NewUI/NewUISystem.h"

PARTICLE  Points[MAX_POINTS];

int g_iLatestPoint = -1;

namespace
{
    // Damage popups are floating numbers above a target. When several land on
    // the same target in the same frame (e.g. detonator cast hit + its combo
    // damage), they render on top of each other and become unreadable. To
    // stagger them, each new popup looks for existing live popups close to
    // its spawn position (XY plane) and queues itself behind the most-delayed
    // one in that cluster by kPointStaggerFrames.
    constexpr float kPointStaggerRadius2 = 30.f * 30.f;  // squared, XY only
    constexpr float kPointStaggerFrames  = 5.f;          // ~200ms at 25fps ref

    float ComputeStaggerDelay(const vec3_t pos)
    {
        float maxNearbyDelay = -1.f;
        for (int i = 0; i < MAX_POINTS; i++)
        {
            const PARTICLE* p = &Points[i];
            if (!p->Live) continue;
            const float dx = p->Position[0] - pos[0];
            const float dy = p->Position[1] - pos[1];
            if (dx * dx + dy * dy > kPointStaggerRadius2) continue;
            if (p->SpawnDelay > maxNearbyDelay) maxNearbyDelay = p->SpawnDelay;
        }
        if (maxNearbyDelay < 0.f) return 0.f;
        return maxNearbyDelay + kPointStaggerFrames;
    }
}

void CreatePoint(vec3_t Position, int Value, vec3_t Color, float scale, bool bMove, bool bRepeatedly, bool bCombo)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    const float staggerDelay = ComputeStaggerDelay(Position);

    for (int i = 0; i < MAX_POINTS; i++)
    {
        PARTICLE* o = &Points[i];
        if (!o->Live)
        {
            o->Live = true;
            o->Type = Value;
            VectorCopy(Position, o->Position);
            o->Position[2] += 140.f;
            VectorCopy(Color, o->Angle);
            o->bRepeatedly = bRepeatedly;
            o->fRepeatedlyHeight = RequestTerrainHeight(o->Position[0], o->Position[1]) + 140.0f;
            o->Gravity = 10.f;
            o->Scale = scale;
            o->LifeTime = 0;
            o->bEnableMove = bMove;
            o->SpawnDelay = staggerDelay;
            o->bCombo = bCombo;
            return;
        }
    }
}


void RenderNumberPoints(vec3_t Position, int Num, vec3_t Color, float Alpha, float Scale)
{
    vec3_t p;
    VectorCopy(Position, p);
    vec3_t Light[4];
    VectorCopy(Color, Light[0]);
    VectorCopy(Color, Light[1]);
    VectorCopy(Color, Light[2]);
    VectorCopy(Color, Light[3]);

    char Text[32];
    itoa(Num, Text, 10);
    p[0] -= strlen(Text) * 5.f;
    unsigned int Length = strlen(Text);
    p[0] -= Length * Scale * 0.125f;
    p[1] -= Length * Scale * 0.125f;

    float sinTh = sinf((float)(ANGLE_TO_RAD * (g_Camera.Angle[2])));
    float cosTh = cosf((float)(ANGLE_TO_RAD * (g_Camera.Angle[2])));

    for (unsigned int i = 0;i < Length;i++)
    {
        float UV[4][2];
        float u = (float)(Text[i] - 48) * 16.f / 256.f;
        TEXCOORD(UV[0], u, 16.f / 32.f);
        TEXCOORD(UV[1], u + 16.f / 256.f, 16.f / 32.f);
        TEXCOORD(UV[2], u + 16.f / 256.f, 0.f);
        TEXCOORD(UV[3], u, 0.f);
        RenderSpriteUV(BITMAP_FONT + 1, p, Scale, Scale, UV, Light, Alpha);
        p[0] += Scale / 0.7071067f * cosTh / 2;
        p[1] -= Scale / 0.7071067f * sinTh / 2;
    }
}

void RenderPoints(BYTE byRenderOneMore)
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    EnableAlphaTest();
    DisableDepthTest();
    for (int i = 0; i < MAX_POINTS; i++)
    {
        PARTICLE* o = &Points[i];
        if (o->Live)
        {
            // Stagger queue: hide until SpawnDelay ticks down to zero.
            if (o->SpawnDelay > 0.f) continue;

            if (byRenderOneMore == 1)
            {
                if (o->Position[2] > 350.f) continue;
            }
            else if (byRenderOneMore == 2)
            {
                if (o->Position[2] <= 300.f) continue;
            }
            else if (o->bRepeatedly)
            {
                if (o->Position[2] <= o->fRepeatedlyHeight) continue;
            }

            if (o->Type > -1)
            {
                const float baseAlpha = o->Gravity * 0.4f;
                // Combo numbers are distinguished by gold color, pop-in
                // scale, COMBO label, and a slightly boosted alpha. No
                // separate "aura" pass — RenderNumberPoints centres glyphs
                // using a scale-dependent offset, so any larger underlay
                // ends up misaligned and reads as a duplicate shadow rather
                // than a glow.
                const float alpha = o->bCombo
                    ? std::min(1.f, baseAlpha * 1.4f)
                    : baseAlpha;
                RenderNumberPoints(o->Position, o->Type, o->Angle, alpha, o->Scale);
            }
            else
            {
                RenderNumber(o->Position, o->Type, o->Angle, o->Gravity * 0.4f, o->Scale);
            }
        }
    }
}

void RenderPointLabels()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }
    if (!g_pRenderText)
    {
        return;
    }

    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetBgColor(0);

    for (int i = 0; i < MAX_POINTS; i++)
    {
        const PARTICLE* o = &Points[i];
        if (!o->Live) continue;
        if (!o->bCombo) continue;
        if (o->SpawnDelay > 0.f) continue;
        if (o->Type < 0) continue;

        // Sit the label a bit above the number. The number itself centers on
        // o->Position; offset upward in world space then project.
        vec3_t labelPos;
        VectorCopy(o->Position, labelPos);
        labelPos[2] += o->Scale * 0.6f + 12.f;

        int sx = 0, sy = 0;
        CameraProjection::WorldToScreen(g_Camera, labelPos, &sx, &sy);
        if (sx <= 0 || sx >= REFERENCE_WIDTH || sy <= 0 || sy >= REFERENCE_HEIGHT)
            continue;

        // Fade with the number's own fade so the label disappears together.
        const float alpha01 = std::clamp(o->Gravity * 0.4f * 1.4f, 0.f, 1.f);
        const BYTE a = static_cast<BYTE>(alpha01 * 255.f);
        // Gold to match the digits.
        g_pRenderText->SetTextColor(255, 200, 25, a);
        g_pRenderText->RenderText(sx - 40, sy - 10, L"COMBO", 80, 0, RT3_SORT_CENTER);
    }

    g_pRenderText->SetTextColor(255, 255, 255, 255);
}

void MovePoints()
{
    if (!g_pOption->GetRenderAllEffects())
    {
        return;
    }

    for (int i = 0; i < MAX_POINTS; i++)
    {
        PARTICLE* o = &Points[i];
        if (o->Live)
        {
            if (o->SpawnDelay > 0.f)
            {
                o->SpawnDelay -= FPS_ANIMATION_FACTOR;
                if (o->SpawnDelay < 0.f) o->SpawnDelay = 0.f;
                continue;
            }

            o->LifeTime -= FPS_ANIMATION_FACTOR;
            if (o->LifeTime < 0)
            {
                if (o->bRepeatedly && o->Position[2] > o->fRepeatedlyHeight)
                {
                    o->Gravity = 10.0f;
                    o->bRepeatedly = false;
                }
                if (o->bEnableMove)
                {
                    o->Position[2] += o->Gravity * FPS_ANIMATION_FACTOR;
                }
                o->Gravity -= 0.3f * FPS_ANIMATION_FACTOR;
                if (o->Gravity <= 0.f)
                    o->Live = false;
                if (o->Type != -2)
                {
                    o->Scale -= 5.f * FPS_ANIMATION_FACTOR;//20.f;
                    if (o->Scale < 15.f)
                        o->Scale = 15.f;
                }
            }
        }
    }
}