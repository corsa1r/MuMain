#include "stdafx.h"
#include "DungeonEnterPrompt.h"

// UTF-8 -> wide string. The packet carries the dungeon name as raw UTF-8 with a
// byte-length prefix; we widen to wstring for the rest of the (wide-string) UI.
static std::wstring Utf8ToWide(const uint8_t* bytes, size_t length)
{
    if (length == 0) return std::wstring();
    int needed = MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length),
        nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length),
        out.data(), needed);
    return out;
}

namespace BloodlustMU
{

DungeonEnterPrompt& DungeonEnterPrompt::Instance()
{
    static DungeonEnterPrompt s_instance;
    return s_instance;
}

bool DungeonEnterPrompt::ApplyFromPacket(const uint8_t* ReceiveBuffer, int32_t Size)
{
    // Packet shape (see ShowDungeonEnterConfirmPlugIn.cs on the server):
    //   [0]      0xC2
    //   [1-2]    total length (big-endian)
    //   [3]      0xCC          (operation: custom channel)
    //   [4]      0x02          (sub-op: dungeon enter confirm)
    //   [5-6]    warpIndex     (little-endian int16)
    //   [7]      nameLen       (byte)
    //   [8..]    name          (UTF-8, nameLen bytes)
    constexpr int WarpIndexOffset = 5;
    constexpr int NameLenOffset = 7;
    constexpr int NameOffset = 8;

    if (Size < NameOffset) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x02) return false;

    const int16_t warpIndex = static_cast<int16_t>(
        ReceiveBuffer[WarpIndexOffset] | (ReceiveBuffer[WarpIndexOffset + 1] << 8));

    const uint8_t nameLen = ReceiveBuffer[NameLenOffset];
    if (NameOffset + nameLen > Size) return false;

    m_warpIndex = warpIndex;
    m_dungeonName = Utf8ToWide(ReceiveBuffer + NameOffset, nameLen);
    return true;
}

} // namespace BloodlustMU
