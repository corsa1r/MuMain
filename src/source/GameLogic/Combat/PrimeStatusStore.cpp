//*****************************************************************************
// File: PrimeStatusStore.cpp
//*****************************************************************************

#include "stdafx.h"
#include "GameLogic/Combat/PrimeStatusStore.h"

#include <array>
#include <atomic>

namespace
{
    // One slot per possible network object ID (0x0000–0xFFFF).
    // std::atomic<uint8_t> gives correct cross-thread visibility
    // with no locking overhead.
    std::array<std::atomic<uint8_t>, 0x10000> s_Elements{};
}

namespace GameLogic::PrimeStatus
{
    void Set(uint16_t targetId, EPrimeElement element)
    {
        s_Elements[targetId].store(static_cast<uint8_t>(element), std::memory_order_relaxed);
    }

    void Clear(uint16_t targetId, EPrimeElement element)
    {
        uint8_t expected = static_cast<uint8_t>(element);
        s_Elements[targetId].compare_exchange_strong(expected, 0u, std::memory_order_relaxed);
    }

    EPrimeElement Get(uint16_t targetId)
    {
        return static_cast<EPrimeElement>(s_Elements[targetId].load(std::memory_order_relaxed));
    }
}
