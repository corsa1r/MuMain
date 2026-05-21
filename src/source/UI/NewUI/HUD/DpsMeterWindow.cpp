#include "stdafx.h"

#include "UI/NewUI/HUD/DpsMeterWindow.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Render/Textures/ZzzOpenglUtil.h"
#include "UI/Legacy/UIControls.h"

using namespace SEASON3B;

namespace
{
    // Layout
    constexpr int WINDOW_WIDTH      = 130;
    constexpr int WINDOW_MARGIN     = 6;    // distance from screen edges
    constexpr int MAINFRAME_HEIGHT  = 51;   // height of the bottom HUD bar
    constexpr int PADDING_X         = 6;
    constexpr int PADDING_Y         = 3;
    constexpr int TITLE_HEIGHT      = 14;
    constexpr int ROW_HEIGHT        = 16;

    // Data lifetime: entries not updated for this long are considered inactive.
    constexpr DWORD ENTRY_EXPIRY_MS = 7000;

    // Background opacity
    constexpr float BG_ALPHA        = 0.50f;

    // Highlight row tint for own player
    constexpr float SELF_ROW_ALPHA  = 0.20f;

    // Colors (R, G, B, A) used with g_pRenderText->SetTextColor
    constexpr BYTE COLOR_TITLE_R    = 180;
    constexpr BYTE COLOR_TITLE_G    = 180;
    constexpr BYTE COLOR_TITLE_B    = 180;

    constexpr BYTE COLOR_SELF_R     = 255;
    constexpr BYTE COLOR_SELF_G     = 210;
    constexpr BYTE COLOR_SELF_B     = 0;    // gold

    constexpr BYTE COLOR_OTHER_R    = 220;
    constexpr BYTE COLOR_OTHER_G    = 220;
    constexpr BYTE COLOR_OTHER_B    = 220;  // near-white

    constexpr BYTE COLOR_DPS_R      = 255;
    constexpr BYTE COLOR_DPS_G      = 255;
    constexpr BYTE COLOR_DPS_B      = 255;

    constexpr BYTE ALPHA_FULL           = 255;

    bool IsExpired(DWORD lastTick)
    {
        return (GetTickCount() - lastTick) > ENTRY_EXPIRY_MS;
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CDpsMeterWindow::CDpsMeterWindow()
    : m_pNewUIMng(nullptr)
    , m_nOtherCount(0)
{
    ZeroMemory(&m_Self,   sizeof(m_Self));
    ZeroMemory(m_Others, sizeof(m_Others));
}

CDpsMeterWindow::~CDpsMeterWindow()
{
    Release();
}

bool CDpsMeterWindow::Create(CNewUIManager* pNewUIMng)
{
    if (pNewUIMng == nullptr)
        return false;

    m_pNewUIMng = pNewUIMng;
    m_pNewUIMng->AddUIObj(SEASON3B::INTERFACE_DPS_METER, this);

    Show(true);
    return true;
}

void CDpsMeterWindow::Release()
{
    if (m_pNewUIMng)
    {
        m_pNewUIMng->RemoveUIObj(this);
        m_pNewUIMng = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Data update (called from packet handler)
// ---------------------------------------------------------------------------

void CDpsMeterWindow::UpdateEntry(WORD objectId, const wchar_t* name, DWORD dps)
{
    if (objectId == DPS_SELF_OBJECT_ID)
    {
        m_Self.ObjectId      = objectId;
        m_Self.Dps           = dps;
        m_Self.LastUpdateTick = GetTickCount();
        wcsncpy_s(m_Self.Name, name, _TRUNCATE);
        return;
    }

    // Search existing other entries.
    for (int i = 0; i < m_nOtherCount; ++i)
    {
        if (m_Others[i].ObjectId == objectId)
        {
            m_Others[i].Dps           = dps;
            m_Others[i].LastUpdateTick = GetTickCount();
            wcsncpy_s(m_Others[i].Name, name, _TRUNCATE);
            return;
        }
    }

    // New entry — add if a slot is free, otherwise replace the lowest-DPS slot.
    if (m_nOtherCount < DPS_MAX_OTHER_ENTRIES)
    {
        DpsEntry& entry      = m_Others[m_nOtherCount++];
        entry.ObjectId       = objectId;
        entry.Dps            = dps;
        entry.LastUpdateTick = GetTickCount();
        wcsncpy_s(entry.Name, name, _TRUNCATE);
        return;
    }

    int lowestIdx = 0;
    for (int i = 1; i < m_nOtherCount; ++i)
    {
        if (m_Others[i].Dps < m_Others[lowestIdx].Dps)
            lowestIdx = i;
    }

    DpsEntry& slot      = m_Others[lowestIdx];
    slot.ObjectId       = objectId;
    slot.Dps            = dps;
    slot.LastUpdateTick = GetTickCount();
    wcsncpy_s(slot.Name, name, _TRUNCATE);
}

// ---------------------------------------------------------------------------
// CNewUIObj interface
// ---------------------------------------------------------------------------

bool CDpsMeterWindow::UpdateMouseEvent() { return true; }
bool CDpsMeterWindow::UpdateKeyEvent()   { return true; }
bool CDpsMeterWindow::Update()           { return true; }

float CDpsMeterWindow::GetLayerDepth()   { return 6.0f; }

void CDpsMeterWindow::OpenningProcess()  {}
void CDpsMeterWindow::ClosingProcess()   {}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

bool CDpsMeterWindow::Render()
{
    PurgeExpiredEntries();

    const bool hasSelf = (m_Self.LastUpdateTick != 0) && !IsExpired(m_Self.LastUpdateTick);
    const int  otherCount = ActiveOtherCount();

    if (!hasSelf && otherCount == 0)
        return true;

    SortOtherEntries();

    const int windowHeight = CalcWindowHeight();
    const int x = REFERENCE_WIDTH - WINDOW_WIDTH - WINDOW_MARGIN;
    const int y = REFERENCE_HEIGHT - MAINFRAME_HEIGHT - WINDOW_MARGIN - windowHeight;

    RenderBackground(x, y, WINDOW_WIDTH, windowHeight);

    // Title row
    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetBgColor(0);
    g_pRenderText->SetTextColor(COLOR_TITLE_R, COLOR_TITLE_G, COLOR_TITLE_B, ALPHA_FULL);
    g_pRenderText->RenderText(x + PADDING_X, y + PADDING_Y, L"DPS", WINDOW_WIDTH - PADDING_X * 2, 0, RT3_SORT_CENTER);

    int rowY = y + PADDING_Y + TITLE_HEIGHT;

    if (hasSelf)
    {
        RenderSelfRow(x, rowY, WINDOW_WIDTH);
        rowY += ROW_HEIGHT;
    }

    for (int i = 0; i < m_nOtherCount; ++i)
    {
        if (!IsExpired(m_Others[i].LastUpdateTick))
        {
            RenderOtherRow(x, rowY, WINDOW_WIDTH, m_Others[i]);
            rowY += ROW_HEIGHT;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void CDpsMeterWindow::PurgeExpiredEntries()
{
    // Compact the others array by removing expired entries.
    int writeIdx = 0;
    for (int i = 0; i < m_nOtherCount; ++i)
    {
        if (!IsExpired(m_Others[i].LastUpdateTick))
        {
            if (writeIdx != i)
                m_Others[writeIdx] = m_Others[i];
            ++writeIdx;
        }
    }
    m_nOtherCount = writeIdx;
}

void CDpsMeterWindow::SortOtherEntries()
{
    // Insertion sort — array is tiny (max 4), so this is fine.
    for (int i = 1; i < m_nOtherCount; ++i)
    {
        DpsEntry key = m_Others[i];
        int j = i - 1;
        while (j >= 0 && m_Others[j].Dps < key.Dps)
        {
            m_Others[j + 1] = m_Others[j];
            --j;
        }
        m_Others[j + 1] = key;
    }
}

void CDpsMeterWindow::RenderBackground(int x, int y, int width, int height) const
{
    EnableAlphaTest();
    RenderColor(x, y, width, height, BG_ALPHA, 1);
    EndRenderColor();
}

void CDpsMeterWindow::RenderSelfRow(int x, int y, int rowWidth) const
{
    // Subtle gold tint background for the self row.
    EnableAlphaTest();
    glColor4f(0.8f, 0.65f, 0.0f, SELF_ROW_ALPHA);
    RenderColor(x, y, rowWidth, ROW_HEIGHT);
    EndRenderColor();

    // ">" indicator + name (gold)
    wchar_t label[MAX_MONSTER_NAME + 4];
    mu_swprintf(label, L"> %ls", m_Self.Name);

    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetBgColor(0);
    g_pRenderText->SetTextColor(COLOR_SELF_R, COLOR_SELF_G, COLOR_SELF_B, ALPHA_FULL);
    g_pRenderText->RenderText(x + PADDING_X, y + 2, label, rowWidth - PADDING_X * 2, 0, RT3_SORT_LEFT);

    RenderDpsNumber(x + rowWidth - PADDING_X, y + 2, m_Self.Dps);
}

void CDpsMeterWindow::RenderOtherRow(int x, int y, int rowWidth, const DpsEntry& entry) const
{
    wchar_t label[MAX_MONSTER_NAME + 4];
    mu_swprintf(label, L". %ls", entry.Name);

    g_pRenderText->SetFont(g_hFont);
    g_pRenderText->SetBgColor(0);
    g_pRenderText->SetTextColor(COLOR_OTHER_R, COLOR_OTHER_G, COLOR_OTHER_B, ALPHA_FULL);
    g_pRenderText->RenderText(x + PADDING_X, y + 2, label, rowWidth - PADDING_X * 2, 0, RT3_SORT_LEFT);

    RenderDpsNumber(x + rowWidth - PADDING_X, y + 2, entry.Dps);
}

void CDpsMeterWindow::RenderDpsNumber(int rightEdgeX, int y, DWORD dps) const
{
    wchar_t buf[16];
    mu_swprintf(buf, L"%lu", dps);

    g_pRenderText->SetTextColor(COLOR_DPS_R, COLOR_DPS_G, COLOR_DPS_B, ALPHA_FULL);
    // RT3_SORT_RIGHT aligns the text so its right edge lands at x + width.
    // Pass a small fixed width (80px) right-aligned within the right portion.
    constexpr int DPS_TEXT_WIDTH = 80;
    g_pRenderText->RenderText(rightEdgeX - DPS_TEXT_WIDTH, y, buf, DPS_TEXT_WIDTH, 0, RT3_SORT_RIGHT);
}

int CDpsMeterWindow::CalcWindowHeight() const
{
    const bool hasSelf   = (m_Self.LastUpdateTick != 0) && !IsExpired(m_Self.LastUpdateTick);
    const int  rowCount  = (hasSelf ? 1 : 0) + ActiveOtherCount();
    return PADDING_Y + TITLE_HEIGHT + rowCount * ROW_HEIGHT + PADDING_Y;
}

int CDpsMeterWindow::ActiveOtherCount() const
{
    int count = 0;
    for (int i = 0; i < m_nOtherCount; ++i)
    {
        if (!IsExpired(m_Others[i].LastUpdateTick))
            ++count;
    }
    return count;
}
