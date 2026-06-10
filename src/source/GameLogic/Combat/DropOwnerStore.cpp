//*****************************************************************************
// File: DropOwnerStore.cpp
//*****************************************************************************

#include "stdafx.h"
#include "GameLogic/Combat/DropOwnerStore.h"

#include <mutex>
#include <string>
#include <unordered_map>

#include <windows.h>

namespace
{
    struct OwnerTag
    {
        std::wstring name;
        DWORD bindUntilTick = 0; // GetTickCount() value past which the bind has expired.
    };

    std::mutex s_Mutex;
    std::unordered_map<uint16_t, OwnerTag> s_Owners;
}

namespace GameLogic::DropOwner
{
    void Set(uint16_t dropId, const wchar_t* ownerName, int bindSeconds)
    {
        if (ownerName == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(s_Mutex);
        OwnerTag tag;
        tag.name = ownerName;
        tag.bindUntilTick = GetTickCount() + static_cast<DWORD>((bindSeconds > 0 ? bindSeconds : 0) * 1000);
        s_Owners[dropId] = std::move(tag);
    }

    bool Get(uint16_t dropId, wchar_t* out, size_t outCount)
    {
        if (out == nullptr || outCount == 0)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(s_Mutex);
        auto it = s_Owners.find(dropId);
        if (it == s_Owners.end())
        {
            return false;
        }

        // GetTickCount() wraps roughly every 49.7 days; the signed difference keeps the comparison correct
        // across a wrap for the short (~minute) bind windows we use here.
        if (static_cast<LONG>(it->second.bindUntilTick - GetTickCount()) <= 0)
        {
            s_Owners.erase(it);
            return false;
        }

        wcsncpy_s(out, outCount, it->second.name.c_str(), _TRUNCATE);
        return true;
    }

    void Reset(uint16_t dropId)
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        s_Owners.erase(dropId);
    }
}
