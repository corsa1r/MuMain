// PostProcessChain.cpp — see header for the frame shape and parity contract.

#include "stdafx.h"
#include "PostProcessChain.h"
#include "PostProcessGL.h"
#include "IPostProcessPass.h"
#include "PostProcessSettings.h"
#include "BloomPass.h"
#include "SsaoPass.h"
#include "LutPass.h"
#include "ColorEffectPasses.h"

#include <windows.h>
#include <vector>
#include <algorithm>
#include <memory>

namespace PostProcess
{
    namespace Chain
    {
        namespace
        {
            // ---- Configuration / status -------------------------------------
            bool s_enabled   = false;  // config flag (default off => parity)
            bool s_available = false;  // GL objects + entry points are healthy

            // Non-owning pointers to each pass (owned by s_passes). Let
            // ApplySettings()/ToggleBloom() reach a pass without a lookup.
            SsaoPass*       s_ssao       = nullptr;
            BloomPass*      s_bloomPass  = nullptr;
            ToneMapPass*    s_toneMap    = nullptr;
            ColorGradePass* s_colorGrade = nullptr;
            LutPass*        s_lut        = nullptr;
            FxaaPass*       s_fxaa       = nullptr;
            SharpenPass*    s_sharpen    = nullptr;
            VignettePass*   s_vignette   = nullptr;
            FilmGrainPass*  s_filmGrain  = nullptr;

            int  s_width  = 0;
            int  s_height = 0;

            // ---- Scene render target (the "RTV") ----------------------------
            GLuint s_sceneFBO      = 0;
            GLuint s_sceneColorTex = 0;
            GLuint s_sceneDepthTex = 0;

            // ---- Ping-pong intermediate (second color target for multi-pass) -
            GLuint s_pingFBO      = 0;
            GLuint s_pingColorTex = 0;

            // ---- MSAA (multisample scene render target) ---------------------
            // When s_msaaSamples > 0 the world renders into this multisample FBO
            // (color + depth renderbuffers), which is then RESOLVED (blitted)
            // down into s_sceneFBO/s_sceneColorTex before the post passes run.
            // MSAA only anti-aliases polygon edges, not alpha-test cutouts.
            GLuint s_msaaFBO     = 0;
            GLuint s_msaaColorRB = 0;
            GLuint s_msaaDepthRB = 0;
            int    s_msaaSamples = 0;   // sample count the targets were built with (0 = off)
            int    s_desiredMsaaSamples = 0;  // requested via ApplySettings (0 = off)

            // The framebuffer the scene is currently rendering into. Mirrors the
            // header's ActiveSceneFramebuffer(): set to s_sceneFBO between
            // BeginSceneCapture/EndSceneCaptureAndPresent, otherwise 0.
            GLuint s_activeSceneFBO = 0;

            // ---- Pass list (owned) ------------------------------------------
            std::vector<std::unique_ptr<IPostProcessPass>> s_passes;

            // Saved GL state captured at EndSceneCaptureAndPresent and restored
            // before we hand control back to the ImGui overlay / next frame.
            struct SavedGL
            {
                GLint     viewport[4]{};
                GLboolean blend, depthTest, cullFace, alphaTest, fog, tex2D;
                GLint     blendSrc, blendDst;
                GLint     boundTex;
                GLboolean colorMask[4]{};
                GLfloat   color[4]{};
            };

            void Snapshot(SavedGL& s)
            {
                glGetIntegerv(GL_VIEWPORT, s.viewport);
                s.blend     = glIsEnabled(GL_BLEND);
                s.depthTest = glIsEnabled(GL_DEPTH_TEST);
                s.cullFace  = glIsEnabled(GL_CULL_FACE);
                s.alphaTest = glIsEnabled(GL_ALPHA_TEST);
                s.fog       = glIsEnabled(GL_FOG);
                s.tex2D     = glIsEnabled(GL_TEXTURE_2D);
                glGetIntegerv(GL_BLEND_SRC, &s.blendSrc);
                glGetIntegerv(GL_BLEND_DST, &s.blendDst);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.boundTex);
                glGetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
                glGetFloatv(GL_CURRENT_COLOR, s.color);
            }

            void Restore(const SavedGL& s)
            {
                const GLProcs& gl = GL();
                gl.UseProgram(0);
                gl.ActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.boundTex));
                if (!s.tex2D)     glDisable(GL_TEXTURE_2D);  else glEnable(GL_TEXTURE_2D);
                if (s.depthTest)  glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
                if (s.cullFace)   glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
                if (s.alphaTest)  glEnable(GL_ALPHA_TEST);   else glDisable(GL_ALPHA_TEST);
                if (s.fog)        glEnable(GL_FOG);          else glDisable(GL_FOG);
                if (s.blend)      glEnable(GL_BLEND);        else glDisable(GL_BLEND);
                glBlendFunc(s.blendSrc, s.blendDst);
                glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
                glColor4fv(s.color);
                glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
            }

            void DestroyTargets()
            {
                const GLProcs& gl = GL();
                if (s_sceneFBO)      { gl.DeleteFramebuffers(1, &s_sceneFBO);  s_sceneFBO = 0; }
                if (s_pingFBO)       { gl.DeleteFramebuffers(1, &s_pingFBO);   s_pingFBO = 0; }
                if (s_sceneColorTex) { glDeleteTextures(1, &s_sceneColorTex);  s_sceneColorTex = 0; }
                if (s_sceneDepthTex) { glDeleteTextures(1, &s_sceneDepthTex);  s_sceneDepthTex = 0; }
                if (s_pingColorTex)  { glDeleteTextures(1, &s_pingColorTex);   s_pingColorTex = 0; }

                if (s_msaaFBO)     { gl.DeleteFramebuffers(1, &s_msaaFBO);   s_msaaFBO = 0; }
                if (s_msaaColorRB) { gl.DeleteRenderbuffers(1, &s_msaaColorRB); s_msaaColorRB = 0; }
                if (s_msaaDepthRB) { gl.DeleteRenderbuffers(1, &s_msaaDepthRB); s_msaaDepthRB = 0; }
                s_msaaSamples = 0;
            }

            // Build the multisample scene FBO at 'samples'. Color + depth use
            // the SAME formats as the single-sample resolve targets (RGBA8 /
            // DEPTH_COMPONENT24) so glBlitFramebuffer can resolve cleanly.
            // Returns false (and leaves nothing dangling) on failure.
            bool CreateMsaaTargets(int w, int h, int samples)
            {
                const GLProcs& gl = GL();

                gl.GenRenderbuffers(1, &s_msaaColorRB);
                gl.BindRenderbuffer(GL_RENDERBUFFER, s_msaaColorRB);
                gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);

                gl.GenRenderbuffers(1, &s_msaaDepthRB);
                gl.BindRenderbuffer(GL_RENDERBUFFER, s_msaaDepthRB);
                gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);

                gl.GenFramebuffers(1, &s_msaaFBO);
                gl.BindFramebuffer(GL_FRAMEBUFFER, s_msaaFBO);
                gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_msaaColorRB);
                gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_RENDERBUFFER, s_msaaDepthRB);
                const bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

                gl.BindRenderbuffer(GL_RENDERBUFFER, 0);
                gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

                if (!ok)
                {
                    OutputDebugStringA("[PostProcess] MSAA FBO incomplete — disabling MSAA\n");
                    if (s_msaaFBO)     { gl.DeleteFramebuffers(1, &s_msaaFBO);   s_msaaFBO = 0; }
                    if (s_msaaColorRB) { gl.DeleteRenderbuffers(1, &s_msaaColorRB); s_msaaColorRB = 0; }
                    if (s_msaaDepthRB) { gl.DeleteRenderbuffers(1, &s_msaaDepthRB); s_msaaDepthRB = 0; }
                }
                return ok;
            }

            bool CreateTargets(int w, int h)
            {
                const GLProcs& gl = GL();
                DestroyTargets();

                // Scene RTV: color + depth. Depth is a texture so depth-aware
                // passes (dynamic fog, SSAO) can sample it later, and so a
                // capture-aware SoftShadow can blit/compare against it.
                s_sceneColorTex = CreateColorTexture(w, h);
                s_sceneDepthTex = CreateDepthTexture(w, h);

                gl.GenFramebuffers(1, &s_sceneFBO);
                gl.BindFramebuffer(GL_FRAMEBUFFER, s_sceneFBO);
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_sceneColorTex, 0);
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_TEXTURE_2D, s_sceneDepthTex, 0);
                if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    OutputDebugStringA("[PostProcess] scene FBO incomplete\n");
                    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
                    return false;
                }

                // Ping target: color only.
                s_pingColorTex = CreateColorTexture(w, h);
                gl.GenFramebuffers(1, &s_pingFBO);
                gl.BindFramebuffer(GL_FRAMEBUFFER, s_pingFBO);
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_pingColorTex, 0);
                if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    OutputDebugStringA("[PostProcess] ping FBO incomplete\n");
                    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
                    return false;
                }

                gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

                // Optional multisample scene target. Non-fatal: if it can't be
                // built, MSAA stays off (s_msaaSamples == 0) and the scene
                // renders into the single-sample s_sceneFBO as usual.
                s_msaaSamples = 0;
                if (s_desiredMsaaSamples >= 2 && MsaaSupported())
                {
                    GLint maxSamples = 0;
                    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
                    int samples = s_desiredMsaaSamples;
                    if (maxSamples > 0 && samples > maxSamples) samples = maxSamples;
                    if (samples >= 2 && CreateMsaaTargets(w, h, samples))
                        s_msaaSamples = samples;
                }

                return true;
            }
        } // namespace

        bool Init(int width, int height)
        {
            if (!Load())              // resolve GL entry points (once)
            {
                s_available = false;
                return false;
            }

            // Register the effects in CANONICAL ORDER. The chain runs the active
            // ones in sequence (ping-pong) and the last active one writes the
            // backbuffer; if none are active the chain blits the scene straight
            // through, so no always-on passthrough pass is needed. Order matters:
            //   SSAO       — darken contact/crevice AO first (depth-based), so
            //                bloom & grading act on the occluded scene
            //   Bloom      — add glow (before tone map so highlights bloom)
            //   ToneMap    — roll bright values (incl. bloom) off filmically
            //   ColorGrade — mood (contrast/saturation/warmth) on the LDR image
            //   FXAA       — anti-alias the graded result
            //   Sharpen    — crisp up after AA
            //   Vignette   — darken edges late
            //   FilmGrain  — final dither to kill banding
            if (s_passes.empty())
            {
                auto add = [](auto pass, auto*& outPtr)
                {
                    outPtr = pass.get();
                    s_passes.emplace_back(std::move(pass));
                };
                add(std::make_unique<SsaoPass>(),       s_ssao);
                add(std::make_unique<BloomPass>(),      s_bloomPass);
                add(std::make_unique<ToneMapPass>(),    s_toneMap);
                add(std::make_unique<ColorGradePass>(), s_colorGrade);
                // LUT after the slider grade (final COLOR transform), before the
                // spatial passes (AA/sharpen/vignette/grain).
                add(std::make_unique<LutPass>(),        s_lut);
                add(std::make_unique<FxaaPass>(),       s_fxaa);
                add(std::make_unique<SharpenPass>(),    s_sharpen);
                add(std::make_unique<VignettePass>(),   s_vignette);
                add(std::make_unique<FilmGrainPass>(),  s_filmGrain);
            }

            Resize(width, height);
            return s_available;
        }

        void Shutdown()
        {
            DestroyTargets();
            s_passes.clear();
            // All were owned by s_passes, now cleared.
            s_ssao = nullptr;
            s_bloomPass = nullptr; s_toneMap = nullptr; s_colorGrade = nullptr;
            s_lut = nullptr;
            s_fxaa = nullptr; s_sharpen = nullptr; s_vignette = nullptr; s_filmGrain = nullptr;
            s_available = false;
            s_width = s_height = 0;
        }

        void Resize(int width, int height)
        {
            if (width <= 0 || height <= 0)
                return;
            if (!Available())          // no entry points => stay inert
            {
                s_available = false;
                return;
            }
            if (width == s_width && height == s_height && s_sceneFBO != 0)
            {
                s_available = true;
                return;
            }
            if (!CreateTargets(width, height))
            {
                s_available = false;   // fall back to direct-to-backbuffer
                return;
            }
            s_width     = width;
            s_height    = height;
            s_available = true;
        }

        void SetEnabled(bool enabled) { s_enabled = enabled; }
        bool IsEnabled()              { return s_enabled; }
        bool IsActive()               { return s_enabled && s_available && s_sceneFBO != 0; }

        bool IsBloomActive()
        {
            return s_bloomPass && s_bloomPass->IsActive();
        }

        bool ToggleBloom()
        {
            const bool newState = !IsBloomActive();
            if (s_bloomPass)
                s_bloomPass->SetActive(newState);
            // Turning bloom ON implies the chain must be capturing, so
            // force-enable it (covers PostProcess=0 in config but the user wants
            // to test live). We do NOT auto-disable the chain on toggle off:
            // leaving it enabled (identity passthrough) avoids churn and is
            // visually identical to disabled.
            if (newState)
                s_enabled = true;
            return newState;
        }

        void ApplySettings(const Settings& s)
        {
            if (s_ssao)
            {
                s_ssao->SetActive(s.ssao);
                s_ssao->SetRadius(s.ssaoRadius);
                s_ssao->SetStrength(s.ssaoStrength);
                s_ssao->SetPower(s.ssaoPower);
            }
            if (s_bloomPass)
            {
                s_bloomPass->SetActive(s.bloom);
                s_bloomPass->SetThreshold(s.bloomThreshold);
                // Map the integer config strength onto the pass intensity: 1 ==
                // the tuned baseline; higher values scale linearly. Clamp so a
                // stray value can't blow the frame out (or go negative).
                constexpr float kBaselineIntensity = 0.9f;
                const int clamped = s.bloomStrength < 0 ? 0 : (s.bloomStrength > 16 ? 16 : s.bloomStrength);
                s_bloomPass->SetIntensity(kBaselineIntensity * static_cast<float>(clamped));
            }
            if (s_toneMap)
            {
                s_toneMap->SetActive(s.toneMap);
                s_toneMap->SetExposure(s.exposure);
            }
            if (s_colorGrade)
            {
                s_colorGrade->SetActive(s.colorGrade);
                s_colorGrade->SetContrast(s.contrast);
                s_colorGrade->SetSaturation(s.saturation);
                s_colorGrade->SetBrightness(s.brightness);
                s_colorGrade->SetTemperature(s.temperature);
                s_colorGrade->SetShadows(s.gradeShadows);
                s_colorGrade->SetMidtones(s.gradeMidtones);
                s_colorGrade->SetHighlights(s.gradeHighlights);
            }
            if (s_lut)
            {
                s_lut->SetActive(s.lut);
                s_lut->SetLutFile(s.lutFile);
            }
            if (s_fxaa)
            {
                s_fxaa->SetActive(s.fxaa);
            }
            if (s_sharpen)
            {
                s_sharpen->SetActive(s.sharpen);
                s_sharpen->SetStrength(s.sharpenStrength);
            }
            if (s_vignette)
            {
                s_vignette->SetActive(s.vignette);
                s_vignette->SetStrength(s.vignetteStrength);
                s_vignette->SetRadius(s.vignetteRadius);
            }
            if (s_filmGrain)
            {
                s_filmGrain->SetActive(s.filmGrain);
                s_filmGrain->SetStrength(s.filmGrainStrength);
            }

            // MSAA: unlike the fullscreen effects, this changes how the scene is
            // rasterized, so a change in sample count requires rebuilding the
            // render targets (not just a uniform). Clamp request to {0 (off), or
            // 2..8}. Rebuild only when the effective sample count actually moves.
            int desired = 0;
            if (s.msaa)
            {
                desired = s.msaaSamples;
                if (desired < 2) desired = 2;
                if (desired > 8) desired = 8;
            }
            if (desired != s_desiredMsaaSamples)
            {
                s_desiredMsaaSamples = desired;
                // Rebuild targets in place if they already exist (startup apply
                // and runtime panel changes both land here). Resize() alone
                // wouldn't react because the window size hasn't changed.
                if (s_sceneFBO != 0 && s_width > 0 && s_height > 0)
                    CreateTargets(s_width, s_height);
            }
        }

        GLuint ActiveSceneFramebuffer() { return s_activeSceneFBO; }

        void AddPass(IPostProcessPass* pass)
        {
            if (pass)
                s_passes.emplace_back(pass);
        }

        void BeginSceneCapture()
        {
            if (!IsActive())
                return;

            const GLProcs& gl = GL();

            // When MSAA is active the world renders into the multisample FBO
            // (resolved to the single-sample scene texture in EndScene...).
            // Otherwise it renders straight into the single-sample scene FBO.
            const GLuint target = (s_msaaSamples > 0) ? s_msaaFBO : s_sceneFBO;
            gl.BindFramebuffer(GL_FRAMEBUFFER, target);
            glViewport(0, 0, s_width, s_height);
            if (s_msaaSamples > 0)
                glEnable(GL_MULTISAMPLE);

            // Publish the scene framebuffer so capture-aware code (SoftShadow)
            // targets the RTV instead of the backbuffer. Reset in EndScene...().
            s_activeSceneFBO = target;

            // Clear the RTV ourselves. The game's SetWorldClearColor() ran just
            // before this and cleared the BACKBUFFER (FBO 0) — but the scene now
            // renders into our RTV, which would otherwise keep uninitialized
            // contents (garbage / solid color — the "everything is red" bug) and
            // an uncleared depth buffer that makes all geometry z-fail. Reuse the
            // clear color the game just set (still the current GL state) so the
            // RTV background exactly matches the legacy path. Masks are forced
            // open so the clear isn't skipped if a channel was left masked.
            GLfloat clearCol[4] = {0, 0, 0, 1};
            glGetFloatv(GL_COLOR_CLEAR_VALUE, clearCol);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glClearColor(clearCol[0], clearCol[1], clearCol[2], clearCol[3]);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }

        void EndSceneCaptureAndPresent(float deltaSeconds)
        {
            if (!IsActive())
                return;

            const GLProcs& gl = GL();

            SavedGL saved;
            Snapshot(saved);

            // Capture the camera projection for depth-aware passes (SSAO). At
            // this point the scene's perspective is still the top of the GL
            // projection stack (SetupMainSceneViewport's BeginOpengl pushed it;
            // the matching EndOpengl runs later, after the UI). Read it BEFORE
            // any DrawFullscreenQuad (which pushes/loads/pops identity). From a
            // standard GL perspective matrix (column-major): m00 = 1/(aspect*
            // tan(fovY/2)), m11 = 1/tan(fovY/2), m22 = -(f+n)/(f-n),
            // m32 = -2fn/(f-n). Invert to near/far/tan. Left at 0 if the matrix
            // isn't a usable perspective -> SSAO no-ops (outputs fully lit).
            float ppNear = 0.0f, ppFar = 0.0f, ppTanX = 0.0f, ppTanY = 0.0f;
            {
                GLfloat proj[16] = {0};
                glGetFloatv(GL_PROJECTION_MATRIX, proj);
                const float m00 = proj[0], m11 = proj[5], m22 = proj[10], m32 = proj[14];
                if (m00 != 0.0f && m11 != 0.0f &&
                    (m22 - 1.0f) != 0.0f && (m22 + 1.0f) != 0.0f)
                {
                    ppTanX = 1.0f / m00;
                    ppTanY = 1.0f / m11;
                    ppNear = m32 / (m22 - 1.0f);
                    ppFar  = m32 / (m22 + 1.0f);
                    // Guard against a non-perspective / garbage matrix.
                    if (ppNear <= 0.0f || ppFar <= 0.0f || ppFar <= ppNear)
                        ppNear = ppFar = ppTanX = ppTanY = 0.0f;
                }
            }

            // Force a clean, known state for the full-screen passes.
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_ALPHA_TEST);
            glDisable(GL_FOG);
            glEnable(GL_TEXTURE_2D);

            // The scene is complete; from here on the scene framebuffer is no
            // longer the active draw target for game code.
            s_activeSceneFBO = 0;

            // MSAA resolve: multisample scene FBO -> single-sample scene texture
            // (color + depth), so the post passes sample a normal 2D texture.
            // Equal rectangles + NEAREST is the required form for a resolve.
            if (s_msaaSamples > 0)
            {
                gl.BindFramebuffer(GL_READ_FRAMEBUFFER, s_msaaFBO);
                gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, s_sceneFBO);
                gl.BlitFramebuffer(0, 0, s_width, s_height,
                                   0, 0, s_width, s_height,
                                   GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
                gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            }

            // Collect the passes that want to run this frame.
            std::vector<IPostProcessPass*> active;
            active.reserve(s_passes.size());
            for (auto& p : s_passes)
            {
                if (p && p->Enabled() && p->EnsureResources(s_width, s_height))
                    active.push_back(p.get());
            }

            if (active.empty())
            {
                // Nothing to do but get the scene onto the backbuffer: a single
                // fast color blit (RTV -> FBO 0). Keeps output correct even if
                // every pass disabled itself.
                gl.BindFramebuffer(GL_READ_FRAMEBUFFER, s_sceneFBO);
                gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                gl.BlitFramebuffer(0, 0, s_width, s_height,
                                   0, 0, s_width, s_height,
                                   GL_COLOR_BUFFER_BIT, GL_LINEAR);
                gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
                Restore(saved);
                return;
            }

            // Ping-pong between the scene color and the ping color. The scene
            // color is the initial source; once consumed it can serve as a write
            // target again, so two color buffers suffice for any pass count.
            GLuint readTex = s_sceneColorTex;
            const size_t last = active.size() - 1;

            for (size_t i = 0; i < active.size(); ++i)
            {
                const bool isLast = (i == last);

                GLuint destFBO;
                GLuint nextRead;
                if (isLast)
                {
                    destFBO  = 0;            // final pass -> backbuffer
                    nextRead = 0;            // unused
                }
                else
                {
                    // Write to whichever color buffer we are NOT reading from.
                    if (readTex == s_sceneColorTex) { destFBO = s_pingFBO;  nextRead = s_pingColorTex; }
                    else                            { destFBO = s_sceneFBO; nextRead = s_sceneColorTex; }
                }

                PassContext ctx;
                ctx.sourceColorTex = readTex;
                ctx.sourceDepthTex = s_sceneDepthTex;
                ctx.destFBO        = destFBO;
                ctx.width          = s_width;
                ctx.height         = s_height;
                ctx.deltaSeconds   = deltaSeconds;
                ctx.nearZ          = ppNear;
                ctx.farZ           = ppFar;
                ctx.tanHalfFovX    = ppTanX;
                ctx.tanHalfFovY    = ppTanY;

                active[i]->Execute(ctx);
                readTex = nextRead;
            }

            // Leave the backbuffer bound for the ImGui overlay + SwapBuffers.
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            Restore(saved);
        }
    } // namespace Chain
}
