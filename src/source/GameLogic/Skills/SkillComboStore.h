//*****************************************************************************
// File: SkillComboStore.h
//*****************************************************************************

#pragma once

#include <cstdint>

// Mirrors server-side SkillComboType ordinals.
enum class ESkillComboType : uint8_t
{
    None      = 0,
    Primer    = 1,
    Detonator = 2,
};

// Mirrors server-side SkillComboElement ordinals (same ordinals as EPrimeElement).
enum class ESkillComboElement : uint8_t
{
    None      = 0,
    Fire      = 1,
    Ice       = 2,
    Lightning = 3,
    Physical  = 4,
};

namespace GameLogic::SkillCombo
{
    // Called from the network thread when the server sends skill combo config (C1 AB 05).
    void SetComboInfo(uint16_t skillNumber, ESkillComboType comboType, ESkillComboElement element);

    // Call at login / map entry before re-receiving config so stale data is flushed.
    void ResetAll();

    ESkillComboType    GetComboType(uint16_t skillNumber);
    ESkillComboElement GetComboElement(uint16_t skillNumber);

    // Returns TEXT_COLOR_* constant matching the health-bar element colour.
    inline int TooltipColor(ESkillComboElement element)
    {
        switch (element)
        {
        case ESkillComboElement::Fire:      return 13; // TEXT_COLOR_ORANGE
        case ESkillComboElement::Ice:       return 1;  // TEXT_COLOR_BLUE
        case ESkillComboElement::Lightning: return 3;  // TEXT_COLOR_YELLOW
        case ESkillComboElement::Physical:  return 10; // TEXT_COLOR_GRAY
        default:                            return 0;  // TEXT_COLOR_WHITE
        }
    }

    inline const wchar_t* ElementName(ESkillComboElement element)
    {
        switch (element)
        {
        case ESkillComboElement::Fire:      return L"Fire";
        case ESkillComboElement::Ice:       return L"Ice";
        case ESkillComboElement::Lightning: return L"Lightning";
        case ESkillComboElement::Physical:  return L"Physical";
        default:                            return L"";
        }
    }
}
