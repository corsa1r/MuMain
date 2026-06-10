#pragma once

// Dungeon flow client state (difficulty selection + ready check).
//
// Two server-pushed windows share this state, both on the C2 / 0xCC channel:
//   * 0x04 — difficulty options: the leader, on talking to a dungeon portal, receives the list of
//     selectable difficulty tiers (scaling, entry requirements, reward gear level range). Drives
//     CNewUIDungeonDifficultySelectBox; the leader picks one and the client sends 0xCD back.
//   * 0x02 — ready-check show/update (+ the chosen tier name/color): after the leader picks, every
//     participant gets the ready-check window with a live "ready (X/N)" count. 0x03 dismisses it.
//
// Layering: networking-side holder. The parsers live here; the UI only reads the accessors.

#include <string>
#include <vector>
#include <cstdint>

namespace BloodlustMU
{
    // One entry requirement for a tier: a quantity of a representative item plus how many the player holds.
    struct DungeonReqView
    {
        int          count{0};
        int          available{0};
        uint8_t      itemGroup{0};
        int16_t      itemNumber{0};
        std::wstring itemName;
    };

    // One selectable difficulty tier.
    struct DungeonTierView
    {
        uint8_t      order{0};
        std::wstring name;
        uint8_t      r{255}, g{255}, b{255};
        float        monsterStatMult{1.f};   // e.g. 6.0 = x6
        float        eliteChanceBonus{0.f};   // 0..1, added
        int          startingLives{0};
        float        expMult{1.f};
        bool         guaranteedChampion{false};
        bool         affordable{false};
        std::vector<DungeonReqView>  requirements;
        uint8_t      lootLevelLow{0};         // reward gear item-level range (low..high) for this tier
        uint8_t      lootLevelHigh{0};
    };

    class DungeonReadyCheckState
    {
    public:
        static DungeonReadyCheckState& Instance();

        // ---- Difficulty selection (0x04) ----

        // Parse a server-pushed difficulty-options packet (C2 / 0xCC / 0x04). Returns true on success,
        // in which case IsDifficultyActive() becomes true and the tier list is populated.
        bool ApplyDifficultyPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        bool IsDifficultyActive() const { return m_difficultyActive; }
        void CloseDifficulty() { m_difficultyActive = false; }
        int  GetDifficultyWarpIndex() const { return m_diffWarpIndex; }
        const std::wstring& GetDifficultyDungeonName() const { return m_diffDungeonName; }
        const std::vector<DungeonTierView>& GetTiers() const { return m_tiers; }

        // ---- Ready check (0x02 / 0x03) ----

        // Parse a server-pushed ready-check show/update packet (C2 / 0xCC / 0x02). Returns true on
        // success, in which case the state is updated and IsActive() becomes true.
        bool ApplyShowPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        // Parse a server-pushed dismiss packet (C2 / 0xCC / 0x03). Marks the check inactive so the
        // window closes itself.
        bool ApplyDismissPacket(const uint8_t* ReceiveBuffer, int32_t Size);

        bool IsActive() const { return m_active; }
        bool IsThisPlayerReady() const { return m_myReady; }
        int  GetTotalPlayers() const { return m_total; }
        int  GetReadyCount() const { return m_readyCount; }
        const std::wstring& GetDungeonName() const { return m_dungeonName; }

        bool HasTier() const { return m_hasTier; }
        const std::wstring& GetTierName() const { return m_tierName; }
        void GetTierColor(uint8_t& r, uint8_t& g, uint8_t& b) const { r = m_tierR; g = m_tierG; b = m_tierB; }

    private:
        DungeonReadyCheckState() = default;

        // Difficulty selection.
        bool                         m_difficultyActive{false};
        int                          m_diffWarpIndex{-1};
        std::wstring                 m_diffDungeonName;
        std::vector<DungeonTierView> m_tiers;

        // Ready check.
        bool         m_active{false};
        bool         m_myReady{false};
        int          m_warpIndex{-1};
        int          m_total{0};
        int          m_readyCount{0};
        std::wstring m_dungeonName;

        // Chosen tier carried on the ready-check packet.
        bool         m_hasTier{false};
        std::wstring m_tierName;
        uint8_t      m_tierR{255}, m_tierG{255}, m_tierB{255};
    };
}
