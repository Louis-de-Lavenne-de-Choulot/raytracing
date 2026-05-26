#pragma once
#include "script_component.h"
#include <string>
#include <unordered_map>
#include <filesystem>

namespace HonHengine {
    class SceneManager;

    class ScriptManager {
    public:
        ScriptManager(SceneManager* sm);
        ~ScriptManager();

        void Update(float deltaTime);
        bool LoadScript(const std::string& scriptGUID, const std::string& sourcePath, ScriptComponent& comp);
        void UnloadScript(ScriptComponent& comp);
        void HotReloadScript(const std::string& scriptGUID);
        void ApplyVariables(ScriptComponent& comp);

        // Set the path to your bundled toolchain
        static void SetToolchainPath(const std::filesystem::path& toolchainPath);
        static void AddIncludePath(const std::filesystem::path& path);
        static void AddLibraryPath(const std::filesystem::path& path);
        static void AddLinkLibrary(const std::string& lib);
        static void SetEngineLibraryPath(const std::filesystem::path& path);
        static void SetSDL2Path(const std::filesystem::path& path);
        bool InitializeToolchain();

        // Libs / lib-dirs that must be linked AFTER GameEngine.a so they can
        // resolve symbols that the engine references (e.g. glad, freetype).
        // Populated in main.cpp alongside the regular toolchain setup.
        static void AddLibraryPathAfter(const std::filesystem::path& path);
        static void AddLinkLibraryAfter(const std::string& lib);

        // Compilation
        static bool CompileScript(const std::string& sourcePath, const std::string& compileCommand, std::string& outDllPath);

        // Read-only accessors for the resolved toolchain paths.
        // Used by ShipBuilder so it compiles game_entry.cpp with the exact
        // same flags (includes, libs, compiler) as hot-reload script compilation.
        static const std::filesystem::path& GetToolchainPath() { return toolchainPath; }
        static const std::filesystem::path& GetCompilerPath() { return compilerPath; }
        static const std::filesystem::path& GetEngineLibraryPath() { return compiledEnginePath; }
        static const std::filesystem::path& GetSDL2Path() { return sdl2Path; }
        static const std::vector<std::filesystem::path>& GetIncludePaths() { return additionalIncludePaths; }
        static const std::vector<std::filesystem::path>& GetLibraryPaths() { return additionalLibraryPaths; }
        static const std::vector<std::string>& GetLinkLibraries() { return additionalLibraries; }
        static const std::vector<std::filesystem::path>& GetLibraryPathsAfter() { return additionalLibraryPathsAfter; }
        static const std::vector<std::string>& GetLinkLibrariesAfter() { return additionalLibrariesAfter; }

    private:
        SceneManager* sceneManager;
        std::unordered_multimap<std::string, ScriptComponent*> activeScripts;
        void DestroyInstance(ScriptComponent& comp);

        static std::filesystem::path toolchainPath;
        static std::filesystem::path compilerPath;
        static std::filesystem::path sdl2Path;
        static std::filesystem::path compiledEnginePath;
        static std::vector<std::filesystem::path> additionalIncludePaths;
        static std::vector<std::filesystem::path> additionalLibraryPaths;
        static std::vector<std::string>           additionalLibraries;
        static std::string                        msvcEnvPrefix;

        // Libs that must come after GameEngine.a in the link order
        static std::vector<std::filesystem::path> additionalLibraryPathsAfter;
        static std::vector<std::string>           additionalLibrariesAfter;
    };
}