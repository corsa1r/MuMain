// PostProcessGL.cpp — see header for design intent.

#include "stdafx.h"
#include "PostProcessGL.h"

#include <windows.h>
#include <cstdio>

namespace PostProcess
{
    namespace
    {
        GLProcs s_procs;
        bool    s_loaded    = false;   // Load() has been attempted
        bool    s_available = false;   // Load() succeeded

        template <typename Fn>
        bool LoadProc(Fn& out, const char* name)
        {
            out = reinterpret_cast<Fn>(wglGetProcAddress(name));
            return out != nullptr;
        }

        GLuint CompileStage(GLenum stage, const char* src)
        {
            GLuint sh = s_procs.CreateShader(stage);
            s_procs.ShaderSource(sh, 1, &src, nullptr);
            s_procs.CompileShader(sh);

            GLint ok = 0;
            s_procs.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024]{};
                s_procs.GetShaderInfoLog(sh, sizeof(log), nullptr, log);
                OutputDebugStringA("[PostProcess] shader compile failed:\n");
                OutputDebugStringA(log);
                s_procs.DeleteShader(sh);
                return 0;
            }
            return sh;
        }
    }

    const GLProcs& GL() { return s_procs; }
    bool Available()    { return s_available; }

    bool Load()
    {
        if (s_loaded)
            return s_available;
        s_loaded = true;

        bool ok = true;
        ok &= LoadProc(s_procs.GenFramebuffers,        "glGenFramebuffers");
        ok &= LoadProc(s_procs.DeleteFramebuffers,     "glDeleteFramebuffers");
        ok &= LoadProc(s_procs.BindFramebuffer,        "glBindFramebuffer");
        ok &= LoadProc(s_procs.FramebufferTexture2D,   "glFramebufferTexture2D");
        ok &= LoadProc(s_procs.CheckFramebufferStatus, "glCheckFramebufferStatus");
        ok &= LoadProc(s_procs.BlitFramebuffer,        "glBlitFramebuffer");

        ok &= LoadProc(s_procs.CreateShader,           "glCreateShader");
        ok &= LoadProc(s_procs.DeleteShader,           "glDeleteShader");
        ok &= LoadProc(s_procs.ShaderSource,           "glShaderSource");
        ok &= LoadProc(s_procs.CompileShader,          "glCompileShader");
        ok &= LoadProc(s_procs.GetShaderiv,            "glGetShaderiv");
        ok &= LoadProc(s_procs.GetShaderInfoLog,       "glGetShaderInfoLog");
        ok &= LoadProc(s_procs.CreateProgram,          "glCreateProgram");
        ok &= LoadProc(s_procs.DeleteProgram,          "glDeleteProgram");
        ok &= LoadProc(s_procs.AttachShader,           "glAttachShader");
        ok &= LoadProc(s_procs.LinkProgram,            "glLinkProgram");
        ok &= LoadProc(s_procs.GetProgramiv,           "glGetProgramiv");
        ok &= LoadProc(s_procs.GetProgramInfoLog,      "glGetProgramInfoLog");
        ok &= LoadProc(s_procs.UseProgram,             "glUseProgram");
        ok &= LoadProc(s_procs.GetUniformLocation,     "glGetUniformLocation");
        ok &= LoadProc(s_procs.Uniform1i,              "glUniform1i");
        ok &= LoadProc(s_procs.Uniform1f,              "glUniform1f");
        ok &= LoadProc(s_procs.Uniform2f,              "glUniform2f");
        ok &= LoadProc(s_procs.Uniform4f,              "glUniform4f");

        ok &= LoadProc(s_procs.ActiveTexture,          "glActiveTexture");
        ok &= LoadProc(s_procs.UniformMatrix4fv,       "glUniformMatrix4fv");

        if (!ok)
            OutputDebugStringA("[PostProcess] failed to resolve required GL entry points\n");

        // Renderbuffer / MSAA entry points — best-effort, NOT part of 'ok'. If
        // any is missing MSAA is simply unavailable (MsaaSupported() == false);
        // every other effect still works.
        LoadProc(s_procs.GenRenderbuffers,                "glGenRenderbuffers");
        LoadProc(s_procs.DeleteRenderbuffers,             "glDeleteRenderbuffers");
        LoadProc(s_procs.BindRenderbuffer,                "glBindRenderbuffer");
        LoadProc(s_procs.RenderbufferStorageMultisample,  "glRenderbufferStorageMultisample");
        LoadProc(s_procs.FramebufferRenderbuffer,         "glFramebufferRenderbuffer");

        s_available = ok;
        return s_available;
    }

    bool MsaaSupported()
    {
        return s_procs.GenRenderbuffers && s_procs.DeleteRenderbuffers &&
               s_procs.BindRenderbuffer && s_procs.RenderbufferStorageMultisample &&
               s_procs.FramebufferRenderbuffer && s_procs.BlitFramebuffer;
    }

    GLuint CompileProgram(const char* vertexSrc, const char* fragmentSrc)
    {
        if (!s_available) return 0;

        GLuint vs = CompileStage(GL_VERTEX_SHADER, vertexSrc);
        GLuint fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);
        if (!vs || !fs)
        {
            if (vs) s_procs.DeleteShader(vs);
            if (fs) s_procs.DeleteShader(fs);
            return 0;
        }

        GLuint prog = s_procs.CreateProgram();
        s_procs.AttachShader(prog, vs);
        s_procs.AttachShader(prog, fs);
        s_procs.LinkProgram(prog);

        GLint ok = 0;
        s_procs.GetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024]{};
            s_procs.GetProgramInfoLog(prog, sizeof(log), nullptr, log);
            OutputDebugStringA("[PostProcess] program link failed:\n");
            OutputDebugStringA(log);
            s_procs.DeleteProgram(prog);
            prog = 0;
        }
        // Shaders are reference-counted by the program once attached.
        s_procs.DeleteShader(vs);
        s_procs.DeleteShader(fs);
        return prog;
    }

    GLuint CreateColorTexture(int width, int height)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // LINEAR so post passes that upscale/jitter (FXAA, bloom) read smoothly.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    GLuint CreateDepthTexture(int width, int height)
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }

    void DrawFullscreenQuad()
    {
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

        glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
            glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
            glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
            glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
        glEnd();

        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    }
}
