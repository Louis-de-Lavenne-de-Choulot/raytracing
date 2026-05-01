#include "GPURenderer.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <SDL.h>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#include "scenemanager.h"
#include "shaderLibrary.h"
#include "textureManager.h"
#include "UIRenderer.h"
#include "baseobject.h"
#include "animation.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "vertice.h"
#include "triangle.h"
#include "vector3.h"
#include "settings.h"
#include "texture.h"
#include "skinnedShader.h"   // SHADOW_VERT / SHADOW_FRAG

namespace HonHengine
{
    static constexpr int   MAX_LIGHTS = 64;
    static constexpr float NEAR_PLANE = 0.05f;
    static constexpr float FAR_PLANE = 1000.0f;
    static constexpr int   MAX_TEX_LAYERS = 8;

    // Shadow-map resolution and scene-coverage half-extent.
    static constexpr int   SHADOW_RES = 2048;
    static constexpr float SHADOW_EXTENT = 80.0f;   // ortho half-width / half-height

    static glm::vec3 toGLM(const Vector3& v)
    {
        return glm::vec3(static_cast<float>(v.x),
            static_cast<float>(v.y),
            static_cast<float>(v.z));
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Constructor
    // ─────────────────────────────────────────────────────────────────────────
    GPURenderer::GPURenderer(SceneManager* sm)
    {
        sceneManager = sm;

        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            std::cerr << "[GPURenderer] SDL_Init failed: " << SDL_GetError() << "\n";
            return;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        window = SDL_CreateWindow(
            "HonHengine - GPU Renderer",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            Settings::canvasWidth, Settings::canvasHeight,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

        if (!window)
        {
            std::cerr << "[GPURenderer] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            SDL_Quit();
            return;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext)
        {
            std::cerr << "[GPURenderer] SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
            SDL_DestroyWindow(window);
            SDL_Quit();
            return;
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
        {
            std::cerr << "[GPURenderer] GLAD failed to load OpenGL\n";
            return;
        }

        std::cout << "[GPURenderer] OpenGL " << glGetString(GL_VERSION) << "\n";

        SDL_GetWindowSize(window, &Settings::canvasWidth, &Settings::canvasHeight);
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_GL_SetSwapInterval(1);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
        glViewport(0, 0, Settings::canvasWidth, Settings::canvasHeight);

        shaderLib.build();

        // Internal streaming VAO for the built-in batcher.
        ManagedVAO internalVAO = shaderLib.createVAO(
            sizeof(GPUVertex),
            {
                { 0, 3, VertexDataType::Float, false, offsetof(GPUVertex, px) },
                { 1, 3, VertexDataType::Float, false, offsetof(GPUVertex, nx) },
                { 2, 3, VertexDataType::Float, false, offsetof(GPUVertex, r)  },
                { 3, 2, VertexDataType::Float, false, offsetof(GPUVertex, u)  },
            });
        vao = internalVAO.vao;
        vbo = internalVAO.vbo;

        uiRenderer.init(Settings::canvasWidth, Settings::canvasHeight,
            shaderLib.get(ShaderType::UI));

        // ── Shadow map setup ──────────────────────────────────────────────────
        initShadowMap();
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  initShadowMap
    //  Creates the depth-only FBO + texture and compiles the depth shader.
    //  Called once from the constructor.
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::initShadowMap()
    {
        // Depth texture with hardware comparison mode (feeds sampler2DShadow).
        glGenTextures(1, &shadowMap);
        glBindTexture(GL_TEXTURE_2D, shadowMap);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
            SHADOW_RES, SHADOW_RES, 0,
            GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        // Fragments outside the shadow frustum should read as fully lit (1.0).
        float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        // Enable hardware PCF via sampler2DShadow.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Framebuffer — depth attachment only.
        glGenFramebuffers(1, &shadowFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D, shadowMap, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "[GPURenderer] Shadow FBO incomplete!\n";

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Depth-only shader (handles both skinned and static geometry).
        shadowProgram = shaderLib.AddShader("__shadow__", SHADOW_VERT, SHADOW_FRAG);
        std::cout << "[GPURenderer] Shadow map initialised ("
            << SHADOW_RES << "x" << SHADOW_RES << ")\n";
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  buildLightSpaceMatrix
    //  Builds an orthographic projection from the sun's perspective.
    //  The result is cached in lightSpaceMatrix for the whole frame.
    // ─────────────────────────────────────────────────────────────────────────
    glm::mat4 GPURenderer::buildLightSpaceMatrix() const
    {
        // Re-use the same sun direction logic as uploadSunUniforms.
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));

        for (BaseLight* bl : *sceneManager->lights)
        {
            if (bl && bl->type == DIRECTIONAL_LIGHT)
            {
                DirectionalLight* dl = static_cast<DirectionalLight*>(bl);
                Vector3 d = dl->direction.normalize();
                sunDir = glm::normalize(glm::vec3(
                    static_cast<float>(-d.x),
                    static_cast<float>(-d.y),
                    static_cast<float>(-d.z)));
                break;
            }
        }

        // Position the light camera far enough above the scene.
        const float dist = 200.0f;
        glm::vec3 lightPos = sunDir * dist;

        glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(
            -SHADOW_EXTENT, SHADOW_EXTENT,
            -SHADOW_EXTENT, SHADOW_EXTENT,
            0.1f, dist * 2.0f);

        return lightProj * lightView;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  renderShadowPass
    //  Renders all visible geometry into the shadow-map FBO using the
    //  depth-only shader.  Must be called before the main colour pass.
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::renderShadowPass(
        const std::vector<BaseObject*>& renderComps,
        const std::vector<BaseObject*>& opaqueObjs)
    {
        if (!shadowProgram || !shadowFBO) return;

        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glViewport(0, 0, SHADOW_RES, SHADOW_RES);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Cull front faces to reduce peter-panning on thin geometry.
        glCullFace(GL_FRONT);

        glUseProgram(shadowProgram);
        glUniformMatrix4fv(
            glGetUniformLocation(shadowProgram, "uLightSpaceMatrix"),
            1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));

        // ── Draw RenderComponent objects (foxes, etc.) ────────────────────────
        for (BaseObject* obj : renderComps)
        {
            if (!obj || !obj->visible) continue;

            RenderComponent& rc = obj->render;
            if (!rc._uploaded) continue;   // not yet on GPU — skip this frame

            // Build model matrix.
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, toGLM(obj->transform.position));
            auto& q = obj->transform.rotation;
            glm::quat glmRot(
                static_cast<float>(q.w),
                static_cast<float>(q.x),
                static_cast<float>(q.y),
                static_cast<float>(q.z));
            model *= glm::mat4_cast(glmRot);
            model = glm::scale(model, toGLM(obj->transform.scale));

            glUniformMatrix4fv(
                glGetUniformLocation(shadowProgram, "uModel"),
                1, GL_FALSE, glm::value_ptr(model));

            // Skinning.
            AnimatorComponent& anim = obj->animator;
            const bool skinned = anim.active();
            glUniform1i(glGetUniformLocation(shadowProgram, "uSkinned"), skinned ? 1 : 0);

            if (skinned && !anim.bonePalette.empty())
            {
                glUniformMatrix4fv(
                    glGetUniformLocation(shadowProgram, "uBonePalette"),
                    static_cast<GLsizei>(anim.bonePalette.size()),
                    GL_FALSE,
                    glm::value_ptr(anim.bonePalette[0]));
            }

            glBindVertexArray(rc._vao.vao);
            if (!rc.indices.empty())
                glDrawElements(GL_TRIANGLES,
                    static_cast<GLsizei>(rc._indexCount),
                    GL_UNSIGNED_INT, nullptr);
            else
                glDrawArrays(GL_TRIANGLES, 0,
                    static_cast<GLsizei>(rc._indexCount));
            glBindVertexArray(0);
        }

        // ── Draw built-in opaque objects (beach, terrain, etc.) ───────────────
        // These use the streaming VBO; batch them all into one draw here
        // since we only need depth, not per-material uniforms.
        if (!opaqueObjs.empty())
        {
            // Static meshes have uSkinned = 0.
            glUniform1i(glGetUniformLocation(shadowProgram, "uSkinned"), 0);

            // The built-in batcher already pre-transforms vertices into world
            // space before uploading, so uModel = identity is correct.
            glm::mat4 identity(1.0f);
            glUniformMatrix4fv(
                glGetUniformLocation(shadowProgram, "uModel"),
                1, GL_FALSE, glm::value_ptr(identity));

            // Collect all triangles into a temporary depth-pass VBO.
            // We reuse the existing streaming vao/vbo pair.
            std::vector<GPUVertex> depthVerts;
            depthVerts.reserve(4096);

            for (BaseObject* obj : opaqueObjs)
            {
                if (!obj || !obj->visible) continue;

                Vector3    sc = obj->transform.scale;
                Vector3    pos = obj->transform.position;
                Quaternion rot = obj->transform.rotation;

                std::vector<Vector3> wPos;
                wPos.reserve(obj->bVertices.size());
                for (const Vertice& v : obj->bVertices)
                {
                    Vector3 vp(v.position.x * sc.x,
                        v.position.y * sc.y,
                        v.position.z * sc.z);
                    wPos.push_back(rot.RotateVector3(&vp) + pos);
                }

                for (const Triangle& tri : obj->bTriangles)
                {
                    int idx[3] = { tri.p0, tri.p1, tri.p2 };
                    for (int j = 0; j < 3; ++j)
                    {
                        int k = idx[j];
                        GPUVertex gv{};
                        gv.px = static_cast<float>(wPos[k].x);
                        gv.py = static_cast<float>(wPos[k].y);
                        gv.pz = static_cast<float>(wPos[k].z);
                        depthVerts.push_back(gv);
                    }
                }
            }

            if (!depthVerts.empty())
            {
                glBindVertexArray(vao);
                glBindBuffer(GL_ARRAY_BUFFER, vbo);
                glBufferData(GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(depthVerts.size() * sizeof(GPUVertex)),
                    depthVerts.data(), GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES, 0,
                    static_cast<GLsizei>(depthVerts.size()));
                glBindVertexArray(0);
            }
        }

        // Restore main framebuffer and render state.
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, Settings::canvasWidth, Settings::canvasHeight);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  RegisterShader
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::RegisterShader(const std::string& name,
        const char* vertSrc,
        const char* fragSrc)
    {
        shaderLib.AddShader(name, vertSrc, fragSrc);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  FlushGLErrors
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::FlushGLErrors()
    {
        while (glGetError() != GL_NO_ERROR) {}
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  uploadTextureLayersToShader
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::uploadTextureLayersToShader(GLuint program, const Material* mat) const
    {
        if (!mat || mat->textureLayers.empty())
        {
            glUniform1i(glGetUniformLocation(program, "uLayerCount"), 0);
            return;
        }

        int layerCount = static_cast<int>(
            std::min(static_cast<int>(mat->textureLayers.size()), MAX_TEX_LAYERS));

        glUniform1i(glGetUniformLocation(program, "uLayerCount"), layerCount);

        for (int i = 0; i < layerCount; ++i)
        {
            std::string samplerName = "uTexLayer[" + std::to_string(i) + "]";
            glUniform1i(glGetUniformLocation(program, samplerName.c_str()), i);
        }

        for (int i = 0; i < layerCount; ++i)
        {
            const TextureLayer& layer = mat->textureLayers[i];
            const Texture& tex = layer.texture;

            GLuint texID = texManager.get(tex.name);
            if (texID != 0)
            {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, texID);
                TextureManager::applySamplerParams(tex);
            }

            auto uloc = [&](const std::string& arr) -> GLint {
                return glGetUniformLocation(program,
                    (arr + "[" + std::to_string(i) + "]").c_str());
                };

            glUniform4f(uloc("uLayerTiling"),
                tex.tilingU, tex.tilingV, tex.offsetU, tex.offsetV);
            glUniform1i(uloc("uLayerBlendMode"), static_cast<int>(layer.blendMode));
            glUniform1f(uloc("uLayerWeight"), layer.blendWeight);
            glUniform1i(uloc("uLayerMaskType"), static_cast<int>(layer.maskType));
            glUniform1f(uloc("uLayerMaskMin"), layer.maskMin);
            glUniform1f(uloc("uLayerMaskMax"), layer.maskMax);
            glUniform1i(uloc("uLayerMaskChannel"), layer.maskChannel);
            glUniform1i(uloc("uLayerMaskInvert"), layer.maskInvert ? 1 : 0);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Camera matrices
    // ─────────────────────────────────────────────────────────────────────────
    glm::mat4 GPURenderer::buildViewMatrix() const
    {
        auto* cam = sceneManager->currentCamera;
        glm::vec3 eye = toGLM(cam->transform.position);

        auto& q = cam->transform.rotation;
        glm::quat camQuat(
            static_cast<float>(q.w),
            static_cast<float>(-q.x),
            static_cast<float>(-q.y),
            static_cast<float>(-q.z));

        glm::quat flipZ = glm::quat(0.0f, 0.0f, 1.0f, 0.0f);
        glm::quat finalQ = camQuat * flipZ;

        glm::mat4 rot = glm::mat4_cast(finalQ);
        glm::mat4 trans = glm::translate(glm::mat4(1.0f), -eye);
        return rot * trans;
    }

    glm::mat4 GPURenderer::buildProjectionMatrix() const
    {
        float aspect = static_cast<float>(Settings::canvasWidth) /
            static_cast<float>(Settings::canvasHeight);

        float fovY = 2.0f * std::atan2(
            static_cast<float>(Settings::viewportHeight) * 0.5f,
            static_cast<float>(Settings::viewportDistance));

        return glm::perspective(fovY, aspect, NEAR_PLANE, FAR_PLANE);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  uploadLights
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::uploadLights(GLuint program) const
    {
        auto& lights = *sceneManager->lights;
        int count = static_cast<int>(
            (std::min)(static_cast<int>(lights.size()), MAX_LIGHTS));

        glUniform1i(glGetUniformLocation(program, "uLightCount"), count);

        for (int i = 0; i < count; ++i)
        {
            BaseLight* bl = lights[i];

            auto loc = [&](const std::string& arr) -> GLint {
                return glGetUniformLocation(program,
                    (arr + "[" + std::to_string(i) + "]").c_str());
                };

            glUniform1i(loc("uLightType"), static_cast<int>(bl->type));
            glUniform1f(loc("uLightIntensity"), static_cast<float>(bl->intensity));
            glUniform3f(loc("uLightColor"),
                bl->color.r / 255.0f,
                bl->color.g / 255.0f,
                bl->color.b / 255.0f);

            if (bl->type == POINT_LIGHT)
            {
                PointLight* pl = static_cast<PointLight*>(bl);
                Vector3 rotatedPos = pl->rotation.RotateVector3(&pl->position);
                glUniform3f(loc("uLightPos"),
                    static_cast<float>(rotatedPos.x),
                    static_cast<float>(rotatedPos.y),
                    static_cast<float>(rotatedPos.z));
            }
            else if (bl->type == DIRECTIONAL_LIGHT)
            {
                DirectionalLight* dl = static_cast<DirectionalLight*>(bl);
                Vector3 dir = dl->direction.normalize();
                glUniform3f(loc("uLightDir"),
                    static_cast<float>(dir.x),
                    static_cast<float>(dir.y),
                    static_cast<float>(dir.z));
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  uploadSunUniforms
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::uploadSunUniforms(GLuint program) const
    {
        glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
        glm::vec3 sunColor = glm::vec3(1.0f, 0.96f, 0.82f);
        float     sunInt = 1.1f;

        for (BaseLight* bl : *sceneManager->lights)
        {
            if (bl && bl->type == DIRECTIONAL_LIGHT)
            {
                DirectionalLight* dl = static_cast<DirectionalLight*>(bl);
                Vector3 d = dl->direction.normalize();
                sunDir = glm::normalize(glm::vec3(
                    static_cast<float>(-d.x),
                    static_cast<float>(-d.y),
                    static_cast<float>(-d.z)));
                sunColor = glm::vec3(
                    bl->color.r / 255.0f,
                    bl->color.g / 255.0f,
                    bl->color.b / 255.0f);
                sunInt = static_cast<float>(bl->intensity);
                break;
            }
        }

        glUniform3fv(glGetUniformLocation(program, "uSunDir"), 1, glm::value_ptr(sunDir));
        glUniform3fv(glGetUniformLocation(program, "uSunColor"), 1, glm::value_ptr(sunColor));
        glUniform1f(glGetUniformLocation(program, "uSunIntensity"), sunInt);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  drawRenderComponent
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::drawRenderComponent(BaseObject* obj,
        const glm::mat4& view,
        const glm::mat4& proj,
        const glm::vec3& camPos,
        float             uTime,
        float             dt)
    {
        RenderComponent& rc = obj->render;

        GLuint program = shaderLib.GetShader(rc.shaderName);
        if (!program) return;

        // ── Lazy VAO upload ───────────────────────────────────────────────────
        if (!rc._uploaded)
        {
            rc._vao = shaderLib.createVAO(rc.vertexStride, rc.layout,
                !rc.indices.empty());
            rc._vao.upload(rc.vertexBytes, rc.indices, BufferUsage::Static);
            rc._indexCount = rc.indices.empty()
                ? rc.vertexBytes.size() / static_cast<size_t>(rc.vertexStride)
                : rc.indices.size();
            rc._uploaded = true;
        }

        // ── Tick animator ─────────────────────────────────────────────────────
        AnimatorComponent& anim = obj->animator;
        const bool skinned = anim.active();
        if (skinned)
            anim.update(dt);

        // ── Build model matrix ────────────────────────────────────────────────
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, toGLM(obj->transform.position));

        auto& q = obj->transform.rotation;
        glm::quat glmRot(
            static_cast<float>(q.w),
            static_cast<float>(q.x),
            static_cast<float>(q.y),
            static_cast<float>(q.z));
        model *= glm::mat4_cast(glmRot);
        model = glm::scale(model, toGLM(obj->transform.scale));

        // ── Render state ──────────────────────────────────────────────────────
        glDepthMask(rc.depthWrite ? GL_TRUE : GL_FALSE);

        if (rc.blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else {
            glDisable(GL_BLEND);
        }

        if (rc.cullFace) glEnable(GL_CULL_FACE);
        else             glDisable(GL_CULL_FACE);

        // ── Bind shader and upload standard uniforms ──────────────────────────
        glUseProgram(program);

        glUniformMatrix4fv(glGetUniformLocation(program, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(program, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, glm::value_ptr(model));
        glUniform3fv(glGetUniformLocation(program, "uCamPos"), 1, glm::value_ptr(camPos));
        glUniform1f(glGetUniformLocation(program, "uTime"), uTime);
        glUniform1f(glGetUniformLocation(program, "uAlpha"), 1.0f);

        uploadSunUniforms(program);

        // ── Shadow uniforms ───────────────────────────────────────────────────
        // Bind the shadow map to slot 1 and tell the shader about it.
        // (Slot 0 is reserved for uAlbedo, bound below via rc.textures.)
        glUniformMatrix4fv(glGetUniformLocation(program, "uLightSpaceMatrix"),
            1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
        glUniform1i(glGetUniformLocation(program, "uShadowMap"), 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowMap);

        // ── Skinning uniforms ─────────────────────────────────────────────────
        glUniform1i(glGetUniformLocation(program, "uSkinned"), skinned ? 1 : 0);

        if (skinned && !anim.bonePalette.empty())
        {
            glUniformMatrix4fv(
                glGetUniformLocation(program, "uBonePalette"),
                static_cast<GLsizei>(anim.bonePalette.size()),
                GL_FALSE,
                glm::value_ptr(anim.bonePalette[0]));
        }

        // ── Bind per-object textures (albedo etc.) ────────────────────────────
        for (const TextureBinding& tb : rc.textures)
        {
            glUniform1i(glGetUniformLocation(program, tb.uniformName.c_str()), tb.slot);
            texManager.bind(tb.textureName, tb.slot);
        }

        // ── Draw ──────────────────────────────────────────────────────────────
        glBindVertexArray(rc._vao.vao);
        if (!rc.indices.empty())
        {
            glDrawElements(GL_TRIANGLES,
                static_cast<GLsizei>(rc._indexCount),
                GL_UNSIGNED_INT,
                nullptr);
        }
        else
        {
            glDrawArrays(GL_TRIANGLES, 0,
                static_cast<GLsizei>(rc._indexCount));
        }
        glBindVertexArray(0);

        // ── Restore default render state ──────────────────────────────────────
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);

        // Unbind shadow map slot so it doesn't bleed into other draws.
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  drawPass — built-in streaming batcher (legacy pipeline)
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::drawPass(GLuint program,
        std::vector<GPUVertex>& verts,
        bool depthWrite,
        bool blend) const
    {
        if (verts.empty()) return;

        glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);

        if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        else {
            glDisable(GL_BLEND);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(verts.size() * sizeof(GPUVertex)),
            verts.data(), GL_STREAM_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        if (blend) glDisable(GL_BLEND);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Render  — main per-frame draw call
    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::Render(float dt)
    {
        // ── Partition objects ─────────────────────────────────────────────────
        std::vector<BaseObject*> renderComps, opaqueObjs, transObjs;

        for (BaseObject* obj : *sceneManager->objects)
        {
            if (!obj || !obj->visible) continue;

            if (obj->render.isValid())
            {
                renderComps.push_back(obj);
            }
            else if (!obj->bVertices.empty() && !obj->bTriangles.empty())
            {
                bool transparent = obj->material &&
                    (obj->material->color.a < 255 || obj->material->isTransparent);
                if (transparent) transObjs.push_back(obj);
                else             opaqueObjs.push_back(obj);
            }
        }

        // ── 0. Shadow pass ────────────────────────────────────────────────────
        //  Compute light-space matrix once per frame; re-used by all shaders.
        lightSpaceMatrix = buildLightSpaceMatrix();
        renderShadowPass(renderComps, opaqueObjs);

        // ── Clear main framebuffer ────────────────────────────────────────────
        glClearColor(0.60f, 0.70f, 0.78f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = buildViewMatrix();
        glm::mat4 proj = buildProjectionMatrix();
        glm::vec3 camPos = toGLM(sceneManager->currentCamera->transform.position);
        float     uTime = SDL_GetTicks() / 1000.0f;

        // ── 1. Built-in opaque / transparent pass ─────────────────────────────
        {
            auto buildVerts = [&](BaseObject* obj) -> std::vector<GPUVertex>
                {
                    std::vector<GPUVertex> out;
                    Vector3    sc = obj->transform.scale;
                    Vector3    pos = obj->transform.position;
                    Quaternion rot = obj->transform.rotation;

                    std::vector<Vector3> wPos, wNorm;
                    for (const Vertice& v : obj->bVertices) {
                        Vector3 vp(v.position.x * sc.x,
                            v.position.y * sc.y,
                            v.position.z * sc.z);
                        wPos.push_back(rot.RotateVector3(&vp) + pos);
                        wNorm.push_back(rot.RotateVector3(&v.normal).normalize());
                    }

                    out.reserve(obj->bTriangles.size() * 3);
                    for (const Triangle& tri : obj->bTriangles) {
                        int idx[3] = { tri.p0, tri.p1, tri.p2 };
                        for (int j = 0; j < 3; ++j) {
                            int k = idx[j];
                            GPUVertex gv;
                            gv.px = static_cast<float>(wPos[k].x);
                            gv.py = static_cast<float>(wPos[k].y);
                            gv.pz = static_cast<float>(wPos[k].z);
                            gv.nx = static_cast<float>(wNorm[k].x);
                            gv.ny = static_cast<float>(wNorm[k].y);
                            gv.nz = static_cast<float>(wNorm[k].z);
                            gv.r = static_cast<float>(obj->bVertices[k].vColor.x);
                            gv.g = static_cast<float>(obj->bVertices[k].vColor.y);
                            gv.b = static_cast<float>(obj->bVertices[k].vColor.z);
                            gv.u = tri.uv[j][0];
                            gv.v = tri.uv[j][1];
                            out.push_back(gv);
                        }
                    }
                    return out;
                };

            auto sortObjs = [](BaseObject* a, BaseObject* b) {
                GLuint pA = (a->material) ? a->material->customShaderProgram : 0;
                GLuint pB = (b->material) ? b->material->customShaderProgram : 0;
                if (pA != pB) return pA < pB;
                return a->material < b->material;
                };
            std::sort(opaqueObjs.begin(), opaqueObjs.end(), sortObjs);

            auto renderGroup = [&](const std::vector<BaseObject*>& group,
                GLuint defaultProgram,
                bool depthWrite, bool blend)
                {
                    if (group.empty()) return;

                    GLuint          currentProgram = 0;
                    const Material* lastMat = reinterpret_cast<const Material*>(-1);
                    std::vector<GPUVertex> batch;

                    for (BaseObject* obj : group)
                    {
                        const Material* mat = obj->material;
                        GLuint desired = (mat && mat->customShaderProgram != 0)
                            ? mat->customShaderProgram
                            : defaultProgram;

                        if (desired != currentProgram || mat != lastMat)
                        {
                            if (!batch.empty()) {
                                drawPass(currentProgram, batch, depthWrite, blend);
                                batch.clear();
                            }

                            if (desired != currentProgram)
                            {
                                currentProgram = desired;
                                glUseProgram(currentProgram);
                                glUniformMatrix4fv(glGetUniformLocation(currentProgram, "uView"),
                                    1, GL_FALSE, glm::value_ptr(view));
                                glUniformMatrix4fv(glGetUniformLocation(currentProgram, "uProj"),
                                    1, GL_FALSE, glm::value_ptr(proj));
                                glUniform3fv(glGetUniformLocation(currentProgram, "uCamPos"),
                                    1, glm::value_ptr(camPos));
                                glUniform1f(glGetUniformLocation(currentProgram, "uTime"), uTime);
                                uploadLights(currentProgram);

                                // ── Shadow uniforms for built-in shaders ──────
                                // The WORLD_FRAG shader doesn't consume these,
                                // but the uploads are harmless and future custom
                                // materials bound here will benefit.
                                glUniformMatrix4fv(
                                    glGetUniformLocation(currentProgram, "uLightSpaceMatrix"),
                                    1, GL_FALSE, glm::value_ptr(lightSpaceMatrix));
                                glUniform1i(glGetUniformLocation(currentProgram, "uShadowMap"), 8);
                                glActiveTexture(GL_TEXTURE8);
                                glBindTexture(GL_TEXTURE_2D, shadowMap);
                                glActiveTexture(GL_TEXTURE0);
                            }

                            if (mat != lastMat) {
                                uploadTextureLayersToShader(currentProgram, mat);
                                lastMat = mat;
                            }
                        }

                        auto objVerts = buildVerts(obj);
                        batch.insert(batch.end(), objVerts.begin(), objVerts.end());
                    }

                    if (!batch.empty()) drawPass(currentProgram, batch, depthWrite, blend);
                };

            renderGroup(opaqueObjs, shaderLib.get(ShaderType::Opaque), true, false);
            renderGroup(transObjs, shaderLib.get(ShaderType::Transparent), false, true);
        }

        // ── 2. RenderComponent (custom shader) pass ───────────────────────────
        for (BaseObject* obj : renderComps)
        {
            drawRenderComponent(obj, view, proj, camPos, uTime, dt);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    void GPURenderer::Present()
    {
        SDL_GL_SwapWindow(window);
    }

    void GPURenderer::Get_MouseState(int* x, int* y)
    {
        SDL_GetRelativeMouseState(x, y);
    }

    void GPURenderer::cleanup()
    {
        uiRenderer.cleanup();
        texManager.clear();
        shaderLib.clear();

        if (shadowFBO) { glDeleteFramebuffers(1, &shadowFBO);  shadowFBO = 0; }
        if (shadowMap) { glDeleteTextures(1, &shadowMap);       shadowMap = 0; }

        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

} // namespace HonHengine