#pragma once
#ifndef RENDERERGPU
#define RENDERERGPU

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

// Forward-declare SDL types to avoid pulling in SDL.h in every TU that
// includes this header.
struct SDL_Window;
typedef void* SDL_GLContext;

#include "scenemanager.h"

namespace PEngine
{
    // -------------------------------------------------------------------------
    //  GPUVertex — internal vertex layout uploaded to the VBO.
    //  Defined here so rendererGPU.cpp and any future tools can share it.
    // -------------------------------------------------------------------------
    struct GPUVertex
    {
        float px, py, pz;   // world-space position
        float nx, ny, nz;   // world-space normal
        float r, g, b;    // base color [0, 1]
    };

    // -------------------------------------------------------------------------
    //  RendererGPU
    //  OpenGL 3.3 Core renderer.  Mirrors the public interface of Renderer
    //  that minecraftScene.cpp relies on.
    // -------------------------------------------------------------------------
    class RendererGPU
    {
    public:
        explicit RendererGPU(SceneManager* sceneManager);

        /// Render one frame and swap buffers.
        void RenderGPU();

        /// Wraps SDL_GetRelativeMouseState.
        void Get_MouseState(int* x, int* y);

        /// Release all GPU and SDL resources.
        void cleanup();

    private:
        // ── Scene access ──────────────────────────────────────────────────────
        SceneManager* sceneManager = nullptr;

        // ── SDL / OpenGL handles ──────────────────────────────────────────────
        SDL_Window* window = nullptr;
        SDL_GLContext glContext = nullptr;

        // ── GPU objects ───────────────────────────────────────────────────────
        GLuint shaderProgram = 0;
        GLuint vao = 0;
        GLuint vbo = 0;

        // ── CPU-side staging buffer (reused every frame) ──────────────────────
        std::vector<GPUVertex> cpuVerts;

        // ── Per-frame helpers ─────────────────────────────────────────────────
        glm::mat4 buildViewMatrix()       const;
        glm::mat4 buildProjectionMatrix() const;
        void      uploadLights()          const;
    };

} // namespace PEngine

#endif // RENDERERGPU