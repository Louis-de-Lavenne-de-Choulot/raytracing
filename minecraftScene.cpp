// minecraftScene.cpp  —  PEngine
// ─────────────────────────────────────────────────────────────────────────────
//  Minecraft-inspired procedural scene — INFINITE CHUNKED WORLD
//
//  Changes vs previous version
//  ────────────────────────────
//  • Terrain blocks now use Rectangle exclusively (no more Plane for top faces).
//    Every terrain column is one Rectangle cube — simpler code, consistent look.
//
//  • Infinite world: the map is divided into CHUNK_SIZE×CHUNK_SIZE column
//    chunks.  A dedicated chunk-management thread loads all chunks within a
//    LOAD_RADIUS chunk radius of the player and unloads chunks that fall outside
//    UNLOAD_RADIUS.  Both radii are measured in chunks (each chunk = CHUNK_SIZE
//    world units wide).  The scene's object list is updated live while rendering
//    continues — protected by the same sceneMutex already used for physics.
//
//  • HeightMap is now globally seeded and chunks query it on demand; already-
//    computed heights are cached so terrain is perfectly consistent across load/
//    unload cycles.
//
//  • Chunk objects are tracked in a per-chunk vector so they can be bulk-erased
//    from sm->objects when the chunk is unloaded.
//
//  • All other gameplay systems (jump, gravity, lighting, torch flicker, day
//    cycle, flashlight toggle) are preserved unchanged.
// ─────────────────────────────────────────────────────────────────────────────

#include "minecraftScene.h"

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>

#include <SDL.h>
#include <windows.h>

#include "vector3.h"
#include "rectangle.h"
#include "renderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sceneManager.h"
#include "basicmovements.h"
#include "rigidbody.h"

namespace PEngine
{
    // ── World constants ───────────────────────────────────────────────────────
    static constexpr double S = 1.0;    // block size (world units)
    static constexpr double EYE_HEIGHT = 1.7;    // camera above ground
    static constexpr double GRAVITY = -40.0;  // downward accel (units/s²)
    static constexpr double JUMP_SPEED = 14.0;   // initial upward velocity
    static constexpr double MOVE_SPEED = 10.0;   // horizontal speed (units/s)
    static constexpr double FIXED_DT = 1.0 / 60.0;
    static constexpr double MAX_DT = 0.1;
    static constexpr int    WATER_Y = -1;     // water table (block units)

    // ── Chunk constants ───────────────────────────────────────────────────────
    // Each chunk covers CHUNK_SIZE×CHUNK_SIZE columns.
    // LOAD_RADIUS   — chunks within this Manhattan radius are kept loaded.
    // UNLOAD_RADIUS — chunks beyond this radius are queued for removal.
    //   (UNLOAD_RADIUS > LOAD_RADIUS gives a hysteresis band that prevents
    //    thrashing when the player walks near a chunk boundary.)
    static constexpr int CHUNK_SIZE = 16;   // columns per side
    static constexpr int LOAD_RADIUS = 2;    // load  2 chunks in each direction
    static constexpr int UNLOAD_RADIUS = 3;    // unload beyond 3 chunks

    // ── Misc ──────────────────────────────────────────────────────────────────
    static Quaternion MCIdentity() { return Quaternion(); }

    // ── Colour palette ────────────────────────────────────────────────────────
    static const Color colGrass{ 91,  139,  54, 255 };
    static const Color colGrassDark{ 67,  104,  38, 255 };
    static const Color colDirt{ 134,   96,  67, 255 };
    static const Color colStone{ 125,  125, 125, 255 };
    static const Color colSand{ 219,  207, 163, 255 };
    static const Color colWood{ 162,  130,  78, 255 };
    static const Color colLogTop{ 102,   81,  51, 255 };
    static const Color colLeaves{ 55,  118,  43, 255 };
    static const Color colLeavesDark{ 37,  88,  27, 255 };
    static const Color colWater{ 64,  115, 211, 200 };
    static const Color colLava{ 207,   72,   0, 255 };
    static const Color colLavaGlow{ 255,  140,  20, 255 };
    static const Color colTorch{ 255,  195,  50, 255 };
    static const Color colSnow{ 240,  248, 255, 255 };
    static const Color colObsidian{ 25,   20,  35, 255 };
    static const Color colBlack{ 0,    0,   0, 255 };

    // ── Terrain noise ─────────────────────────────────────────────────────────
    static int TerrainHeight(int x, int z)
    {
        double nx = x * 0.22, nz = z * 0.22;
        double base = sin(nx) + cos(nz) + 0.5 * sin(nx * 2.3 + nz * 1.8);
        double distSq = nx * nx + nz * nz;
        double mountain = (std::max)(0.0, 8.0 - distSq * 0.9);
        return (int)std::round(base * 1.5 + mountain);
    }

    // ── HeightMap — infinite, lazily populated ────────────────────────────────
    struct HeightMap
    {
        std::unordered_map<int64_t, double> data;
        std::mutex mtx;   // only needed when chunks load concurrently

        static int64_t Key(int xi, int zi)
        {
            return (int64_t)(xi + 100000) * 200003LL + (int64_t)(zi + 100000);
        }

        // Called from chunk generation — sets the surface world-Y for a column.
        void Set(int xi, int zi, double worldY)
        {
            std::lock_guard<std::mutex> lk(mtx);
            data[Key(xi, zi)] = worldY;
        }

        // Called from physics tick — returns the surface Y under the player.
        double SurfaceAt(double wx, double wz) const
        {
            int xi = (int)std::floor(wx / S + 0.5);
            int zi = (int)std::floor(wz / S + 0.5);
            // No lock needed: reads from a fully-loaded column are safe because
            // the chunk loader always finishes writing before it signals "done".
            // If the player walks off a loaded chunk edge they'll just fall.
            auto it = data.find(Key(xi, zi));
            return (it != data.end()) ? it->second : -50.0;
        }
    };

    // ── Chunk key ─────────────────────────────────────────────────────────────
    struct ChunkKey
    {
        int cx, cz;   // chunk coordinates (column / CHUNK_SIZE)
        bool operator==(const ChunkKey& o) const { return cx == o.cx && cz == o.cz; }
    };
    struct ChunkKeyHash
    {
        size_t operator()(const ChunkKey& k) const
        {
            return std::hash<int64_t>()(
                (int64_t)(k.cx + 100000) * 200003LL + (int64_t)(k.cz + 100000));
        }
    };

    // Column coordinate → chunk coordinate
    static int ColToChunk(int col) { return (int)std::floor((double)col / CHUNK_SIZE); }
    // World position → chunk coordinate
    static int WorldToChunk(double w) { return (int)std::floor(w / (S * CHUNK_SIZE)); }

    // ── Per-chunk data ────────────────────────────────────────────────────────
    struct ChunkData
    {
        std::vector<BaseObject*>  objects;   // objects owned by sm->objects
        std::vector<PointLight*>  torches;   // torch lights
        std::vector<PointLight*>  lavaLights;
    };

    // ── Geometry helpers ──────────────────────────────────────────────────────

    // One Rectangle = one cube block.  Used for EVERYTHING including terrain
    // top faces — no Plane type needed anywhere.
    static BaseObject* Cube(double x, double y, double z,
        double scaleX, double scaleY, double scaleZ,
        Color col, double refl = 0.0)
    {
        return new Rectangle(
            Vector3(scaleX, scaleY, scaleZ),
            Vector3(x, y, z),
            MCIdentity(),
            new Material(refl, 0, col, colBlack)
        );
    }

    // Convenience: uniform-scale cube.
    static BaseObject* Cube(double x, double y, double z,
        double scale, Color col, double refl = 0.0)
    {
        return Cube(x, y, z, scale * 0.5, scale * 0.5, scale * 0.5, col, refl);
    }

    // Add to scene manager and return pointer (so callers can store it).
    static BaseObject* SpawnCube(SceneManager* sm,
        double x, double y, double z,
        double scale, Color col, double refl = 0.0)
    {
        BaseObject* obj = Cube(x, y, z, scale, col, refl);
        sm->objects->push_back(obj);
        return obj;
    }

    // Cliff-face slab: non-uniform scale for vertical wall patches.
    static BaseObject* SpawnSlab(SceneManager* sm,
        double x, double y, double z,
        double hx, double hy, double hz,
        Color col)
    {
        BaseObject* obj = Cube(x, y, z, hx, hy, hz, col);
        sm->objects->push_back(obj);
        return obj;
    }

    // ── Tree ──────────────────────────────────────────────────────────────────
    static void PlaceTree(SceneManager* sm, ChunkData& cd,
        double tx, double groundY, double tz, int height)
    {
        // Trunk
        for (int i = 0; i < height; ++i)
        {
            Color c = (i % 2 == 0) ? colWood : colLogTop;
            cd.objects.push_back(SpawnCube(sm, tx, groundY + S * 0.5 + i * S, tz, S, c));
        }

        // Leaf canopy — two layers of 5×5 minus corners, then a 3×3 cap, then apex
        double leafBase = groundY + (height - 1) * S;
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    if (abs(dx) == 2 && abs(dz) == 2) continue;
                    Color lc = ((dx + dz + dy) % 2 == 0) ? colLeaves : colLeavesDark;
                    cd.objects.push_back(
                        SpawnCube(sm, tx + dx * S, leafBase + dy * S, tz + dz * S, S, lc));
                }
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                cd.objects.push_back(
                    SpawnCube(sm, tx + dx * S, leafBase + 2 * S, tz + dz * S, S, colLeaves));
        cd.objects.push_back(
            SpawnCube(sm, tx, leafBase + 3 * S, tz, S, colLeavesDark));
    }

    // ── Torch ─────────────────────────────────────────────────────────────────
    static Vector3 PlaceTorch(SceneManager* sm, ChunkData& cd,
        double tx, double ty, double tz)
    {
        cd.objects.push_back(SpawnCube(sm, tx, ty + S * 0.35, tz, S * 0.12,
            Color(120, 80, 40, 255)));
        cd.objects.push_back(SpawnCube(sm, tx, ty + S * 0.82, tz, S * 0.15,
            colTorch, 0.2));
        return Vector3(tx, ty + S * 0.85, tz);
    }

    // ── Chunk generation ──────────────────────────────────────────────────────
    // Generates all objects for one CHUNK_SIZE×CHUNK_SIZE chunk and appends
    // them directly to sm->objects (caller holds sceneMutex).
    static ChunkData GenerateChunk(SceneManager* sm, HeightMap& heightMap,
        int cx, int cz)
    {
        ChunkData cd;

        int x0 = cx * CHUNK_SIZE;
        int z0 = cz * CHUNK_SIZE;

        for (int lx = 0; lx < CHUNK_SIZE; ++lx)
        {
            for (int lz = 0; lz < CHUNK_SIZE; ++lz)
            {
                int x = x0 + lx;
                int z = z0 + lz;

                int    h = TerrainHeight(x, z);
                double centreY = h * S;            // block centre Y
                double surfaceY = centreY + S * 0.5; // top face Y

                // Surface colour
                Color topCol = (h < WATER_Y) ? colSand
                    : (h > 7) ? colSnow
                    : (h > 3) ? colStone
                    : colGrass;

                // ── Terrain block (Rectangle cube) ────────────────────────────
                // Half-extents: full S in X/Z, half-block in Y (flat-ish slab)
                // Using a thin slab (hy = S*0.25) so the visible top face is
                // at the correct height without a tall pillar below.
                BaseObject* surf = new Rectangle(
                    Vector3(S * 0.5, S * 0.25, S * 0.5),
                    Vector3(x * S, centreY + S * 0.25, z * S),
                    MCIdentity(),
                    new Material(0, 0, topCol, colBlack)
                );
                sm->objects->push_back(surf);
                cd.objects.push_back(surf);

                // Register surface height in the heightmap
                heightMap.Set(x, z, surfaceY);

                // ── Cliff faces (all 4 sides) ─────────────────────────────────
                int hS = TerrainHeight(x, z - 1);  // South (Z-)
                int hN = TerrainHeight(x, z + 1);  // North (Z+)
                int hE = TerrainHeight(x + 1, z);  // East  (X+)
                int hW = TerrainHeight(x - 1, z);  // West  (X-)

                if (hS < h)
                {
                    // South face (Z-)
                    BaseObject* face = new Rectangle(
                        Vector3(S * 0.5, (h - hS) * S * 0.5, 0.01),
                        Vector3(x * S, (h + hS) * S * 0.5, (z - 0.5) * S),
                        MCIdentity(),
                        new Material(0, 0, colDirt, colBlack)
                    );
                    sm->objects->push_back(face);
                    cd.objects.push_back(face);
                }
                if (hN < h)
                {
                    // North face (Z+)
                    BaseObject* face = new Rectangle(
                        Vector3(S * 0.5, (h - hN) * S * 0.5, 0.01),
                        Vector3(x * S, (h + hN) * S * 0.5, (z + 0.5) * S),
                        MCIdentity(),
                        new Material(0, 0, colDirt, colBlack)
                    );
                    sm->objects->push_back(face);
                    cd.objects.push_back(face);
                }
                if (hE < h)
                {
                    // East face (X+)
                    BaseObject* face = new Rectangle(
                        Vector3(0.01, (h - hE) * S * 0.5, S * 0.5),
                        Vector3((x + 0.5) * S, (h + hE) * S * 0.5, z * S),
                        MCIdentity(),
                        new Material(0, 0, colDirt, colBlack)
                    );
                    sm->objects->push_back(face);
                    cd.objects.push_back(face);
                }
                if (hW < h)
                {
                    // West face (X-)
                    BaseObject* face = new Rectangle(
                        Vector3(0.01, (h - hW) * S * 0.5, S * 0.5),
                        Vector3((x - 0.5) * S, (h + hW) * S * 0.5, z * S),
                        MCIdentity(),
                        new Material(0, 0, colDirt, colBlack)
                    );
                    sm->objects->push_back(face);
                    cd.objects.push_back(face);
                }

                // ── Water / lava surface cube (thin slab) ─────────────────────
                if (h < WATER_Y)
                {
                    double lx2 = x * S, lz2 = z * S;
                    bool   isLava = (x > 5 && z > 5);
                    Color  liqCol = isLava ? colLava : colWater;
                    double liqY = WATER_Y * S + S * 0.45;

                    BaseObject* liq = new Rectangle(
                        Vector3(S * 0.5, S * 0.05, S * 0.5),
                        Vector3(lx2, liqY, lz2),
                        MCIdentity(),
                        new Material(0.35, 0, liqCol, colBlack)
                    );
                    sm->objects->push_back(liq);
                    cd.objects.push_back(liq);

                    // Lava glow point light
                    if (isLava && (abs(x * 31 + z * 17) % 100) < 12)
                    {
                        PointLight* ll = new PointLight(
                            0.4f, colLavaGlow,
                            Vector3(x * S, WATER_Y * S + S, z * S));
                        sm->lights->push_back(ll);
                        cd.lavaLights.push_back(ll);
                    }
                }

                // ── Trees ─────────────────────────────────────────────────────
                if (h >= 0 && h <= 2 && topCol.r == colGrass.r)
                    if ((abs(x * 73 + z * 37) % 100) < 6)
                        PlaceTree(sm, cd, x * S, surfaceY, z * S, 4 + (abs(x) % 3));

                // ── Torches on stone highlands ────────────────────────────────
                if (h > 4 && topCol.r == colStone.r
                    && (abs(x * 11 + z * 13) % 100) < 15)
                {
                    Vector3 flame = PlaceTorch(sm, cd, x * S, surfaceY, z * S);
                    PointLight* tl = new PointLight(0.8f, colTorch, flame);
                    sm->lights->push_back(tl);
                    cd.torches.push_back(tl);
                }
            }
        }

        return cd;
    }

    // ── Chunk unloading ───────────────────────────────────────────────────────
    // Removes all objects belonging to the chunk from sm->objects and
    // sm->lights, then frees their memory.
    // Caller must hold sceneMutex.
    static void UnloadChunk(SceneManager* sm, ChunkData& cd)
    {
        // Build fast lookup set for objects to remove
        std::unordered_set<BaseObject*> toRemove(cd.objects.begin(), cd.objects.end());

        // Erase from sm->objects (partition-then-erase to stay O(n)).
        // NOTE: std::remove_if leaves [endIt, end()) in a moved-from / indeterminate
        // state — the original pointers are no longer reliable there.  Delete via
        // the 'toRemove' set (which still holds the original valid pointers) AFTER
        // the erase so the vector is consistent before any pointer is freed.
        auto endIt = std::remove_if(sm->objects->begin(), sm->objects->end(),
            [&](BaseObject* o) { return toRemove.count(o) > 0; });
        sm->objects->erase(endIt, sm->objects->end());

        // Now safe to free — toRemove still holds the original valid pointers.
        for (BaseObject* o : toRemove)
            delete o;

        // Remove torch / lava lights
        auto removeLights = [&](std::vector<PointLight*>& lights)
            {
                for (PointLight* pl : lights)
                {
                    auto it2 = std::find(sm->lights->begin(), sm->lights->end(),
                        static_cast<BaseLight*>(pl));
                    if (it2 != sm->lights->end()) sm->lights->erase(it2);
                    delete pl;
                }
            };
        removeLights(cd.torches);
        removeLights(cd.lavaLights);

        cd.objects.clear();
        cd.torches.clear();
        cd.lavaLights.clear();
    }

    // ═════════════════════════════════════════════════════════════════════════
    //  LoadScene entry point
    // ═════════════════════════════════════════════════════════════════════════
    void MinecraftScene::LoadScene(SceneManager* sm)
    {
        std::mutex        sceneMutex;
        std::atomic<bool> running{ true };

        // ── Camera ────────────────────────────────────────────────────────────
        Camera* camera = new Camera(Vector3(0, EYE_HEIGHT, 0), MCIdentity(), 75);
        sm->currentCamera = camera;
        sm->cameras = new std::vector<Camera*>();
        sm->cameras->push_back(camera);

        // ── Player vertical state ─────────────────────────────────────────────
        double playerVY = 0.0;
        double playerWorldY = 0.0;
        bool   isGrounded = false;

        // ── Input controller ──────────────────────────────────────────────────
        BasicMovements player(sm);
        player.moveSpeed = 0.0;   // we handle XZ manually
        player.eyeHeight = -1.0;  // we control Y ourselves
        player.useBounds = false;

        // ── Shared world state ────────────────────────────────────────────────
        HeightMap heightMap;

        // Loaded chunks: key → ChunkData (all objects/lights it spawned)
        std::unordered_map<ChunkKey, ChunkData, ChunkKeyHash> loadedChunks;

        // ── Global lights (not chunk-owned) ───────────────────────────────────
        sm->lights->push_back(new BaseLight(0.15f, Color(160, 190, 230, 255)));

        DirectionalLight* sun = new DirectionalLight(
            0.65f, Color(255, 235, 180, 255), Vector3(1, 3, 0.5));
        sm->lights->push_back(sun);

        sm->lights->push_back(new DirectionalLight(
            0.38f, Color(150, 180, 255, 255), Vector3(0, 1, 0)));

        PointLight* handLight = new PointLight(
            0.0f, Color(255, 200, 130, 255), camera->transform.position);
        sm->lights->push_back(handLight);

        // Flashlight toggle (E key)
        player.RegisterToggle('E', [&](bool on) {
            handLight->intensity = on ? 1.2f : 0.0f;
            std::cout << "[E] Hand light " << (on ? "ON" : "OFF") << "\n";
            }, /*initialState=*/false);

        // ── Seed: load initial chunks around origin ───────────────────────────
        {
            std::lock_guard<std::mutex> lk(sceneMutex);
            for (int dcx = -LOAD_RADIUS; dcx <= LOAD_RADIUS; ++dcx)
                for (int dcz = -LOAD_RADIUS; dcz <= LOAD_RADIUS; ++dcz)
                {
                    ChunkKey ck{ dcx, dcz };
                    loadedChunks[ck] = GenerateChunk(sm, heightMap, dcx, dcz);
                }
        }

        // Snap player to spawn surface
        playerWorldY = heightMap.SurfaceAt(0, 0);
        camera->transform.position = Vector3(0, playerWorldY + EYE_HEIGHT, 0);

        // ── Chunk-management state (accessed only by anim thread) ─────────────
        int lastPlayerCX = 0, lastPlayerCZ = 0;
        bool needChunkUpdate = true;   // force first check

        // ── Animation / physics / chunk-loading thread ────────────────────────
        double flickerTime = 0.0;
        double sunTime = 0.0;

        std::thread animThread([&]()
            {
                using Clock = std::chrono::high_resolution_clock;
                auto   prev = Clock::now();
                double acc = 0.0;

                while (running)
                {
                    auto   now = Clock::now();
                    double dt = std::chrono::duration<double>(now - prev).count();
                    prev = now;
                    if (dt > MAX_DT) dt = MAX_DT;
                    acc += dt;

                    while (acc >= FIXED_DT)
                    {
                        acc -= FIXED_DT;
                        flickerTime += FIXED_DT;
                        sunTime += FIXED_DT;

                        std::lock_guard<std::mutex> lk(sceneMutex);

                        // ── 1. Quit input ─────────────────────────────────────────
                        if (!player.TickInput(FIXED_DT)) { running = false; break; }

                        // ── 2. Horizontal movement ────────────────────────────────
                        {
                            Vector3 pos = camera->transform.position;
                            Vector3 fwd = camera->transform.forward();
                            Vector3 rgt = camera->transform.right();

                            fwd = Vector3(fwd.x, 0, fwd.z);
                            rgt = Vector3(rgt.x, 0, rgt.z);
                            double fm = fwd.magnitude(), rm = rgt.magnitude();
                            if (fm > 0.001) fwd = fwd * (1.0 / fm);
                            if (rm > 0.001) rgt = rgt * (1.0 / rm);

                            Vector3 move(0, 0, 0);
                            if (GetAsyncKeyState('W') & 0x8000 ||
                                GetAsyncKeyState('Z') & 0x8000) move = move + fwd;
                            if (GetAsyncKeyState('S') & 0x8000) move = move - fwd;
                            if (GetAsyncKeyState('A') & 0x8000 ||
                                GetAsyncKeyState('Q') & 0x8000) move = move - rgt;
                            if (GetAsyncKeyState('D') & 0x8000) move = move + rgt;

                            double ml = move.magnitude();
                            if (ml > 0.001) move = move * (MOVE_SPEED * FIXED_DT / ml);

                            pos.x += move.x;
                            pos.z += move.z;

                            // ── 3. Vertical physics ───────────────────────────────
                            playerVY += GRAVITY * FIXED_DT;
                            playerWorldY += playerVY * FIXED_DT;

                            double surfY = heightMap.SurfaceAt(pos.x, pos.z);
                            if (playerWorldY <= surfY)
                            {
                                playerWorldY = surfY;
                                playerVY = 0.0;
                                isGrounded = true;
                            }
                            else
                            {
                                isGrounded = false;
                            }

                            // ── 4. Jump ───────────────────────────────────────────
                            if (isGrounded && (GetAsyncKeyState(VK_SPACE) & 0x8000))
                            {
                                playerVY = JUMP_SPEED;
                                isGrounded = false;
                            }

                            // ── 5. Sync camera ────────────────────────────────────
                            pos.y = playerWorldY + EYE_HEIGHT;
                            camera->transform.position = pos;
                            handLight->position = pos;

                            // ── 6. Chunk streaming ────────────────────────────────
                            int pcx = WorldToChunk(pos.x);
                            int pcz = WorldToChunk(pos.z);

                            if (pcx != lastPlayerCX || pcz != lastPlayerCZ)
                                needChunkUpdate = true;

                            if (needChunkUpdate)
                            {
                                needChunkUpdate = false;
                                lastPlayerCX = pcx;
                                lastPlayerCZ = pcz;

                                // Load missing chunks within LOAD_RADIUS
                                for (int dcx = -LOAD_RADIUS; dcx <= LOAD_RADIUS; ++dcx)
                                {
                                    for (int dcz = -LOAD_RADIUS; dcz <= LOAD_RADIUS; ++dcz)
                                    {
                                        ChunkKey ck{ pcx + dcx, pcz + dcz };
                                        if (loadedChunks.find(ck) == loadedChunks.end())
                                        {
                                            loadedChunks[ck] = GenerateChunk(
                                                sm, heightMap, ck.cx, ck.cz);
                                        }
                                    }
                                }

                                // Unload chunks beyond UNLOAD_RADIUS
                                std::vector<ChunkKey> toUnload;
                                for (auto& [ck, _] : loadedChunks)
                                {
                                    int dx = abs(ck.cx - pcx);
                                    int dz = abs(ck.cz - pcz);
                                    if (dx > UNLOAD_RADIUS || dz > UNLOAD_RADIUS)
                                        toUnload.push_back(ck);
                                }
                                for (auto& ck : toUnload)
                                {
                                    UnloadChunk(sm, loadedChunks[ck]);
                                    loadedChunks.erase(ck);
                                }
                            }
                        }

                        // ── 7. Ambient animations ─────────────────────────────────
                        // Iterate all loaded chunks' lights for flicker
                        for (auto& [ck, cd] : loadedChunks)
                        {
                            for (int i = 0; i < (int)cd.torches.size(); ++i)
                            {
                                double ph = i * 1.37 + ck.cx * 0.73 + ck.cz * 0.51;
                                cd.torches[i]->intensity =
                                    (float)(1.5 + 0.3 * sin(flickerTime * 14.3 + ph));
                            }
                            for (int i = 0; i < (int)cd.lavaLights.size(); ++i)
                            {
                                cd.lavaLights[i]->intensity =
                                    (float)(0.35 + 0.12 * sin(
                                        flickerTime * 3.1 + i * 0.9
                                        + ck.cx * 0.4 + ck.cz * 0.6));
                            }
                        }

                        // Gentle day cycle
                        double t = sin(sunTime * 0.04) * 0.5 + 0.5;
                        sun->color = Color(255,
                            (uint8_t)(190 + 60 * t),
                            (uint8_t)(110 + 110 * t), 255);
                        sun->intensity = (float)(0.35 + 0.3 * t);
                    }

                    std::this_thread::sleep_for(
                        std::chrono::duration<double>(FIXED_DT * 0.5));
                }
            });

        // ═════════════════════════════════════════════════════════════════════
        //  RENDER / EVENT LOOP
        // ═════════════════════════════════════════════════════════════════════
        Renderer renderer(sm);
        std::cout << "\n=== MINECRAFT PROCEDURAL SCENE — INFINITE WORLD ===\n";
        std::cout << "  Z/Q/S/D  or  W/A/S/D  — move\n";
        std::cout << "  SPACE                  — jump\n";
        std::cout << "  Mouse                  — look\n";
        std::cout << "  E                      — toggle hand light\n";
        std::cout << "  ESC                    — quit\n";
        std::cout << "  Chunk size: " << CHUNK_SIZE << "  |  "
            << "Load radius: " << LOAD_RADIUS << " chunk(s)  |  "
            << "Unload radius: " << UNLOAD_RADIUS << " chunk(s)\n\n";

        while (running)
        {
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN &&
                    ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            }

            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);
            if (mx != 0 || my != 0)
            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                player.TickMouse(-mx, -my);
            }

            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                renderer.Render();
            }
        }

        animThread.join();
        renderer.cleanup();

        // ── Cleanup: unload all remaining chunks ──────────────────────────────
        {
            std::lock_guard<std::mutex> lk(sceneMutex);
            for (auto& [ck, cd] : loadedChunks)
                UnloadChunk(sm, cd);
            loadedChunks.clear();
        }
    }

} // namespace PEngine