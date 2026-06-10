#include "stdafx.h"
#include "DungeonReadyCheckState.h"

// UTF-8 -> wide string. The packet carries strings as raw UTF-8 with a byte-length prefix.
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

namespace
{
    // Little, bounds-checked cursor over the packet body. Every read validates against Size and sets a
    // failure flag instead of reading past the end, so a malformed packet can't crash the client.
    struct Reader
    {
        const uint8_t* buf;
        int32_t        size;
        int32_t        pos;
        bool           ok{true};

        Reader(const uint8_t* b, int32_t s, int32_t start) : buf(b), size(s), pos(start) {}

        uint8_t U8()
        {
            if (!ok || pos + 1 > size) { ok = false; return 0; }
            return buf[pos++];
        }

        uint16_t U16()
        {
            if (!ok || pos + 2 > size) { ok = false; return 0; }
            uint16_t v = static_cast<uint16_t>(buf[pos] | (buf[pos + 1] << 8));
            pos += 2;
            return v;
        }

        int16_t I16() { return static_cast<int16_t>(U16()); }

        std::wstring Str()
        {
            uint8_t len = U8();
            if (!ok || pos + len > size) { ok = false; return std::wstring(); }
            std::wstring s = Utf8ToWide(buf + pos, len);
            pos += len;
            return s;
        }
    };
}

namespace BloodlustMU
{

DungeonReadyCheckState& DungeonReadyCheckState::Instance()
{
    static DungeonReadyCheckState s_instance;
    return s_instance;
}

bool DungeonReadyCheckState::ApplyDifficultyPacket(const uint8_t* ReceiveBuffer, int32_t Size)
{
    // Packet shape (see ShowDungeonDifficultyPlugIn.cs on the server). C2 header is 5 bytes; the body
    // starts at offset 5 with the warp index. All multi-byte ints are little-endian.
    if (Size < 8) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x04) return false;

    Reader r(ReceiveBuffer, Size, 5);
    const int16_t warpIndex = r.I16();
    const std::wstring dungeonName = r.Str();
    const uint8_t tierCount = r.U8();
    if (!r.ok) return false;

    std::vector<DungeonTierView> tiers;
    tiers.reserve(tierCount);
    for (uint8_t i = 0; i < tierCount; ++i)
    {
        DungeonTierView t;
        t.order = r.U8();
        t.r = r.U8();
        t.g = r.U8();
        t.b = r.U8();
        t.monsterStatMult = r.U16() / 100.f;
        t.eliteChanceBonus = r.U8() / 100.f;
        t.startingLives = r.U8();
        t.expMult = r.U16() / 100.f;
        const uint8_t flags = r.U8();
        t.guaranteedChampion = (flags & 0x01) != 0;
        t.affordable = (flags & 0x02) != 0;
        t.name = r.Str();

        const uint8_t reqCount = r.U8();
        for (uint8_t j = 0; j < reqCount && r.ok; ++j)
        {
            DungeonReqView req;
            req.count = r.U16();
            req.available = r.U16();
            req.itemGroup = r.U8();
            req.itemNumber = r.I16();
            req.itemName = r.Str();
            t.requirements.push_back(std::move(req));
        }

        t.lootLevelLow = r.U8();
        t.lootLevelHigh = r.U8();

        if (!r.ok) return false;
        tiers.push_back(std::move(t));
    }

    if (!r.ok) return false;

    m_diffWarpIndex = warpIndex;
    m_diffDungeonName = dungeonName;
    m_tiers = std::move(tiers);
    m_difficultyActive = true;
    return true;
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
    //   then (optional, appended): hasTier(byte) tierR tierG tierB tierNameLen tierName(UTF-8)
    if (Size < 11) return false;
    if (ReceiveBuffer[0] != 0xC2) return false;
    if (ReceiveBuffer[3] != 0xCC) return false;
    if (ReceiveBuffer[4] != 0x02) return false;

    Reader r(ReceiveBuffer, Size, 5);
    const int16_t warpIndex = r.I16();
    const uint8_t total = r.U8();
    const uint8_t readyCount = r.U8();
    const uint8_t myReady = r.U8();
    const std::wstring name = r.Str();
    if (!r.ok) return false;

    // Optional tier block (older servers won't send it).
    bool hasTier = false;
    std::wstring tierName;
    uint8_t tr = 255, tg = 255, tb = 255;
    if (r.pos < Size)
    {
        const uint8_t flag = r.U8();
        tr = r.U8();
        tg = r.U8();
        tb = r.U8();
        tierName = r.Str();
        hasTier = r.ok && flag != 0 && !tierName.empty();
    }

    m_warpIndex = warpIndex;
    m_total = total;
    m_readyCount = readyCount;
    m_myReady = myReady != 0;
    m_dungeonName = name;
    m_hasTier = hasTier;
    m_tierName = tierName;
    m_tierR = tr;
    m_tierG = tg;
    m_tierB = tb;
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
