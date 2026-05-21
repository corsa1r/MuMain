#pragma once

#include "UI/NewUI/NewUIBase.h"
#include "UI/NewUI/NewUIManager.h"

namespace SEASON3B
{
    // Maximum number of other players tracked alongside self.
    static constexpr int DPS_MAX_OTHER_ENTRIES = 4;

    // Object ID the server uses to identify the local player in DPS packets.
    static constexpr WORD DPS_SELF_OBJECT_ID = 0x0200;

    struct DpsEntry
    {
        WORD    ObjectId;
        wchar_t Name[MAX_MONSTER_NAME + 1];
        DWORD   Dps;
        DWORD   LastUpdateTick; // GetTickCount() timestamp
    };

    class CDpsMeterWindow : public CNewUIObj
    {
    public:
        CDpsMeterWindow();
        virtual ~CDpsMeterWindow();

        bool Create(CNewUIManager* pNewUIMng);
        void Release();

        // Called from packet handler with data parsed from the 0x0A packet.
        void UpdateEntry(WORD objectId, const wchar_t* name, DWORD dps);

        // CNewUIObj overrides
        bool UpdateMouseEvent() override;
        bool UpdateKeyEvent()   override;
        bool Update()           override;
        bool Render()           override;

        float GetLayerDepth()   override; // 6.0f — renders above most HUD elements

        void OpenningProcess();
        void ClosingProcess();

    private:
        void PurgeExpiredEntries();
        void SortOtherEntries();

        void RenderBackground(int x, int y, int width, int height) const;
        void RenderSelfRow(int x, int y, int rowWidth) const;
        void RenderOtherRow(int x, int y, int rowWidth, const DpsEntry& entry) const;
        void RenderDpsNumber(int rightEdgeX, int y, DWORD dps) const;

        int  CalcWindowHeight() const;
        int  ActiveOtherCount() const;

        CNewUIManager* m_pNewUIMng;
        DpsEntry       m_Self;
        DpsEntry       m_Others[DPS_MAX_OTHER_ENTRIES];
        int            m_nOtherCount;
    };
}
