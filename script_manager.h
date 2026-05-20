#pragma once
#include "script_component.h"
#include <string>
#include <unordered_map>
#include <filesystem>
#include <vector>

namespace HonHengine {

    class SceneManager;

    class ScriptManager {
    public:
        ScriptManager(SceneManager* sm);
        ~ScriptManager();

        // Core functionality
        void Update(float deltaTime);
        bool LoadScript(const std::string& scriptGUID, const std::string& sourcePath, ScriptComponent& comp);
        void UnloadScript(ScriptComponent& comp);
        void HotReloadScript(const std::string& scriptGUID);
        void ApplyVariables(ScriptComponent& comp);

        // Toolchain configuration
        static void SetToolchainPath(const std::filesystem::path& toolchainPath);
        static void AddIncludePath(const std::filesystem::path& path);
        static void AddLibraryPath(const std::filesystem::path& path);
        static void AddLinkLibrary(const std::string& lib);
        static void SetEngineLibraryPath(const std::filesystem::path& path);
        static void SetSDL2Path(const std::filesystem::path& path);
        bool InitializeToolchain();

        // Compilation
        static bool CompileScript(const std::string& sourcePath, const std::string& compileCommand, std::string& outDllPath);

    private:
        SceneManager* sceneManager;
        std::unordered_multimap<std::string, ScriptComponent*> activeScripts;
        void DestroyInstance(ScriptComponent& comp);

        // Static configuration
        static std::filesystem::path toolchainPath;
        static std::filesystem::path compilerPath;
        static std::filesystem::path sdl2Path;
        static std::filesystem::path compiledEnginePath;
        static std::vector<std::filesystem::path> additionalIncludePaths;
        static std::vector<std::filesystem::path> additionalLibraryPaths;
        static std::vector<std::string> additionalLibraries;
        static std::string msvcEnvPrefix;
    };

} // namespace HonHengine