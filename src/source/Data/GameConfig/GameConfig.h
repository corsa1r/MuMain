#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

class GameConfig
{
public:
    static GameConfig& GetInstance();

    void Load();
    void Save();

    // Window
    int  GetWindowWidth()  const { return m_windowWidth; }
    int  GetWindowHeight() const { return m_windowHeight; }
    bool GetWindowMode()   const { return m_windowMode; }

    void SetWindowSize(int width, int height);
    void SetWindowMode(bool windowed);

    // Audio — volume 0 = off, >0 = on. No separate Enabled flag.
    int  GetSoundVolume()  const { return m_soundVolume; }
    int  GetMusicVolume()  const { return m_musicVolume; }

    void SetSoundVolume(int level);
    void SetMusicVolume(int level);

    // Login
    bool GetRememberMe() const { return m_rememberMe; }
    void SetRememberMe(bool remember);

    std::wstring GetLanguageSelection() const { return m_languageSelection; }
    void SetLanguageSelection(const std::wstring& lang);

    void SetEncryptedUsername(const std::wstring& encryptedUsername);
    std::wstring GetEncryptedUsername() const { return m_encryptedUsername; }

    void SetEncryptedPassword(const std::wstring& encryptedPassword);
    std::wstring GetEncryptedPassword() const { return m_encryptedPassword; }

    // Connection
    std::wstring GetServerIP() const { return m_serverIP; }
    int GetServerPort() const { return m_serverPort; }

    void SetServerIP(const std::wstring& ip);
    void SetServerPort(int port);

    // Camera
    int GetZoom() const { return m_zoom; }
    void SetZoom(int zoom);

    // Graphics — [Graphics] PostProcess. First-class persisted setting so it
    // round-trips through Save() and survives the obsolete-section cleanup in
    // Load() (which otherwise wipes the whole [Graphics] section on startup).
    bool GetPostProcess() const { return m_postProcess; }
    void SetPostProcess(bool enabled);

    // [Graphics] SSAO — depth-based ambient occlusion.
    bool  GetSSAO()          const { return m_ssao; }
    float GetSSAORadius()    const { return m_ssaoRadius; }
    float GetSSAOStrength()  const { return m_ssaoStrength; }
    float GetSSAOPower()     const { return m_ssaoPower; }
    void SetSSAO(bool enabled);
    void SetSSAORadius(float v);
    void SetSSAOStrength(float v);
    void SetSSAOPower(float v);

    // [Graphics] Bloom — whether the bloom pass is active when the chain runs.
    bool GetBloom() const { return m_bloom; }
    void SetBloom(bool enabled);

    // [Graphics] BloomStrength — integer multiplier over the tuned baseline
    // glow (1 = baseline, 2 = double, ...). Stored raw; clamped at apply time.
    int  GetBloomStrength() const { return m_bloomStrength; }
    void SetBloomStrength(int strength);

    // [Graphics] — remaining post-process effect getters. All read-only here;
    // edited in config.ini. Each effect has an on/off plus its tunable value(s).
    float GetBloomThreshold()    const { return m_bloomThreshold; }
    bool  GetToneMap()           const { return m_toneMap; }
    float GetExposure()          const { return m_exposure; }
    bool  GetColorGrade()        const { return m_colorGrade; }
    float GetContrast()          const { return m_contrast; }
    float GetSaturation()        const { return m_saturation; }
    float GetBrightness()        const { return m_brightness; }
    float GetTemperature()       const { return m_temperature; }
    float GetGradeShadows()      const { return m_gradeShadows; }
    float GetGradeMidtones()     const { return m_gradeMidtones; }
    float GetGradeHighlights()   const { return m_gradeHighlights; }
    bool  GetVignette()          const { return m_vignette; }
    float GetVignetteStrength()  const { return m_vignetteStrength; }
    float GetVignetteRadius()    const { return m_vignetteRadius; }
    bool  GetMSAA()              const { return m_msaa; }
    int   GetMSAASamples()       const { return m_msaaSamples; }
    bool  GetFXAA()              const { return m_fxaa; }
    bool  GetSharpen()           const { return m_sharpen; }
    float GetSharpenStrength()   const { return m_sharpenStrength; }
    bool  GetFilmGrain()         const { return m_filmGrain; }
    float GetFilmGrainStrength() const { return m_filmGrainStrength; }

    // [Graphics] LUT — .cube color grade. File is a bare name under
    // Data/PostProcess/. Empty/missing => no grade.
    bool         GetLut()     const { return m_lut; }
    std::wstring GetLutFile() const { return m_lutFile; }
    void SetLut(bool enabled);
    void SetLutFile(const std::wstring& file);

    // [Graphics] — setters used by the live editor panel's "Save to config.ini".
    // Each updates the in-memory value and writes it through to config.ini.
    void SetBloomThreshold(float v);
    void SetToneMap(bool enabled);
    void SetExposure(float v);
    void SetColorGrade(bool enabled);
    void SetContrast(float v);
    void SetSaturation(float v);
    void SetBrightness(float v);
    void SetTemperature(float v);
    void SetGradeShadows(float v);
    void SetGradeMidtones(float v);
    void SetGradeHighlights(float v);
    void SetVignette(bool enabled);
    void SetVignetteStrength(float v);
    void SetVignetteRadius(float v);
    void SetMSAA(bool enabled);
    void SetMSAASamples(int samples);
    void SetFXAA(bool enabled);
    void SetSharpen(bool enabled);
    void SetSharpenStrength(float v);
    void SetFilmGrain(bool enabled);
    void SetFilmGrainStrength(float v);

    // [Graphics] Anisotropic — texture filtering quality (independent of the
    // post-process chain). Level is the max-anisotropy (e.g. 16), clamped to
    // the driver max at apply time. Takes effect on textures loaded after
    // startup (effectively: restart / map reload to change).
    bool GetAnisotropic()      const { return m_anisotropic; }
    int  GetAnisotropicLevel() const { return m_anisotropicLevel; }
    void SetAnisotropic(bool enabled);
    void SetAnisotropicLevel(int level);

    // Helpers
    static std::wstring BinaryToHex(const BYTE* data, DWORD size);
    static std::vector<BYTE> HexToBinary(const std::wstring& hex);

    void DecryptCredentials(wchar_t* outUser, wchar_t* outPass, size_t userBufSize, size_t passBufSize);
    void EncryptAndSaveCredentials(const wchar_t* user, const wchar_t* pass);

private:
    GameConfig();
    GameConfig(const GameConfig&) = delete;
    GameConfig& operator=(const GameConfig&) = delete;

    std::filesystem::path m_configPath;

    int  m_windowWidth;
    int  m_windowHeight;
    bool m_windowMode;

    int  m_soundVolume;
    int  m_musicVolume;

    bool m_rememberMe;
    std::wstring m_languageSelection;
    std::wstring m_encryptedUsername;
    std::wstring m_encryptedPassword;

    std::wstring m_serverIP;
    int m_serverPort;

    int m_zoom;

    bool m_postProcess;
    bool m_anisotropic;
    int  m_anisotropicLevel;
    bool  m_ssao;
    float m_ssaoRadius;
    float m_ssaoStrength;
    float m_ssaoPower;
    bool m_bloom;
    int  m_bloomStrength;
    float m_bloomThreshold;
    bool  m_toneMap;
    float m_exposure;
    bool  m_colorGrade;
    float m_contrast;
    float m_saturation;
    float m_brightness;
    float m_temperature;
    float m_gradeShadows;
    float m_gradeMidtones;
    float m_gradeHighlights;
    bool  m_vignette;
    float m_vignetteStrength;
    float m_vignetteRadius;
    bool  m_msaa;
    int   m_msaaSamples;
    bool  m_fxaa;
    bool  m_sharpen;
    float m_sharpenStrength;
    bool  m_filmGrain;
    float m_filmGrainStrength;
    bool         m_lut;
    std::wstring m_lutFile;

    int ReadInt(const wchar_t* section, const wchar_t* key, int defaultValue);
    void WriteInt(const wchar_t* section, const wchar_t* key, int value);

    bool ReadBool(const wchar_t* section, const wchar_t* key, bool defaultValue);
    void WriteBool(const wchar_t* section, const wchar_t* key, bool value);

    float ReadFloat(const wchar_t* section, const wchar_t* key, float defaultValue);
    void  WriteFloat(const wchar_t* section, const wchar_t* key, float value);

    // Write the whole [Graphics] post-process block. Used by both Load() (to
    // seed config.ini with all tunables so they're discoverable) and Save().
    void PersistGraphics();

    std::wstring ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& defaultValue);
    void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value);

    void RemoveObsoleteKey(const wchar_t* section, const wchar_t* key);
    void RemoveObsoleteSection(const wchar_t* section);

    std::wstring DecryptSetting(const std::wstring& hexInput);
    std::wstring EncryptSetting(const wchar_t* input);
};
