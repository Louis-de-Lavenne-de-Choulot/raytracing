#include "script_manager.h"
#include "scenemanager.h"
#include "baseobject.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;
namespace HonHengine {

    // Static member initialization
    fs::path ScriptManager::toolchainPath;
    fs::path ScriptManager::compilerPath;
    fs::path ScriptManager::sdl2Path;
    fs::path ScriptManager::compiledEnginePath;
    std::vector<fs::path> ScriptManager::additionalIncludePaths;
    std::vector<fs::path> ScriptManager::additionalLibraryPaths;
    std::vector<std::string> ScriptManager::additionalLibraries;
    std::string ScriptManager::msvcEnvPrefix;

    ScriptManager::ScriptManager(SceneManager* sm) : sceneManager(sm) {
        if (compilerPath.empty()) {
            fs::path exePath = fs::current_path();

            // Use MinGW compiler
            fs::path mingwCompiler = exePath / "tools" / "mingw64" / "bin" / "g++.exe";
            sdl2Path = exePath / "tools" / "sdl2" / "x64" / "SDL2.dll";
            compiledEnginePath = exePath / "tools" / "honhengine" / "GameEngine.lib";

            if (fs::exists(mingwCompiler)) {
                compilerPath = mingwCompiler;
                std::cout << "Using MinGW compiler: " << compilerPath << std::endl;
            }
            else {
                compilerPath = "g++";
                std::cout << "Using system compiler: g++" << std::endl;
            }
        }
    }

    void ScriptManager::SetToolchainPath(const fs::path& path) {
        toolchainPath = path;

        // Resolve the full absolute path
        fs::path fullPath = fs::absolute(toolchainPath);
        compilerPath = fullPath / "bin" / "g++.exe";

        auto normalizePath = [](const fs::path& p) -> std::string {
            std::string path = p.string();
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
            };

        if (!fs::exists(compilerPath)) {
            std::cerr << "Warning: Compiler not found at " << normalizePath(compilerPath) << std::endl;
            compilerPath = "g++";
        }
        else {
            std::cout << "Toolchain set to: " << normalizePath(compilerPath) << std::endl;
        }
    }

    void ScriptManager::AddIncludePath(const fs::path& path) {
        if (fs::exists(path)) {
            additionalIncludePaths.push_back(path);
            std::cout << "Added include path: " << path << std::endl;
        }
        else {
            std::cerr << "Warning: Include path does not exist: " << path << std::endl;
        }
    }

    void ScriptManager::AddLibraryPath(const fs::path& path) {
        if (fs::exists(path)) {
            additionalLibraryPaths.push_back(path);
            std::cout << "Added library path: " << path << std::endl;
        }
        else {
            std::cerr << "Warning: Library path does not exist: " << path << std::endl;
        }
    }

    void ScriptManager::AddLinkLibrary(const std::string& lib) {
        additionalLibraries.push_back(lib);
        std::cout << "Added link library: " << lib << std::endl;
    }

    void ScriptManager::SetEngineLibraryPath(const fs::path& path) {
        compiledEnginePath = path;
        if (fs::exists(compiledEnginePath)) {
            std::cout << "Engine library set to: " << compiledEnginePath << std::endl;
        }
        else {
            std::cerr << "Warning: Engine library not found at: " << compiledEnginePath << std::endl;
        }
    }

    void ScriptManager::SetSDL2Path(const fs::path& path) {
        sdl2Path = path;
        if (fs::exists(sdl2Path)) {
            std::cout << "SDL2 path set to: " << sdl2Path << std::endl;
        }
        else {
            std::cerr << "Warning: SDL2 not found at: " << sdl2Path << std::endl;
        }
    }

    bool ScriptManager::InitializeToolchain() {
        if (compilerPath != "g++" && !fs::exists(compilerPath)) {
            std::cerr << "Compiler not found: " << compilerPath << std::endl;
            return false;
        }

        // Test the compiler
        std::string testCmd = "\"" + compilerPath.string() + "\" --version";
        int result = std::system(testCmd.c_str());

        if (result != 0) {
            std::cerr << "Compiler test failed" << std::endl;
            return false;
        }

        std::cout << "Toolchain initialized successfully" << std::endl;
        return true;
    }

    ScriptManager::~ScriptManager() {
        for (auto& [guid, comp] : activeScripts) {
            if (comp && comp->instance) {
                comp->instance->OnDestroy();
                delete comp->instance;
                comp->instance = nullptr;
            }
            if (comp->libHandle) {
#ifdef _WIN32
                FreeLibrary((HMODULE)comp->libHandle);
#else
                dlclose(comp->libHandle);
#endif
                comp->libHandle = nullptr;
            }
        }
        activeScripts.clear();
    }

    bool ScriptManager::CompileScript(const std::string& sourcePath, const std::string& compileCommand, std::string& outDllPath) {
        fs::path src = sourcePath;
        fs::path dllDir = src.parent_path() / "build";
        fs::create_directories(dllDir);
        std::string baseName = src.stem().string();

#ifdef _WIN32
        outDllPath = (dllDir / (baseName + ".dll")).string();
#else
        outDllPath = (dllDir / (baseName + ".so")).string();
#endif

        // Get absolute paths
        fs::path engineRoot = fs::current_path();
        fs::path absSourcePath = fs::absolute(src);
        fs::path absOutPath = fs::absolute(outDllPath);
        fs::path absCompilerPath = fs::absolute(compilerPath);

        // Build include paths
        std::string includes;
        includes += " -I\"" + (engineRoot / "include").string() + "\"";
        includes += " -I\"" + (engineRoot / "src").string() + "\"";

        // Add vcpkg include path
        fs::path vcpkgInclude = engineRoot / "vcpkg" / "installed" / "x64-windows" / "include";
        if (fs::exists(vcpkgInclude)) {
            includes += " -I\"" + vcpkgInclude.string() + "\"";
        }

        // Add additional include paths
        for (const auto& includePath : additionalIncludePaths) {
            if (fs::exists(includePath)) {
                includes += " -I\"" + includePath.string() + "\"";
            }
        }

        // Build library paths
        std::string libpaths;

        // Add MinGW lib path
        fs::path mingwLib = engineRoot / "tools" / "mingw64" / "lib";
        if (fs::exists(mingwLib)) {
            libpaths += " -L\"" + mingwLib.string() + "\"";
        }

        // Add additional library paths
        for (const auto& libPath : additionalLibraryPaths) {
            if (fs::exists(libPath)) {
                libpaths += " -L\"" + libPath.string() + "\"";
            }
        }

        // Build link libraries
        std::string linkLibs;

        // Link against engine library
        if (!compiledEnginePath.empty() && fs::exists(compiledEnginePath)) {
            linkLibs += " \"" + compiledEnginePath.string() + "\"";
        }

        // Add additional libraries
        for (const auto& lib : additionalLibraries) {
            linkLibs += " -l" + lib;
        }

        // Build command line for MinGW
        std::string cmdLine;
        if (compileCommand.empty()) {
#ifdef _WIN32
            cmdLine = "\"" + absCompilerPath.string() + "\" -std=c++20 -shared" +
                includes + libpaths + linkLibs +
                " -o \"" + absOutPath.string() + "\" \"" + absSourcePath.string() + "\"";
#else
            cmdLine = "\"" + absCompilerPath.string() + "\" -std=c++20 -shared -fPIC" +
                includes + libpaths + linkLibs +
                " -o \"" + absOutPath.string() + "\" \"" + absSourcePath.string() + "\"";
#endif
        }
        else {
            cmdLine = compileCommand;
        }

        std::cout << "Compilation command: " << cmdLine << std::endl;

        if (compilerPath != "g++" && !fs::exists(absCompilerPath)) {
            std::cerr << "Compiler does not exist: " << absCompilerPath << std::endl;
            return false;
        }

#ifdef _WIN32
        // Use CreateProcess with pipe to capture output
        SECURITY_ATTRIBUTES sa = { sizeof(sa) };
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            std::cerr << "Failed to create pipe" << std::endl;
            return false;
        }

        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        // Create a modifiable copy of the command line
        std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
        cmdBuffer.push_back('\0');

        BOOL success = CreateProcessA(
            NULL,
            cmdBuffer.data(),
            NULL, NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi
        );

        CloseHandle(hWritePipe);

        if (!success) {
            std::cerr << "CreateProcess failed with error: " << GetLastError() << std::endl;
            CloseHandle(hReadPipe);
            return false;
        }

        // Read the compiler output
        char buffer[4096];
        std::string compilerOutput;
        DWORD bytesRead;

        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            compilerOutput += buffer;
        }

        // Wait for process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);

        // Clean up
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe);

        // Display compiler output
        if (!compilerOutput.empty()) {
            std::cout << "Compiler output:\n" << compilerOutput << std::endl;
        }

        if (exitCode != 0) {
            std::cerr << "Compilation failed with code: " << exitCode << std::endl;
            return false;
        }
#else
        int result = std::system(cmdLine.c_str());
        if (result != 0) {
            std::cerr << "Compilation failed with code: " << result << std::endl;
            return false;
        }
#endif

        return true;
    }

    bool ScriptManager::LoadScript(const std::string& scriptGUID, const std::string& sourcePath, ScriptComponent& comp) {
        if (comp.libHandle) UnloadScript(comp);
        std::string dllPath;
        if (!CompileScript(sourcePath, "", dllPath)) return false;
        comp.compiledPath = dllPath;
        comp.scriptGUID = sourcePath;

#ifdef _WIN32
        HMODULE lib = LoadLibraryA(dllPath.c_str());
        if (!lib) {
            std::cerr << "Failed to load library: " << dllPath << " (Error: " << GetLastError() << ")" << std::endl;
            return false;
        }
        comp.libHandle = lib;
        comp.createScript = (IScript * (*)())GetProcAddress(lib, "CreateScript");
        comp.destroyScript = (void(*)(IScript*))GetProcAddress(lib, "DestroyScript");
#else
        void* lib = dlopen(dllPath.c_str(), RTLD_NOW);
        if (!lib) {
            std::cerr << "Failed to load library: " << dllPath << " (Error: " << dlerror() << ")" << std::endl;
            return false;
        }
        comp.libHandle = lib;
        comp.createScript = (IScript * (*)())dlsym(lib, "CreateScript");
        comp.destroyScript = (void(*)(IScript*))dlsym(lib, "DestroyScript");
#endif

        if (!comp.createScript || !comp.destroyScript) {
            std::cerr << "Failed to get script functions from library" << std::endl;
            UnloadScript(comp);
            return false;
        }

        comp.instance = comp.createScript();
        if (!comp.instance) {
            std::cerr << "Failed to create script instance" << std::endl;
            UnloadScript(comp);
            return false;
        }

        ApplyVariables(comp);

        if (comp.instance) {
            comp.instance->Start();
        }

        activeScripts.insert({ scriptGUID, &comp });
        return true;
    }

    void ScriptManager::UnloadScript(ScriptComponent& comp) {
        if (comp.instance) {
            comp.instance->OnDestroy();
            comp.destroyScript(comp.instance);
            comp.instance = nullptr;
        }
        if (comp.libHandle) {
            for (auto it = activeScripts.begin(); it != activeScripts.end(); ) {
                if (it->second == &comp) {
                    it = activeScripts.erase(it);
                }
                else {
                    ++it;
                }
            }

#ifdef _WIN32
            FreeLibrary((HMODULE)comp.libHandle);
#else
            dlclose(comp.libHandle);
#endif
            comp.libHandle = nullptr;
            comp.createScript = nullptr;
            comp.destroyScript = nullptr;
        }
    }

    void ScriptManager::ApplyVariables(ScriptComponent& comp) {
        if (!comp.instance) return;

        for (auto& var : comp.exposedVars) {
            if (var.dirty) {
                // TODO: Apply variable values to script instance
                var.dirty = false;
            }
        }
    }

    void ScriptManager::Update(float deltaTime) {
        if (!sceneManager || !sceneManager->objects) return;

        auto& objs = *sceneManager->objects;
        for (size_t i = 0; i < objs.size(); ++i) {
            BaseObject* obj = objs[i];
            if (!obj) continue;

            for (size_t j = 0; j < obj->scripts.size(); ++j) {
                ScriptComponent& scriptComp = obj->scripts[j];
                if (scriptComp.instance) {
                    scriptComp.instance->Update(deltaTime);
                }
            }
        }
    }

    void ScriptManager::HotReloadScript(const std::string& scriptGUID) {
        auto range = activeScripts.equal_range(scriptGUID);
        if (range.first == range.second) {
            std::cerr << "No active scripts found for GUID: " << scriptGUID << std::endl;
            return;
        }

        const ScriptComponent* first = range.first->second;
        const std::string sourcePath = first->scriptGUID;

        std::string newDllPath;
        if (!CompileScript(sourcePath, "", newDllPath)) {
            std::cerr << "Failed to compile script for hot reload: " << scriptGUID << std::endl;
            return;
        }

        std::vector<ScriptComponent*> toReload;
        for (auto it = range.first; it != range.second; ++it) {
            toReload.push_back(it->second);
        }

        for (ScriptComponent* comp : toReload) {
            std::vector<ScriptVariable> savedVars = comp->exposedVars;

            BaseObject* owner = nullptr;
            if (sceneManager && sceneManager->objects) {
                auto& objs = *sceneManager->objects;
                for (size_t oi = 0; oi < objs.size() && !owner; ++oi) {
                    BaseObject* candidate = objs[oi];
                    if (!candidate) continue;

                    for (size_t si = 0; si < candidate->scripts.size(); ++si) {
                        if (&candidate->scripts[si] == comp) {
                            owner = candidate;
                            break;
                        }
                    }
                }
            }

            if (!owner) {
                std::cerr << "Could not find owner for script component during hot reload" << std::endl;
                continue;
            }

            UnloadScript(*comp);

            if (LoadScript(scriptGUID, sourcePath, *comp)) {
                comp->exposedVars = std::move(savedVars);
                ApplyVariables(*comp);

                if (comp->instance) {
                    comp->instance->OnReload();
                }

                std::cout << "Hot reload successful for script: " << scriptGUID << std::endl;
            }
            else {
                std::cerr << "Failed to load script during hot reload: " << scriptGUID << std::endl;
            }
        }
    }
}