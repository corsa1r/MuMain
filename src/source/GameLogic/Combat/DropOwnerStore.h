//*****************************************************************************
// File: DropOwnerStore.h
//*****************************************************************************

#pragma once

#include <cstddef>
#include <cstdint>

namespace GameLogic::DropOwner
{
    // Owner tag pushed by the server (C1 0xAB sub-op 0x09), keyed by the dropped item's network id (the
    // Items[] index). Reward drops are bound to the clearing player for a short window; while bound, the
    // ground label is prefixed "(Owner's) …". After the window the tag expires and the name renders plain.

    // Network handler: bind the drop to an owner for the given number of seconds.
    void Set(uint16_t dropId, const wchar_t* ownerName, int bindSeconds);

    // Render thread: if the drop is still bound, copies the owner name into 'out' and returns true.
    // Returns false (and forgets an expired tag) once the bind window has elapsed or the drop is unknown.
    bool Get(uint16_t dropId, wchar_t* out, size_t outCount);

    // Clears the owner tag for a drop. Call when the Items[] slot is recycled (drop picked up / removed).
    void Reset(uint16_t dropId);
}
