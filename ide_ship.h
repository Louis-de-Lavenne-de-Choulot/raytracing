#pragma once
// ide_ship.h — HonHon Engine real shipping pipeline
// =============================================================================
// Replaces the old "copy files" ShipGame() with a proper compile-and-link build:
//
//   1. Discover every .cpp script attached to any object in every .honscene
//      file inside the project.
//   2. Compile each script source into a .o translation unit (not a shared lib).
//   3. Produce a generated "game_entry.cpp" that:
//        • Includes every script class header / TU
//        • Instantiates a SceneManager, loads the serialised scene, wires up
//          scripts, and hands control to GPURenderer::RunGameLoop().
//   4. Link everything into a single standalone executable against
//        GameEngine.a (the renderer/engine static lib) + SDL2 + GL.
//   5. Copy assets (textures, meshes, .honscene files) next to the executable.
//
// The renderer is a static library (GameEngine.a / libGameEngine.a) referenced
// through ScriptManager::compiledEnginePath — the exact same lib the editor
// uses to hot-reload scripts.  Scripts are now compiled as regular .o files
// and linked in, so no DLL/dlopen machinery exists at runtime.
// =============================================================================

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <atomic>

namespace HonHengine {

    // ---------------------------------------------------------------------------
    // Progress callback — called from the worker thread; must be thread-safe.
    // ---------------------------------------------------------------------------
    using ShipProgressCb = std::function<void(float progress,   // 0..1
        const std::string& msg,
        bool isError)>;

    // ---------------------------------------------------------------------------
    // Per-script source file discovered in the project.
    // ---------------------------------------------------------------------------
    struct ScriptSource {
        std::string guid;         // asset GUID (from .honassets)
        std::string sourcePath;   // absolute path to the .cpp file
        std::string className;    // deduced class name (stem of file)
    };

    // ---------------------------------------------------------------------------
    // Snapshot of a scene object for baked scene generation.
    // ---------------------------------------------------------------------------
    struct TextureLayerSnapshot {
        std::string texName;
        std::string texPath;
        float tilingU = 1, tilingV = 1, offsetU = 0, offsetV = 0;
        float blendWeight = 1.f;
        int   blendMode = 0;
    };

    struct ObjectSnapshot {
        std::string name;
        std::string kind;  // "Plane","Sphere","Rectangle","OBJ","GLTF","Base"
        std::string assetPath; // for OBJ/GLTF
        float px = 0, py = 0, pz = 0;
        float sx = 1, sy = 1, sz = 1;
        float rx = 0, ry = 0, rz = 0, rw = 1;
        bool visible = true;
        std::string tag;
        // Material
        bool hasMaterial = false;
        float specularity = 0, reflectivity = 0;
        float cr = 1, cg = 1, cb = 1, ca = 1;
        std::vector<TextureLayerSnapshot> textureLayers;
        // Scripts
        std::vector<std::string> scriptGUIDs;
    };

    struct LightSnapshot {
        std::string name;
        std::string kind; // "directional","point","base"
        float intensity = 1.f;
        float r = 255, g = 255, b = 255;
        float px = 0, py = 5, pz = 0;
        float qx = 0, qy = 0, qz = 0, qw = 1;
    };

    // ---------------------------------------------------------------------------
    // Snapshot of a scene camera (position, orientation, fov) for baked scene.
    // ---------------------------------------------------------------------------
    struct CameraSnapshot {
        std::string name;
        float px = 0, py = 5, pz = 15;
        float rx = 0, ry = 0, rz = 0, rw = 1;
        float fov = 60.f;
    };

    // ---------------------------------------------------------------------------
    // Snapshot of skybox state for baked scene.
    // mode: 0 = procedural, 1 = HDR, 2 = cubemap
    // ---------------------------------------------------------------------------
    struct SkyboxSnapshot {
        int mode = 0;
        std::string hdrPath;
        std::string faces[6];
        float sunIntensity = 0.8f;
        float sunColor[3] = { 1.f, 0.95f, 0.8f };
    };

    // ---------------------------------------------------------------------------
    // Build configuration filled in by the Ship modal UI.
    // ---------------------------------------------------------------------------
    struct ShipConfig {
        std::string projectDir;       // root folder of the project
        std::string outputDir;        // where to write the dist/
        std::string gameName = "Game";// executable name (no extension)
        std::string entryScene;       // relative path of the first .honscene to load
        std::vector<std::string> selectedScenes;

        // Toolchain (mirrors ScriptManager's static fields)
        std::string compilerPath;     // e.g. "tools/mingw64/bin/g++.exe" or "g++"
        std::string engineLibPath;    // path to GameEngine.a / libGameEngine.a
        std::string sdl2LibDir;       // dir containing SDL2.lib / libSDL2.a
        std::string sdl2DllPath;      // SDL2.dll to copy (Windows only)
        std::vector<std::string> includeDirs;
        std::vector<std::string> libraryDirs;
        std::vector<std::string> linkLibraries; // e.g. {"SDL2","opengl32","GameEngine"}

        bool debugBuild = false;
        bool createArchive = true;

        // ── Baked scene options ──────────────────────────────────────────────────
        bool bakeSceneToCpp = false;              // Generate baked_scene.cpp instead of JSON load
        std::vector<CameraSnapshot> cameras;      // Cameras captured from the live scene
        SkyboxSnapshot skybox;                    // Skybox state captured from the live scene
        std::vector<ObjectSnapshot> sceneObjects; // Objects captured from the live scene
        std::vector<LightSnapshot>  sceneLights;  // Lights captured from the live scene
    };

    // ---------------------------------------------------------------------------
    // Result of a completed build.
    // ---------------------------------------------------------------------------
    struct ShipResult {
        bool success = false;
        std::string executablePath;
        std::string errorLog;
        size_t filesWritten = 0;
    };

    // ---------------------------------------------------------------------------
    // ShipBuilder — stateless, all work done in Build().
    // Call from a detached thread; progress comes through the callback.
    // ---------------------------------------------------------------------------
    class ShipBuilder {
    public:
        // Synchronous build — call from a background thread.
        ShipResult Build(const ShipConfig& cfg,
            const ShipProgressCb& progress,
            std::atomic<bool>& cancelFlag);

    private:
        // --- Phase helpers ---

        // Scan every .honscene in projectDir and collect the script GUIDs used.
        std::vector<std::string> CollectSceneScriptGUIDs(
            const std::string& projectDir,
            const std::string& honassetsPath,
            std::string& errorOut,
            const std::vector<std::string>& selectedScenes = {}) const;

        // Resolve GUIDs → ScriptSource via .honassets database.
        std::vector<ScriptSource> ResolveGUIDsToSources(
            const std::vector<std::string>& guids,
            const std::string& honassetsPath,
            std::string& errorOut) const;

        // Compile all script .cpp files to .o inside buildDir.
        // Returns false on first error (sets errorOut).
        bool CompileScripts(
            const ShipConfig& cfg,
            const std::vector<ScriptSource>& scripts,
            const std::filesystem::path& buildDir,
            std::vector<std::filesystem::path>& outObjects,
            const ShipProgressCb& progress,
            std::atomic<bool>& cancelFlag,
            std::string& errorOut) const;

        // Write the generated game_entry.cpp that bootstraps the game.
        bool WriteGameEntry(
            const ShipConfig& cfg,
            const std::vector<ScriptSource>& scripts,
            const std::filesystem::path& buildDir,
            std::filesystem::path& outEntryPath,
            std::string& errorOut) const;

        // Generate baked_scene.cpp that reconstructs the live scene in C++.
        bool WriteSceneCpp(const ShipConfig& cfg,
            const std::filesystem::path& buildDir,
            const std::vector<ScriptSource>& scripts,
            std::filesystem::path& outSceneCppPath,
            std::string& errorOut) const;

        // Compile game_entry.cpp → game_entry.o
        bool CompileGameEntry(
            const ShipConfig& cfg,
            const std::filesystem::path& entryPath,
            const std::filesystem::path& buildDir,
            std::filesystem::path& outObject,
            std::string& errorOut) const;

        // Link all .o files + engine lib into the final executable.
        bool LinkExecutable(
            const ShipConfig& cfg,
            const std::vector<std::filesystem::path>& objects,
            const std::filesystem::path& exePath,
            std::string& errorOut) const;

        // Copy runtime assets (textures, meshes, scenes, DLLs) to outputDir.
        size_t CopyAssets(
            const ShipConfig& cfg,
            const std::filesystem::path& outputDir,
            const ShipProgressCb& progress,
            std::string& errorOut) const;

        // Create a .tar.gz / .zip archive next to outputDir.
        void CreateArchive(
            const std::filesystem::path& outputDir,
            const std::string& gameName,
            const ShipProgressCb& progress) const;

        // --- Utilities ---
        std::string RunCommand(const std::string& cmd, int& exitCode) const;
        std::string QuotePath(const std::filesystem::path& p) const;
        std::string BuildIncludeFlags(const ShipConfig& cfg) const;
        std::string BuildLibFlags(const ShipConfig& cfg) const;
    };

} // namespace HonHengine