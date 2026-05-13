#include "exampleScene.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL.h>

#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>
#include <memory>
#include <random>
#include <string>

#include "GPURenderer.h"
#include "sceneManager.h"
#include "rectangle.h"
#include "camera.h"
#include "importer.h"    // includes GltfImporter — use Importer::ImportFromOBJ / ImportFromGLTF
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "settings.h"
#include "basicmovements.h"
#include "waterShader.h"
#include "beachShader.h"
#include "skinnedShader.h"
#include "skinnedVertex.h"
#include "animation.h"
#include "textureManager.h"

namespace HonHengine
{

    // ─────────────────────────────────────────────────────────────────────────
    //  Procedural terrain helpers
    // ─────────────────────────────────────────────────────────────────────────

    static float hash(float n) { return glm::fract(sin(n) * 43758.5453123f); }

    static float noise(const glm::vec2& x)
    {
        glm::vec2 p = glm::floor(x);
        glm::vec2 f = glm::fract(x);
        f = f * f * (3.0f - 2.0f * f);
        float n = p.x + p.y * 57.0f;
        return glm::mix(glm::mix(hash(n + 0.0f), hash(n + 1.0f), f.x),
            glm::mix(hash(n + 57.0f), hash(n + 58.0f), f.x), f.y);
    }

    static float fBm(glm::vec2 p)
    {
        float value = 0.0f, amplitude = 0.5f, frequency = 0.1f;
        for (int i = 0; i < 6; ++i) {
            value += amplitude * noise(p * frequency);
            p *= 2.0f;
            amplitude *= 0.5f;
        }
        return value;
    }

    // ── Named free function so both the mesh builder and the fox AI can use it.
    //    Mirrors the lambda in BuildBeachMesh exactly.
    static constexpr float BEACH_HALF = 500.0f * 0.5f;   // half of BEACH_SIZE

    static float TerrainHeight(float x, float z)
    {
        glm::vec2 p(x, z);
        float h = fBm(p * 0.15f) * 12.0f;
        float dist = glm::length(p) / BEACH_HALF;
        float mask = 1.0f - glm::smoothstep(0.4f, 0.9f, dist);
        return (h * mask) - 2.0f;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Vertex types — local to this translation unit
    // ─────────────────────────────────────────────────────────────────────────

    struct BeachVert { float px, py, pz;  float nx, ny, nz;  float u, v; };
    struct WaterVert { float px, py, pz;  float nx, ny, nz;  float r, g, b;  float u, v; };

    // ─────────────────────────────────────────────────────────────────────────
    //  Mesh generators
    // ─────────────────────────────────────────────────────────────────────────

    static void BuildBeachMesh(int N, float size,
        std::vector<BeachVert>& outVerts,
        std::vector<unsigned int>& outIdx)
    {
        float step = size / static_cast<float>(N);
        float half = size * 0.5f;

        for (int row = 0; row <= N; ++row)
        {
            for (int col = 0; col <= N; ++col)
            {
                float x = -half + col * step;
                float z = -half + row * step;
                float y = TerrainHeight(x, z);

                float eps = 0.1f;
                glm::vec3 n = glm::normalize(glm::vec3(
                    TerrainHeight(x - eps, z) - TerrainHeight(x + eps, z),
                    2.0f * eps,
                    TerrainHeight(x, z - eps) - TerrainHeight(x, z + eps)));

                BeachVert bv;
                bv.px = x;   bv.py = y;   bv.pz = z;
                bv.nx = n.x; bv.ny = n.y; bv.nz = n.z;
                bv.u = x * 0.05f;
                bv.v = z * 0.05f;
                outVerts.push_back(bv);
            }
        }

        int stride = N + 1;
        for (int row = 0; row < N; ++row)
            for (int col = 0; col < N; ++col)
            {
                unsigned int tl = row * stride + col;
                unsigned int tr = tl + 1;
                unsigned int bl = (row + 1) * stride + col;
                unsigned int br = bl + 1;
                outIdx.push_back(tl); outIdx.push_back(tr); outIdx.push_back(bl);
                outIdx.push_back(tr); outIdx.push_back(br); outIdx.push_back(bl);
            }
    }

    static void BuildWaterMesh(int N, float size,
        std::vector<WaterVert>& outVerts,
        std::vector<unsigned int>& outIdx)
    {
        float step = size / static_cast<float>(N);
        float half = size * 0.5f;

        for (int row = 0; row <= N; ++row)
        {
            for (int col = 0; col <= N; ++col)
            {
                float x = -half + col * step;
                float z = -half + row * step;
                float distEdge = (std::max)(std::abs(x) / half, std::abs(z) / half);

                WaterVert wv;
                wv.px = x;    wv.py = 0.0f;  wv.pz = z;
                wv.nx = 0.0f; wv.ny = 1.0f;  wv.nz = 0.0f;
                wv.r = 0.05f; wv.g = 0.25f; wv.b = distEdge;
                wv.u = static_cast<float>(col) / N;
                wv.v = static_cast<float>(row) / N;
                outVerts.push_back(wv);
            }
        }

        int stride = N + 1;
        for (int row = 0; row < N; ++row)
            for (int col = 0; col < N; ++col)
            {
                unsigned int tl = row * stride + col;
                unsigned int tr = tl + 1;
                unsigned int bl = (row + 1) * stride + col;
                unsigned int br = bl + 1;
                outIdx.push_back(tl); outIdx.push_back(tr); outIdx.push_back(bl);
                outIdx.push_back(tr); outIdx.push_back(br); outIdx.push_back(bl);
            }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  FoxAI  —  per-fox behaviour state machine
    //
    //  States:
    //    Idle   → stands still, plays Survey clip, waits idleTimer seconds,
    //             then randomly picks Walk or Run.
    //    Walk   → moves forward at walkSpeed, plays Walk clip, until
    //             moveTimer expires, then returns to Idle.
    //    Run    → moves forward at runSpeed,  plays Run  clip, until
    //             moveTimer expires, then returns to Idle.
    //
    //  Every time a new movement state begins, a new random heading is chosen.
    //  The fox is clamped to the playable terrain region (radius < BEACH_HALF*0.35)
    //  and its Y is sampled from TerrainHeight() each tick so it sits on the ground.
    // ─────────────────────────────────────────────────────────────────────────

    enum class FoxState { Idle, Walk, Run };

    struct FoxAI
    {
        BaseObject* object = nullptr;

        // Animation clips (indices into the loaded clip array)
        std::shared_ptr<AnimationClip> clipSurvey;
        std::shared_ptr<AnimationClip> clipWalk;
        std::shared_ptr<AnimationClip> clipRun;

        FoxState state = FoxState::Idle;
        float    stateTimer = 0.0f;   // seconds until next state change
        float    heading = 0.0f;   // current movement direction, radians (Y-up)

        // Tunable per-fox speeds (world units / second)
        float    walkSpeed = 2.0f;
        float    runSpeed = 6.0f;

        // Random engine per fox so they don't all move in sync
        std::mt19937 rng;

        // ── Helpers ───────────────────────────────────────────────────────────
        float randFloat(float lo, float hi)
        {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        }

        // ── Initialise — call once after construction ─────────────────────────
        void Init(BaseObject* obj,
            std::shared_ptr<AnimationClip> survey,
            std::shared_ptr<AnimationClip> walk,
            std::shared_ptr<AnimationClip> run,
            uint32_t seed)
        {
            object = obj;
            clipSurvey = survey;
            clipWalk = walk;
            clipRun = run;
            rng.seed(seed);

            // Start with a random idle duration so foxes don't all act simultaneously
            stateTimer = randFloat(1.0f, 4.0f);
            heading = randFloat(0.0f, 6.2831853f);

            // Snap to ground and orient immediately on spawn.
            float initSlope = SnapToGround();
            ApplyRotation(initSlope);

            // Begin Survey animation
            if (clipSurvey) obj->animator.play(clipSurvey);
        }

        // ── Snap Y to terrain surface and compute terrain slope pitch ────────
        //
        //  Samples two points straddling the fox's pivot along its heading:
        //    front : kProbeOffset world units ahead
        //    rear  : kProbeOffset world units behind
        //
        //  Y is set to the average of front/rear heights so neither end floats.
        //  kGroundOffset corrects for the GLB pivot sitting above the feet —
        //  tune this if the paws sink into or hover above the sand.
        //
        //  Returns the terrain slope pitch (radians) for ApplyRotation().
        //
        static constexpr float kProbeOffset = 0.8f;   // half-footprint, world units
        static constexpr float kGroundOffset = 0.35f;  // pivot-to-paw distance (world scale)

        float SnapToGround()
        {
            float x = static_cast<float>(object->transform.position.x);
            float z = static_cast<float>(object->transform.position.z);

            float fwdX = std::cos(heading);
            float fwdZ = std::sin(heading);

            float hFront = TerrainHeight(x + fwdX * kProbeOffset, z + fwdZ * kProbeOffset);
            float hRear = TerrainHeight(x - fwdX * kProbeOffset, z - fwdZ * kProbeOffset);

            // Midpoint height minus the pivot offset so paws sit on the ground.
            object->transform.position.y =
                static_cast<double>((hFront + hRear) * 0.6f - kGroundOffset);

            float slopePitch = std::atan2(hFront - hRear, 2.0f * kProbeOffset);
            return slopePitch;
        }

        // ── Apply heading + slope pitch as a composed rotation ────────────────
        //
        //  Composition order (right-to-left):
        //    qYaw * qSlope * qPitch
        //
        //  1. qPitch  : -90° X.
        //  2. qSlope  : nose-up/down to match the terrain gradient.
        //  3. qYaw    : turn to face the movement direction.
        //
        void ApplyRotation(float slopePitch)
        {
            // 1. GLB baked-tilt correction (-90° around X).
            float halfPitch = glm::radians(-90.0f) * 0.5f;
            Quaternion qPitch(std::cos(halfPitch), std::sin(halfPitch), 0.0, 0.0);

            // 2. Terrain slope tilt around local X.
            float halfSlope = slopePitch * 0.5f;
            Quaternion qSlope(std::cos(halfSlope), std::sin(halfSlope), 0.0, 0.0);

            // 3. Heading yaw around Y.
            float halfYaw = heading * 0.5f;
            Quaternion qYaw(std::cos(halfYaw), 0.0, std::sin(halfYaw), 0.0);

            object->transform.rotation = qYaw * qSlope * qPitch;
        }

        // ── Per-frame update ──────────────────────────────────────────────────
        void Update(float dt)
        {
            if (!object) return;

            stateTimer -= dt;

            switch (state)
            {
                // ── Idle ──────────────────────────────────────────────────────
            case FoxState::Idle:
            {
                if (stateTimer <= 0.0f)
                {
                    // Randomly decide: 40% Walk, 30% Run, 30% Idle again
                    float roll = randFloat(0.0f, 1.0f);
                    if (roll < 0.40f)
                    {
                        EnterWalk();
                    }
                    else if (roll < 0.70f)
                    {
                        EnterRun();
                    }
                    else
                    {
                        // Another idle period, new random duration
                        stateTimer = randFloat(1.5f, 5.0f);
                    }
                }
                break;
            }

            // ── Walk ──────────────────────────────────────────────────────
            case FoxState::Walk:
            {
                MoveForward(walkSpeed, dt);
                if (stateTimer <= 0.0f) EnterIdle();
                break;
            }

            // ── Run ───────────────────────────────────────────────────────
            case FoxState::Run:
            {
                MoveForward(runSpeed, dt);
                if (stateTimer <= 0.0f) EnterIdle();
                break;
            }
            }

            float slopePitch = SnapToGround();
            ApplyRotation(slopePitch);
        }

    private:
        // Terrain boundary: keep foxes well inside the visible island
        static constexpr float kMaxRadius = BEACH_HALF * 0.32f;

        void MoveForward(float speed, float dt)
        {
            float dx = std::cos(heading) * speed * dt;
            float dz = std::sin(heading) * speed * dt;

            float nx = static_cast<float>(object->transform.position.x) + dx;
            float nz = static_cast<float>(object->transform.position.z) + dz;

            // If we'd walk off the island, pick a new heading back toward centre
            if (std::sqrt(nx * nx + nz * nz) > kMaxRadius)
            {
                // Point roughly toward origin + a small random offset
                float angleToCenter = std::atan2(
                    -static_cast<float>(object->transform.position.z),
                    -static_cast<float>(object->transform.position.x));
                heading = angleToCenter + randFloat(-0.5f, 0.5f);
                return; // skip this tick's move; next tick will go the right way
            }

            object->transform.position.x += dx;
            object->transform.position.z += dz;
        }

        void EnterIdle()
        {
            state = FoxState::Idle;
            stateTimer = randFloat(1.5f, 5.0f);
            if (clipSurvey) object->animator.crossFadeTo(clipSurvey, 0.25f);
        }

        void EnterWalk()
        {
            state = FoxState::Walk;
            stateTimer = randFloat(3.0f, 8.0f);
            heading = randFloat(0.0f, 6.2831853f);
            if (clipWalk) object->animator.crossFadeTo(clipWalk, 0.2f);
        }

        void EnterRun()
        {
            state = FoxState::Run;
            stateTimer = randFloat(2.0f, 5.0f);
            heading = randFloat(0.0f, 6.2831853f);
            if (clipRun) object->animator.crossFadeTo(clipRun, 0.15f);
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  ExampleScene::Run
    // ─────────────────────────────────────────────────────────────────────────
    void ExampleScene::Run()
    {
        // ── Scene setup ───────────────────────────────────────────────────────
        SceneManager* sm = new SceneManager();

        Camera* camera = new Camera(Vector3(10.0, 1.7, 10.0), Quaternion());
        sm->cameras->push_back(camera);
        sm->currentCamera = camera;

        DirectionalLight* sun = new DirectionalLight();
        sun->transform.rotation = Quaternion::LookRotation(Vector3(-0.5, -0.8, -0.3));
        sun->color = Color(255, 245, 209, 255);
        sun->intensity = 1.1;
        sm->lights->push_back(sun);

        // ── Renderer + shaders ────────────────────────────────────────────────
        GPURenderer renderer(sm);
        renderer.FlushGLErrors();

        renderer.texManager.load("sand", "assets/sand.jpg");
        renderer.texManager.load("teapot", "assets/default.png");

        renderer.RegisterShader("beach", BEACH_VERT, BEACH_FRAG);
        renderer.RegisterShader("water", WATER_VERT, WATER_FRAG);
        renderer.RegisterShader("skinned", SKINNED_VERT, SKINNED_FRAG);

		sm->objects->push_back(new Rectangle(Vector3(1, 5, 1), Vector3(10, 5, 15), Quaternion(), new Material(1, 1, Color(255, 25, 25, 255))));

        // ── Beach object ──────────────────────────────────────────────────────
        {
            constexpr int   BEACH_N = 300;
            constexpr float BEACH_SIZE = 500.0f;

            std::vector<BeachVert>    verts;
            std::vector<unsigned int> idx;
            BuildBeachMesh(BEACH_N, BEACH_SIZE, verts, idx);

            BaseObject* beach = new BaseObject();
            beach->render.shaderName = "beach";
            beach->render.vertexStride = sizeof(BeachVert);
            beach->render.layout = {
                { 0, 3, VertexDataType::Float, false, offsetof(BeachVert, px) },
                { 1, 3, VertexDataType::Float, false, offsetof(BeachVert, nx) },
                { 2, 2, VertexDataType::Float, false, offsetof(BeachVert, u)  },
            };
            beach->render.textures = { { "uAlbedo", "sand", 0 } };
            beach->render.cullFace = true;
            beach->render.setMesh(verts, idx);

            sm->objects->push_back(beach);
        }

        // ── Water object ──────────────────────────────────────────────────────
        {
            constexpr int   WATER_N = 150;
            constexpr float WATER_SIZE = 800.0f;

            std::vector<WaterVert>    verts;
            std::vector<unsigned int> idx;
            BuildWaterMesh(WATER_N, WATER_SIZE, verts, idx);

            BaseObject* water = new BaseObject();
            water->render.shaderName = "water";
            water->render.vertexStride = sizeof(WaterVert);
            water->render.layout = {
                { 0, 3, VertexDataType::Float, false, offsetof(WaterVert, px) },
                { 1, 3, VertexDataType::Float, false, offsetof(WaterVert, nx) },
                { 2, 3, VertexDataType::Float, false, offsetof(WaterVert, r)  },
                { 3, 2, VertexDataType::Float, false, offsetof(WaterVert, u)  },
            };
            water->render.depthWrite = false;
            water->render.blend = true;
            water->render.cullFace = false;
            water->render.setMesh(verts, idx);

            sm->objects->push_back(water);
        }

        // ── Player movement ───────────────────────────────────────────────────
        BasicMovements player(sm);
        // eyeHeight in world units above the terrain surface.
        // The beach terrain spans roughly ±6 Y units; a value of 1.7 gives a
        // human-scale viewpoint without towering over the foxes.
        player.eyeHeight = 3.7;
        player.moveSpeed = 0.15;
        player.flySpeed = 0.35;   // faster in god mode
        player.sdlKeys = SdlBindings::WASD();

        // Walk on the terrain: Y is locked to TerrainHeight(x,z) + eyeHeight.
        // God mode (G key) suspends this and enables free-fly.
        player.terrainSnapFn = [](float x, float z) {
            return TerrainHeight(x, z);
            };

        std::cout << "[Player] Press G to toggle god mode (free-fly).\n"
            << "         In god mode: WASD = fly along look dir, "
            << "Space = up, L-Ctrl = down.\n";

        // ─────────────────────────────────────────────────────────────────────
        //  Fox spawner
        //
        //  We load the mesh + textures ONCE, then clone the BaseObject for each
        //  additional fox — all clones share the same VAO, texture key, skeleton
        //  pointer and clip list.  Each fox gets its own AnimatorComponent so
        //  its bone palette is independent.
        //
        //  The Khronos Fox GLB exports three named animations:
        //    clips[0] = "Survey"  (idle look-around)
        //    clips[1] = "Walk"
        //    clips[2] = "Run"
        //
        // ─────────────────────────────────────────────────────────────────────

        constexpr int   FOX_COUNT = 6;
        constexpr float FOX_SCALE = 0.03f;

        // Spawn positions — a loose ring on the island interior
        // Y is deliberately 0 here; FoxAI::SnapToGround() corrects it immediately.
        const Vector3 spawnPositions[FOX_COUNT] = {
            {  10.0,  0.0,   5.0 },
            { -12.0,  0.0,   8.0 },
            {   5.0,  0.0, -14.0 },
            { -8.0,   0.0, -10.0 },
            {  18.0,  0.0,  -3.0 },
            {  -3.0,  0.0,  16.0 },
        };

        // ImportFromOBJ always produces [pos(3), normal(3), uv(2)] with shaderName="beach".
        // Normals are auto-computed if the OBJ has none — no manual layout wiring needed.
        BaseObject* teapot = Importer::ImportFromOBJ("assets/teapot.obj");
        teapot->render.textures = { { "uAlbedo", "teapot", 0 } };
        teapot->transform.scale = Vector3(0.1, 0.1, 0.1);
        sm->objects->push_back(teapot);

		camera->transform.LookAt(teapot->transform);

        // AI controllers — one per fox, lives as long as the scene does
        std::vector<FoxAI> foxAIs(FOX_COUNT);

        {
            auto result = Importer::ImportFromGLTF<TextureManager>("assets/Fox.glb", &renderer.texManager);

            if (!result.ok)
            {
                std::cerr << "[Scene] Fox glTF import failed: " << result.error << "\n";
            }
            else
            {
                // Guard: make sure all three expected clips are present
                const int clipCount = static_cast<int>(result.clips.size());
                std::shared_ptr<AnimationClip> clipSurvey = (clipCount > 0) ? result.clips[0] : nullptr;
                std::shared_ptr<AnimationClip> clipWalk = (clipCount > 1) ? result.clips[1] : clipSurvey;
                std::shared_ptr<AnimationClip> clipRun = (clipCount > 2) ? result.clips[2] : clipSurvey;

                std::cout << "[Scene] Fox loaded — clips:";
                for (auto& c : result.clips) std::cout << " \"" << c->name << "\"";
                std::cout << "\n";

                // ── Fox 0: reuse the object the importer created ──────────────
                {
                    BaseObject* fox = result.object;
                    fox->transform.scale = Vector3(FOX_SCALE, FOX_SCALE, FOX_SCALE);
                    fox->transform.position = spawnPositions[0];
                    fox->render.cullFace = false; // Fox mesh has inconsistent winding
                    sm->objects->push_back(fox);

                    foxAIs[0].Init(fox, clipSurvey, clipWalk, clipRun, 42u);
                }

                // ── Foxes 1-N: clone the RenderComponent from fox 0 ──────────
                //
                //  Cloning means:
                //    - Same vertexBytes/indices  → same GPU geometry (shared VAO
                //      after the first upload, or the engine will re-upload each —
                //      either way is correct; instanced rendering is a future opt.)
                //    - Same textures binding      → shared albedo texture key
                //    - Same skeleton pointer     → shared bone hierarchy definition
                //    - OWN AnimatorComponent     → independent bone palette & time
                //
                BaseObject* proto = result.object; // fox 0 is the prototype

                for (int i = 1; i < FOX_COUNT; ++i)
                {
                    BaseObject* clone = new BaseObject();

                    // Copy render data (vertex bytes, indices, layout, textures…)
                    clone->render = proto->render;
                    // Each clone needs its own GPU upload flag so the renderer
                    // uploads its own VAO.
                    clone->render._uploaded = false;

                    // Shared skeleton — AnimatorComponent holds its own bonePalette
                    clone->animator.skeleton = proto->animator.skeleton;
                    clone->animator.bonePalette = proto->animator.bonePalette; // right size, identity

                    clone->transform.scale = Vector3(FOX_SCALE, FOX_SCALE, FOX_SCALE);
                    clone->transform.position = spawnPositions[i];
                    clone->render.cullFace = false;

                    sm->objects->push_back(clone);
                    foxAIs[i].Init(clone, clipSurvey, clipWalk, clipRun,
                        static_cast<uint32_t>(i * 1337 + 7));
                }
            }
        }

        // ── Main loop ─────────────────────────────────────────────────────────
        using Clock = std::chrono::high_resolution_clock;
        auto tPrev = Clock::now();
        bool running = true;

        while (running)
        {
            auto  tNow = Clock::now();
            float dt = static_cast<float>(
                std::chrono::duration<double>(tNow - tPrev).count());
            tPrev = tNow;

            // Cap dt so a frame stall doesn't teleport foxes across the island
            if (dt > 0.1f) dt = 0.1f;

            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
            }

            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);
            if (mx != 0 || my != 0) player.TickMouse(-mx, -my);

            const Uint8* sdlKeyState = SDL_GetKeyboardState(nullptr);
            if (!player.TickKeys(sdlKeyState, dt)) running = false;

            // ── Tick all fox AI controllers ───────────────────────────────────
            // This updates position/rotation/state only.  The animator itself
            // (bone palette computation) is ticked inside the renderer — see the
            // note in the previous file.  Do NOT call obj->animator.update() here.
            for (FoxAI& ai : foxAIs)
                ai.Update(dt);

            renderer.Render(dt, 0);
            renderer.Present();
        }

        // ── Cleanup ───────────────────────────────────────────────────────────
        renderer.cleanup();

        for (BaseObject* o : *sm->objects) delete o;
        sm->objects->clear();
        for (BaseLight* l : *sm->lights)   delete l;
        sm->lights->clear();
        delete sm;
    }

} // namespace HonHengine