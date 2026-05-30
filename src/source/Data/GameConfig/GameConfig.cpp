#include "stdafx.h"
#include "GameConfig.h"

#include <imagehlp.h>

#include "GameConfigConstants.h"
#include <windows.h>

GameConfig& GameConfig::GetInstance()
{
    static GameConfig instance;
    return instance;
}

GameConfig::GameConfig()
{
    // Get executable directory and construct config path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    // Find last backslash to get directory
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash)
    {
        *(lastSlash + 1) = L'\0';  // Keep the trailing backslash
    }

    m_configPath = exePath;
    m_configPath += L"config.ini";

    Load();
}

void GameConfig::Load()
{
    using namespace CfgSections;
    using namespace CfgKeys;
    using namespace CfgDefaults;

    m_windowWidth  = ReadInt(CfgSectionWindow, CfgKeyWidth, CfgDefaultWindowWidth);
    m_windowHeight = ReadInt(CfgSectionWindow, CfgKeyHeight, CfgDefaultWindowHeight);
    m_windowMode   = ReadBool(CfgSectionWindow, CfgKeyWindowed, CfgDefaultWindowed);

    m_soundVolume  = ReadInt(CfgSectionAudio, CfgKeySoundVolume, CfgDefaultSoundVolume);
    m_musicVolume  = ReadInt(CfgSectionAudio, CfgKeyMusicVolume, CfgDefaultMusicVolume);

    m_rememberMe        = ReadBool(CfgSectionLogin, CfgKeyRememberMe, CfgDefaultRememberMe);
    m_languageSelection = ReadString(CfgSectionLogin, CfgKeyLanguage, CfgDefaultLanguage);
    m_encryptedUsername = ReadString(CfgSectionLogin, CfgKeyEncryptedUsername, CfgDefaultEncryptedUsername);
    m_encryptedPassword = ReadString(CfgSectionLogin, CfgKeyEncryptedPassword, CfgDefaultEncryptedPassword);

    m_serverIP   = ReadString(CfgSectionConnectionSettings, CfgKeyServerIP, CfgDefaultServerIP);
    m_serverPort = ReadInt(CfgSectionConnectionSettings, CfgKeyServerPort, CfgDefaultServerPort);

    m_zoom = ReadInt(CfgSectionCamera, CfgKeyZoom, CfgDefaultZoom);

    // Read BEFORE the obsolete-section cleanup below. (The previous bug: this
    // was read in Winmain AFTER Load() had already deleted [Graphics], so the
    // flag was always the default.)
    // Post-process chain — read BEFORE the obsolete-section cleanup below. (The
    // previous bug: these were read in Winmain AFTER Load() had already deleted
    // [Graphics], so the flags were always the defaults.)
    m_postProcess      = ReadBool (CfgSectionGraphics, CfgKeyPostProcess, CfgDefaultPostProcess);
    m_bloom            = ReadBool (CfgSectionGraphics, CfgKeyBloom, CfgDefaultBloom);
    m_bloomStrength    = ReadInt  (CfgSectionGraphics, CfgKeyBloomStrength, CfgDefaultBloomStrength);
    m_bloomThreshold   = ReadFloat(CfgSectionGraphics, CfgKeyBloomThreshold, CfgDefaultBloomThreshold);
    m_toneMap          = ReadBool (CfgSectionGraphics, CfgKeyToneMap, CfgDefaultToneMap);
    m_exposure         = ReadFloat(CfgSectionGraphics, CfgKeyExposure, CfgDefaultExposure);
    m_colorGrade       = ReadBool (CfgSectionGraphics, CfgKeyColorGrade, CfgDefaultColorGrade);
    m_contrast         = ReadFloat(CfgSectionGraphics, CfgKeyContrast, CfgDefaultContrast);
    m_saturation       = ReadFloat(CfgSectionGraphics, CfgKeySaturation, CfgDefaultSaturation);
    m_brightness       = ReadFloat(CfgSectionGraphics, CfgKeyBrightness, CfgDefaultBrightness);
    m_temperature      = ReadFloat(CfgSectionGraphics, CfgKeyTemperature, CfgDefaultTemperature);
    m_gradeShadows     = ReadFloat(CfgSectionGraphics, CfgKeyGradeShadows, CfgDefaultGradeShadows);
    m_gradeMidtones    = ReadFloat(CfgSectionGraphics, CfgKeyGradeMidtones, CfgDefaultGradeMidtones);
    m_gradeHighlights  = ReadFloat(CfgSectionGraphics, CfgKeyGradeHighlights, CfgDefaultGradeHighlights);
    m_vignette         = ReadBool (CfgSectionGraphics, CfgKeyVignette, CfgDefaultVignette);
    m_vignetteStrength = ReadFloat(CfgSectionGraphics, CfgKeyVignetteStrength, CfgDefaultVignetteStrength);
    m_vignetteRadius   = ReadFloat(CfgSectionGraphics, CfgKeyVignetteRadius, CfgDefaultVignetteRadius);
    m_msaa             = ReadBool (CfgSectionGraphics, CfgKeyMSAA, CfgDefaultMSAA);
    m_msaaSamples      = ReadInt  (CfgSectionGraphics, CfgKeyMSAASamples, CfgDefaultMSAASamples);
    m_fxaa             = ReadBool (CfgSectionGraphics, CfgKeyFXAA, CfgDefaultFXAA);
    m_sharpen          = ReadBool (CfgSectionGraphics, CfgKeySharpen, CfgDefaultSharpen);
    m_sharpenStrength  = ReadFloat(CfgSectionGraphics, CfgKeySharpenStrength, CfgDefaultSharpenStrength);
    m_filmGrain        = ReadBool (CfgSectionGraphics, CfgKeyFilmGrain, CfgDefaultFilmGrain);
    m_filmGrainStrength = ReadFloat(CfgSectionGraphics, CfgKeyFilmGrainStrength, CfgDefaultFilmGrainStrength);

    // Strip keys/sections we used to write but no longer use, so user config
    // files don't accumulate orphans. Append one line per retired key — no
    // central registry of valid keys to keep in sync.
    // NOTE: [Graphics] is a LIVE section again (post-process) — strip only its
    // dead keys, NOT the whole section, or the saved flags would be wiped.
    RemoveObsoleteKey(CfgSectionGraphics, L"RenderTextType");
    RemoveObsoleteKey(CfgSectionGraphics, L"ColorDepth");      // 16/32bpp toggle, dead since fullscreen uses GetDesktopBitsPerPel
    RemoveObsoleteKey(CfgSectionAudio,    L"SoundEnabled");   // replaced by SoundVolume==0
    RemoveObsoleteKey(CfgSectionAudio,    L"MusicEnabled");   // replaced by MusicVolume==0
    RemoveObsoleteKey(CfgSectionAudio,    L"VolumeLevel");    // legacy single-volume key
    RemoveObsoleteKey(CfgSectionLogin,    L"Version");        // launcher metadata, never read by client
    RemoveObsoleteKey(CfgSectionLogin,    L"TestVersion");    // launcher metadata, never read by client
    RemoveObsoleteSection(L"PARTITION");                      // launcher metadata, never read by client

    // Seed config.ini with the full [Graphics] block so every tunable is
    // discoverable/editable even on a fresh config or before the first Save().
    // (Idempotent — values were just read above; this only materializes them.)
    PersistGraphics();
}

void GameConfig::Save()
{
    using namespace CfgSections;
    using namespace CfgKeys;

    WriteInt(CfgSectionWindow, CfgKeyWidth, m_windowWidth);
    WriteInt(CfgSectionWindow, CfgKeyHeight, m_windowHeight);
    WriteBool(CfgSectionWindow, CfgKeyWindowed, m_windowMode);

    WriteInt(CfgSectionAudio, CfgKeySoundVolume, m_soundVolume);
    WriteInt(CfgSectionAudio, CfgKeyMusicVolume, m_musicVolume);

    WriteBool(CfgSectionLogin, CfgKeyRememberMe, m_rememberMe);
    WriteString(CfgSectionLogin, CfgKeyLanguage, m_languageSelection);
    WriteString(CfgSectionLogin, CfgKeyEncryptedUsername, m_encryptedUsername);
    WriteString(CfgSectionLogin, CfgKeyEncryptedPassword, m_encryptedPassword);

    WriteString(CfgSectionConnectionSettings, CfgKeyServerIP, m_serverIP);
    WriteInt(CfgSectionConnectionSettings, CfgKeyServerPort, m_serverPort);

    WriteInt(CfgSectionCamera, CfgKeyZoom, m_zoom);

    PersistGraphics();
}

void GameConfig::PersistGraphics()
{
    using namespace CfgSections;
    using namespace CfgKeys;

    WriteBool (CfgSectionGraphics, CfgKeyPostProcess, m_postProcess);
    WriteBool (CfgSectionGraphics, CfgKeyBloom, m_bloom);
    WriteInt  (CfgSectionGraphics, CfgKeyBloomStrength, m_bloomStrength);
    WriteFloat(CfgSectionGraphics, CfgKeyBloomThreshold, m_bloomThreshold);
    WriteBool (CfgSectionGraphics, CfgKeyToneMap, m_toneMap);
    WriteFloat(CfgSectionGraphics, CfgKeyExposure, m_exposure);
    WriteBool (CfgSectionGraphics, CfgKeyColorGrade, m_colorGrade);
    WriteFloat(CfgSectionGraphics, CfgKeyContrast, m_contrast);
    WriteFloat(CfgSectionGraphics, CfgKeySaturation, m_saturation);
    WriteFloat(CfgSectionGraphics, CfgKeyBrightness, m_brightness);
    WriteFloat(CfgSectionGraphics, CfgKeyTemperature, m_temperature);
    WriteFloat(CfgSectionGraphics, CfgKeyGradeShadows, m_gradeShadows);
    WriteFloat(CfgSectionGraphics, CfgKeyGradeMidtones, m_gradeMidtones);
    WriteFloat(CfgSectionGraphics, CfgKeyGradeHighlights, m_gradeHighlights);
    WriteBool (CfgSectionGraphics, CfgKeyVignette, m_vignette);
    WriteFloat(CfgSectionGraphics, CfgKeyVignetteStrength, m_vignetteStrength);
    WriteFloat(CfgSectionGraphics, CfgKeyVignetteRadius, m_vignetteRadius);
    WriteBool (CfgSectionGraphics, CfgKeyMSAA, m_msaa);
    WriteInt  (CfgSectionGraphics, CfgKeyMSAASamples, m_msaaSamples);
    WriteBool (CfgSectionGraphics, CfgKeyFXAA, m_fxaa);
    WriteBool (CfgSectionGraphics, CfgKeySharpen, m_sharpen);
    WriteFloat(CfgSectionGraphics, CfgKeySharpenStrength, m_sharpenStrength);
    WriteBool (CfgSectionGraphics, CfgKeyFilmGrain, m_filmGrain);
    WriteFloat(CfgSectionGraphics, CfgKeyFilmGrainStrength, m_filmGrainStrength);
}

void GameConfig::SetWindowSize(int width, int height)
{
    m_windowWidth = width;
    m_windowHeight = height;
}

void GameConfig::SetWindowMode(bool windowed)
{
    m_windowMode = windowed;
}

void GameConfig::SetSoundVolume(int level)
{
    m_soundVolume = level;
}

void GameConfig::SetMusicVolume(int level)
{
    m_musicVolume = level;
}

void GameConfig::SetRememberMe(bool remember)
{
    m_rememberMe = remember;
}

void GameConfig::SetLanguageSelection(const std::wstring& lang)
{
    m_languageSelection = lang;
}

void GameConfig::SetEncryptedUsername(const std::wstring& encryptedUsername)
{
    m_encryptedUsername = encryptedUsername;
}

void GameConfig::SetEncryptedPassword(const std::wstring& encryptedPassword)
{
    m_encryptedPassword = encryptedPassword;
}

void GameConfig::SetServerIP(const std::wstring& ip)
{
    m_serverIP = ip;
}

void GameConfig::SetServerPort(int port)
{
    m_serverPort = port;
}

void GameConfig::SetZoom(int zoom)
{
    m_zoom = zoom;
}

// Helper function to convert binary data to hex string
std::wstring GameConfig::BinaryToHex(const BYTE* data, DWORD size)
{
    std::wstring hex;
    hex.reserve(size * 2);

    const wchar_t hexChars[] = L"0123456789ABCDEF";
    for (DWORD i = 0; i < size; ++i)
    {
        hex += hexChars[(data[i] >> 4) & 0x0F];
        hex += hexChars[data[i] & 0x0F];
    }

    return hex;
}

// Helper function to convert hex string to binary data
std::vector<BYTE> GameConfig::HexToBinary(const std::wstring& hex)
{
    std::vector<BYTE> binary;

    if (hex.empty() || hex.length() % 2 != 0)
        return binary;

    binary.reserve(hex.length() / 2);

    auto hex_char_to_byte = [](wchar_t c) -> BYTE {
        if (c >= L'0' && c <= L'9') return (c - L'0');
        if (c >= L'a' && c <= L'f') return (c - L'a' + 10);
        return (c - L'A' + 10);
    };

    for (size_t i = 0; i < hex.length(); i += 2)
    {
        wchar_t high = hex[i];
        wchar_t low = hex[i + 1];

        if (!iswxdigit(high) || !iswxdigit(low))
        {
            // Invalid hex character detected, return empty vector
            return {};
        }

        binary.push_back((hex_char_to_byte(high) << 4) | hex_char_to_byte(low));
    }

    return binary;
}

void GameConfig::DecryptCredentials(wchar_t* outUser, wchar_t* outPass, size_t userBufSize, size_t passBufSize)
{
    // Decrypt Username
    std::wstring user = DecryptSetting(GetEncryptedUsername());
    if (!user.empty()) {
        wcsncpy_s(outUser, userBufSize, user.c_str(), _TRUNCATE);
    }

    // Decrypt Password
    std::wstring pass = DecryptSetting(GetEncryptedPassword());
    if (!pass.empty()) {
        wcsncpy_s(outPass, passBufSize, pass.c_str(), _TRUNCATE);
    }
}

// Helper functions using Windows INI API
int GameConfig::ReadInt(const wchar_t* section, const wchar_t* key, int defaultValue)
{
    return GetPrivateProfileIntW(section, key, defaultValue, m_configPath.c_str());
}

void GameConfig::WriteInt(const wchar_t* section, const wchar_t* key, int value)
{
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d", value);

    WritePrivateProfileStringW(section, key, buffer, m_configPath.c_str());
}

bool GameConfig::ReadBool(const wchar_t* section, const wchar_t* key, bool defaultValue)
{
    return GetPrivateProfileIntW(section, key, defaultValue ? 1 : 0, m_configPath.c_str()) != 0;
}

void GameConfig::SetPostProcess(bool enabled)
{
    // Fully qualified: this function is outside the `using namespace` scope that
    // Load()/Save() rely on.
    m_postProcess = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyPostProcess, enabled);
}

void GameConfig::SetBloom(bool enabled)
{
    m_bloom = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyBloom, enabled);
}

void GameConfig::SetBloomStrength(int strength)
{
    m_bloomStrength = strength;
    WriteInt(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyBloomStrength, strength);
}

// --- Remaining [Graphics] setters (write-through to config.ini) -------------
// All fully qualify the section/key namespaces because these live outside the
// `using namespace` scope of Load()/Save().
void GameConfig::SetBloomThreshold(float v)
{
    m_bloomThreshold = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyBloomThreshold, v);
}
void GameConfig::SetToneMap(bool enabled)
{
    m_toneMap = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyToneMap, enabled);
}
void GameConfig::SetExposure(float v)
{
    m_exposure = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyExposure, v);
}
void GameConfig::SetColorGrade(bool enabled)
{
    m_colorGrade = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyColorGrade, enabled);
}
void GameConfig::SetContrast(float v)
{
    m_contrast = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyContrast, v);
}
void GameConfig::SetSaturation(float v)
{
    m_saturation = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeySaturation, v);
}
void GameConfig::SetBrightness(float v)
{
    m_brightness = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyBrightness, v);
}
void GameConfig::SetTemperature(float v)
{
    m_temperature = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyTemperature, v);
}
void GameConfig::SetGradeShadows(float v)
{
    m_gradeShadows = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyGradeShadows, v);
}
void GameConfig::SetGradeMidtones(float v)
{
    m_gradeMidtones = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyGradeMidtones, v);
}
void GameConfig::SetGradeHighlights(float v)
{
    m_gradeHighlights = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyGradeHighlights, v);
}
void GameConfig::SetVignette(bool enabled)
{
    m_vignette = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyVignette, enabled);
}
void GameConfig::SetVignetteStrength(float v)
{
    m_vignetteStrength = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyVignetteStrength, v);
}
void GameConfig::SetVignetteRadius(float v)
{
    m_vignetteRadius = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyVignetteRadius, v);
}
void GameConfig::SetMSAA(bool enabled)
{
    m_msaa = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyMSAA, enabled);
}
void GameConfig::SetMSAASamples(int samples)
{
    m_msaaSamples = samples;
    WriteInt(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyMSAASamples, samples);
}
void GameConfig::SetFXAA(bool enabled)
{
    m_fxaa = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyFXAA, enabled);
}
void GameConfig::SetSharpen(bool enabled)
{
    m_sharpen = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeySharpen, enabled);
}
void GameConfig::SetSharpenStrength(float v)
{
    m_sharpenStrength = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeySharpenStrength, v);
}
void GameConfig::SetFilmGrain(bool enabled)
{
    m_filmGrain = enabled;
    WriteBool(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyFilmGrain, enabled);
}
void GameConfig::SetFilmGrainStrength(float v)
{
    m_filmGrainStrength = v;
    WriteFloat(CfgSections::CfgSectionGraphics, CfgKeys::CfgKeyFilmGrainStrength, v);
}

void GameConfig::WriteBool(const wchar_t* section, const wchar_t* key, bool value)
{
    WritePrivateProfileStringW(section, key, value ? L"1" : L"0", m_configPath.c_str());
}

float GameConfig::ReadFloat(const wchar_t* section, const wchar_t* key, float defaultValue)
{
    wchar_t defBuf[64];
    swprintf_s(defBuf, L"%g", defaultValue);

    wchar_t buf[64]{};
    GetPrivateProfileStringW(section, key, defBuf, buf, static_cast<DWORD>(_countof(buf)), m_configPath.c_str());
    return static_cast<float>(wcstod(buf, nullptr));
}

void GameConfig::WriteFloat(const wchar_t* section, const wchar_t* key, float value)
{
    wchar_t buf[64];
    swprintf_s(buf, L"%g", value);
    WritePrivateProfileStringW(section, key, buf, m_configPath.c_str());
}

std::wstring GameConfig::ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& defaultValue)
{
    std::vector<wchar_t> buffer(2048);
    while (true)
    {
        DWORD charsRead = GetPrivateProfileStringW(section, key, defaultValue.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()), m_configPath.c_str());
        if (charsRead < buffer.size() - 1)
        {
            return std::wstring(buffer.data());
        }
        buffer.resize(buffer.size() * 2);
    }
}

void GameConfig::WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    WritePrivateProfileStringW(section, key, value.c_str(), m_configPath.c_str());
}

void GameConfig::RemoveObsoleteKey(const wchar_t* section, const wchar_t* key)
{
    // Passing nullptr as the value deletes the key (Windows INI API).
    WritePrivateProfileStringW(section, key, nullptr, m_configPath.c_str());
}

void GameConfig::RemoveObsoleteSection(const wchar_t* section)
{
    // Passing nullptr as the key deletes the entire section.
    WritePrivateProfileStringW(section, nullptr, nullptr, m_configPath.c_str());
}

std::wstring GameConfig::DecryptSetting(const std::wstring& hexInput)
{
    if (hexInput.empty()) return L"";

    // Convert Hex String back to Binary Blob
    std::vector<BYTE> encryptedData = HexToBinary(hexInput);
    if (encryptedData.empty()) return L"";

    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = encryptedData.data();
    dataIn.cbData = static_cast<DWORD>(encryptedData.size());

    // Decrypt using Windows DPAPI
    if (CryptUnprotectData(&dataIn, nullptr, nullptr, nullptr, nullptr, 0, &dataOut))
    {
        std::wstring result(reinterpret_cast<wchar_t*>(dataOut.pbData), dataOut.cbData / sizeof(wchar_t));
        LocalFree(dataOut.pbData); // Safety: Windows allocated this, we free it
        // The decrypted string might contain the null terminator, let's remove it if it exists.
        if (!result.empty() && result.back() == L'\0') {
            result.pop_back();
        }
        return result;
    }

    return L"";
}

std::wstring GameConfig::EncryptSetting(const wchar_t* input)
{
    if (!input || wcslen(input) == 0) return L"";

    DATA_BLOB dataIn, dataOut;
    dataIn.cbData = static_cast<DWORD>((wcslen(input) + 1) * sizeof(wchar_t));
    dataIn.pbData = reinterpret_cast<BYTE*>(const_cast<wchar_t*>(input));

    if (CryptProtectData(&dataIn, nullptr, nullptr, nullptr, nullptr, 0, &dataOut))
    {
        std::wstring hexResult = BinaryToHex(dataOut.pbData, dataOut.cbData);
        LocalFree(dataOut.pbData);
        return hexResult;
    }
    return L"";
}

void GameConfig::EncryptAndSaveCredentials(const wchar_t* user, const wchar_t* pass)
{
    std::wstring encUser = EncryptSetting(user);
    std::wstring encPass = EncryptSetting(pass);

    if (!encUser.empty() && !encPass.empty())
    {
        SetEncryptedUsername(encUser);
        SetEncryptedPassword(encPass);
        Save(); // Actually write to the .ini file
    }
}
