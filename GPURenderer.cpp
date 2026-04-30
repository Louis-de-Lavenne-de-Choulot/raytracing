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

namespace PEngine
{
    static constexpr int   MAX_LIGHTS = 64;
    static constexpr float NEAR_PLANE = 0.05f;
    static constexpr float FAR_PLANE = 1000.0f;
    static constexpr int   MAX_TEX_LAYERS = 8;

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
            "PEngine - GPU Renderer",
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
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  RegisterShader  — public entry point for scenes to add named shaders
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
    //  uploadTextureLayersToShader  — built-in multi-layer material system
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
    //  Extracts the first directional light and uploads uSunDir / uSunColor /
    //  uSunIntensity. Used automatically for every RenderComponent object.
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
    //  Called once per RenderComponent object inside Render().
    //  Lazy-uploads the VAO on the first call, then issues a draw every frame.
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

        // ── Tick animator and compute bone palette ────────────────────────────
        AnimatorComponent& anim = obj->animator;
        const bool skinned = anim.active();
        if (skinned)
            anim.update(dt);

        // ── Build model matrix from the object's transform ────────────────────
        // Scale is applied here — this is what was missing for the fox.
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

        // ── Skinning uniforms ─────────────────────────────────────────────────
        glUniform1i(glGetUniformLocation(program, "uSkinned"), skinned ? 1 : 0);

        if (skinned)
        {
            glUniformMatrix4fv(
                glGetUniformLocation(program, "uBonePalette"),
                static_cast<GLsizei>(anim.bonePalette.size()),
                GL_FALSE,
                glm::value_ptr(anim.bonePalette[0]));
        }

        // ── Bind textures declared on the object ──────────────────────────────
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
        glClearColor(0.60f, 0.70f, 0.78f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = buildViewMatrix();
        glm::mat4 proj = buildProjectionMatrix();
        glm::vec3 camPos = toGLM(sceneManager->currentCamera->transform.position);
        float     uTime = SDL_GetTicks() / 1000.0f;

        // ── Partition objects into the three dispatch buckets ─────────────────
        //   • renderComps : objects with a valid RenderComponent (custom shader)
        //   • opaqueObjs  : built-in pipeline, opaque
        //   • transObjs   : built-in pipeline, transparent
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

        // ── 1. Built-in opaque pass ───────────────────────────────────────────
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
        // Drawn after the built-in opaque pass. Blend order within this group
        // is determined by the order objects were pushed into sceneManager.
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
        shaderLib.clear();   // frees all ManagedVAOs

        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

} // namespace PEngine