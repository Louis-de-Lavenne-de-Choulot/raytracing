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
// Build configuration filled in by the Ship modal UI.
// ---------------------------------------------------------------------------
struct ShipConfig {
    std::string projectDir;       // root folder of the project
    std::string outputDir;        // where to write the dist/
    std::string gameName = "Game";// executable name (no extension)
    std::string entryScene;       // relative path of the first .honscene to load

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
        std::string& errorOut) const;

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
