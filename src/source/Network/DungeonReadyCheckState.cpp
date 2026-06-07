#include "stdafx.h"
#include "DungeonReadyCheckState.h"

// UTF-8 -> wide string. The packet carries the dungeon name as raw UTF-8 with a byte-length prefix.
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

DungeonReadyCheckState& DungeonReadyCheckState::Instance()
{
    static DungeonReadyCheckState s_instance;
    return s_instance;
}

bool DungeonReadyCheckState::ApplyShowPacket(const uint8_t* ReceiveBuffer, int32_t Size)
{
    // Packet shape (see ShowDungeonReadyCheckPlugIn.cs on the server):
    //   [0]      0xC2
    //   [1-2]    total length (big-endian)
    //   [3]      0xCC
    //   [4]      0x02          (sub-op: ready-check show/update)
    //   [5-6]    warpIndex     (little-endian int16)
    //   [7]      totalPlayers  (byte)
    //   [8]      readyCount    (byte)
    //   [9]      myReady       (byte, 0/1)
    //   [10]     nameLen       (byte)
    //   [11..]   name          (UTF-8, nameLen bytes)
    constexpr int WarpIndexOffset = 5;
    constexpr int TotalOffset = 7;
    constexpr int ReadyCountOffset = 8;
    constexpr int MyReadyOffset = 9;
    constexpr int NameLenOffset = 10;
    constexpr int NameOffset = 11;

    if (Size < NameOffset) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x02) return false;

    const int16_t warpIndex = static_cast<int16_t>(
        ReceiveBuffer[WarpIndexOffset] | (ReceiveBuffer[WarpIndexOffset + 1] << 8));
    const uint8_t total = ReceiveBuffer[TotalOffset];
    const uint8_t readyCount = ReceiveBuffer[ReadyCountOffset];
    const uint8_t myReady = ReceiveBuffer[MyReadyOffset];
    const uint8_t nameLen = ReceiveBuffer[NameLenOffset];
    if (NameOffset + nameLen > Size) return false;

    m_warpIndex = warpIndex;
    m_total = total;
    m_readyCount = readyCount;
    m_myReady = myReady != 0;
    m_dungeonName = Utf8ToWide(ReceiveBuffer + NameOffset, nameLen);
    m_active = true;
    return true;
}

bool DungeonReadyCheckState::ApplyDismissPacket(const uint8_t* ReceiveBuffer, int32_t Size)
{
    //   [0]   0xC2
    //   [1-2] total length (6)
    //   [3]   0xCC
    //   [4]   0x03   (sub-op: dismiss)
    //   [5]   reason (0 cancelled, 1 entering) — currently unused client-side
    if (Size < 6) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x03) return false;

    m_active = false;
    return true;
}

} // namespace BloodlustMU
