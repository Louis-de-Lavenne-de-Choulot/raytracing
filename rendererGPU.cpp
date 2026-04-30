// rendererGPU.cpp  —  PEngine
// ─────────────────────────────────────────────────────────────────────────────
//  GPU-backed renderer using OpenGL 3.3 Core (GLAD + GLM).
//
//  Drop-in replacement for Renderer: exposes the same public surface used by
//  minecraftScene.cpp —
//      RendererGPU(SceneManager*)
//      void RenderGPU()
//      void Get_MouseState(int* x, int* y)
//      void cleanup()
//
//  Pipeline overview
//  ─────────────────
//  CPU side (per frame)
//    1. Walk sceneManager->objects, transform every vertex to world space
//       exactly as the software renderer does (scale → rotate → translate).
//    2. Pack position + normal + color into a flat VBO.
//    3. Upload via glBufferData (streaming draw).
//
//  GPU side
//    Vertex shader  — applies the view + projection matrices built from the
//                     engine camera (position / quaternion rotation).
//    Fragment shader — Phong illumination loop over up to MAX_LIGHTS lights
//                      (ambient, point, directional) matching ComputeIllumination.
//
//  The near-plane clipping, back-face culling, and winding-order logic that
//  were done manually in the software renderer are handled automatically by
//  the OpenGL fixed-function pipeline when the correct projection matrix and
//  face-cull state are set up.
// ─────────────────────────────────────────────────────────────────────────────

#include "rendererGPU.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL.h>

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <algorithm>

#include "scenemanager.h"
#include "baseobject.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "vertice.h"
#include "triangle.h"
#include "vector3.h"
#include "settings.h"

namespace PEngine
{
    // ─────────────────────────────────────────────────────────────────────────
    //  Constants
    // ─────────────────────────────────────────────────────────────────────────

    static constexpr int   MAX_LIGHTS = 64;   // must match shader
    static constexpr float NEAR_PLANE = 0.1f;
    static constexpr float FAR_PLANE = 1000.0f;

    // ─────────────────────────────────────────────────────────────────────────
    //  Helper: convert PEngine::Vector3 → glm::vec3
    // ─────────────────────────────────────────────────────────────────────────
    static glm::vec3 toGLM(const Vector3& v)
    {
        return glm::vec3(static_cast<float>(v.x),
            static_cast<float>(v.y),
            static_cast<float>(v.z));
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  GLSL sources
    // ─────────────────────────────────────────────────────────────────────────

    static const char* VERT_SRC = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    vWorldPos = aPos;
    vNormal   = normalize(aNormal);
    vColor    = aColor;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

    // Fragment shader mirrors ComputeIllumination() from renderer.cpp.
    // Light types:  0 = ambient,  1 = point,  2 = directional
    static const char* FRAG_SRC = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;

out vec4 FragColor;

// ── Light uniform block ───────────────────────────────────────────────────
#define MAX_LIGHTS 64

uniform int   uLightCount;
uniform int   uLightType[MAX_LIGHTS];       // 0=ambient 1=point 2=directional
uniform vec3  uLightColor[MAX_LIGHTS];      // [0,1]
uniform float uLightIntensity[MAX_LIGHTS];
uniform vec3  uLightPos[MAX_LIGHTS];        // point only
uniform vec3  uLightDir[MAX_LIGHTS];        // directional only

void main()
{
    vec3 N   = normalize(vNormal);
    vec3 acc = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i)
    {
        vec3  lc   = uLightColor[i];
        float intens = uLightIntensity[i];

        if (uLightType[i] == 0)
        {
            // ── Ambient ───────────────────────────────────────────────────
            acc += vColor * lc * intens;
        }
        else if (uLightType[i] == 1)
        {
            // ── Point (Lambertian diffuse) ────────────────────────────────
            vec3  toLight = uLightPos[i] - vWorldPos;
            float dist    = length(toLight);
            if (dist < 1e-6) continue;
            vec3  L       = toLight / dist;
            float diff    = max(dot(N, L), 0.0);
            acc += vColor * lc * intens * diff;
        }
        else if (uLightType[i] == 2)
        {
            // ── Directional (Lambertian diffuse) ──────────────────────────
            vec3  L    = normalize(uLightDir[i]);
            float diff = max(dot(N, L), 0.0);
            acc += vColor * lc * intens * diff;
        }
    }

    // Clamp to [0,1] — mirrors std::clamp(accX * 255.0, 0.0, 255.0) / 255
    acc = clamp(acc, vec3(0.0), vec3(1.0));
    FragColor = vec4(acc, 1.0);
}
)GLSL";

    // ─────────────────────────────────────────────────────────────────────────
    //  Shader compilation helpers
    // ─────────────────────────────────────────────────────────────────────────

    static GLuint compileShader(GLenum type, const char* src)
    {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);

        GLint ok = 0;
            glGetShaderInfoLog(id, sizeof(log), nullptr, log);
            std::cerr << "[RendererGPU] Shader compile error:\n" << log << "\n";
            glDeleteShader(id);
            return 0;
        }
        return id;
    }

    static GLuint linkProgram(GLuint vert, GLuint frag)
    {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert);
        glAttachShader(prog, frag);
        glLinkProgram(prog);

        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::cerr << "[RendererGPU] Program link error:\n" << log << "\n";
            glDeleteProgram(prog);
            return 0;
        }
        glDeleteShader(vert);
        glDeleteShader(frag);
        return prog;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Constructor — creates SDL2 window with an OpenGL 3.3 context
    // ─────────────────────────────────────────────────────────────────────────
    RendererGPU::RendererGPU(SceneManager* sm)
        : sceneManager(sm)
    {
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            std::cerr << "[RendererGPU] SDL_Init failed: " << SDL_GetError() << "\n";
            return;
        }

        // Request OpenGL 3.3 Core profile
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

        window = SDL_CreateWindow(
            "PEngine — GPU Renderer",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            Settings::canvasWidth, Settings::canvasHeight,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

        if (!window)
        {
            std::cerr << "[RendererGPU] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
            SDL_Quit();
            return;
        }

        glContext = SDL_GL_CreateContext(window);
        if (!glContext)
        {
            std::cerr << "[RendererGPU] SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
            SDL_DestroyWindow(window);
            SDL_Quit();
            return;
        }

        // Load OpenGL function pointers via GLAD
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress)))
        {
            std::cerr << "[RendererGPU] GLAD failed to load OpenGL\n";
            return;
        }

        std::cout << "[RendererGPU] OpenGL " << glGetString(GL_VERSION) << "\n";

        SDL_GetWindowSize(window, &Settings::canvasWidth, &Settings::canvasHeight);
        SDL_SetRelativeMouseMode(SDL_TRUE);
        SDL_GL_SetSwapInterval(1); // vsync

        // ── OpenGL state ─────────────────────────────────────────────────────
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Software renderer uses clockwise winding for back-face culling
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);

        glViewport(0, 0, Settings::canvasWidth, Settings::canvasHeight);

        // ── Shader program ────────────────────────────────────────────────────
        GLuint vs = compileShader(GL_VERTEX_SHADER, VERT_SRC);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
        shaderProgram = linkProgram(vs, fs);

        // ── VAO / VBO (streaming — contents re-uploaded every frame) ─────────
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        // layout(location = 0) aPos
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex),
            reinterpret_cast<void*>(offsetof(GPUVertex, px)));
        // layout(location = 1) aNormal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex),
            reinterpret_cast<void*>(offsetof(GPUVertex, nx)));
        // layout(location = 2) aColor
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GPUVertex),
            reinterpret_cast<void*>(offsetof(GPUVertex, r)));

        glBindVertexArray(0);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  buildViewMatrix
    //  Replicates what the software renderer does when it subtracts camPos and
    //  applies camConjugate to every vertex before projection.
    // ─────────────────────────────────────────────────────────────────────────
    glm::mat4 RendererGPU::buildViewMatrix() const
    {
        auto* cam = sceneManager->currentCamera;

        // Camera position
        glm::vec3 eye = toGLM(cam->transform.position);

        // The engine camera quaternion represents the camera's orientation in
        // world space.  The view matrix is the inverse of the camera's world
        // transform, so we need the conjugate rotation.
        //
        // PEngine stores quaternion as (w, x, y, z) — check quaternion.h.
        // GLM stores as (w, x, y, z) with constructor glm::quat(w, x, y, z).
        auto& q = cam->transform.rotation;
        // Conjugate = (w, -x, -y, -z) — inverts the camera's world-space rotation
        // so we can transform world vertices into camera space.
        glm::quat camQuat(
            static_cast<float>(q.w),
            static_cast<float>(-q.x),
            static_cast<float>(-q.y),
            static_cast<float>(-q.z));

        // The engine uses +Z as the camera forward direction, but OpenGL's
        // perspective matrix expects the camera to look down -Z (right-handed NDC).
        // A 180° rotation around Y flips +Z→-Z and +X→-X, realigning the axes.
        glm::quat flipZ = glm::quat(0.0f, 0.0f, 1.0f, 0.0f); // 180° around Y
        glm::quat finalQuat = camQuat * flipZ;

        glm::mat4 rot = glm::mat4_cast(finalQuat);
        glm::mat4 trans = glm::translate(glm::mat4(1.0f), -eye);
        return rot * trans;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  buildProjectionMatrix
    //  Converts the engine's viewport/canvas settings into a standard
    //  perspective matrix.  The software renderer uses:
    //      projected.x = vertex.x * viewportDistance / vertex.z  (then scaled)
    //  which is equivalent to a perspective projection with:
    //      fovY = 2 * atan(viewportHeight/2 / viewportDistance)
    // ─────────────────────────────────────────────────────────────────────────
    glm::mat4 RendererGPU::buildProjectionMatrix() const
    {
        float aspect = static_cast<float>(Settings::canvasWidth) /
            static_cast<float>(Settings::canvasHeight);

        float fovY = 2.0f * std::atan2(
            static_cast<float>(Settings::viewportHeight) * 0.5f,
            static_cast<float>(Settings::viewportDistance));

        return glm::perspective(fovY, aspect, NEAR_PLANE, FAR_PLANE);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  uploadLights  —  packs sceneManager->lights into shader uniforms
    // ─────────────────────────────────────────────────────────────────────────
    void RendererGPU::uploadLights() const
    {
        auto& lights = *sceneManager->lights;
        int   count = static_cast<int>(
            (std::min)(static_cast<int>(lights.size()), MAX_LIGHTS));

        glUniform1i(glGetUniformLocation(shaderProgram, "uLightCount"), count);

        for (int i = 0; i < count; ++i)
        {
            BaseLight* bl = lights[i];

            // Build uniform name strings for each light element
            auto loc = [&](const std::string& arr) -> GLint {
                std::string name = arr + "[" + std::to_string(i) + "]";
                return glGetUniformLocation(shaderProgram, name.c_str());
                };

            glUniform1i(loc("uLightType"), static_cast<int>(bl->type));

            glUniform1f(loc("uLightIntensity"),
                static_cast<float>(bl->intensity));

            glUniform3f(loc("uLightColor"),
                bl->color.r / 255.0f,
                bl->color.g / 255.0f,
                bl->color.b / 255.0f);

            if (bl->type == POINT_LIGHT)
            {
                PointLight* pl = static_cast<PointLight*>(bl);
                // Apply light's own rotation, mirroring the software renderer:
                //   Vector3 rotated = pl->rotation.RotateVector3(&pl->position)
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
    //  RenderGPU  —  main render entry point (called once per frame)
    // ─────────────────────────────────────────────────────────────────────────
    void RendererGPU::RenderGPU()
    {
        glClearColor(0.53f, 0.81f, 0.98f, 1.0f); // sky-blue clear color
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shaderProgram);

        // ── Matrices ──────────────────────────────────────────────────────────
        glm::mat4 view = buildViewMatrix();
        glm::mat4 proj = buildProjectionMatrix();

        glUniformMatrix4fv(
            glGetUniformLocation(shaderProgram, "uView"),
            1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(
            glGetUniformLocation(shaderProgram, "uProj"),
            1, GL_FALSE, glm::value_ptr(proj));

        // ── Lights ────────────────────────────────────────────────────────────
        uploadLights();

        // ── Geometry — build one combined vertex buffer for all objects ────────
        //
        // We replicate the per-vertex transform from RenderInstance():
        //   vPos (model)  →  scale  →  rotate (obj quat)  →  + objPos  = worldPos
        //   normal        = normalize( rotate(objQuat, vPos) )
        //
        // Each triangle emits 3 GPUVertex entries (unindexed draw).

        cpuVerts.clear();
        cpuVerts.reserve(65536);

        for (BaseObject* obj : *sceneManager->objects)
        {
            if (!obj || obj->bVertices.empty() || obj->bTriangles.empty())
                continue;

            Vector3    objScale = obj->transform.scale;
            Vector3    objPos = obj->transform.position;
            Quaternion objRot = obj->transform.rotation;

            Color matColor = obj->material ? obj->material->color : Color(255, 0, 255, 255);

            // Pre-transform all vertices to world space once
            std::vector<Vector3> worldPos;
            std::vector<Vector3> worldNorm;
            worldPos.reserve(obj->bVertices.size());
            worldNorm.reserve(obj->bVertices.size());

            for (const Vertice& v : obj->bVertices)
            {
                // Scale
                Vector3 vp(v.position.x * objScale.x,
                    v.position.y * objScale.y,
                    v.position.z * objScale.z);
                // Rotate
                vp = objRot.RotateVector3(&vp);
                // Normal in world space (before translation)
                Vector3 norm = objRot.RotateVector3(&v.position);
                double  nmag = norm.magnitude();
                if (nmag > 1e-9)
                    norm = norm * (1.0 / nmag);

                // Translate
                vp = vp + objPos;

                worldPos.push_back(vp);
                worldNorm.push_back(norm);
            }

            // Emit a triangle per Triangle entry
            for (const Triangle& tri : obj->bTriangles)
            {
                int idx[3] = { tri.p0, tri.p1, tri.p2 };
                Color triColor = tri.material ? tri.material->color : matColor;

                float cr = triColor.r / 255.0f;
                float cg = triColor.g / 255.0f;
                float cb = triColor.b / 255.0f;

                for (int j = 0; j < 3; ++j)
                {
                    int k = idx[j];
                    if (k < 0 || k >= static_cast<int>(worldPos.size()))
                        continue;

                    const Vector3& wp = worldPos[k];
                    const Vector3& wn = worldNorm[k];

                    // Apply vertex shade factor to base color (matches software renderer)
                    float shade = static_cast<float>(obj->bVertices[k].shade);
                    GPUVertex gv;
                    gv.px = static_cast<float>(wp.x);
                    gv.py = static_cast<float>(wp.y);
                    gv.pz = static_cast<float>(wp.z);
                    gv.nx = static_cast<float>(wn.x);
                    gv.ny = static_cast<float>(wn.y);
                    gv.nz = static_cast<float>(wn.z);
                    gv.r = cr * shade;
                    gv.g = cg * shade;
                    gv.b = cb * shade;
                    cpuVerts.push_back(gv);
                }
            }
        }

        // ── Upload and draw ───────────────────────────────────────────────────
        if (!cpuVerts.empty())
        {
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(cpuVerts.size() * sizeof(GPUVertex)),
                cpuVerts.data(),
                GL_STREAM_DRAW);

            glDrawArrays(GL_TRIANGLES, 0,
                static_cast<GLsizei>(cpuVerts.size()));
            glBindVertexArray(0);
        }

        SDL_GL_SwapWindow(window);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Get_MouseState — thin wrapper around SDL2 relative mouse
    // ─────────────────────────────────────────────────────────────────────────
    void RendererGPU::Get_MouseState(int* x, int* y)
    {
        SDL_GetRelativeMouseState(x, y);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  cleanup — releases all GPU and SDL resources
    // ─────────────────────────────────────────────────────────────────────────
    void RendererGPU::cleanup()
    {
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteProgram(shaderProgram);

        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

} // namespace PEngine