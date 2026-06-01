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
    inline constexpr wchar_t CfgKeyPPGlobalOverride[] = L"GlobalOverride";
    inline constexpr wchar_t CfgKeyAnisotropic[]      = L"Anisotropic";
    inline constexpr wchar_t CfgKeyAnisotropicLevel[] = L"AnisotropicLevel";
    inline constexpr wchar_t CfgKeyTextureLodBias[]   = L"TextureLodBias";
    inline constexpr wchar_t CfgKeyTerrainTiling[]    = L"TerrainTiling";
    inline constexpr wchar_t CfgKeySSAO[]            = L"SSAO";
    inline constexpr wchar_t CfgKeySSAORadius[]      = L"SSAORadius";
    inline constexpr wchar_t CfgKeySSAOStrength[]    = L"SSAOStrength";
    inline constexpr wchar_t CfgKeySSAOPower[]       = L"SSAOPower";
    inline constexpr wchar_t CfgKeyFog[]             = L"Fog";
    inline constexpr wchar_t CfgKeyFogColorR[]       = L"FogColorR";
    inline constexpr wchar_t CfgKeyFogColorG[]       = L"FogColorG";
    inline constexpr wchar_t CfgKeyFogColorB[]       = L"FogColorB";
    inline constexpr wchar_t CfgKeyFogDensity[]      = L"FogDensity";
    inline constexpr wchar_t CfgKeyFogStart[]        = L"FogStart";
    inline constexpr wchar_t CfgKeyFogHeightStrength[] = L"FogHeightStrength";
    inline constexpr wchar_t CfgKeyFogHeightTop[]    = L"FogHeightTop";
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
    inline constexpr wchar_t CfgKeyGodRays[]          = L"GodRays";
    inline constexpr wchar_t CfgKeyGodRaysLightX[]    = L"GodRaysLightX";
    inline constexpr wchar_t CfgKeyGodRaysLightY[]    = L"GodRaysLightY";
    inline constexpr wchar_t CfgKeyGodRaysSunZ[]      = L"GodRaysSunZ";
    inline constexpr wchar_t CfgKeyGodRaysDensity[]   = L"GodRaysDensity";
    inline constexpr wchar_t CfgKeyGodRaysWeight[]    = L"GodRaysWeight";
    inline constexpr wchar_t CfgKeyGodRaysDecay[]     = L"GodRaysDecay";
    inline constexpr wchar_t CfgKeyGodRaysThreshold[] = L"GodRaysThreshold";
    inline constexpr wchar_t CfgKeyGodRaysIntensity[] = L"GodRaysIntensity";
    inline constexpr wchar_t CfgKeyGodRaysSamples[]   = L"GodRaysSamples";
    inline constexpr wchar_t CfgKeyGodRaysColorR[]    = L"GodRaysColorR";
    inline constexpr wchar_t CfgKeyGodRaysColorG[]    = L"GodRaysColorG";
    inline constexpr wchar_t CfgKeyGodRaysColorB[]    = L"GodRaysColorB";
    inline constexpr wchar_t CfgKeyLut[]             = L"LUT";
    inline constexpr wchar_t CfgKeyLutFile[]         = L"LUTFile";

    // Graphics — per-pixel model lighting + normal mapping (geometry, not a
    // screen-space post pass).
    inline constexpr wchar_t CfgKeyPerPixelLighting[] = L"PerPixelLighting";
    inline constexpr wchar_t CfgKeyNormalMapStrength[] = L"NormalMapStrength";
    inline constexpr wchar_t CfgKeySpecularStrength[]  = L"SpecularStrength";
    inline constexpr wchar_t CfgKeySpecularPower[]     = L"SpecularPower";
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

    // Per-map presets vs. one global look. false (default) = each map uses its
    // own preset file (falling back to the global [Graphics] values when it has
    // none); true = the global [Graphics] values override every map.
    inline constexpr bool CfgDefaultPPGlobalOverride = false;

    // Anisotropic filtering on by default — pure texture-clarity win at MU's
    // oblique camera, independent of the post-process chain. 16x (clamped to
    // the driver's GL_MAX_TEXTURE_MAX_ANISOTROPY at apply time).
    inline constexpr bool CfgDefaultAnisotropic      = true;
    inline constexpr int  CfgDefaultAnisotropicLevel = 16;
    // Mip LOD bias for mipmapped textures: negative = sharper. -0.5 is a mild
    // crispness boost for the low-res world/character art with little aliasing.
    inline constexpr float CfgDefaultTextureLodBias  = -0.5f;
    // Terrain texture tiling density. 1.0 = legacy. Set to match the terrain
    // texture upscale factor (e.g. 4.0 for x4 tiles) so the added resolution
    // increases ground detail density instead of stretching the pattern.
    inline constexpr float CfgDefaultTerrainTiling   = 1.0f;

    // SSAO off by default (heaviest, most scene-dependent effect).
    inline constexpr bool  CfgDefaultSSAO         = false;
    inline constexpr float CfgDefaultSSAORadius   = 60.0f;
    inline constexpr float CfgDefaultSSAOStrength = 1.0f;
    inline constexpr float CfgDefaultSSAOPower    = 1.5f;

    // Fog off by default; cool dark-fantasy haze, distance-only (no height fog).
    inline constexpr bool  CfgDefaultFog              = false;
    inline constexpr float CfgDefaultFogColorR        = 0.04f;
    inline constexpr float CfgDefaultFogColorG        = 0.05f;
    inline constexpr float CfgDefaultFogColorB        = 0.07f;
    inline constexpr float CfgDefaultFogDensity       = 0.6f;
    inline constexpr float CfgDefaultFogStart         = 0.30f;
    inline constexpr float CfgDefaultFogHeightStrength = 0.0f;
    inline constexpr float CfgDefaultFogHeightTop     = 200.0f;

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

    // God rays off by default (opt-in, most dramatic outdoors). Light position
    // is screen-space UV; warm-white tint. Tuned GPU Gems defaults.
    inline constexpr bool  CfgDefaultGodRays          = false;
    inline constexpr float CfgDefaultGodRaysLightX    = 0.0f;   // sun AZIMUTH degrees (0 = world +X = legacy)
    inline constexpr float CfgDefaultGodRaysLightY    = 0.0f;   // unused (legacy)
    inline constexpr float CfgDefaultGodRaysSunZ      = 2.0f;   // sun world dir Z (up; default → legacy shadow look)
    inline constexpr float CfgDefaultGodRaysDensity   = 0.9f;
    inline constexpr float CfgDefaultGodRaysWeight    = 0.5f;
    inline constexpr float CfgDefaultGodRaysDecay     = 0.95f;
    inline constexpr float CfgDefaultGodRaysThreshold = 100.0f; // occluder height (world units above ground)
    inline constexpr float CfgDefaultGodRaysIntensity = 0.6f;
    inline constexpr int   CfgDefaultGodRaysSamples   = 64;
    inline constexpr float CfgDefaultGodRaysColorR    = 1.0f;
    inline constexpr float CfgDefaultGodRaysColorG    = 0.9f;
    inline constexpr float CfgDefaultGodRaysColorB    = 0.7f;

    // LUT (.cube color grade) off by default; "look.cube" ships in
    // Data/PostProcess/ so selecting it in the editor Just Works.
    inline constexpr bool     CfgDefaultLut       = false;
    inline constexpr wchar_t  CfgDefaultLutFile[] = L"look.cube";

    // Per-pixel model lighting + normal mapping. Off by default → guaranteed
    // parity with the legacy per-vertex CPU lighting until the user opts in.
    // NormalMapStrength scales the tangent-space normal perturbation (0 = flat,
    // 1 = full). Specular: Blinn-Phong glint strength + exponent (higher power
    // = tighter highlight). Tuned conservatively so it never looks plasticky.
    inline constexpr bool  CfgDefaultPerPixelLighting = false;
    inline constexpr float CfgDefaultNormalMapStrength = 1.0f;
    inline constexpr float CfgDefaultSpecularStrength  = 0.30f;
    inline constexpr float CfgDefaultSpecularPower     = 24.0f;
}