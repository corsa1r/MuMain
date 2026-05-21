//*****************************************************************************
// File: PrimeStatusStore.cpp
//*****************************************************************************

#include "stdafx.h"
#include "GameLogic/Combat/PrimeStatusStore.h"

#include <array>
#include <atomic>

namespace
{
    std::array<std::atomic<uint8_t>, 0x10000> s_Masks{};
}

namespace GameLogic::PrimeStatus
{
    void Set(uint16_t targetId, EPrimeElement element)
    {
        if (element == EPrimeElement::None)
        {
            return;
        }

        const uint8_t bit = ElementBit(element);
        uint8_t current = s_Masks[targetId].load(std::memory_order_relaxed);
        while (!s_Masks[targetId].compare_exchange_weak(current, current | bit, std::memory_order_relaxed))
        {
        }
    }

    void Clear(uint16_t targetId, EPrimeElement element)
    {
        if (element == EPrimeElement::None)
        {
            return;
        }

        const uint8_t bit = ElementBit(element);
        uint8_t current = s_Masks[targetId].load(std::memory_order_relaxed);
        while (!s_Masks[targetId].compare_exchange_weak(current, current & ~bit, std::memory_order_relaxed))
        {
        }
    }

    PrimeElementMask GetMask(uint16_t targetId)
    {
        return s_Masks[targetId].load(std::memory_order_relaxed);
    }

    void Reset(uint16_t targetId)
    {
        s_Masks[targetId].store(0, std::memory_order_relaxed);
    }
}
