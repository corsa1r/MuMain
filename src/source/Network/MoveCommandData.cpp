// MoveCommandData.cpp: implementation of the CMoveCommandData class.
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "MoveCommandData.h"
#include "Network/ServerMapManifest.h"

#include <cwchar>

using namespace SEASON3B;

#pragma pack(push, 1)
typedef struct
{
    int		index;
    char	szMainMapName[32];
    char	szSubMapName[32];
    int		iReqLevel;
    int		m_iReqMaxLevel;
    int		iReqZen;
    int		iGateNum;
} MOVEREQINFO_FILE;
#pragma pack(pop)

CMoveCommandData::CMoveCommandData()
{
}

CMoveCommandData::~CMoveCommandData()
{
    Release();
}

CMoveCommandData* CMoveCommandData::GetInstance()
{
    static CMoveCommandData s_Instance;
    return &s_Instance;
}

bool CMoveCommandData::Create(const std::wstring& filename)
{
    FILE* fp = _wfopen(filename.c_str(), L"rb");
    if (fp == NULL) return false;

    int count = 0;
    fread(&count, sizeof(int), 1, fp);

    for (int i = 0; i < count; i++)
    {
        auto* pMoveInfoData = new MOVEINFODATA;
        MOVEREQINFO_FILE moveReqInfo{};
        fread(&moveReqInfo, sizeof moveReqInfo, 1, fp);

        BuxConvert((BYTE*)&moveReqInfo, sizeof moveReqInfo);
        pMoveInfoData->_ReqInfo.index = moveReqInfo.index;
        pMoveInfoData->_ReqInfo.iGateNum = moveReqInfo.iGateNum;
        pMoveInfoData->_ReqInfo.iReqLevel = moveReqInfo.iReqLevel;
        pMoveInfoData->_ReqInfo.iReqZen = moveReqInfo.iReqZen;
        pMoveInfoData->_ReqInfo.m_iReqMaxLevel = moveReqInfo.m_iReqMaxLevel;
        CMultiLanguage::ConvertFromUtf8(pMoveInfoData->_ReqInfo.szMainMapName, moveReqInfo.szMainMapName, sizeof moveReqInfo.szMainMapName);
        CMultiLanguage::ConvertFromUtf8(pMoveInfoData->_ReqInfo.szSubMapName, moveReqInfo.szSubMapName, sizeof moveReqInfo.szSubMapName);

        m_listMoveInfoData.push_back(pMoveInfoData);
    }
    fclose(fp);

    return true;
}

void CMoveCommandData::Release()
{
    auto li = m_listMoveInfoData.begin();
    for (; li != m_listMoveInfoData.end(); li++)
        delete (*li);
    m_listMoveInfoData.clear();
}

bool CMoveCommandData::OpenMoveReqScript(const std::wstring& filename)
{
    return CMoveCommandData::GetInstance()->Create(filename);
}

void CMoveCommandData::RebuildFromServerManifest()
{
    // Wipe whatever was loaded from BMD or a prior manifest.
    Release();

    const auto& warps = BloodlustMU::ServerMapManifest::Instance().Warps();
    const auto& maps  = BloodlustMU::ServerMapManifest::Instance().Maps();

    auto FindMapName = [&maps](int mapNumber) -> const wchar_t*
    {
        for (const auto& m : maps)
        {
            if (m.number == mapNumber)
            {
                return m.name.c_str();
            }
        }
        return L"";
    };

    for (const auto& w : warps)
    {
        auto* p = new MOVEINFODATA{};
        p->_ReqInfo.index = w.index;
        p->_ReqInfo.iReqLevel = w.levelRequirement;
        p->_ReqInfo.m_iReqMaxLevel = 0;
        p->_ReqInfo.iReqZen = w.costs;
        p->_ReqInfo.iGateNum = 0;
        // Main map name = the destination map's display name from the manifest.
        std::wcsncpy(p->_ReqInfo.szMainMapName, FindMapName(w.targetMapNumber),
                     std::size(p->_ReqInfo.szMainMapName) - 1);
        // Sub label = the warp entry's own name (e.g. "Devias2"). Players see this
        // when one map has several variants in the list.
        std::wcsncpy(p->_ReqInfo.szSubMapName, w.name.c_str(),
                     std::size(p->_ReqInfo.szSubMapName) - 1);
        m_listMoveInfoData.push_back(p);
    }

    m_listMoveInfoData.sort([](const MOVEINFODATA* a, const MOVEINFODATA* b)
    {
        return a->_ReqInfo.index < b->_ReqInfo.index;
    });
}

int CMoveCommandData::GetNumMoveMap()
{
    if (m_listMoveInfoData.size() > 0)
        return m_listMoveInfoData.size();

    return -1;
}

const CMoveCommandData::MOVEINFODATA* CMoveCommandData::GetMoveCommandDataByIndex(int iIndex)
{
    auto li = m_listMoveInfoData.begin();
    for (; li != m_listMoveInfoData.end(); li++)
    {
        if ((*li)->_ReqInfo.index == iIndex)
        {
            return (*li);
        }
    }
    return 0;
}

const std::list<CMoveCommandData::MOVEINFODATA*>& CMoveCommandData::GetMoveCommandDatalist()
{
    return m_listMoveInfoData;
}