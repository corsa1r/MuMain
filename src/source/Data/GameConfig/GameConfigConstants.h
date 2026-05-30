#pragma once

namespace CfgSections
{
    inline constexpr wchar_t CfgSectionWindow[]     = L"Window";
    inline constexpr wchar_t CfgSectionGraphics[]   = L"Graphics";
    inline constexpr wchar_t CfgSectionAudio[]      = L"Audio";
    inline constexpr wchar_t CfgSectionLogin[]      = L"LOGIN";
    inline constexpr wchar_t CfgSectionConnectionSettings[] = L"CONNECTION SETTINGS";
    inline constexpr wchar_t CfgSectionCamera[] = L"Camera";
}

namespace CfgKeys
{
    // Window
    inline constexpr wchar_t CfgKeyWidth[]      = L"Width";
    inline constexpr wchar_t CfgKeyHeight[]     = L"Height";
    inline constexpr wchar_t CfgKeyWindowed[]   = L"Windowed";

    // Audio — volume 0 = off, >0 = on (no separate Enabled flag).
    inline constexpr wchar_t CfgKeySoundVolume[]  = L"SoundVolume";
    inline constexpr wchar_t CfgKeyMusicVolume[] = L"MusicVolume";

    // Login
    inline constexpr wchar_t CfgKeyRememberMe[]        = L"RememberMe";
    inline constexpr wchar_t CfgKeyLanguage[]          = L"Language";
    inline constexpr wchar_t CfgKeyEncryptedUsername[] = L"EncryptedUsername";
    inline constexpr wchar_t CfgKeyEncryptedPassword[] = L"EncryptedPassword";

    // Connection
    inline constexpr wchar_t CfgKeyServerIP[]   = L"ServerIP";
    inline constexpr wchar_t CfgKeyServerPort[] = L"ServerPort";

    // Camera
    inline constexpr wchar_t CfgKeyZoom[] = L"Zoom";

    // Graphics — post-process chain
    inline constexpr wchar_t CfgKeyPostProcess[]     = L"PostProcess";
    inline constexpr wchar_t CfgKeyAnisotropic[]      = L"Anisotropic";
    inline constexpr wchar_t CfgKeyAnisotropicLevel[] = L"AnisotropicLevel";
    inline constexpr wchar_t CfgKeySSAO[]            = L"SSAO";
    inline constexpr wchar_t CfgKeySSAORadius[]      = L"SSAORadius";
    inline constexpr wchar_t CfgKeySSAOStrength[]    = L"SSAOStrength";
    inline constexpr wchar_t CfgKeySSAOPower[]       = L"SSAOPower";
    inline constexpr wchar_t CfgKeyBloom[]           = L"Bloom";
    inline constexpr wchar_t CfgKeyBloomStrength[]   = L"BloomStrength";
    inline constexpr wchar_t CfgKeyBloomThreshold[]  = L"BloomThreshold";
    inline constexpr wchar_t CfgKeyToneMap[]         = L"ToneMap";
    inline constexpr wchar_t CfgKeyExposure[]        = L"Exposure";
    inline constexpr wchar_t CfgKeyColorGrade[]      = L"ColorGrade";
    inline constexpr wchar_t CfgKeyContrast[]        = L"Contrast";
    inline constexpr wchar_t CfgKeySaturation[]      = L"Saturation";
    inline constexpr wchar_t CfgKeyBrightness[]      = L"Brightness";
    inline constexpr wchar_t CfgKeyTemperature[]     = L"Temperature";
    inline constexpr wchar_t CfgKeyGradeShadows[]    = L"GradeShadows";
    inline constexpr wchar_t CfgKeyGradeMidtones[]   = L"GradeMidtones";
    inline constexpr wchar_t CfgKeyGradeHighlights[] = L"GradeHighlights";
    inline constexpr wchar_t CfgKeyVignette[]        = L"Vignette";
    inline constexpr wchar_t CfgKeyVignetteStrength[] = L"VignetteStrength";
    inline constexpr wchar_t CfgKeyVignetteRadius[]  = L"VignetteRadius";
    inline constexpr wchar_t CfgKeyMSAA[]            = L"MSAA";
    inline constexpr wchar_t CfgKeyMSAASamples[]     = L"MSAASamples";
    inline constexpr wchar_t CfgKeyFXAA[]            = L"FXAA";
    inline constexpr wchar_t CfgKeySharpen[]         = L"Sharpen";
    inline constexpr wchar_t CfgKeySharpenStrength[] = L"SharpenStrength";
    inline constexpr wchar_t CfgKeyFilmGrain[]       = L"FilmGrain";
    inline constexpr wchar_t CfgKeyFilmGrainStrength[] = L"FilmGrainStrength";
}

namespace CfgDefaults
{
    inline constexpr int  CfgDefaultWindowWidth  = 1024;
    inline constexpr int  CfgDefaultWindowHeight = 768;
    inline constexpr bool CfgDefaultWindowed     = true;

    inline constexpr int  CfgDefaultSoundVolume = 5;
    inline constexpr int  CfgDefaultMusicVolume = 5;

    inline constexpr bool CfgDefaultRememberMe = false;
    inline constexpr wchar_t CfgDefaultLanguage[] = L"Eng";
    inline constexpr wchar_t CfgDefaultEncryptedUsername[] = L"";
    inline constexpr wchar_t CfgDefaultEncryptedPassword[] = L"";

    inline constexpr wchar_t CfgDefaultServerIP[] = L"127.127.127.127";
    inline constexpr int CfgDefaultServerPort = 44406;

    inline constexpr int CfgDefaultZoom = 1735;  // OrbitalCamera DEFAULT_RADIUS — matches Default-cam camera-to-Hero distance

    // Post-process chain off by default → guaranteed parity with the legacy
    // direct-to-backbuffer path until the user opts in.
    inline constexpr bool CfgDefaultPostProcess = false;

    // Anisotropic filtering on by default — pure texture-clarity win at MU's
    // oblique camera, independent of the post-process chain. 16x (clamped to
    // the driver's GL_MAX_TEXTURE_MAX_ANISOTROPY at apply time).
    inline constexpr bool CfgDefaultAnisotropic      = true;
    inline constexpr int  CfgDefaultAnisotropicLevel = 16;

    // SSAO off by default (heaviest, most scene-dependent effect).
    inline constexpr bool  CfgDefaultSSAO         = false;
    inline constexpr float CfgDefaultSSAORadius   = 60.0f;
    inline constexpr float CfgDefaultSSAOStrength = 1.0f;
    inline constexpr float CfgDefaultSSAOPower    = 1.5f;

    // Bloom on by default once the chain is enabled; strength is an integer
    // multiplier where 1 == the tuned baseline glow, 2 == twice as strong, etc.
    inline constexpr bool  CfgDefaultBloom          = true;
    inline constexpr int   CfgDefaultBloomStrength  = 1;
    inline constexpr float CfgDefaultBloomThreshold = 0.75f;

    // Remaining post-process effects: all ON by default with subtle values, so
    // PostProcess=1 yields a tasteful enhanced look. Each is independently
    // toggleable / tunable in config.ini.
    inline constexpr bool  CfgDefaultToneMap        = true;
    inline constexpr float CfgDefaultExposure       = 1.0f;

    inline constexpr bool  CfgDefaultColorGrade     = true;
    inline constexpr float CfgDefaultContrast       = 1.05f;
    inline constexpr float CfgDefaultSaturation     = 1.10f;
    inline constexpr float CfgDefaultBrightness     = 1.0f;
    inline constexpr float CfgDefaultTemperature    = 0.10f;
    inline constexpr float CfgDefaultGradeShadows    = 1.0f;
    inline constexpr float CfgDefaultGradeMidtones   = 1.0f;
    inline constexpr float CfgDefaultGradeHighlights = 1.0f;

    inline constexpr bool  CfgDefaultVignette        = true;
    inline constexpr float CfgDefaultVignetteStrength = 0.35f;
    inline constexpr float CfgDefaultVignetteRadius   = 0.75f;

    // MSAA off by default (heavier; only geometry edges). 4 samples when on.
    inline constexpr bool  CfgDefaultMSAA           = false;
    inline constexpr int   CfgDefaultMSAASamples    = 4;

    inline constexpr bool  CfgDefaultFXAA           = true;

    inline constexpr bool  CfgDefaultSharpen        = true;
    inline constexpr float CfgDefaultSharpenStrength = 0.30f;

    inline constexpr bool  CfgDefaultFilmGrain      = true;
    inline constexpr float CfgDefaultFilmGrainStrength = 0.05f;
}