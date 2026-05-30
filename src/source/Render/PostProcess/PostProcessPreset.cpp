// PostProcessPreset.cpp — per-map presets + global override. See header.

#include "stdafx.h"
#include "PostProcessPreset.h"
#include "PostProcessChain.h"

#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

namespace PostProcess
{
    namespace Presets
    {
        namespace
        {
            Settings s_globalBase;          // the global [Graphics] look
            bool     s_globalOverride = false;

            Settings s_current;             // last settings ApplyForWorld pushed
            int      s_activeWorld = -999;  // world id of that apply (sentinel = none)

            const wchar_t* kSection = L"PostProcess";

            std::wstring MapDir()  { return L"Data\\PostProcess\\maps"; }
            std::wstring MapPath(int worldId)
            {
                wchar_t buf[64];
                swprintf_s(buf, L"\\map%d.ini", worldId);
                return MapDir() + buf;
            }

            // --- INI scalar helpers (one [PostProcess] section per file) ------
            void WB(const std::wstring& path, const wchar_t* key, bool v)
            {
                WritePrivateProfileStringW(kSection, key, v ? L"1" : L"0", path.c_str());
            }
            void WI(const std::wstring& path, const wchar_t* key, int v)
            {
                wchar_t b[32]; swprintf_s(b, L"%d", v);
                WritePrivateProfileStringW(kSection, key, b, path.c_str());
            }
            void WF(const std::wstring& path, const wchar_t* key, float v)
            {
                wchar_t b[64]; swprintf_s(b, L"%g", v);
                WritePrivateProfileStringW(kSection, key, b, path.c_str());
            }
            void WS(const std::wstring& path, const wchar_t* key, const std::string& v)
            {
                std::wstring w(v.begin(), v.end());
                WritePrivateProfileStringW(kSection, key, w.c_str(), path.c_str());
            }

            bool RB(const std::wstring& path, const wchar_t* key, bool def)
            {
                return GetPrivateProfileIntW(kSection, key, def ? 1 : 0, path.c_str()) != 0;
            }
            int RI(const std::wstring& path, const wchar_t* key, int def)
            {
                return GetPrivateProfileIntW(kSection, key, def, path.c_str());
            }
            float RF(const std::wstring& path, const wchar_t* key, float def)
            {
                wchar_t defBuf[64]; swprintf_s(defBuf, L"%g", def);
                wchar_t buf[64]{};
                GetPrivateProfileStringW(kSection, key, defBuf, buf,
                                         static_cast<DWORD>(std::size(buf)), path.c_str());
                return static_cast<float>(wcstod(buf, nullptr));
            }
            std::string RS(const std::wstring& path, const wchar_t* key, const std::string& def)
            {
                std::wstring wdef(def.begin(), def.end());
                wchar_t buf[260]{};
                GetPrivateProfileStringW(kSection, key, wdef.c_str(), buf,
                                         static_cast<DWORD>(std::size(buf)), path.c_str());
                std::wstring w(buf);
                return std::string(w.begin(), w.end());
            }

            // Serialize every Settings field. Defaults (for read) come from a
            // default-constructed Settings, so a partial/old file still loads.
            void WriteAll(const std::wstring& p, const Settings& s)
            {
                WB(p, L"SSAO", s.ssao); WF(p, L"SSAORadius", s.ssaoRadius);
                WF(p, L"SSAOStrength", s.ssaoStrength); WF(p, L"SSAOPower", s.ssaoPower);
                WB(p, L"Fog", s.fog);
                WF(p, L"FogColorR", s.fogR); WF(p, L"FogColorG", s.fogG); WF(p, L"FogColorB", s.fogB);
                WF(p, L"FogDensity", s.fogDensity); WF(p, L"FogStart", s.fogStart);
                WF(p, L"FogHeightStrength", s.fogHeightStrength); WF(p, L"FogHeightTop", s.fogHeightTop);
                WB(p, L"Bloom", s.bloom); WI(p, L"BloomStrength", s.bloomStrength);
                WF(p, L"BloomThreshold", s.bloomThreshold);
                WB(p, L"ToneMap", s.toneMap); WF(p, L"Exposure", s.exposure);
                WB(p, L"ColorGrade", s.colorGrade);
                WF(p, L"Contrast", s.contrast); WF(p, L"Saturation", s.saturation);
                WF(p, L"Brightness", s.brightness); WF(p, L"Temperature", s.temperature);
                WF(p, L"GradeShadows", s.gradeShadows); WF(p, L"GradeMidtones", s.gradeMidtones);
                WF(p, L"GradeHighlights", s.gradeHighlights);
                WB(p, L"Vignette", s.vignette); WF(p, L"VignetteStrength", s.vignetteStrength);
                WF(p, L"VignetteRadius", s.vignetteRadius);
                WB(p, L"MSAA", s.msaa); WI(p, L"MSAASamples", s.msaaSamples);
                WB(p, L"FXAA", s.fxaa);
                WB(p, L"Sharpen", s.sharpen); WF(p, L"SharpenStrength", s.sharpenStrength);
                WB(p, L"FilmGrain", s.filmGrain); WF(p, L"FilmGrainStrength", s.filmGrainStrength);
                WB(p, L"LUT", s.lut); WS(p, L"LUTFile", s.lutFile);
            }
            void ReadAll(const std::wstring& p, Settings& s)
            {
                const Settings d;   // defaults
                s.ssao = RB(p, L"SSAO", d.ssao); s.ssaoRadius = RF(p, L"SSAORadius", d.ssaoRadius);
                s.ssaoStrength = RF(p, L"SSAOStrength", d.ssaoStrength); s.ssaoPower = RF(p, L"SSAOPower", d.ssaoPower);
                s.fog = RB(p, L"Fog", d.fog);
                s.fogR = RF(p, L"FogColorR", d.fogR); s.fogG = RF(p, L"FogColorG", d.fogG); s.fogB = RF(p, L"FogColorB", d.fogB);
                s.fogDensity = RF(p, L"FogDensity", d.fogDensity); s.fogStart = RF(p, L"FogStart", d.fogStart);
                s.fogHeightStrength = RF(p, L"FogHeightStrength", d.fogHeightStrength); s.fogHeightTop = RF(p, L"FogHeightTop", d.fogHeightTop);
                s.bloom = RB(p, L"Bloom", d.bloom); s.bloomStrength = RI(p, L"BloomStrength", d.bloomStrength);
                s.bloomThreshold = RF(p, L"BloomThreshold", d.bloomThreshold);
                s.toneMap = RB(p, L"ToneMap", d.toneMap); s.exposure = RF(p, L"Exposure", d.exposure);
                s.colorGrade = RB(p, L"ColorGrade", d.colorGrade);
                s.contrast = RF(p, L"Contrast", d.contrast); s.saturation = RF(p, L"Saturation", d.saturation);
                s.brightness = RF(p, L"Brightness", d.brightness); s.temperature = RF(p, L"Temperature", d.temperature);
                s.gradeShadows = RF(p, L"GradeShadows", d.gradeShadows); s.gradeMidtones = RF(p, L"GradeMidtones", d.gradeMidtones);
                s.gradeHighlights = RF(p, L"GradeHighlights", d.gradeHighlights);
                s.vignette = RB(p, L"Vignette", d.vignette); s.vignetteStrength = RF(p, L"VignetteStrength", d.vignetteStrength);
                s.vignetteRadius = RF(p, L"VignetteRadius", d.vignetteRadius);
                s.msaa = RB(p, L"MSAA", d.msaa); s.msaaSamples = RI(p, L"MSAASamples", d.msaaSamples);
                s.fxaa = RB(p, L"FXAA", d.fxaa);
                s.sharpen = RB(p, L"Sharpen", d.sharpen); s.sharpenStrength = RF(p, L"SharpenStrength", d.sharpenStrength);
                s.filmGrain = RB(p, L"FilmGrain", d.filmGrain); s.filmGrainStrength = RF(p, L"FilmGrainStrength", d.filmGrainStrength);
                s.lut = RB(p, L"LUT", d.lut); s.lutFile = RS(p, L"LUTFile", d.lutFile);
            }
        } // namespace

        void Init(const Settings& globalBase, bool globalOverride)
        {
            s_globalBase = globalBase;
            s_globalOverride = globalOverride;
            s_current = globalBase;   // sensible default until first ApplyForWorld
        }

        const Settings& GetCurrent() { return s_current; }
        int GetActiveWorld() { return s_activeWorld; }

        void SetGlobalBase(const Settings& globalBase) { s_globalBase = globalBase; }
        const Settings& GetGlobalBase() { return s_globalBase; }

        void SetGlobalOverride(bool enabled) { s_globalOverride = enabled; }
        bool GetGlobalOverride() { return s_globalOverride; }

        bool HasMapPreset(int worldId)
        {
            DWORD a = GetFileAttributesW(MapPath(worldId).c_str());
            return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        }

        bool LoadMapPreset(int worldId, Settings& out)
        {
            if (!HasMapPreset(worldId))
                return false;
            ReadAll(MapPath(worldId), out);
            return true;
        }

        bool SaveMapPreset(int worldId, const Settings& s)
        {
            // Ensure Data\PostProcess\maps exists (best-effort, nested).
            CreateDirectoryW(L"Data\\PostProcess", nullptr);
            CreateDirectoryW(MapDir().c_str(), nullptr);
            WriteAll(MapPath(worldId), s);
            return true;
        }

        bool DeleteMapPreset(int worldId)
        {
            return DeleteFileW(MapPath(worldId).c_str()) != 0;
        }

        void ApplyForWorld(int worldId)
        {
            s_activeWorld = worldId;
            if (s_globalOverride)
            {
                s_current = s_globalBase;
                Chain::ApplySettings(s_current);
                return;
            }
            Settings s;
            if (worldId >= 0 && LoadMapPreset(worldId, s))
                s_current = s;
            else
                s_current = s_globalBase;
            Chain::ApplySettings(s_current);
        }
    } // namespace Presets
}
