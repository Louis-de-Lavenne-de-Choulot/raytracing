#pragma once
#ifndef GPURENDERER
#define GPURENDERER

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "scenemanager.h"
#include "textureManager.h"
#include "shaderLibrary.h"
#include "UIRenderer.h"
#include "animation.h"

struct SDL_Window;
typedef void* SDL_GLContext;

namespace HonHengine
{
    struct GPUVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float r, g, b;
        float u, v;
    };

    class GPURenderer
    {
    public:
        bool debugMode = false;

        explicit GPURenderer(SceneManager* sceneManager);
        explicit GPURenderer(SceneManager* sm, SDL_Window* existingWindow, SDL_GLContext existingContext);

        // ── Scene setup (call before the loop) ───────────────────────────────
        // Register a named shader so objects can reference it by name.
        // Assets (textures) are loaded through texManager directly.
        void RegisterShader(const std::string& name,
            const char* vertSrc,
            const char* fragSrc);

        // ── Per-frame API (the only calls a scene loop needs) ─────────────────
        void Render(float dt = 0.0f, GLuint fbo = 0);  // clear + draw all scene objects; dt drives animators
        void Present();   // SDL_GL_SwapWindow

        // ── Play mode ─────────────────────────────────────────────────────────
        // SetPlayMode(true)  → calls Start()     on all script instances.
        // SetPlayMode(false) → calls OnDestroy() on all script instances.
        // UpdateGameLogic(dt) is driven automatically from inside Render().
        void SetPlayMode(bool playing);
        bool IsPlaying() const { return m_playing; }

        // ── Input ─────────────────────────────────────────────────────────────
        void Get_MouseState(int* x, int* y);

        // ── Lifetime ──────────────────────────────────────────────────────────
        void cleanup();
        void FlushGLErrors();

        // ── Public subsystems (scenes load textures through these) ────────────
        TextureManager texManager;

        // ── Camera helpers (used by BasicMovements / player controllers) ──────
        glm::mat4 buildViewMatrix()       const;
        glm::mat4 buildProjectionMatrix() const;

        UIRenderer& GetUIRenderer() { return uiRenderer; }
        const glm::mat4& GetLastViewMatrix() const { return m_lastView; }
        const glm::mat4& GetLastProjMatrix() const { return m_lastProj; }

    private:
        glm::mat4 m_lastView = glm::mat4(1.0f);
        glm::mat4 m_lastProj = glm::mat4(1.0f);
        bool ownContext;
        // ── Internal helpers ──────────────────────────────────────────────────
        void uploadLights(GLuint program)                              const;
        void uploadSunUniforms(GLuint program)                         const;
        void uploadTextureLayersToShader(GLuint program,
            const Material* mat)          const;

        // Advance scripts, animations, and any other per-frame game logic.
        // Called unconditionally from Render(); no-ops when m_playing is false.
        void UpdateGameLogic(float dt);

        // Draw one RenderComponent object (lazy VAO upload on first call).
        void drawRenderComponent(BaseObject* obj,
            const glm::mat4& view,
            const glm::mat4& proj,
            const glm::vec3& camPos,
            float            uTime,
            float            dt);

        // Built-in triangle-soup streaming batcher (legacy pipeline).
        void drawPass(GLuint program,
            std::vector<GPUVertex>& verts,
            bool depthWrite,
            bool blend) const;

        void CreateFrameBuffer(GLuint& fboOut, GLuint& texOut, int width, int height);

        void      initShadowMap();
        glm::mat4 buildLightSpaceMatrix() const;
        void      renderShadowPass(const std::vector<BaseObject*>& renderComps,
            const std::vector<BaseObject*>& opaqueObjs);

        // ── Members ───────────────────────────────────────────────────────────
        SceneManager* sceneManager = nullptr;

        bool m_playing = false;   // true while the scene is in play mode

        SDL_Window* window = nullptr;
        SDL_GLContext glContext = nullptr;

        UIRenderer    uiRenderer;
        ShaderLibrary shaderLib;   // private — scenes never touch this directly

        // Internal streaming VAO for the built-in batcher.
        GLuint vao = 0;
        GLuint vbo = 0;

        GLuint    shadowFBO = 0;
        GLuint    shadowMap = 0;   // depth texture with GL_COMPARE_REF_TO_TEXTURE
        GLuint    shadowProgram = 0;   // depth-only shader program
        glm::mat4 lightSpaceMatrix = glm::mat4(1.0f); // Light-space MVP used by all shaders

        std::vector<GPUVertex> cpuVerts;
        std::vector<GPUVertex> transparentVerts;
    };

} // namespace HonHengine

#endif // GPURENDERER