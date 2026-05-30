// ============================================================================
//  LutPass.h  —  3D color LUT (.cube) grade
// ----------------------------------------------------------------------------
//  Applies an artist-authored color grade stored as a .cube 3D LUT: the scene
//  RGB is used as a coordinate into a 3D texture whose cells hold the graded
//  output color, so the whole look is one texture fetch (hardware-interpolated).
//  This is how film/AAA "looks" are imported — author in Photoshop/Resolve,
//  export a .cube, load it here. Complementary to the slider ColorGrade pass:
//  sliders for live math tweaks, LUT for a locked-in authored palette.
//
//  Runtime-swappable: SetLutFile() picks a .cube from Data/PostProcess/; the
//  pass lazily (re)uploads the 3D texture when the requested file changes, so
//  the editor dropdown can hot-swap looks without a restart. If the file is
//  missing/invalid the pass reports !EnsureResources → the chain skips it (the
//  scene passes through ungraded), so a bad path never breaks the frame.
//
//  Self-contained like the other passes; loads glTexImage3D via wglGetProcAddress
//  (GL 1.2 — not in the base opengl32 import lib, and glew is never init'd here).
// ============================================================================
#pragma once

#include "IPostProcessPass.h"
#include <string>

namespace PostProcess
{
    class LutPass final : public IPostProcessPass
    {
    public:
        const char* Name() const override { return "LUT"; }
        bool EnsureResources(int width, int height) override;
        void Execute(const PassContext& ctx) override;

        bool Enabled() const override { return m_active; }
        void SetActive(bool active) { m_active = active; }
        bool IsActive() const       { return m_active; }

        // Filename only (e.g. "look.cube"); resolved under Data/PostProcess/.
        // Reload happens lazily in EnsureResources when this differs from the
        // currently-loaded file. Empty disables the pass.
        void SetLutFile(const std::string& file) { m_requestedFile = file; }
        const std::string& GetLutFile() const    { return m_requestedFile; }

    private:
        bool LoadCube(const std::string& path);   // parse + upload 3D texture
        void Destroy();

        GLuint m_program = 0;
        GLint  m_locScene = -1, m_locLut = -1, m_locSize = -1;

        GLuint m_lutTex  = 0;     // GL_TEXTURE_3D, 0 = none loaded
        int    m_lutSize = 0;     // edge length (e.g. 33)

        std::string m_requestedFile;   // what we WANT loaded ("" = none)
        std::string m_loadedFile;      // what IS loaded
        bool        m_loadFailed = false; // requested file failed → don't retry every frame

        bool m_active = false;    // off by default (opt-in look)
    };
}
