#include "minecraftScene.h"

#include <iostream>
#include <thread>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <algorithm>
#include <queue>
#include <condition_variable>
#include <array>
#include <functional>
#include <string>
#include <sstream>
#include <iomanip>

#include <SDL.h>

#include "vector3.h"
#include "rectangle.h"
#include "GPURenderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sceneManager.h"
#include "basicmovements.h"
#include "rigidbody.h"
#include "settings.h"

namespace HonHengine
{
    static constexpr double S = 1.0;
    static constexpr double EYE_HEIGHT = 1.7;
    static constexpr double GRAVITY = -40.0;
    static constexpr double JUMP_SPEED = 14.0;
    static constexpr double MOVE_SPEED = 10.0;
    static constexpr double FIXED_DT = 1.0 / 60.0;
    static constexpr double MAX_DT = 0.1;
    static constexpr int    WATER_Y = -1;
    static constexpr int    CHUNK_SIZE = 16;
    static constexpr int    LOAD_RADIUS = 3;
    static constexpr int    UNLOAD_RADIUS = 4;

    static Quaternion MCIdentity() { return Quaternion(); }

    static const Color colGrass{ 91, 139,  54, 255 };
    static const Color colGrassDark{ 67, 104,  38, 255 };
    static const Color colDirt{ 134,  96,  67, 255 };
    static const Color colStone{ 125, 125, 125, 255 };
    static const Color colSand{ 219, 207, 163, 255 };
    static const Color colWood{ 162, 130,  78, 255 };
    static const Color colLogTop{ 102,  81,  51, 255 };
    static const Color colLeaves{ 55, 118,  43, 255 };
    static const Color colLeavesDark{ 37,  88,  27, 255 };
    static const Color colWater{ 64, 115, 211, 200 };
    static const Color colLava{ 207,  72,   0, 255 };
    static const Color colLavaGlow{ 255, 140,  20, 255 };
    static const Color colTorch{ 255, 195,  50, 255 };
    static const Color colSnow{ 240, 248, 255, 255 };
    static const Color colObsidian{ 25,  20,  35, 255 };
    static const Color colBlack{ 0,   0,   0, 255 };
    static const Color colCactus{ 58, 140,  40, 255 };
    static const Color colDeadGrass{ 165, 142,  80, 255 };
    static const Color colPine{ 30,  80,  40, 255 };

    static float HashF(int x, int z)
    {
        uint32_t h = static_cast<uint32_t>(x * 1619 + z * 31337 + 13);
        h ^= (h >> 16); h *= 0x45d9f3b;
        h ^= (h >> 16); h *= 0x45d9f3b;
        h ^= (h >> 16);
        return static_cast<float>(h) / static_cast<float>(0xFFFFFFFFu);
    }

    static float ValueNoise(float x, float z)
    {
        int   ix = static_cast<int>(std::floor(x));
        int   iz = static_cast<int>(std::floor(z));
        float fx = x - ix, fz = z - iz;
        float ux = fx * fx * (3.f - 2.f * fx);
        float uz = fz * fz * (3.f - 2.f * fz);
        float bot = HashF(ix, iz) + ux * (HashF(ix + 1, iz) - HashF(ix, iz));
        float top = HashF(ix, iz + 1) + ux * (HashF(ix + 1, iz + 1) - HashF(ix, iz + 1));
        return bot + uz * (top - bot);
    }

    static float FBM(float x, float z)
    {
        float val = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
        for (int oct = 0; oct < 4; ++oct)
        {
            val += amp * (ValueNoise(x * freq, z * freq) * 2.f - 1.f);
            norm += amp; amp *= 0.5f; freq *= 2.1f;
        }
        return val / norm;
    }

    static int TerrainHeight(int x, int z)
    {
        float base = FBM(x * 0.04f, z * 0.04f) * 12.f;
        float mountain = FBM(x * 0.015f, z * 0.015f);
        float ridge = (std::max)(0.f, mountain) * (std::max)(0.f, mountain) * 18.f;
        return static_cast<int>(std::round(base + ridge));
    }

    static float TemperatureAt(int x, int z)
    {
        return ValueNoise(x * 0.008f + 200.f, z * 0.008f + 300.f);
    }

    enum class Biome { Grassland, Forest, Desert, Tundra, Mountain };

    static Biome GetBiome(int x, int z, int h)
    {
        float temp = TemperatureAt(x, z);
        if (h > 8)         return Biome::Mountain;
        if (temp < 0.25f)  return Biome::Tundra;
        if (temp < 0.45f)  return Biome::Forest;
        if (temp > 0.75f)  return Biome::Desert;
        return Biome::Grassland;
    }

    static Color SurfaceColor(Biome biome, int h)
    {
        switch (biome)
        {
        case Biome::Mountain:  return (h > 10) ? colSnow : colStone;
        case Biome::Tundra:    return (h > 5) ? colSnow : Color{ 200, 210, 195, 255 };
        case Biome::Desert:    return colSand;
        case Biome::Forest:    return colGrassDark;
        case Biome::Grassland:
        default:
            if (h < WATER_Y) return colSand;
            if (h > 7)       return colSnow;
            if (h > 3)       return colStone;
            return colGrass;
        }
    }

    static const char* BiomeName(Biome b)
    {
        switch (b)
        {
        case Biome::Mountain:  return "MOUNTAIN";
        case Biome::Tundra:    return "TUNDRA";
        case Biome::Desert:    return "DESERT";
        case Biome::Forest:    return "FOREST";
        default:               return "GRASSLAND";
        }
    }

    struct HeightMap
    {
        std::unordered_map<int64_t, double> data;
        mutable std::shared_mutex           mtx;

        static int64_t Key(int xi, int zi)
        {
            return static_cast<int64_t>(xi + 100000) * 200003LL
                + static_cast<int64_t>(zi + 100000);
        }
        void SetBulk(const std::vector<std::pair<int64_t, double>>& entries)
        {
            std::unique_lock<std::shared_mutex> lk(mtx);
            for (auto it = entries.begin(); it != entries.end(); ++it)
                data[it->first] = it->second;
        }
        double SurfaceAt(double wx, double wz) const
        {
            int xi = static_cast<int>(std::floor(wx / S + 0.5));
            int zi = static_cast<int>(std::floor(wz / S + 0.5));
            std::shared_lock<std::shared_mutex> lk(mtx);
            auto it = data.find(Key(xi, zi));
            return (it != data.end()) ? it->second : -50.0;
        }
    };

    struct ChunkKey
    {
        int cx, cz;
        bool operator==(const ChunkKey& o) const { return cx == o.cx && cz == o.cz; }
    };
    struct ChunkKeyHash
    {
        size_t operator()(const ChunkKey& k) const
        {
            return std::hash<int64_t>()(
                static_cast<int64_t>(k.cx + 100000) * 200003LL
                + static_cast<int64_t>(k.cz + 100000));
        }
    };

    static int WorldToChunk(double w) { return static_cast<int>(std::floor(w / (S * CHUNK_SIZE))); }

    struct ChunkData
    {
        ChunkKey                 key{ 0, 0 };
        std::vector<BaseObject*> objects;
        std::vector<PointLight*> torches;
        std::vector<PointLight*> lavaLights;
        float aabbMinX = 0, aabbMinZ = 0;
        float aabbMaxX = 0, aabbMaxZ = 0;
        float aabbMinY = 0, aabbMaxY = 0;
    };

    struct Plane { float a, b, c, d; };

    static bool FrustumTestAABB(const std::array<Plane, 6>& planes,
        float minX, float minY, float minZ,
        float maxX, float maxY, float maxZ)
    {
        for (int i = 0; i < 6; ++i)
        {
            const Plane& p = planes[i];
            float px = (p.a >= 0) ? maxX : minX;
            float py = (p.b >= 0) ? maxY : minY;
            float pz = (p.c >= 0) ? maxZ : minZ;
            if (p.a * px + p.b * py + p.c * pz + p.d < 0) return false;
        }
        return true;
    }

    static std::array<Plane, 6> BuildFrustumPlanes(
        const Vector3& camPos, const Vector3& fwd,
        const Vector3& right, const Vector3& up,
        float fovYRad, float aspect, float nearZ, float farZ)
    {
        float hHalfNear = std::tan(fovYRad * 0.5f) * nearZ;
        float wHalfNear = hHalfNear * aspect;
        Vector3 nc = camPos + fwd * nearZ;
        Vector3 fc = camPos + fwd * farZ;

        auto norm3 = [](Vector3 v) {
            double m = v.magnitude();
            return m > 1e-9 ? v * (1.0 / m) : v;
            };

        Vector3 rn = norm3((fwd * nearZ + right * wHalfNear).cross(up));
        Vector3 ln = norm3(up.cross(fwd * nearZ - right * wHalfNear));
        Vector3 tn = norm3(right.cross(fwd * nearZ + up * hHalfNear));
        Vector3 bn = norm3((fwd * nearZ - up * hHalfNear).cross(right));

        auto makePlane = [](Vector3 n, Vector3 pt) -> Plane {
            float a = static_cast<float>(n.x);
            float b = static_cast<float>(n.y);
            float c = static_cast<float>(n.z);
            float d = -(a * static_cast<float>(pt.x)
                + b * static_cast<float>(pt.y)
                + c * static_cast<float>(pt.z));
            return { a, b, c, d };
            };

        std::array<Plane, 6> planes;
        planes[0] = makePlane(fwd, nc);
        planes[1] = makePlane(fwd * -1.0, fc);
        planes[2] = makePlane(rn, camPos);
        planes[3] = makePlane(ln, camPos);
        planes[4] = makePlane(tn, camPos);
        planes[5] = makePlane(bn, camPos);
        return planes;
    }

    static BaseObject* SpawnCube(SceneManager* sm,
        double x, double y, double z, double half, Color col, double shadeTop = 1.0)
    {
        BaseObject* obj = new Rectangle(
            Vector3(half, half, half),
            Vector3(x, y, z),
            MCIdentity(),
            new Material(0, 0, col, colBlack));
        (void)shadeTop;
        sm->objects->push_back(obj);
        return obj;
    }

    static BaseObject* SpawnSlab(SceneManager* sm,
        double x, double y, double z,
        double hx, double hy, double hz, Color col)
    {
        BaseObject* obj = new Rectangle(
            Vector3(hx, hy, hz),
            Vector3(x, y, z),
            MCIdentity(),
            new Material(0, 0, col, colBlack));
        sm->objects->push_back(obj);
        return obj;
    }

    static void PlaceTree(SceneManager* sm, ChunkData& cd,
        double tx, double groundY, double tz,
        int height, Color leafColor, Color leafDark)
    {
        for (int i = 0; i < height; ++i)
        {
            Color c = (i % 2 == 0) ? colWood : colLogTop;
            cd.objects.push_back(SpawnCube(sm, tx, groundY + S * 0.5 + i * S, tz, S, c));
        }
        double leafBase = groundY + (height - 1) * S;
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    if (abs(dx) == 2 && abs(dz) == 2) continue;
                    Color lc = ((dx + dz + dy) % 2 == 0) ? leafColor : leafDark;
                    cd.objects.push_back(
                        SpawnCube(sm, tx + dx * S, leafBase + dy * S, tz + dz * S, S, lc));
                }
        for (int dx = -1; dx <= 1; ++dx)
            for (int dz = -1; dz <= 1; ++dz)
                cd.objects.push_back(
                    SpawnCube(sm, tx + dx * S, leafBase + 2 * S, tz + dz * S, S, leafColor));
        cd.objects.push_back(SpawnCube(sm, tx, leafBase + 3 * S, tz, S, leafDark));
    }

    static void PlaceCactus(SceneManager* sm, ChunkData& cd,
        double tx, double groundY, double tz, int height)
    {
        for (int i = 0; i < height; ++i)
            cd.objects.push_back(
                SpawnCube(sm, tx, groundY + S * 0.5 + i * S, tz, S * 0.85, colCactus));
    }

    static Vector3 PlaceTorch(SceneManager* sm, ChunkData& cd,
        double tx, double ty, double tz)
    {
        cd.objects.push_back(SpawnSlab(sm, tx, ty + S * 0.35, tz,
            S * 0.06, S * 0.35, S * 0.06, Color{ 120, 80, 40, 255 }));
        cd.objects.push_back(SpawnSlab(sm, tx, ty + S * 0.82, tz,
            S * 0.08, S * 0.08, S * 0.08, colTorch));
        return Vector3(tx, ty + S * 0.85, tz);
    }

    static ChunkData GenerateChunkData(SceneManager* sm, HeightMap& heightMap, int cx, int cz)
    {
        ChunkData cd;
        cd.key = { cx, cz };
        int x0 = cx * CHUNK_SIZE;
        int z0 = cz * CHUNK_SIZE;

        const int GS = CHUNK_SIZE + 2;
        std::vector<int> hGrid(GS * GS);
        auto gridIdx = [&](int lx, int lz) { return lz * GS + lx; };
        for (int lz = 0; lz < GS; ++lz)
            for (int lx = 0; lx < GS; ++lx)
                hGrid[gridIdx(lx, lz)] = TerrainHeight(x0 + lx - 1, z0 + lz - 1);

        std::unordered_set<int64_t> treeOccupied;
        auto treeKey = [](int x, int z) {
            return static_cast<int64_t>(x + 100000) * 200003LL
                + static_cast<int64_t>(z + 100000);
            };

        float aMinY = 1e9f, aMaxY = -1e9f;
        std::vector<std::pair<int64_t, double>> hmEntries;
        hmEntries.reserve(CHUNK_SIZE * CHUNK_SIZE);

        for (int lz = 0; lz < CHUNK_SIZE; ++lz)
        {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx)
            {
                int x = x0 + lx;
                int z = z0 + lz;

                int h = hGrid[gridIdx(lx + 1, lz + 1)];
                int hS = hGrid[gridIdx(lx + 1, lz)];
                int hN = hGrid[gridIdx(lx + 1, lz + 2)];
                int hE = hGrid[gridIdx(lx + 2, lz + 1)];
                int hW = hGrid[gridIdx(lx, lz + 1)];

                double centreY = h * S;
                double surfaceY = centreY + S * 0.5;

                Biome biome = GetBiome(x, z, h);
                Color topCol = SurfaceColor(biome, h);

                hmEntries.push_back({ HeightMap::Key(x, z), surfaceY });

                BaseObject* surf = new Rectangle(
                    Vector3(S * 0.5, S * 0.25, S * 0.5),
                    Vector3(x * S, centreY + S * 0.25, z * S),
                    MCIdentity(),
                    new Material(0, 0, topCol, colBlack));
                sm->objects->push_back(surf);
                cd.objects.push_back(surf);

                float sy = static_cast<float>(surfaceY);
                if (sy < aMinY) aMinY = sy;
                if (sy > aMaxY) aMaxY = sy;

                auto SpawnFace = [&](double fx, double fy, double fz,
                    double hx, double hy, double hz)
                    {
                        BaseObject* face = new Rectangle(
                            Vector3(hx, hy, hz),
                            Vector3(fx, fy, fz),
                            MCIdentity(),
                            new Material(0, 0, colDirt, colBlack));
                        sm->objects->push_back(face);
                        cd.objects.push_back(face);
                    };

                if (hS < h) SpawnFace(x * S, (h + hS) * S * 0.5, (z - 0.5) * S, S * 0.5, (h - hS) * S * 0.5, S * 0.5);
                if (hN < h) SpawnFace(x * S, (h + hN) * S * 0.5, (z + 0.5) * S, S * 0.5, (h - hN) * S * 0.5, S * 0.5);
                if (hE < h) SpawnFace((x + 0.5) * S, (h + hE) * S * 0.5, z * S, S * 0.5, (h - hE) * S * 0.5, S * 0.5);
                if (hW < h) SpawnFace((x - 0.5) * S, (h + hW) * S * 0.5, z * S, S * 0.5, (h - hW) * S * 0.5, S * 0.5);

                if (h < WATER_Y)
                {
                    bool isLava = (abs(x * 17 + z * 31 + 7) % 11) < 2 && biome != Biome::Tundra;
                    Color liqCol = isLava ? colLava : colWater;
                    for (int wy = h + 1; wy <= WATER_Y; ++wy)
                    {
                        bool   isTop = (wy == WATER_Y);
                        double liqY = wy * S;
                        double halfThick = isTop ? S * 0.05 : S * 0.5;
                        BaseObject* liq = new Rectangle(
                            Vector3(S * 0.5, halfThick, S * 0.5),
                            Vector3(x * S, liqY + (isTop ? S * 0.45 : 0.0), z * S),
                            MCIdentity(),
                            new Material(0.35, 0, liqCol, colBlack));
                        sm->objects->push_back(liq);
                        cd.objects.push_back(liq);
                    }
                    if (isLava && (abs(x * 31 + z * 17) % 100) < 12)
                    {
                        PointLight* ll = new PointLight(0.4f, colLavaGlow,
                            Vector3(x * S, WATER_Y * S + S, z * S));
                        sm->lights->push_back(ll);
                        cd.lavaLights.push_back(ll);
                    }
                }

                if (h >= 0)
                {
                    if (biome == Biome::Grassland || biome == Biome::Forest)
                    {
                        float treeDensity = (biome == Biome::Forest) ? 0.10f : 0.05f;
                        bool  wantTree = (abs(x * 73 + z * 37) % 100) < static_cast<int>(treeDensity * 100.f);
                        if (wantTree)
                        {
                            bool blocked = false;
                            for (int ex = -1; ex <= 1 && !blocked; ++ex)
                                for (int ez = -1; ez <= 1 && !blocked; ++ez)
                                    if (treeOccupied.count(treeKey(x + ex, z + ez))) blocked = true;
                            if (!blocked)
                            {
                                treeOccupied.insert(treeKey(x, z));
                                PlaceTree(sm, cd, x * S, surfaceY, z * S,
                                    4 + (abs(x) % 3), colLeaves, colLeavesDark);
                            }
                        }
                    }
                    else if (biome == Biome::Desert)
                    {
                        if ((abs(x * 53 + z * 67) % 100) < 4)
                            PlaceCactus(sm, cd, x * S, surfaceY, z * S, 2 + abs(z) % 3);
                    }
                    else if (biome == Biome::Tundra && h <= 2)
                    {
                        if ((abs(x * 41 + z * 29) % 100) < 5)
                        {
                            bool blocked = false;
                            for (int ex = -1; ex <= 1 && !blocked; ++ex)
                                for (int ez = -1; ez <= 1 && !blocked; ++ez)
                                    if (treeOccupied.count(treeKey(x + ex, z + ez))) blocked = true;
                            if (!blocked)
                            {
                                treeOccupied.insert(treeKey(x, z));
                                PlaceTree(sm, cd, x * S, surfaceY, z * S,
                                    3 + abs(x) % 2, colPine, Color{ 20, 60, 30, 255 });
                            }
                        }
                    }

                    if ((biome == Biome::Grassland || biome == Biome::Mountain)
                        && h > 4 && topCol.r == colStone.r
                        && (abs(x * 11 + z * 13) % 100) < 15)
                    {
                        Vector3 flame = PlaceTorch(sm, cd, x * S, surfaceY, z * S);
                        PointLight* tl = new PointLight(0.8f, colTorch, flame);
                        sm->lights->push_back(tl);
                        cd.torches.push_back(tl);
                    }
                }
            }
        }

        heightMap.SetBulk(hmEntries);

        cd.aabbMinX = static_cast<float>(x0 * S - S * 0.5f);
        cd.aabbMaxX = static_cast<float>((x0 + CHUNK_SIZE) * S + S * 0.5f);
        cd.aabbMinZ = static_cast<float>(z0 * S - S * 0.5f);
        cd.aabbMaxZ = static_cast<float>((z0 + CHUNK_SIZE) * S + S * 0.5f);
        cd.aabbMinY = aMinY - 2.f;
        cd.aabbMaxY = aMaxY + 12.f;

        return cd;
    }

    static void UnloadChunk(SceneManager* sm, ChunkData& cd)
    {
        std::unordered_set<BaseObject*> toRemove(cd.objects.begin(), cd.objects.end());
        auto endIt = std::remove_if(sm->objects->begin(), sm->objects->end(),
            [&](BaseObject* o) { return toRemove.count(o) > 0; });
        sm->objects->erase(endIt, sm->objects->end());
        for (BaseObject* o : toRemove) delete o;

        std::unordered_set<BaseLight*> lightSet;
        for (PointLight* pl : cd.torches)    lightSet.insert(static_cast<BaseLight*>(pl));
        for (PointLight* pl : cd.lavaLights) lightSet.insert(static_cast<BaseLight*>(pl));
        auto lightEnd = std::remove_if(sm->lights->begin(), sm->lights->end(),
            [&](BaseLight* bl) { return lightSet.count(bl) > 0; });
        sm->lights->erase(lightEnd, sm->lights->end());
        for (BaseLight* bl : lightSet) delete bl;

        cd.objects.clear();
        cd.torches.clear();
        cd.lavaLights.clear();
    }

    struct ChunkStreamer
    {
        SceneManager* sm;
        HeightMap& heightMap;
        std::mutex                                     pendingMtx;
        std::vector<ChunkData>                         pendingReady;
        std::unordered_set<ChunkKey, ChunkKeyHash>     inFlight;
        std::mutex                                     inFlightMtx;
        std::queue<ChunkKey>                           workQueue;
        std::mutex                                     workMtx;
        std::condition_variable                        workCV;
        std::thread                                    worker;
        std::atomic<bool>                              running{ true };

        explicit ChunkStreamer(SceneManager* sm_, HeightMap& hm)
            : sm(sm_), heightMap(hm)
        {
            worker = std::thread([this]() { WorkerLoop(); });
        }

        ~ChunkStreamer()
        {
            running = false;
            workCV.notify_all();
            if (worker.joinable()) worker.join();
        }

        void RequestChunk(ChunkKey ck)
        {
            { std::lock_guard<std::mutex> lk(inFlightMtx); if (!inFlight.insert(ck).second) return; }
            { std::lock_guard<std::mutex> lk(workMtx);     workQueue.push(ck); }
            workCV.notify_one();
        }

        std::vector<ChunkData> CommitReadyData()
        {
            std::vector<ChunkData> ready;
            std::lock_guard<std::mutex> lk(pendingMtx);
            ready.swap(pendingReady);
            return ready;
        }

    private:
        void WorkerLoop()
        {
            while (running)
            {
                ChunkKey ck{ 0, 0 };
                {
                    std::unique_lock<std::mutex> lk(workMtx);
                    workCV.wait(lk, [this] { return !workQueue.empty() || !running; });
                    if (!running) break;
                    ck = workQueue.front();
                    workQueue.pop();
                }
                ChunkData cd = GenerateChunkData(sm, heightMap, ck.cx, ck.cz);
                { std::lock_guard<std::mutex> lk(pendingMtx);    pendingReady.push_back(std::move(cd)); }
                { std::lock_guard<std::mutex> lk(inFlightMtx);   inFlight.erase(ck); }
            }
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  UI helpers
    // ─────────────────────────────────────────────────────────────────────────

    static void DrawHotbar(UIRenderer& ui, int W, int H)
    {
        static constexpr int   SLOTS = 9;
        static constexpr float SLOT_SIZE = 44.f;
        static constexpr float SLOT_GAP = 4.f;
        static constexpr float CORNER_RAD = 4.f;

        float barW = SLOTS * SLOT_SIZE + (SLOTS - 1) * SLOT_GAP + 16.f;
        float barH = SLOT_SIZE + 16.f;
        float barX = (W - barW) * 0.5f;
        float barY = H - barH - 10.f;

        // Bar background - dark translucent panel
        ui.pushQuad(barX, barY, barW, barH,
            0.f, 0.f, 1.f, 1.f,
            0.05f, 0.05f, 0.08f, 0.78f);

        // Slot cells
        for (int i = 0; i < SLOTS; ++i)
        {
            float sx = barX + 8.f + i * (SLOT_SIZE + SLOT_GAP);
            float sy = barY + 8.f;

            // Slot inset
            bool selected = (i == 0);
            float br = selected ? 0.9f : 0.22f;
            float bg = selected ? 0.7f : 0.22f;
            float bb = selected ? 0.3f : 0.22f;
            float ba = selected ? 1.0f : 0.65f;

            ui.pushQuad(sx, sy, SLOT_SIZE, SLOT_SIZE,
                0.f, 0.f, 1.f, 1.f,
                br, bg, bb, ba);

            // Inner highlight on top edge
            ui.pushQuad(sx + 2.f, sy + 2.f, SLOT_SIZE - 4.f, 2.f,
                0.f, 0.f, 1.f, 1.f,
                1.f, 1.f, 1.f, selected ? 0.35f : 0.12f);
        }
    }

    static void DrawStatusBars(UIRenderer& ui, int W, int H,
        float health, float hunger, float oxygen)
    {
        static constexpr float BAR_W = 160.f;
        static constexpr float BAR_H = 10.f;
        static constexpr float PAD = 8.f;
        static constexpr float SPACING = 16.f;

        // Health - left side above hotbar
        float baseY = H - 74.f;
        float leftX = (W - 2.f * BAR_W - SPACING) * 0.5f;
        float rightX = leftX + BAR_W + SPACING;

        // Health bar backing
        ui.pushQuad(leftX, baseY, BAR_W, BAR_H,
            0.f, 0.f, 1.f, 1.f, 0.1f, 0.05f, 0.05f, 0.8f);
        // Health fill - green → red gradient via alpha blend
        float hf = (std::max)(0.f, (std::min)(health, 1.f));
        float hr = 1.f - hf * 0.5f;
        float hg = hf * 0.85f;
        ui.pushQuad(leftX, baseY, BAR_W * hf, BAR_H,
            0.f, 0.f, 1.f, 1.f, hr, hg, 0.1f, 0.92f);
        // Highlight
        ui.pushQuad(leftX, baseY, BAR_W * hf, 2.f,
            0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.18f);

        // Hunger bar
        ui.pushQuad(rightX, baseY, BAR_W, BAR_H,
            0.f, 0.f, 1.f, 1.f, 0.1f, 0.07f, 0.02f, 0.8f);
        float hnf = (std::max)(0.f, (std::min)(hunger, 1.f));
        ui.pushQuad(rightX, baseY, BAR_W * hnf, BAR_H,
            0.f, 0.f, 1.f, 1.f, 0.85f, 0.55f, 0.15f, 0.92f);
        ui.pushQuad(rightX, baseY, BAR_W * hnf, 2.f,
            0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.18f);

        // Oxygen bar (only visible when < 1)
        if (oxygen < 0.999f)
        {
            float oxy = (std::max)(0.f, oxygen);
            float oy = baseY - BAR_H - 4.f;
            ui.pushQuad(leftX, oy, BAR_W * 2.f + SPACING, BAR_H,
                0.f, 0.f, 1.f, 1.f, 0.04f, 0.08f, 0.25f, 0.8f);
            ui.pushQuad(leftX, oy, (BAR_W * 2.f + SPACING) * oxy, BAR_H,
                0.f, 0.f, 1.f, 1.f, 0.2f, 0.55f, 1.0f, 0.92f);
        }
    }

    static void DrawCompass(UIRenderer& ui, int W, float yawDeg)
    {
        static constexpr float CW = 140.f;
        static constexpr float CH = 22.f;
        static constexpr float TICK_W = 2.f;
        float cx = (W - CW) * 0.5f;
        float cy = 14.f;

        // Compass background
        ui.pushQuad(cx, cy, CW, CH, 0.f, 0.f, 1.f, 1.f, 0.05f, 0.05f, 0.1f, 0.72f);

        // North/South/East/West ticks
        struct { const char* label; float angle; float r, g, b; } dirs[] = {
            { "N",   0.f,   1.0f, 0.25f, 0.25f },
            { "E",  90.f,   0.9f, 0.9f,  0.9f  },
            { "S", 180.f,   0.9f, 0.9f,  0.9f  },
            { "W", 270.f,   0.9f, 0.9f,  0.9f  },
        };

        for (int i = 0; i < 4; ++i)
        {
            float diff = dirs[i].angle - yawDeg;
            while (diff > 180.f) diff -= 360.f;
            while (diff < -180.f) diff += 360.f;
            if (diff < -90.f || diff > 90.f) continue;

            float tx = cx + CW * 0.5f + (diff / 90.f) * (CW * 0.5f - 8.f);
            ui.pushQuad(tx - TICK_W * 0.5f, cy + 3.f, TICK_W, CH - 6.f,
                0.f, 0.f, 1.f, 1.f,
                dirs[i].r, dirs[i].g, dirs[i].b, 0.9f);
        }

        // Cursor tick (always centred)
        ui.pushQuad(cx + CW * 0.5f - 1.f, cy + 2.f, 2.f, CH - 4.f,
            0.f, 0.f, 1.f, 1.f, 1.f, 0.85f, 0.2f, 1.0f);
    }

    static void DrawDebugPanel(UIRenderer& ui, int W, int H,
        const Vector3& pos, float fps,
        int chunksLoaded, Biome biome,
        bool handLight, double sunT)
    {
        float panW = 220.f;
        float panH = 118.f;
        float px = 10.f;
        float py = 10.f;

        // Panel background
        ui.pushQuad(px, py, panW, panH,
            0.f, 0.f, 1.f, 1.f, 0.03f, 0.03f, 0.05f, 0.72f);

        // Accent left border
        ui.pushQuad(px, py, 2.f, panH,
            0.f, 0.f, 1.f, 1.f, 0.4f, 0.75f, 1.f, 0.9f);

        // Mini bars as visual metric rows (fps, chunks)
        auto DrawMetricBar = [&](float bx, float by, float frac, float r, float g, float b)
            {
                ui.pushQuad(bx, by, 80.f, 6.f,
                    0.f, 0.f, 1.f, 1.f, 0.12f, 0.12f, 0.12f, 0.6f);
                ui.pushQuad(bx, by, 80.f * (std::min)(frac, 1.f), 6.f,
                    0.f, 0.f, 1.f, 1.f, r, g, b, 0.88f);
            };

        // FPS bar (60 = full)
        DrawMetricBar(px + 130.f, py + 14.f, fps / 60.f,
            fps > 50.f ? 0.3f : fps > 30.f ? 0.9f : 1.f,
            fps > 50.f ? 0.9f : fps > 30.f ? 0.7f : 0.2f,
            0.2f);

        // Chunks loaded bar (out of max expected)
        float maxChunks = (float)((2 * LOAD_RADIUS + 1) * (2 * LOAD_RADIUS + 1));
        DrawMetricBar(px + 130.f, py + 30.f, chunksLoaded / maxChunks,
            0.3f, 0.7f, 1.0f);

        // Day progress bar
        double dayFrac = std::sin(sunT * 0.04) * 0.5 + 0.5;
        DrawMetricBar(px + 130.f, py + 46.f, static_cast<float>(dayFrac),
            1.0f, 0.85f, 0.3f);

        // Hand-light indicator dot
        ui.pushQuad(px + 10.f, py + panH - 14.f, 8.f, 8.f,
            0.f, 0.f, 1.f, 1.f,
            handLight ? 1.f : 0.3f,
            handLight ? 0.85f : 0.3f,
            handLight ? 0.2f : 0.3f,
            0.95f);
    }

    static void DrawVignette(UIRenderer& ui, int W, int H)
    {
        static constexpr float EDGE = 0.18f;
        static constexpr float A = 0.45f;

        // Four edge quads - simulate a vignette without a shader
        ui.pushQuad(0.f, 0.f, W * EDGE, H, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, A);
        ui.pushQuad(W * (1 - EDGE), 0.f, W * EDGE, H, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, A);
        ui.pushQuad(0.f, 0.f, W, H * EDGE, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, A);
        ui.pushQuad(0.f, H * (1 - EDGE), W, H * EDGE, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, A);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  LoadScene
    // ─────────────────────────────────────────────────────────────────────────
    void MinecraftScene::LoadScene(SceneManager* sm)
    {
        std::mutex        sceneMutex;
        std::atomic<bool> running{ true };

        Camera* camera = new Camera(Vector3(0, EYE_HEIGHT, 0), MCIdentity(), 75);
        sm->currentCamera = camera;
        sm->cameras = new std::vector<Camera*>();
        sm->cameras->push_back(camera);

        double playerVY = 0.0;
        double playerWorldY = 0.0;
        bool   isGrounded = false;

        BasicMovements player(sm);
        player.moveSpeed = 0.0;
        player.eyeHeight = -1.0;
        player.useBounds = false;

        HeightMap heightMap;
        std::unordered_map<ChunkKey, ChunkData, ChunkKeyHash> loadedChunks;

        // Global lights
        sm->lights->push_back(new BaseLight(0.15f, Color{ 160, 190, 230, 255 }));
        DirectionalLight* sun = new DirectionalLight(
            0.65f, Color{ 255, 235, 180, 255 }, Vector3(1, 3, 0.5));
        sm->lights->push_back(sun);
        sm->lights->push_back(new DirectionalLight(
            0.38f, Color{ 150, 180, 255, 255 }, Vector3(0, 1, 0)));
        PointLight* handLight = new PointLight(
            0.0f, Color{ 255, 200, 130, 255 }, camera->transform.position);
        sm->lights->push_back(handLight);

        bool handLightOn = false;
        player.RegisterToggle('E', [&](bool on) {
            handLight->intensity = on ? 1.2f : 0.0f;
            handLightOn = on;
            }, false);

        ChunkStreamer streamer(sm, heightMap);

        {
            std::lock_guard<std::mutex> lk(sceneMutex);
            for (int dcx = -1; dcx <= 1; ++dcx)
                for (int dcz = -1; dcz <= 1; ++dcz)
                {
                    ChunkKey ck{ dcx, dcz };
                    loadedChunks[ck] = GenerateChunkData(sm, heightMap, dcx, dcz);
                }
            for (int dcx = -LOAD_RADIUS; dcx <= LOAD_RADIUS; ++dcx)
                for (int dcz = -LOAD_RADIUS; dcz <= LOAD_RADIUS; ++dcz)
                {
                    ChunkKey ck{ dcx, dcz };
                    if (loadedChunks.find(ck) == loadedChunks.end())
                        streamer.RequestChunk(ck);
                }
        }

        playerWorldY = heightMap.SurfaceAt(0, 0);
        camera->transform.position = Vector3(0, playerWorldY + EYE_HEIGHT, 0);

        int  lastPlayerCX = 0, lastPlayerCZ = 0;
        bool needChunkUpdate = true;

        double flickerTime = 0.0;
        double sunTime = 0.0;

        // Shared mutable UI state (written by anim thread, read by render thread)
        std::atomic<float> uiFPS{ 60.f };

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

                        if (!player.TickInput(FIXED_DT)) { running = false; break; }

                        {
                            Vector3 pos = camera->transform.position;
                            Vector3 fwd = camera->transform.forward();
                            Vector3 rgt = camera->transform.right();

                            fwd = Vector3(fwd.x, 0, fwd.z);
                            rgt = Vector3(rgt.x, 0, rgt.z);
                            double fm = fwd.magnitude(), rm = rgt.magnitude();
                            if (fm > 0.001) fwd = fwd * (1.0 / fm);
                            if (rm > 0.001) rgt = rgt * (1.0 / rm);

                            const Uint8* keys = SDL_GetKeyboardState(nullptr);
                            Vector3 move(0, 0, 0);
                            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z]) move = move + fwd;
                            if (keys[SDL_SCANCODE_S])                          move = move - fwd;
                            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_Q]) move = move - rgt;
                            if (keys[SDL_SCANCODE_D])                          move = move + rgt;

                            double ml = move.magnitude();
                            if (ml > 0.001) move = move * (MOVE_SPEED * FIXED_DT / ml);

                            pos.x += move.x;
                            pos.z += move.z;

                            playerVY += GRAVITY * FIXED_DT;
                            playerWorldY += playerVY * FIXED_DT;

                            double surfY = heightMap.SurfaceAt(pos.x, pos.z);
                            if (playerWorldY <= surfY)
                            {
                                playerWorldY = surfY;
                                playerVY = 0.0;
                                isGrounded = true;
                            }
                            else { isGrounded = false; }

                            if (isGrounded && keys[SDL_SCANCODE_SPACE])
                            {
                                playerVY = JUMP_SPEED;
                                isGrounded = false;
                            }

                            pos.y = playerWorldY + EYE_HEIGHT;
                            camera->transform.position = pos;
                            handLight->transform.position = pos;

                            int pcx = WorldToChunk(pos.x);
                            int pcz = WorldToChunk(pos.z);

                            if (pcx != lastPlayerCX || pcz != lastPlayerCZ)
                                needChunkUpdate = true;

                            if (needChunkUpdate)
                            {
                                needChunkUpdate = false;
                                lastPlayerCX = pcx;
                                lastPlayerCZ = pcz;

                                for (int dcx = -LOAD_RADIUS; dcx <= LOAD_RADIUS; ++dcx)
                                    for (int dcz = -LOAD_RADIUS; dcz <= LOAD_RADIUS; ++dcz)
                                    {
                                        ChunkKey ck{ pcx + dcx, pcz + dcz };
                                        if (loadedChunks.find(ck) == loadedChunks.end())
                                            streamer.RequestChunk(ck);
                                    }

                                std::vector<ChunkKey> toUnload;
                                for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ++it)
                                {
                                    const ChunkKey& ck = it->first;
                                    if (abs(ck.cx - pcx) > UNLOAD_RADIUS ||
                                        abs(ck.cz - pcz) > UNLOAD_RADIUS)
                                        toUnload.push_back(ck);
                                }
                                for (int i = 0; i < (int)toUnload.size(); ++i)
                                {
                                    UnloadChunk(sm, loadedChunks[toUnload[i]]);
                                    loadedChunks.erase(toUnload[i]);
                                }
                            }
                        }

                        {
                            std::vector<ChunkData> ready = streamer.CommitReadyData();
                            for (int i = 0; i < (int)ready.size(); ++i)
                            {
                                ChunkKey ck = ready[i].key;
                                if (loadedChunks.find(ck) == loadedChunks.end())
                                    loadedChunks[ck] = std::move(ready[i]);
                            }
                        }

                        for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ++it)
                        {
                            ChunkData& cd = it->second;
                            const ChunkKey& ck = it->first;
                            for (int i = 0; i < (int)cd.torches.size(); ++i)
                            {
                                double ph = i * 1.37 + ck.cx * 0.73 + ck.cz * 0.51;
                                cd.torches[i]->intensity =
                                    static_cast<float>(1.5 + 0.3 * sin(flickerTime * 14.3 + ph));
                            }
                            for (int i = 0; i < (int)cd.lavaLights.size(); ++i)
                            {
                                cd.lavaLights[i]->intensity =
                                    static_cast<float>(0.35 + 0.12 * sin(
                                        flickerTime * 3.1 + i * 0.9
                                        + ck.cx * 0.4 + ck.cz * 0.6));
                            }
                        }

                        double t = sin(sunTime * 0.04) * 0.5 + 0.5;
                        sun->color = Color(255,
                            static_cast<uint8_t>(190 + 60 * t),
                            static_cast<uint8_t>(110 + 110 * t), 255);
                        sun->intensity = static_cast<float>(0.35 + 0.3 * t);
                    }

                    std::this_thread::sleep_for(std::chrono::duration<double>(FIXED_DT * 0.5));
                }
            });

        // ─────────────────────────────────────────────────────────────────────
        //  Renderer + render loop
        // ─────────────────────────────────────────────────────────────────────
        GPURenderer renderer = GPURenderer(sm);

        std::cout << "\n=== MINECRAFT PROCEDURAL SCENE - INFINITE WORLD (GPU) ===\n";
        std::cout << "  W/A/S/D / Z/Q  - move       SPACE - jump\n";
        std::cout << "  Mouse          - look        E     - hand light\n";
        std::cout << "  ESC            - quit\n\n";

        static constexpr float FRUSTUM_FOV_Y = 75.f * (3.14159265f / 180.f);
        static constexpr float FRUSTUM_ASPECT = 16.f / 9.f;
        static constexpr float FRUSTUM_NEAR = 0.1f;
        static constexpr float FRUSTUM_FAR = 500.f;

        using Clock = std::chrono::high_resolution_clock;
        auto framePrev = Clock::now();

        while (running)
        {
            auto   frameNow = Clock::now();
            double frameDt = std::chrono::duration<double>(frameNow - framePrev).count();
            framePrev = frameNow;
            if (frameDt > 0.001) uiFPS.store(static_cast<float>(1.0 / frameDt));

            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT) running = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            }

            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);

            {
                std::lock_guard<std::mutex> lk(sceneMutex);

                if (mx != 0 || my != 0)
                    player.TickMouse(-mx, -my);

                // Frustum culling
                {
                    auto* cam = sm->currentCamera;
                    Vector3 cp = cam->transform.position;
                    Vector3 cf = cam->transform.forward();
                    Vector3 cr = cam->transform.right();
                    Vector3 cu = cam->transform.up();

                    auto norm = [](Vector3 v) {
                        double m = v.magnitude();
                        return m > 1e-9 ? v * (1.0 / m) : v;
                        };
                    cf = norm(cf); cr = norm(cr); cu = norm(cu);

                    auto planes = BuildFrustumPlanes(cp, cf, cr, cu,
                        FRUSTUM_FOV_Y, FRUSTUM_ASPECT, FRUSTUM_NEAR, FRUSTUM_FAR);

                    for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ++it)
                    {
                        ChunkData& cd = it->second;
                        bool visible = FrustumTestAABB(planes,
                            cd.aabbMinX, cd.aabbMinY, cd.aabbMinZ,
                            cd.aabbMaxX, cd.aabbMaxY, cd.aabbMaxZ);
                        for (int i = 0; i < (int)cd.objects.size(); ++i)
                            if (cd.objects[i]) cd.objects[i]->visible = visible;
                    }
                }

                // ── 3D pass ───────────────────────────────────────────────────
                renderer.Render();

                // ── UI pass ───────────────────────────────────────────────────
                int W = Settings::canvasWidth;
                int H = Settings::canvasHeight;

                Vector3    camPos = sm->currentCamera->transform.position;
                float      fps = uiFPS.load();
                int        chunks = static_cast<int>(loadedChunks.size());
                int        cx = static_cast<int>(std::floor(camPos.x));
                int        cz = static_cast<int>(std::floor(camPos.z));
                Biome      biome = GetBiome(cx, cz, TerrainHeight(cx, cz));

                // Yaw from camera quaternion (approximate)
                auto& q = sm->currentCamera->transform.rotation;
                float yawR = static_cast<float>(std::atan2(
                    2.0 * (q.w * q.y + q.x * q.z),
                    1.0 - 2.0 * (q.y * q.y + q.z * q.z)));
                float yawDeg = yawR * (180.f / 3.14159265f);

                UIRenderer& ui = renderer.getUI();
                ui.beginFrame();

                DrawVignette(ui, static_cast<float>(W), static_cast<float>(H));

                // Crosshair
                float cx2 = W * 0.5f, cy2 = H * 0.5f;
                ui.pushCrosshair(cx2, cy2, 9.f, 2.f, 1.f, 1.f, 1.f, 0.88f);
                // Crosshair inner dot
                ui.pushQuad(cx2 - 1.5f, cy2 - 1.5f, 3.f, 3.f,
                    0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.6f);

                DrawHotbar(ui, static_cast<float>(W), static_cast<float>(H));
                DrawStatusBars(ui, static_cast<float>(W), static_cast<float>(H),
                    0.85f, 0.70f, 1.0f);
                DrawCompass(ui, static_cast<float>(W), yawDeg);
                DrawDebugPanel(ui, static_cast<float>(W), static_cast<float>(H),
                    camPos, fps, chunks, biome, handLightOn, sunTime);

                ui.flush(0);
                renderer.Present();
            }
        }

        animThread.join();
        renderer.cleanup();

        {
            std::lock_guard<std::mutex> lk(sceneMutex);
            for (auto it = loadedChunks.begin(); it != loadedChunks.end(); ++it)
                UnloadChunk(sm, it->second);
            loadedChunks.clear();
        }
    }

} // namespace HonHengine