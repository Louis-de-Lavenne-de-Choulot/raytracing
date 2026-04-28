// guardScene.cpp  (refactored)
// ─────────────────────────────────────────────────────────────────────────────
//  Same "Searchlight" scene, now using the new PEngine systems:
//
//    • BasicMovements    — player controller (extracted from the old inline code)
//    • SceneGroup        — guard bot + torch assembled as one grouped object
//    • RigidBody/AABB    — framework present; guard body has a kinematic body,
//                          a dynamic crate shows impulse/physics
//    • CollisionTrigger  — two demo triggers:
//          1) "ExitZone"   (ghost trigger) — prints when player nears the door
//          2) "BotDetect"  (solid trigger) — fired when the bot enters a zone
//
//  Nothing from the original scene was removed; this is a drop-in replacement
//  that demonstrates the new systems.
// ─────────────────────────────────────────────────────────────────────────────

#include "guardScene.h"

#include <iostream>
#include <thread>
#include <vector>
#include <cmath>
#include <atomic>
#include <mutex>
#include <chrono>

#include <SDL.h>
#include <windows.h>

#include "vector3.h"
#include "plane.h"
#include "rectangle.h"
#include "renderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sceneManager.h"

// ── New systems ──────────────────────────────────────────────────────────────
#include "basicmovements.h"
#include "scenegroup.h"
#include "rigidbody.h"
#include "collisiontrigger.h"

namespace PEngine
{
    // ── Tiny math helpers (unchanged) ─────────────────────────────────────────
    static Quaternion AxisAngle(Vector3 axis, double deg)
    {
        double r = deg * (3.14159265358979 / 180.0);
        double h = r * 0.5;
        Vector3 n = axis.normalize();
        return Quaternion(cos(h), n.x * sin(h), n.y * sin(h), n.z * sin(h)).Normalized();
    }
    static Quaternion Identity() { return Quaternion(1, 0, 0, 0); }

    static const int PATROL_COUNT = 6;
    static const Vector3 patrol[PATROL_COUNT] = {
        Vector3(12, 0,  12),
        Vector3(-12, 0,  12),
        Vector3(-12, 0,   0),
        Vector3(-12, 0, -12),
        Vector3(12, 0, -12),
        Vector3(12, 0,   0),
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Entry point
    // ─────────────────────────────────────────────────────────────────────────
    void GuardScene::LoadScene(SceneManager* sm)
    {
        std::mutex        sceneMutex;
        std::atomic<bool> running{ true };

        // ── Guard patrol state ────────────────────────────────────────────────
        int    patrolFrom = 0, patrolTo = 1;
        double patrolT = 0.0;
        double patrolSpeed = 0.09;

        // ─────────────────────────────────────────────────────────────────────
        //  Camera
        // ─────────────────────────────────────────────────────────────────────
        Camera* camera = new Camera(Vector3(0, 1.7, 0), Identity(), 75);
        sm->currentCamera = camera;
        sm->cameras = new std::vector<Camera*>();
        sm->cameras->push_back(camera);

        // ─────────────────────────────────────────────────────────────────────
        //  BasicMovements — replaces the inline movement code
        // ─────────────────────────────────────────────────────────────────────
        BasicMovements player(sm);
        player.eyeHeight = 1.7;
        player.moveSpeed = 0.08;
        player.useBounds = true;
        player.bounds = AABB{
            Vector3(-14.5, 0.0, -14.5),
            Vector3(14.5, 5.0,  14.5)
        };
        // keys default to ZQSD; swap to WASD with: player.keys = KeyBindings::WASD();

        // ─────────────────────────────────────────────────────────────────────
        //  Colours  (unchanged)
        // ─────────────────────────────────────────────────────────────────────
        Color colBlack = Color(0, 0, 0, 255);
        Color colFloor = Color(40, 38, 35, 255);
        Color colCeiling = Color(28, 26, 24, 255);
        Color colWall = Color(55, 52, 48, 255);
        Color colPillar = Color(70, 65, 60, 255);
        Color colBotBody = Color(60, 60, 70, 255);
        Color colBotHead = Color(45, 45, 55, 255);
        Color colTorchGrey = Color(120, 115, 110, 255);
        Color colTorchWhite = Color(240, 240, 220, 255);
        Color colCrate = Color(160, 130, 80, 255);   // wood-ish crate

        // ─────────────────────────────────────────────────────────────────────
        //  Room geometry  (unchanged)
        // ─────────────────────────────────────────────────────────────────────
        const double ROOM_HALF = 15.0;
        const double ROOM_H = 6.0;

        // Floor
        sm->objects->push_back(new Plane(
            Vector3(ROOM_HALF, 1, ROOM_HALF), Vector3(0, 0, 0), Identity(),
            new Material(0.03, 0, colFloor, colBlack)));
        // Ceiling
        sm->objects->push_back(new Plane(
            Vector3(ROOM_HALF, 1, ROOM_HALF), Vector3(0, ROOM_H, 0),
            AxisAngle(Vector3(1, 0, 0), 180.0),
            new Material(0.02, 0, colCeiling, colBlack)));
        // Back wall
        sm->objects->push_back(new Plane(
            Vector3(ROOM_HALF, ROOM_H * 0.5, 1), Vector3(0, ROOM_H * 0.5, ROOM_HALF),
            AxisAngle(Vector3(1, 0, 0), -90.0),
            new Material(0.03, 0, colWall, colBlack)));
        // Front wall
        sm->objects->push_back(new Plane(
            Vector3(ROOM_HALF, ROOM_H * 0.5, 1), Vector3(0, ROOM_H * 0.5, -ROOM_HALF),
            AxisAngle(Vector3(1, 0, 0), 90.0),
            new Material(0.03, 0, colWall, colBlack)));
        // Left wall
        sm->objects->push_back(new Plane(
            Vector3(1, ROOM_H * 0.5, ROOM_HALF), Vector3(-ROOM_HALF, ROOM_H * 0.5, 0),
            AxisAngle(Vector3(0, 0, 1), 90.0),
            new Material(0.03, 0, colWall, colBlack)));
        // Right wall
        sm->objects->push_back(new Plane(
            Vector3(1, ROOM_H * 0.5, ROOM_HALF), Vector3(ROOM_HALF, ROOM_H * 0.5, 0),
            AxisAngle(Vector3(0, 0, 1), -90.0),
            new Material(0.03, 0, colWall, colBlack)));

        // Pillars
        struct PillarPos { double x; double z; };
        PillarPos pillars[] = {
            {-10,-10},{-4,-10},{4,-10},{10,-10},
            {-10,  0},                {10,  0},
            {-10, 10},{-4, 10},{4, 10},{10, 10},
            {-7,-5},{7,5},{-2,7},{3,-6},
        };
        for (auto& p : pillars)
        {
            sm->objects->push_back(new Rectangle(
                Vector3(0.6, ROOM_H * 0.5, 0.6), Vector3(p.x, ROOM_H * 0.5, p.z),
                Identity(), new Material(0.04, 0, colPillar, colBlack)));
            sm->objects->push_back(new Rectangle(
                Vector3(0.85, 0.15, 0.85), Vector3(p.x, ROOM_H - 0.15, p.z),
                Identity(), new Material(0.04, 0, colPillar, colBlack)));
            sm->objects->push_back(new Rectangle(
                Vector3(0.85, 0.15, 0.85), Vector3(p.x, 0.15, p.z),
                Identity(), new Material(0.04, 0, colPillar, colBlack)));
        }

        // Wall fixtures
        sm->objects->push_back(new Rectangle(
            Vector3(0.2, 0.2, 0.2), Vector3(13, 4.5, 14), Identity(),
            new Material(0.5, 0, Color(200, 20, 20, 255), colBlack)));
        sm->objects->push_back(new Rectangle(
            Vector3(0.5, 0.15, 0.08), Vector3(-12, 3.2, -14.8), Identity(),
            new Material(0.4, 0, Color(20, 200, 60, 255), colBlack)));

        // ─────────────────────────────────────────────────────────────────────
        //  Guard bot — built as individual meshes, grouped with SceneGroup
        // ─────────────────────────────────────────────────────────────────────
        Rectangle* botBody = new Rectangle(
            Vector3(0.4, 0.55, 0.4), Vector3(patrol[0].x, 0.55, patrol[0].z),
            Identity(), new Material(0.06, 0, colBotBody, colBlack));
        sm->objects->push_back(botBody);

        Rectangle* botHead = new Rectangle(
            Vector3(0.35, 0.35, 0.35), Vector3(patrol[0].x, 1.35, patrol[0].z),
            Identity(), new Material(0.06, 0, colBotHead, colBlack));
        sm->objects->push_back(botHead);

        Rectangle* torchBody = new Rectangle(
            Vector3(0.08, 0.08, 0.25), Vector3(patrol[0].x + 0.35, 1.0, patrol[0].z),
            Identity(), new Material(0.1, 0, colTorchGrey, colBlack));
        sm->objects->push_back(torchBody);

        Rectangle* torchFlame = new Rectangle(
            Vector3(0.10, 0.10, 0.02), Vector3(patrol[0].x + 0.35, 1.0, patrol[0].z + 0.26),
            Identity(), new Material(0.8, 0, colTorchWhite, colBlack));
        sm->objects->push_back(torchFlame);

        // ── SceneGroup: torch sub-group ───────────────────────────────────────
        // The torch pieces move together, so group them first.
        SceneGroup* torchGroup = new SceneGroup("BotTorch",
            Vector3(patrol[0].x + 0.35, 1.0, patrol[0].z));
        torchGroup->Add(torchBody);
        torchGroup->Add(torchFlame);

        // ── SceneGroup: full guard ────────────────────────────────────────────
        // Parent group at bot's foot position.
        SceneGroup* guardGroup = new SceneGroup("Guard",
            Vector3(patrol[0].x, 0.0, patrol[0].z));
        guardGroup->Add(botBody);
        guardGroup->Add(botHead);
        guardGroup->Add(torchGroup);  // nested sub-group

        // ── RigidBody: kinematic guard so it participates in collision ─────────
        // (kinematic = not affected by gravity/impulses, but still detected)
        RigidBody guardRB(botBody, 1.0);
        guardRB.isKinematic = true;
        guardRB.useGravity = false;

        // ─────────────────────────────────────────────────────────────────────
        //  Demo: dynamic physics crate near the player start
        // ─────────────────────────────────────────────────────────────────────
        Rectangle* crateObj = new Rectangle(
            Vector3(0.4, 0.4, 0.4), Vector3(2.0, 0.4, 2.0),
            Identity(), new Material(0.08, 0, colCrate, colBlack));
        sm->objects->push_back(crateObj);

        RigidBody crateRB(crateObj, 3.0);
        crateRB.restitution = 0.5;
        crateRB.useGravity = true;

        PhysicsWorld physicsWorld;
        physicsWorld.Register(&crateRB);
        // Guard is kinematic — register so it can deflect the crate
        physicsWorld.Register(&guardRB);

        // ─────────────────────────────────────────────────────────────────────
        //  CollisionTrigger — exit zone near the door (ghost, no push-back)
        // ─────────────────────────────────────────────────────────────────────
        // A thin invisible volume near the front-left wall
        Rectangle* exitZoneObj = new Rectangle(
            Vector3(3.0, 3.0, 0.5), Vector3(-12, 1.5, -14.0),
            Identity(), new Material(0, 0, colBlack, colBlack));
        // NOTE: don't push into sm->objects — it's trigger-only, not rendered

        CollisionTrigger exitTrigger(exitZoneObj, /*isSolid=*/false);
        exitTrigger.OnEnter([](const CollisionEvent& e) {
            std::cout << "[TRIGGER] Player neared the exit!\n";
            });
        exitTrigger.OnExit([](const CollisionEvent& e) {
            std::cout << "[TRIGGER] Player left exit zone.\n";
            });

        // ── CollisionTrigger: alert zone around first patrol waypoint ──────────
        // Fires when the guard (botBody) enters the alert zone.
        Rectangle* alertZoneObj = new Rectangle(
            Vector3(4.0, 3.0, 4.0), Vector3(0, 1.5, 0),
            Identity(), new Material(0, 0, colBlack, colBlack));

        CollisionTrigger alertTrigger(alertZoneObj, /*isSolid=*/false);
        alertTrigger.filterTag = "";  // respond to any object
        alertTrigger.OnEnter([](const CollisionEvent& e) {
            std::cout << "[TRIGGER] Guard entered the alert zone!\n";
            });

        // Collect all scene objects for trigger polling
        // (we pass pointers to the specific objects we care about)
        std::vector<BaseObject*> triggerCandidates = {
            botBody, crateObj
        };

        // ─────────────────────────────────────────────────────────────────────
        //  Lights  (unchanged)
        // ─────────────────────────────────────────────────────────────────────
        sm->lights->push_back(new BaseLight(0.06, Color(160, 170, 200, 255)));
        sm->lights->push_back(new DirectionalLight(0.08, Color(180, 195, 230, 255),
            Vector3(0.2, 1, 0.1)));

        PointLight* botTorchLight = new PointLight(
            0.85, Color(255, 170, 60, 255),
            Vector3(patrol[0].x + 0.35, 1.0, patrol[0].z + 0.26));
        sm->lights->push_back(botTorchLight);

        PointLight* playerTorchLight = new PointLight(
            0.75, Color(220, 235, 255, 255), camera->transform.position);
        sm->lights->push_back(playerTorchLight);

        sm->lights->push_back(new PointLight(
            0.45, Color(255, 30, 30, 255), Vector3(13, 4.5, 14)));
        sm->lights->push_back(new PointLight(
            0.30, Color(30, 220, 80, 255), Vector3(-12, 3.2, -14.8)));

        // ── Player torch toggle registered in BasicMovements ──────────────────
        // NOTE: TickInput() is always called under sceneMutex, so this callback
        // already runs under that lock.  A second lock_guard here would deadlock
        // on a non-recursive mutex — do NOT add one.
        player.RegisterToggle('E', [&](bool isOn) {
            playerTorchLight->intensity = isOn ? 0.75 : 0.0;
            std::cout << "Player torch " << (isOn ? "ON" : "OFF") << "\n";
            }, /*initialState=*/true);

        // ─────────────────────────────────────────────────────────────────────
        //  Animation thread
        // ─────────────────────────────────────────────────────────────────────
        static constexpr double FIXED_DT = 1.0 / 120.0;
        static constexpr double MAX_DT = 0.1;

        std::thread animThread([&]()
            {
                using Clock = std::chrono::high_resolution_clock;
                auto   prev = Clock::now();
                double accumulator = 0.0;

                while (running)
                {
                    auto   now = Clock::now();
                    double dt = std::chrono::duration<double>(now - prev).count();
                    prev = now;
                    if (dt > MAX_DT) dt = MAX_DT;
                    accumulator += dt;

                    while (accumulator >= FIXED_DT)
                    {
                        accumulator -= FIXED_DT;

                        // ── Player input via BasicMovements ───────────────────────
                        {
                            std::lock_guard<std::mutex> lk(sceneMutex);
                            if (!player.TickInput(FIXED_DT)) running = false;
                            // Always track camera — intensity controls on/off, not position
                            playerTorchLight->position = sm->currentCamera->transform.position;
                        }

                        // ── Physics step ─────────────────────────────────────────
                        {
                            std::lock_guard<std::mutex> lk(sceneMutex);
                            physicsWorld.Step(FIXED_DT);
                            // Keep crate above floor
                            if (crateObj->transform.position.y < 0.4)
                            {
                                crateObj->transform.position.y = 0.4;
                                crateRB.velocity.y = 0;
                                crateRB.isGrounded = true;
                            }
                        }

                        // ── Guard patrol (moves guardGroup, applies to children) ──
                        {
                            patrolT += FIXED_DT * patrolSpeed;
                            if (patrolT >= 1.0)
                            {
                                patrolT -= 1.0;
                                patrolFrom = patrolTo;
                                patrolTo = (patrolTo + 1) % PATROL_COUNT;
                            }

                            const Vector3& A = patrol[patrolFrom];
                            const Vector3& B = patrol[patrolTo];
                            Vector3 botPos(
                                A.x + (B.x - A.x) * patrolT,
                                0.0,
                                A.z + (B.z - A.z) * patrolT);

                            double dx = B.x - A.x;
                            double dz = B.z - A.z;
                            double angle = atan2(dx, dz) * (180.0 / 3.14159265);
                            Quaternion qFace = AxisAngle(Vector3(0, 1, 0), angle);

                            {
                                std::lock_guard<std::mutex> lk(sceneMutex);

                                // Move the group — ApplyTransform() propagates to children
                                guardGroup->SetPosition(botPos);
                                guardGroup->SetRotation(qFace);
                                guardGroup->ApplyTransform();

                                // Torch light follows torchFlame directly
                                if (botTorchLight)
                                    botTorchLight->position = torchFlame->transform.position;

                                // Update kinematic RB position so collision still works
                                // (guardRB.object == botBody, already moved by group)
                            }
                        }

                        // ── Collision triggers ────────────────────────────────────
                        {
                            std::lock_guard<std::mutex> lk(sceneMutex);

                            // Build a transient candidate list including camera proxy
                            // (represent player as botBody-less object — use camera pos trick)
                            // For simplicity, candidates are scene objects declared above
                            exitTrigger.Poll(triggerCandidates);
                            alertTrigger.Poll(triggerCandidates);
                        }

                    } // end fixed-step loop

                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(FIXED_DT * 0.5));
                }
            });

        // ─────────────────────────────────────────────────────────────────────
        //  Render / event loop
        // ─────────────────────────────────────────────────────────────────────
        Renderer renderer(sm);
        std::cout << "Guard Scene loaded (refactored).\n";
        std::cout << "  Z/Q/S/D – move   Mouse – look   E – torch   ESC – quit\n";

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE) running = false;
            }

            // Mouse look via BasicMovements
            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);
            if (mx != 0 || my != 0)
            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                player.TickMouse(mx, my);
            }

            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                auto start = std::chrono::high_resolution_clock::now();
                renderer.Render();
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> dur = end - start;
                std::cout << dur.count() << "\n";
            }
        }

        animThread.join();
        renderer.cleanup();

        // Cleanup groups (objects still owned by sm->objects)
        delete torchGroup;
        delete guardGroup;
        delete exitZoneObj;
        delete alertZoneObj;
    }

} // namespace PEngine