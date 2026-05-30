// PassthroughPass.cpp — identity copy. See header.

#include "stdafx.h"
#include "PassthroughPass.h"
#include "PostProcessGL.h"

namespace PostProcess
{
    namespace
    {
        // GLSL 120 to match the existing compatibility context (same target the
        // SoftShadow pass uses). Reads gl_MultiTexCoord0 emitted by the chain's
        // fullscreen quad.
        const char* kVS = R"(
#version 120
varying vec2 vUV;
void main()
{
    gl_Position = gl_Vertex;
    vUV = gl_MultiTexCoord0.xy;
}
)";

        const char* kFS = R"(
#version 120
uniform sampler2D uScene;
varying vec2 vUV;
void main()
{
    gl_FragColor = texture2D(uScene, vUV);
}
)";
    }

    bool PassthroughPass::EnsureResources(int /*width*/, int /*height*/)
    {
        if (m_program)
            return true;
        m_program = CompileProgram(kVS, kFS);
        if (!m_program)
            return false;
        m_locTex = GL().GetUniformLocation(m_program, "uScene");
        return true;
    }

    void PassthroughPass::Execute(const PassContext& ctx)
    {
        const GLProcs& gl = GL();

        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx.destFBO);
        glViewport(0, 0, ctx.width, ctx.height);

        // Identity copy: no blend, no depth — just stamp the texture.
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);

        gl.UseProgram(m_program);
        gl.ActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.sourceColorTex);
        gl.Uniform1i(m_locTex, 0);

        DrawFullscreenQuad();

        gl.UseProgram(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
