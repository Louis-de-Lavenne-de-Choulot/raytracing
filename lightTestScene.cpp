// lightTestScene.cpp
// ─────────────────────────────────────────────────────────────────────────────
//  Light verification scene for PEngine.
//
//  ZONES
//  ─────
//   A (X≈-10.5)  BaseLight (ambient) only
//   B (X≈ -3.5)  PointLight only          — light follows a figure-8 path
//   C (X≈  3.5)  DirectionalLight only    — direction rotates in a cone
//   D (X≈ 10.5)  All three combined       — point light follows Lissajous
//
//  CONTROLS
//  ────────
//   Z / S       – move forward / backward
//   Q / D       – strafe left / right
//   Mouse       – look around (yaw + pitch, no roll)
//   ESC         – quit
//
//  The camera is fully manual; nothing moves it automatically.
//  The lights animate on their own so you can walk up to any zone and
//  observe the dynamic shading from whatever angle you choose.
// ─────────────────────────────────────────────────────────────────────────────

#include "lightTestScene.h"

#include <windows.h>       // GetAsyncKeyState
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cmath>
#include <vector>

#include <SDL.h>

#include "sceneManager.h"
#include "vector3.h"
#include "sphere.h"
#include "rectangle.h"
#include "renderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"

namespace PEngine
{
    // ─────────────────────────────────────────────
    //  Helpers
    // ─────────────────────────────────────────────
    static Quaternion LT_Identity() { return Quaternion(1, 0, 0, 0); }

    static Quaternion LT_AxisAngle(Vector3 axis, double deg)
    {
        double r = deg * (3.14159265358979 / 180.0);
        double h = r * 0.5;
        Vector3 n = axis.normalize();
        return Quaternion(cos(h), n.x * sin(h), n.y * sin(h), n.z * sin(h)).Normalized();
    }

    // Build quaternion from yaw (world Y) then pitch (local X).
    // Same order as main.cpp — no roll can accumulate.
    static Quaternion LT_YawPitch(float yawDeg, float pitchDeg)
    {
        float yR = yawDeg * (3.14159265f / 180.0f);
        float pR = pitchDeg * (3.14159265f / 180.0f);
        Quaternion qY(cos(yR * 0.5f), 0.0f, sin(yR * 0.5f), 0.0f);
        Quaternion qP(cos(pR * 0.5f), sin(pR * 0.5f), 0.0f, 0.0f);
        return (qY * qP).Normalized();
    }

    static Material* WhiteMat()
    {
        return new Material(0.0, 0, Color(230, 230, 230, 255), Color(0, 0, 0, 255));
    }

    // ─────────────────────────────────────────────
    //  Zone centres
    // ─────────────────────────────────────────────
    static constexpr double ZONE_A_X = -10.5;
    static constexpr double ZONE_B_X = -3.5;
    static constexpr double ZONE_C_X = 3.5;
    static constexpr double ZONE_D_X = 10.5;
    static constexpr double ZONE_Z = 0.0;

    // ─────────────────────────────────────────────
    //  Scene helpers
    // ─────────────────────────────────────────────
    static void SpawnProbeGrid(SceneManager* sm, double cx, double cz)
    {
        const double SPACING = 2.2;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                double x = cx + (col - 1) * SPACING;
                double z = cz + (row - 1) * SPACING;
                double s = 0.45 + row * 0.07;
                sm->objects->push_back(
                    new Sphere(Vector3(s, s, s), Vector3(x, 0.6, z),
                        LT_Identity(), WhiteMat(), 16, 16));
            }
    }

    static void SpawnGroundTile(SceneManager* sm, double cx, double cz)
    {
        sm->objects->push_back(new Rectangle(
            Vector3(4.0, 0.05, 4.0), Vector3(cx, -0.1, cz), LT_Identity(),
            new Material(0.0, 0, Color(60, 60, 65, 255), Color(0, 0, 0, 255))));
    }

    static void SpawnLabel(SceneManager* sm, double cx, double cz, Color col)
    {
        sm->objects->push_back(new Rectangle(
            Vector3(3.8, 0.25, 0.15), Vector3(cx, 0.25, cz + 3.5), LT_Identity(),
            new Material(0.0, 0, col, Color(0, 0, 0, 255))));
    }

    // ─────────────────────────────────────────────
    //  Scene entry point
    // ─────────────────────────────────────────────
    void LightTestScene::LoadScene(SceneManager* sm)
    {
        std::mutex        sceneMutex;
        std::atomic<bool> running{ true };

        PointLight* pointLightB = nullptr;
        DirectionalLight* dirLightC = nullptr;
        PointLight* pointLightD = nullptr;
        Rectangle* markerB = nullptr;
        Rectangle* markerD = nullptr;

        // ── Camera state — mirrors main.cpp exactly ───────────────────────
        Camera* camera = new Camera();
        sm->currentCamera = camera;
        if (!sm->cameras) sm->cameras = new std::vector<Camera*>();
        sm->cameras->emplace_back(camera);

        // Start position: slightly elevated, looking down the Z axis
        sm->currentCamera->transform.position = Vector3(0, 2, -18);

        float cameraYaw = 0.0f;
        float cameraPitch = 10.0f;   // slight downward tilt to see the spheres
        constexpr float PITCH_LIMIT = 89.0f;
        constexpr float MOVE_SPEED = 0.15f;
        constexpr float SENSITIVITY = 0.04f;

        sm->currentCamera->transform.rotation = LT_YawPitch(cameraYaw, cameraPitch);

        // ─────────────────────────────────────────────
        //  ZONE A – Ambient only
        // ─────────────────────────────────────────────
        SpawnProbeGrid(sm, ZONE_A_X, ZONE_Z);
        SpawnGroundTile(sm, ZONE_A_X, ZONE_Z);
        SpawnLabel(sm, ZONE_A_X, ZONE_Z, Color(60, 80, 180, 255));
        sm->lights->push_back(new BaseLight(0.55, Color(160, 180, 255, 255)));

        // ─────────────────────────────────────────────
        //  ZONE B – Point light only, figure-8 path
        // ─────────────────────────────────────────────
        SpawnProbeGrid(sm, ZONE_B_X, ZONE_Z);
        SpawnGroundTile(sm, ZONE_B_X, ZONE_Z);
        SpawnLabel(sm, ZONE_B_X, ZONE_Z, Color(200, 60, 60, 255));

        pointLightB = new PointLight(0.80, Color(255, 80, 80, 255),
            Vector3(ZONE_B_X, 2.5, 0.0));
        sm->lights->push_back(pointLightB);

        markerB = new Rectangle(
            Vector3(0.18, 0.18, 0.18), Vector3(ZONE_B_X, 2.5, 0.0), LT_Identity(),
            new Material(0.0, 0, Color(255, 60, 60, 255), Color(0, 0, 0, 255)));
        sm->objects->push_back(markerB);

        // ─────────────────────────────────────────────
        //  ZONE C – Directional light only, cone rotation
        // ─────────────────────────────────────────────
        SpawnProbeGrid(sm, ZONE_C_X, ZONE_Z);
        SpawnGroundTile(sm, ZONE_C_X, ZONE_Z);
        SpawnLabel(sm, ZONE_C_X, ZONE_Z, Color(60, 190, 80, 255));

        dirLightC = new DirectionalLight(0.85, Color(80, 255, 100, 255),
            Vector3(1.0, 1.2, 0.0).normalize());
        sm->lights->push_back(dirLightC);

        // ─────────────────────────────────────────────
        //  ZONE D – All three combined, Lissajous point light
        // ─────────────────────────────────────────────
        SpawnProbeGrid(sm, ZONE_D_X, ZONE_Z);
        SpawnGroundTile(sm, ZONE_D_X, ZONE_Z);
        SpawnLabel(sm, ZONE_D_X, ZONE_Z, Color(200, 160, 40, 255));

        sm->lights->push_back(new BaseLight(0.20, Color(255, 245, 220, 255)));

        pointLightD = new PointLight(0.65, Color(100, 160, 255, 255),
            Vector3(ZONE_D_X, 3.0, 0.0));
        sm->lights->push_back(pointLightD);

        sm->lights->push_back(new DirectionalLight(0.40, Color(255, 220, 80, 255),
            Vector3(-1.0, 2.5, 1.0).normalize()));

        markerD = new Rectangle(
            Vector3(0.18, 0.18, 0.18), Vector3(ZONE_D_X, 3.0, 0.0), LT_Identity(),
            new Material(0.0, 0, Color(80, 140, 255, 255), Color(0, 0, 0, 255)));
        sm->objects->push_back(markerD);

        // ── Separator walls ───────────────────────────────────────────────
        auto MakeSeparator = [&](double x)
            {
                sm->objects->push_back(new Rectangle(
                    Vector3(0.05, 1.5, 5.0), Vector3(x, 0.7, 0.0), LT_Identity(),
                    new Material(0.0, 0, Color(15, 15, 15, 255), Color(0, 0, 0, 255))));
            };
        MakeSeparator((ZONE_A_X + ZONE_B_X) / 2.0);
        MakeSeparator((ZONE_B_X + ZONE_C_X) / 2.0);
        MakeSeparator((ZONE_C_X + ZONE_D_X) / 2.0);

        // ── Zone banners ──────────────────────────────────────────────────
        struct Banner { double cx; Color col; };
        Banner banners[] = {
            { ZONE_A_X, Color(50, 60,150,255) },
            { ZONE_B_X, Color(160, 40, 40,255) },
            { ZONE_C_X, Color(40,140, 60,255) },
            { ZONE_D_X, Color(160,120, 20,255) },
        };
        for (auto& b : banners)
            sm->objects->push_back(new Rectangle(
                Vector3(3.5, 0.12, 0.6), Vector3(b.cx, 2.8, -3.8), LT_Identity(),
                new Material(0.0, 0, b.col, Color(0, 0, 0, 255))));

        // ─────────────────────────────────────────────
        //  Light animation thread  (lights only — no camera)
        // ─────────────────────────────────────────────
        std::thread animThread([&]()
            {
                using Clock = std::chrono::high_resolution_clock;
                auto   prev = Clock::now();
                double time = 0.0;

                while (running)
                {
                    auto   now = Clock::now();
                    double dt = std::chrono::duration<double>(now - prev).count();
                    prev = now;
                    time += dt;

                    std::lock_guard<std::mutex> lk(sceneMutex);

                    // Zone B: figure-8 (Lissajous 2:1)
                    if (pointLightB && markerB)
                    {
                        const double R = 3.2, FQ = 0.7;
                        double lx = ZONE_B_X + R * sin(2.0 * FQ * time);
                        double lz = R * sin(FQ * time);
                        double ly = 2.5 + 0.8 * cos(2.0 * FQ * time);
                        pointLightB->position = Vector3(lx, ly, lz);
                        markerB->transform.position = Vector3(lx, ly, lz);
                        pointLightB->intensity = 0.75 + 0.25 * sin(2.8 * FQ * time);
                    }

                    // Zone C: direction traces a cone
                    if (dirLightC)
                    {
                        double dx = sin(1.1 * time);
                        double dz = cos(1.1 * time);
                        dirLightC->direction = Vector3(dx, 1.2, dz).normalize();
                    }

                    // Zone D: incommensurate Lissajous (never repeats)
                    if (pointLightD && markerD)
                    {
                        double lx = ZONE_D_X + 3.5 * sin(1.1 * time);
                        double lz = 2.0 * sin(0.7 * time);
                        double ly = 3.0 + 1.2 * fabs(cos(1.1 * time));
                        pointLightD->position = Vector3(lx, ly, lz);
                        markerD->transform.position = Vector3(lx, ly, lz);
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            });

        // ─────────────────────────────────────────────
        //  Movement thread  — GetAsyncKeyState, same as main.cpp
        // ─────────────────────────────────────────────
        std::thread moveThread([&]()
            {
                while (running)
                {
                    Vector3 pos, fwd, left, right;
                    {
                        std::lock_guard<std::mutex> lk(sceneMutex);
                        pos = sm->currentCamera->transform.position;
                        fwd = sm->currentCamera->transform.forward();
                        left = sm->currentCamera->transform.left();
                        right = sm->currentCamera->transform.right();
                    }

                    if (GetAsyncKeyState('Z') & 0x8000) pos = pos + fwd * MOVE_SPEED;
                    if (GetAsyncKeyState('S') & 0x8000) pos = pos - fwd * MOVE_SPEED;
                    if (GetAsyncKeyState('Q') & 0x8000) pos = pos + left * MOVE_SPEED;
                    if (GetAsyncKeyState('D') & 0x8000) pos = pos + right * MOVE_SPEED;

                    {
                        std::lock_guard<std::mutex> lk(sceneMutex);
                        sm->currentCamera->transform.position = pos;
                    }

                    Sleep(10);
                }
            });

        // ─────────────────────────────────────────────
        //  Renderer + main loop
        // ─────────────────────────────────────────────
        Renderer renderer(sm);

        std::cout
            << "\n╔═══════════════════════════════════════════╗\n"
            << "║       LIGHT TEST SCENE  –  controls        ║\n"
            << "║                                            ║\n"
            << "║  Z / S      – forward / back               ║\n"
            << "║  Q / D      – strafe left / right          ║\n"
            << "║  Mouse      – look around                  ║\n"
            << "║  ESC        – quit                         ║\n"
            << "║                                            ║\n"
            << "║  Coloured cubes = live light positions.    ║\n"
            << "╚═══════════════════════════════════════════╝\n\n";

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
            }

            // ── Mouse look ───────────────────────────────────────────────
            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);
            if (mx != 0 || my != 0)
            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                cameraYaw += mx * SENSITIVITY;
                cameraPitch += my * SENSITIVITY;

                if (cameraYaw >= 360.0f) cameraYaw -= 360.0f;
                if (cameraYaw < 0.0f) cameraYaw += 360.0f;
                if (cameraPitch > PITCH_LIMIT) cameraPitch = PITCH_LIMIT;
                if (cameraPitch < -PITCH_LIMIT) cameraPitch = -PITCH_LIMIT;

                sm->currentCamera->transform.rotation =
                    LT_YawPitch(cameraYaw, cameraPitch);
            }

            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                auto s = std::chrono::high_resolution_clock::now();
                renderer.Render();
                auto e = std::chrono::high_resolution_clock::now();
                std::cout << "frame: "
                    << std::chrono::duration<double>(e - s).count() * 1000.0
                    << " ms\n";
            }
        }

        animThread.join();
        moveThread.join();
        renderer.cleanup();
    }

} // namespace PEngine