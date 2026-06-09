//*****************************************************************************
// File: EliteMonsterStore.cpp
//*****************************************************************************

#include "stdafx.h"
#include "GameLogic/Combat/EliteMonsterStore.h"

#include <mutex>
#include <unordered_map>

namespace
{
    std::mutex s_Mutex;
    std::unordered_map<uint16_t, GameLogic::EliteMonster::Info> s_Elites;
}

namespace GameLogic::EliteMonster
{
    bool Set(uint16_t targetId, uint8_t rank, uint8_t r, uint8_t g, uint8_t b, bool enraged, const wchar_t* name)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        const bool isNew = s_Elites.find(targetId) == s_Elites.end();

        Info info;
        info.rank = rank;
        info.r = r;
        info.g = g;
        info.b = b;
        info.enraged = enraged;
        if (name != nullptr)
        {
            wcsncpy_s(info.name, name, _TRUNCATE);
        }

        s_Elites[targetId] = info;
        return isNew;
    }

    bool Get(uint16_t targetId, Info& out)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = s_Elites.find(targetId);
        if (it == s_Elites.end())
        {
            return false;
        }

        out = it->second;
        return true;
    }

    bool IsElite(uint16_t targetId)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        return s_Elites.find(targetId) != s_Elites.end();
    }

    void Reset(uint16_t targetId)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Elites.erase(targetId);
    }
}
