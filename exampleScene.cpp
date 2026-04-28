// marioMenuScene.cpp
// ─────────────────────────────────────────────────────────────────────────────
//  "Super Mario World"-style overworld main-menu scene.
//
//  Layout (top-down view, camera is isometric / angled from above):
//
//      START  ──path──  LEVEL-1  ──path──  LEVEL-2
//                                              │
//                                           LEVEL-3  ──path──  CASTLE
//
//  Mario (a tiny 3-rectangle figure) walks along the path nodes in a loop.
//  Each level node is a coloured box that bobs up and down.
//  A "?" coin-block bounces above START.
//  Tiny flag-pole rectangles spin slowly on each completed node.
//  Lights breathe and orbit to give life to the scene.
// ─────────────────────────────────────────────────────────────────────────────

#include "exampleScene.h"

#include <iostream>
#include <thread>
#include <vector>
#include <cmath>
#include <atomic>
#include <mutex>
#include <chrono>

#include <SDL.h>

#include "vector3.h"
#include "plane.h"
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
    static Quaternion AxisAngle(Vector3 axis, double deg)
    {
        double r = deg * (3.14159265358979 / 180.0);
        double h = r * 0.5;
        Vector3 n = axis.normalize();
        return Quaternion(cos(h), n.x * sin(h), n.y * sin(h), n.z * sin(h)).Normalized();
    }

    static Quaternion Identity() { return Quaternion(1, 0, 0, 0); }

    // Linear interpolation between two Vector3s
    static Vector3 Lerp(const Vector3& a, const Vector3& b, double t)
    {
        return Vector3(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        );
    }

    // ─────────────────────────────────────────────
    //  World-map node positions (X-Z plane, Y=0)
    //  The overworld "road" is laid flat; camera looks
    //  slightly down from a fixed elevated position.
    // ─────────────────────────────────────────────
    //
    //   START(0)  →  L1(1)  →  L2(2)
    //                              ↓
    //                           L3(3) →  CASTLE(4)
    //
    static const int NODE_COUNT = 5;
    static const Vector3 nodePos[NODE_COUNT] = {
        Vector3(-9,  0,  0),   // 0 START
        Vector3(-3,  0,  0),   // 1 Level 1
        Vector3(3,  0,  0),   // 2 Level 2
        Vector3(3,  0,  6),   // 3 Level 3
        Vector3(9,  0,  6),   // 4 CASTLE
    };

    // ─────────────────────────────────────────────
    //  Path segment data (straight stretches)
    //  Each segment: from node[a] to node[b], rendered
    //  as a thin flat rectangle lying on the ground.
    // ─────────────────────────────────────────────
    struct PathSeg { int a; int b; };
    static const PathSeg paths[] = { {0,1},{1,2},{2,3},{3,4} };
    static const int PATH_COUNT = 4;

    // ─────────────────────────────────────────────
    //  Mario walking animation
    //  Mario travels the node list in order, then loops back.
    // ─────────────────────────────────────────────
    struct MarioWalker
    {
        int    fromNode = 0;
        int    toNode = 1;
        double t = 0.0;   // 0→1 along current segment
        double speed = 0.35;  // units per second through the segment (fraction of seg)

        // Advance position; returns true when a new node is reached
        bool update(double dt)
        {
            t += dt * speed;
            if (t >= 1.0)
            {
                t = 0.0;
                fromNode = toNode;
                toNode = (toNode + 1) % NODE_COUNT;
                // Pause a tiny bit at the node (handled in anim thread via sleep)
                return true;
            }
            return false;
        }

        Vector3 position() const
        {
            return Lerp(nodePos[fromNode], nodePos[toNode], t);
        }
    };

    // ─────────────────────────────────────────────
    //  Node colours: grass green, yellow, orange, red, grey castle
    // ─────────────────────────────────────────────
    static const Color nodeColors[NODE_COUNT] = {
        Color(80,  180,  60, 255),   // START  — green
        Color(240, 200,  40, 255),   // L1     — yellow
        Color(220, 100,  20, 255),   // L2     — orange
        Color(180,  50, 200, 255),   // L3     — purple
        Color(130, 130, 140, 255),   // CASTLE — stone grey
    };

    // ─────────────────────────────────────────────
    //  Scene entry point
    // ─────────────────────────────────────────────
    void ExampleScene::LoadScene(SceneManager* sm)
    {
        std::mutex        sceneMutex;
        std::atomic<bool> running{ true };

        // ── Animated object handles ──────────────────
        // Level node boxes (they bob)
        Rectangle* nodeBox[NODE_COUNT] = {};
        // Flag poles on each node (they spin)
        Rectangle* flagPole[NODE_COUNT] = {};
        // Flag banners attached to poles
        Rectangle* flagBanner[NODE_COUNT] = {};

        // Mario body parts
        Rectangle* marioBody = nullptr;   // torso
        Rectangle* marioHead = nullptr;   // head
        Rectangle* marioCap = nullptr;   // cap (red)

        // Coin "?" block above START
        Rectangle* coinBlock = nullptr;

        // Orbit light
        PointLight* orbitLight = nullptr;
        PointLight* sunLight = nullptr;   // slow colour breathe

        double        nodeBobTime = 0.0;
        double        flagSpinAngle[NODE_COUNT] = {};
        double        coinBounce = 0.0;
        double        orbitAngle = 0.0;
        double        sunTime = 0.0;
        bool          pauseAtNode = false;
        double        pauseTimer = 0.0;

        MarioWalker   mario;

        // ─────────────────────────────────────────────
        //  Camera: isometric-ish, fixed elevated angle
        //  that slowly pans to follow Mario on X and Z.
        // ─────────────────────────────────────────────
        struct CamAnim
        {
            double time = 0.0;
            Vector3 camPos = Vector3(0, 12, -10);

            void update(double dt, SceneManager* sm, const Vector3& target, std::mutex& mtx)
            {
                time += dt;

                // Smooth-follow Mario horizontally, stay elevated
                double tx = target.x;
                double tz = target.z;

                // Gentle lazy follow (exponential smoothing)
                camPos.x += (tx - camPos.x) * 0.02;
                camPos.z += (tz - 5.0 - camPos.z) * 0.02;   // keep camera behind
                camPos.y = 12.0 + 0.4 * sin(time * 0.5);   // very slow bob

                // Fixed pitch-down angle (~50°), no yaw
                Quaternion qPitch = AxisAngle(Vector3(1, 0, 0), 50.0);

                std::lock_guard<std::mutex> lk(mtx);
                sm->currentCamera->transform.position = camPos;
                sm->currentCamera->transform.rotation = qPitch;
            }
        } camAnim;

        // ─────────────────────────────────────────────
        //  Animation thread
        // ─────────────────────────────────────────────
        std::thread animThread([&]()
            {
                using Clock = std::chrono::high_resolution_clock;
                auto prev = Clock::now();

                while (running)
                {
                    auto   now = Clock::now();
                    double dt = std::chrono::duration<double>(now - prev).count();
                    prev = now;

                    // — Mario walking —
                    if (!pauseAtNode)
                    {
                        bool reached = mario.update(dt);
                        if (reached)
                        {
                            pauseAtNode = true;
                            pauseTimer = 0.0;
                        }
                    }
                    else
                    {
                        pauseTimer += dt;
                        if (pauseTimer >= 0.9)   // 0.9 s pause at each node
                            pauseAtNode = false;
                    }

                    Vector3 mPos = mario.position();
                    // Mario stands slightly above the ground
                    mPos.y = 0.0;

                    camAnim.update(dt, sm, mPos, sceneMutex);

                    {
                        std::lock_guard<std::mutex> lk(sceneMutex);

                        // — Node boxes bob up and down independently —
                        nodeBobTime += dt;
                        for (int i = 0; i < NODE_COUNT; ++i)
                        {
                            double phase = i * (3.14159265 * 2.0 / NODE_COUNT);
                            double bobY = 0.15 * sin(nodeBobTime * 1.8 + phase);
                            if (nodeBox[i])
                            {
                                nodeBox[i]->transform.position =
                                    Vector3(nodePos[i].x, bobY, nodePos[i].z);
                            }
                        }

                        // — Flag poles spin —
                        for (int i = 0; i < NODE_COUNT; ++i)
                        {
                            flagSpinAngle[i] += 45.0 * dt;   // 45°/s
                            if (flagSpinAngle[i] >= 360.0)
                                flagSpinAngle[i] -= 360.0;
                            if (flagPole[i])
                            {
                                double phase = i * (3.14159265 * 2.0 / NODE_COUNT);
                                double bobY = 0.15 * sin(nodeBobTime * 1.8 + phase);
                                flagPole[i]->transform.position =
                                    Vector3(nodePos[i].x, 0.6 + bobY, nodePos[i].z);
                            }
                            if (flagBanner[i])
                            {
                                double phase = i * (3.14159265 * 2.0 / NODE_COUNT);
                                double bobY = 0.15 * sin(nodeBobTime * 1.8 + phase);
                                flagBanner[i]->transform.position =
                                    Vector3(nodePos[i].x + 0.35, 0.9 + bobY, nodePos[i].z);
                                flagBanner[i]->transform.rotation =
                                    AxisAngle(Vector3(0, 1, 0), flagSpinAngle[i]);
                            }
                        }

                        // — Coin block bounce above START —
                        coinBounce += dt;
                        if (coinBlock)
                        {
                            double by = 1.8 + 0.3 * std::abs(sin(coinBounce * 2.5));
                            coinBlock->transform.position =
                                Vector3(nodePos[0].x, by, nodePos[0].z);
                        }

                        // — Mario figure follows walker position —
                        if (marioBody)
                            marioBody->transform.position = Vector3(mPos.x, 0.18, mPos.z);
                        if (marioHead)
                            marioHead->transform.position = Vector3(mPos.x, 0.5, mPos.z);
                        if (marioCap)
                            marioCap->transform.position = Vector3(mPos.x, 0.7, mPos.z);

                        // Tilt Mario slightly in walk direction when moving
                        if (!pauseAtNode)
                        {
                            Vector3 dir = nodePos[mario.toNode] - nodePos[mario.fromNode];
                            double walkAngle = atan2(dir.x, dir.z) * (180.0 / 3.14159265);
                            Quaternion qTurn = AxisAngle(Vector3(0, 1, 0), walkAngle);
                            if (marioBody) marioBody->transform.rotation = qTurn;
                            if (marioHead) marioHead->transform.rotation = qTurn;
                            if (marioCap)  marioCap->transform.rotation = qTurn;
                        }

                        // — Orbit light circles the map centre —
                        orbitAngle += 25.0 * dt;   // degrees/s
                        if (orbitLight)
                        {
                            double rad = orbitAngle * (3.14159265 / 180.0);
                            orbitLight->position = Vector3(
                                12.0 * cos(rad),
                                6.0,
                                3.0 + 12.0 * sin(rad)
                            );
                        }

                        // — Sun light colour breathes between warm day and cool dusk —
                        sunTime += dt * 0.15;   // very slow
                        if (sunLight)
                        {
                            double s = 0.5 + 0.5 * sin(sunTime);   // 0→1
                            // Warm yellow ↔ soft pink
                            Uint8 r = (Uint8)(255);
                            Uint8 g = (Uint8)(200 + s * 40);
                            Uint8 b = (Uint8)(100 + s * 80);
                            sunLight->color = Color(r, g, b, 255);
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            });

        // ─────────────────────────────────────────────
        //  Camera
        // ─────────────────────────────────────────────
        Camera* camera = new Camera(
            Vector3(0, 12, -10),
            AxisAngle(Vector3(1, 0, 0), 50.0),
            75
        );
        sm->currentCamera = camera;
        sm->cameras = new std::vector<Camera*>();
        sm->cameras->push_back(camera);

        // ─────────────────────────────────────────────
        //  Ground  — large grassy plane
        // ─────────────────────────────────────────────
        Color colGrass = Color(80, 160, 60, 255);
        Color colPath = Color(210, 180, 130, 255);   // sandy path
        Color colSky = Color(95, 175, 255, 255);   // backdrop
        Color colBlack = Color(0, 0, 0, 255);
        Color colCoin = Color(255, 210, 30, 255);
        Color colRedMario = Color(200, 30, 30, 255);
        Color colBlueMario = Color(30, 80, 200, 255);
        Color colSkin = Color(240, 190, 130, 255);
        Color colCastle = Color(130, 130, 145, 255);

        sm->objects->push_back(
            new Plane(
                Vector3(40, 1, 40), Vector3(0, -0.5, 3),
                Identity(),
                new Material(0.05, 0, colGrass, colBlack)
            )
        );

        // ─────────────────────────────────────────────
        //  Path segments between nodes
        //  Each path is a thin flat rectangle on the ground.
        // ─────────────────────────────────────────────
        for (int p = 0; p < PATH_COUNT; ++p)
        {
            Vector3 A = nodePos[paths[p].a];
            Vector3 B = nodePos[paths[p].b];
            Vector3 mid = Lerp(A, B, 0.5);
            double  dx = B.x - A.x;
            double  dz = B.z - A.z;
            double  len = sqrt(dx * dx + dz * dz);
            double  angle = atan2(dx, dz) * (180.0 / 3.14159265);

            sm->objects->push_back(
                new Rectangle(
                    Vector3(0.6, 0.02, len * 0.5),   // half-extent: narrow strip, half-length
                    Vector3(mid.x, -0.48, mid.z),
                    AxisAngle(Vector3(0, 1, 0), angle),
                    new Material(0.03, 0, colPath, colBlack)
                )
            );
        }

        // ─────────────────────────────────────────────
        //  Level nodes — coloured boxes
        //  Each is a small cube-ish rectangle sitting on the ground.
        // ─────────────────────────────────────────────
        for (int i = 0; i < NODE_COUNT; ++i)
        {
            float sz = (i == NODE_COUNT - 1) ? 1.0f : 0.5f;   // castle is bigger
            nodeBox[i] = new Rectangle(
                Vector3(sz, sz * 0.6f, sz),
                Vector3(nodePos[i].x, 0, nodePos[i].z),
                Identity(),
                new Material(0.1, 0, nodeColors[i], colBlack)
            );
            sm->objects->push_back(nodeBox[i]);
        }

        // ─────────────────────────────────────────────
        //  Flag poles + banners on each node
        // ─────────────────────────────────────────────
        for (int i = 0; i < NODE_COUNT; ++i)
        {
            // Pole (thin vertical bar)
            flagPole[i] = new Rectangle(
                Vector3(0.05, 0.6, 0.05),
                Vector3(nodePos[i].x, 0.6, nodePos[i].z),
                Identity(),
                new Material(0.05, 0, Color(240, 240, 240, 255), colBlack)
            );
            sm->objects->push_back(flagPole[i]);

            // Banner (tiny flat rect sticking out sideways)
            Color fc = (i % 2 == 0) ? Color(220, 30, 30, 255) : Color(30, 30, 220, 255);
            flagBanner[i] = new Rectangle(
                Vector3(0.35, 0.18, 0.05),
                Vector3(nodePos[i].x + 0.35, 0.9, nodePos[i].z),
                Identity(),
                new Material(0.08, 0, fc, colBlack)
            );
            sm->objects->push_back(flagBanner[i]);
        }

        // ─────────────────────────────────────────────
        //  Coin "?" block above START node
        // ─────────────────────────────────────────────
        coinBlock = new Rectangle(
            Vector3(0.4, 0.4, 0.4),
            Vector3(nodePos[0].x, 1.8, nodePos[0].z),
            Identity(),
            new Material(0.15, 0, colCoin, colBlack)
        );
        sm->objects->push_back(coinBlock);

        // ─────────────────────────────────────────────
        //  Castle battlements  (CASTLE node, index 4)
        //  A row of small rectangular merlons on top.
        // ─────────────────────────────────────────────
        Color colMerlon = Color(115, 115, 130, 255);
        double cX = nodePos[4].x, cZ = nodePos[4].z;
        double merlonOffsets[] = { -0.75, -0.25, 0.25, 0.75 };
        for (double mo : merlonOffsets)
        {
            sm->objects->push_back(
                new Rectangle(
                    Vector3(0.2, 0.25, 0.2),
                    Vector3(cX + mo, 0.85, cZ - 0.6),
                    Identity(),
                    new Material(0.04, 0, colMerlon, colBlack)
                )
            );
        }

        // Castle gate (dark arch hint — a short dark rectangle)
        sm->objects->push_back(
            new Rectangle(
                Vector3(0.25, 0.3, 0.05),
                Vector3(cX, 0.1, cZ - 1.05),
                Identity(),
                new Material(0.0, 0, Color(20, 15, 10, 255), colBlack)
            )
        );

        // ─────────────────────────────────────────────
        //  Some decorative trees — tall thin green pillars
        //  scattered around the map edges
        // ─────────────────────────────────────────────
        struct TreePos { double x; double z; };
        TreePos trees[] = {
            {-12, -4}, {-10, 4}, {-7, -6}, {-5,  7},
            { -1, -5}, { 1,  8}, { 6, -4}, { 7,  12},
            { 11, -3}, {-13, 9}, { 5, -7}, {13,  4},
        };
        Color colTrunk = Color(90, 55, 25, 255);
        Color colLeaf = Color(40, 140, 30, 255);
        Color colLeaf2 = Color(55, 165, 45, 255);
        for (auto& tr : trees)
        {
            // trunk
            sm->objects->push_back(
                new Rectangle(
                    Vector3(0.18, 0.4, 0.18),
                    Vector3(tr.x, 0.0, tr.z),
                    Identity(),
                    new Material(0.02, 0, colTrunk, colBlack)
                )
            );
            // foliage — two overlapping pyramidal boxes
            sm->objects->push_back(
                new Rectangle(
                    Vector3(0.55, 0.55, 0.55),
                    Vector3(tr.x, 0.65, tr.z),
                    Identity(),
                    new Material(0.04, 0, colLeaf, colBlack)
                )
            );
            sm->objects->push_back(
                new Rectangle(
                    Vector3(0.38, 0.38, 0.38),
                    Vector3(tr.x, 1.05, tr.z),
                    Identity(),
                    new Material(0.04, 0, colLeaf2, colBlack)
                )
            );
        }

        // ─────────────────────────────────────────────
        //  Some clouds — flat white rectangles high above
        // ─────────────────────────────────────────────
        struct Cloud { double x; double y; double z; double sx; double sz; };
        Cloud clouds[] = {
            { -8, 8,  -2, 2.5, 0.8 },
            {  0, 9,   4, 3.0, 0.7 },
            {  7, 7.5, 0, 2.0, 0.6 },
            { -4, 8.5, 8, 2.8, 0.7 },
            { 10, 8,   7, 2.2, 0.65},
        };
        Color colCloud = Color(245, 245, 255, 255);
        for (auto& cl : clouds)
        {
            sm->objects->push_back(
                new Rectangle(
                    Vector3(cl.sx, 0.3, cl.sz),
                    Vector3(cl.x, cl.y, cl.z),
                    Identity(),
                    new Material(0.0, 0, colCloud, colBlack)
                )
            );
        }

        // ─────────────────────────────────────────────
        //  Mario figure — 3 stacked rectangles
        //  Body (blue), Head (skin), Cap (red)
        // ─────────────────────────────────────────────
        marioBody = new Rectangle(
            Vector3(0.18, 0.22, 0.14),
            Vector3(nodePos[0].x, 0.18, nodePos[0].z),
            Identity(),
            new Material(0.08, 0, colBlueMario, colBlack)
        );
        sm->objects->push_back(marioBody);

        marioHead = new Rectangle(
            Vector3(0.16, 0.16, 0.16),
            Vector3(nodePos[0].x, 0.5, nodePos[0].z),
            Identity(),
            new Material(0.08, 0, colSkin, colBlack)
        );
        sm->objects->push_back(marioHead);

        marioCap = new Rectangle(
            Vector3(0.18, 0.08, 0.18),
            Vector3(nodePos[0].x, 0.7, nodePos[0].z),
            Identity(),
            new Material(0.08, 0, colRedMario, colBlack)
        );
        sm->objects->push_back(marioCap);

        // ─────────────────────────────────────────────
        //  Lights
        // ─────────────────────────────────────────────

        // Ambient
        sm->lights->push_back(
            new BaseLight(0.18, Color(200, 220, 255, 255))
        );

        // Sun — directional from upper-right
        sm->lights->push_back(
            new DirectionalLight(0.30, Color(255, 230, 160, 255),
                Vector3(1, 3, 0.5))
        );

        // Orbit warm point light (circles the map)
        orbitLight = new PointLight(0.60, Color(255, 165, 60, 255),
            Vector3(10, 6, 3));
        sm->lights->push_back(orbitLight);

        // Breathing sun colour light (slow warm↔cool cycle)
        sunLight = new PointLight(0.35, Color(255, 220, 140, 255),
            Vector3(0, 10, -5));
        sm->lights->push_back(sunLight);

        // Cool fill from below-front
        sm->lights->push_back(
            new PointLight(0.20, Color(100, 180, 255, 255),
                Vector3(0, -2, -8))
        );

        // ─────────────────────────────────────────────
        //  Renderer + main loop
        // ─────────────────────────────────────────────
        Renderer renderer(sm);
        std::cout << "MarioMenuScene running. Close window or press ESC to quit.\n";

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_QUIT) running = false;
                if (event.type == SDL_KEYDOWN &&
                    event.key.keysym.sym == SDLK_ESCAPE)
                    running = false;
            }

            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                auto start = std::chrono::high_resolution_clock::now();
                renderer.Render();
                auto end = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> duration = end - start;

                // Output the duration in seconds
                std::cout << duration.count() << std::endl;
            }
        }

        animThread.join();
        renderer.cleanup();
    }

} 