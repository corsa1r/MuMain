// LutPass.cpp — 3D .cube color LUT. See header.

#include "stdafx.h"
#include "LutPass.h"
#include "PostProcessGL.h"

#include <windows.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace PostProcess
{
    namespace
    {
        // Fullscreen VS — GLSL 120, fixed-function attributes (matches every
        // other pass). Emits gl_MultiTexCoord0 as the sample UV.
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        // LUT apply. Scene color is the lookup coordinate into the 3D LUT. The
        // (c*(N-1)+0.5)/N scaling samples TEXEL CENTERS so the edges of the cube
        // map to the true first/last entries (avoids a half-texel color bias).
        const char* kFS = R"(
#version 120
uniform sampler2D uScene;
uniform sampler3D uLut;
uniform float uLutSize;
varying vec2 vUV;
void main()
{
    vec3 c = clamp(texture2D(uScene, vUV).rgb, 0.0, 1.0);
    vec3 uvw = (c * (uLutSize - 1.0) + 0.5) / uLutSize;
    gl_FragColor = vec4(texture3D(uLut, uvw).rgb, 1.0);
}
)";

        // glTexImage3D is GL 1.2 — resolve at runtime (the base opengl32 import
        // lib only exports 1.1, and this project never calls glewInit()).
        typedef void (APIENTRY * PFN_glTexImage3D)(GLenum, GLint, GLint, GLsizei,
                                                   GLsizei, GLsizei, GLint, GLenum,
                                                   GLenum, const void*);
        PFN_glTexImage3D GetTexImage3D()
        {
            static PFN_glTexImage3D p =
                reinterpret_cast<PFN_glTexImage3D>(wglGetProcAddress("glTexImage3D"));
            return p;
        }
    }

    void LutPass::Destroy()
    {
        if (m_lutTex) { glDeleteTextures(1, &m_lutTex); m_lutTex = 0; }
        m_lutSize = 0;
        m_loadedFile.clear();
    }

    // Parse a .cube and upload it as a GL_TEXTURE_3D. Returns false (and leaves
    // any previous LUT intact-but-cleared) on any parse/upload problem.
    bool LutPass::LoadCube(const std::string& path)
    {
        std::ifstream in(path.c_str());
        if (!in.is_open())
        {
            OutputDebugStringA(("[LUT] cannot open " + path + "\n").c_str());
            return false;
        }

        int size = 0;
        std::vector<float> data;     // r,g,b triplets, red-fastest
        data.reserve(33 * 33 * 33 * 3);

        std::string line;
        while (std::getline(in, line))
        {
            // Skip blanks and comments.
            size_t firstNonWs = line.find_first_not_of(" \t\r\n");
            if (firstNonWs == std::string::npos) continue;
            if (line[firstNonWs] == '#') continue;

            // Keyword lines.
            if (line.compare(firstNonWs, 12, "LUT_3D_SIZE") == 0 ||
                line.compare(firstNonWs, 11, "LUT_3D_SIZE") == 0)
            {
                std::istringstream ss(line.substr(firstNonWs));
                std::string kw; ss >> kw >> size;
                continue;
            }
            // Ignore other keywords (TITLE, DOMAIN_*, LUT_1D_SIZE...).
            char f = line[firstNonWs];
            if (!(f == '-' || f == '+' || f == '.' || (f >= '0' && f <= '9')))
                continue;

            std::istringstream ss(line);
            float r, g, b;
            if (ss >> r >> g >> b)
            {
                data.push_back(r);
                data.push_back(g);
                data.push_back(b);
            }
        }

        if (size < 2 || static_cast<size_t>(size) * size * size * 3u != data.size())
        {
            char msg[160];
            sprintf_s(msg, sizeof(msg),
                      "[LUT] %s: bad data (size=%d, triplets=%zu)\n",
                      path.c_str(), size, data.size() / 3);
            OutputDebugStringA(msg);
            return false;
        }

        PFN_glTexImage3D texImage3D = GetTexImage3D();
        if (!texImage3D)
        {
            OutputDebugStringA("[LUT] glTexImage3D unavailable — LUT disabled\n");
            return false;
        }

        // (Re)create the 3D texture. .cube red-fastest ordering matches the GL
        // 3D layout (x fastest), so the buffer uploads directly.
        if (m_lutTex == 0)
            glGenTextures(1, &m_lutTex);
        glBindTexture(GL_TEXTURE_3D, m_lutTex);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        texImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, size, size, size, 0,
                   GL_RGB, GL_FLOAT, data.data());
        glBindTexture(GL_TEXTURE_3D, 0);

        m_lutSize = size;
        return true;
    }

    bool LutPass::EnsureResources(int /*w*/, int /*h*/)
    {
        if (!m_program)
        {
            m_program = CompileProgram(kVS, kFS);
            if (!m_program) return false;
            const GLProcs& gl = GL();
            m_locScene = gl.GetUniformLocation(m_program, "uScene");
            m_locLut   = gl.GetUniformLocation(m_program, "uLut");
            m_locSize  = gl.GetUniformLocation(m_program, "uLutSize");
        }

        // Hot-swap: (re)load when the requested file changed. Failures are
        // remembered so we don't hit the disk every frame for a bad path.
        if (m_requestedFile != m_loadedFile)
        {
            m_loadFailed = false;
            Destroy();                        // drop old LUT
            if (!m_requestedFile.empty())
            {
                const std::string path = "Data\\PostProcess\\" + m_requestedFile;
                if (LoadCube(path))
                    m_loadedFile = m_requestedFile;
                else
                    m_loadFailed = true;      // m_lutTex stays 0
            }
            else
            {
                m_loadFailed = true;          // no file selected
            }
        }

        // No valid LUT → tell the chain to SKIP this pass (scene passes through
        // ungraded). Never breaks the frame on a missing/bad file.
        return m_lutTex != 0;
    }

    void LutPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_TEXTURE_2D);

        gl.UseProgram(m_program);

        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_locScene, 0);

        gl.ActiveTexture(GL_TEXTURE1);
        glEnable(GL_TEXTURE_3D);
        glBindTexture(GL_TEXTURE_3D, m_lutTex);
        gl.Uniform1i(m_locLut, 1);
        gl.Uniform1f(m_locSize, static_cast<float>(m_lutSize));

        DrawFullscreenQuad();

        // Tidy: unbind the 3D LUT, drop unit 1 back to a clean state, return to
        // unit 0 so the next pass / legacy draws aren't surprised.
        glBindTexture(GL_TEXTURE_3D, 0);
        glDisable(GL_TEXTURE_3D);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        gl.UseProgram(0);
    }
}
