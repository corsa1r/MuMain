//*****************************************************************************
// File: SkillComboStore.cpp
//*****************************************************************************

#include "stdafx.h"
#include "GameLogic/Skills/SkillComboStore.h"

#include <array>

namespace
{
    // Skill numbers in MU go up to ~700. 1024 gives comfortable headroom.
    constexpr uint16_t kMaxSkillNumber = 1024;
    std::array<uint8_t, kMaxSkillNumber> s_Types{};
    std::array<uint8_t, kMaxSkillNumber> s_Elements{};
}

namespace GameLogic::SkillCombo
{
    void SetComboInfo(uint16_t skillNumber, ESkillComboType comboType, ESkillComboElement element)
    {
        if (skillNumber >= kMaxSkillNumber)
            return;
        s_Types[skillNumber]    = static_cast<uint8_t>(comboType);
        s_Elements[skillNumber] = static_cast<uint8_t>(element);
    }

    void ResetAll()
    {
        s_Types.fill(0);
        s_Elements.fill(0);
    }

    ESkillComboType GetComboType(uint16_t skillNumber)
    {
        if (skillNumber >= kMaxSkillNumber)
            return ESkillComboType::None;
        return static_cast<ESkillComboType>(s_Types[skillNumber]);
    }

    ESkillComboElement GetComboElement(uint16_t skillNumber)
    {
        if (skillNumber >= kMaxSkillNumber)
            return ESkillComboElement::None;
        return static_cast<ESkillComboElement>(s_Elements[skillNumber]);
    }
}
