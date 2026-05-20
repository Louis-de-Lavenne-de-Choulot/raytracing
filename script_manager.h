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
        static bool CompileScript(const std::string& sourcePath, const std::string& compileCommand, std::string& outDllPath);
        void ApplyVariables(ScriptComponent& comp);

        // Set the path to your bundled toolchain
        static void SetToolchainPath(const std::filesystem::path& toolchainPath);

    private:
        SceneManager* sceneManager;
        std::unordered_multimap<std::string, ScriptComponent*> activeScripts;
        void DestroyInstance(ScriptComponent& comp);

        static std::filesystem::path toolchainPath;
        static std::filesystem::path compilerPath;
        static bool InitializeToolchain();
    };
}