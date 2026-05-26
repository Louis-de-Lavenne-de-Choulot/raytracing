#include "script_manager.h"
#include "scenemanager.h"
#include "baseobject.h"
#include <iostream>
#include <filesystem>
#include <fstream>
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
    std::vector<std::filesystem::path> ScriptManager::additionalLibraryPathsAfter;
    std::vector<std::string>           ScriptManager::additionalLibrariesAfter;
    std::string ScriptManager::msvcEnvPrefix;

    ScriptManager::ScriptManager(SceneManager* sm) : sceneManager(sm) {
        if (compilerPath.empty()) {
            fs::path exePath = fs::current_path();
            fs::path bundledCompiler = exePath / "tools" / "mingw64" / "bin" / "g++.exe";

            // Use MinGW compiler
            fs::path mingwCompiler = exePath / "tools" / "mingw64" / "bin" / "g++.exe";
            sdl2Path = exePath / "tools" / "sdl2" / "x64" / "SDL2.dll";

            // Use MinGW .a library
            fs::path mingwLib = exePath / "tools" / "honhengine" / "GameEngine.a";

            if (fs::exists(mingwLib)) {
                compiledEnginePath = mingwLib;
                std::cout << "Using MinGW engine library: " << compiledEnginePath << std::endl;
            }
            else {
                std::cerr << "Warning: MinGW engine library not found at " << mingwLib << std::endl;
            }

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

        // Normalize for display
        auto normalizePath = [](const fs::path& p) -> std::string {
            std::string path = p.string();
            std::replace(path.begin(), path.end(), '\\', '/');
            return path;
            };

        if (!fs::exists(compilerPath)) {
            std::cerr << "Warning: Compiler not found at " << normalizePath(compilerPath) << std::endl;
            compilerPath = "g++"; // Fallback to system
        }
        else {
            std::cout << "Toolchain set to: " << normalizePath(compilerPath) << std::endl;
        }
    }

    void ScriptManager::AddIncludePath(const fs::path& path) {

        fs::path fullPath = fs::absolute(path);
        if (fs::exists(fullPath)) {
            additionalIncludePaths.push_back(fullPath);
            std::cout << "Added include path: " << fullPath << std::endl;
        }
        else {
            std::cerr << "Warning: Include path does not exist: " << fullPath << std::endl;
        }
    }

    void ScriptManager::AddLibraryPath(const fs::path& path) {
        fs::path fullPath = fs::absolute(path);
        if (fs::exists(fullPath)) {
            additionalLibraryPaths.push_back(fullPath);
            std::cout << "Added library path: " << fullPath << std::endl;
        }
        else {
            std::cerr << "Warning: Library path does not exist: " << fullPath << std::endl;
        }
    }

    void ScriptManager::AddLinkLibrary(const std::string& lib) {
        additionalLibraries.push_back(lib);
        std::cout << "Added link library: " << lib << std::endl;
    }

    void ScriptManager::AddLibraryPathAfter(const std::filesystem::path& path) {
        additionalLibraryPathsAfter.push_back(fs::absolute(path));
    }

    void ScriptManager::AddLinkLibraryAfter(const std::string& lib) {
        additionalLibrariesAfter.push_back(lib);
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
        if (!fs::exists(compilerPath)) {
            std::cerr << "Compiler not found: " << compilerPath << std::endl;
            return false;
        }

        // Test the compiler using CreateProcess
        std::string testCmd = "\"" + compilerPath.string() + "\" --version";
        int result = std::system(testCmd.c_str());

        if (result != 0) {
            std::cerr << "Compiler test failed" << std::endl;
            return false;
        }

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

        // Build include paths - ORDER MATTERS! Root first, then engine include, then user paths
        std::string includes;

        // Add engine include folder
        includes += " -I\"" + (engineRoot / "include").string() + "\"";

        // Add user additional include paths
        for (const auto& inc : additionalIncludePaths) {
            std::cout << "Adding include path: " << inc << std::endl;
            includes += " -I\"" + inc.string() + "\"";
        }

        // Build library paths
        std::string libpaths;

        for (const auto& libPath : additionalLibraryPaths) {
            libpaths += " -L\"" + libPath.string() + "\"";
        }

        // Add the directory containing GameEngine.a
        if (!compiledEnginePath.empty() && fs::exists(compiledEnginePath)) {
            fs::path libDir = compiledEnginePath.parent_path();
            libpaths += " -L\"" + libDir.string() + "\"";
        }

        // Build link libraries
        std::string linkLibs;

        // Add additional libraries
        for (const auto& lib : additionalLibraries) {
            linkLibs += " -l" + lib;
        }

        // Build command line
        std::string cmdLine;
        if (compileCommand.empty()) {
#ifdef _WIN32
            // CRITICAL FIX: Engine library MUST come AFTER the source file
            // This allows the linker to resolve symbols from the script first,
            // then pull in required object files from the static library
            cmdLine = "\"" + absCompilerPath.string() + "\" -std=c++20 -shared" +
                includes + libpaths +
                " -o \"" + absOutPath.string() + "\" \"" + absSourcePath.string() + "\"";

            // Add engine library AFTER the source file (critical for symbol resolution)
            if (!compiledEnginePath.empty() && fs::exists(compiledEnginePath)) {
                cmdLine += " \"" + compiledEnginePath.string() + "\"";
            }

            // Add other link libraries
            cmdLine += linkLibs;
#else
            cmdLine = "\"" + absCompilerPath.string() + "\" -std=c++20 -shared -fPIC" +
                includes + libpaths +
                " -o \"" + absOutPath.string() + "\" \"" + absSourcePath.string() + "\"";

            // Add engine library AFTER the source file (critical for symbol resolution)
            if (!compiledEnginePath.empty() && fs::exists(compiledEnginePath)) {
                cmdLine += " \"" + compiledEnginePath.string() + "\"";
            }

            // Add other link libraries
            cmdLine += linkLibs;
#endif
        }
        else {
            cmdLine = compileCommand;
        }

        std::cout << "Compilation command: " << cmdLine << std::endl;

        if (!fs::exists(absCompilerPath)) {
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
            TRUE,                   // bInheritHandles - important for pipe
            CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi
        );

        // Close the write end of the pipe in the parent process
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
        // On non-Windows, use system()
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
        comp.scriptGUID = sourcePath; // Store the source path for hot reload

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

        // Call Start() after everything is set up
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
            // Remove from activeScripts map
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

        // Apply all exposed variables to the script instance
        for (auto& var : comp.exposedVars) {
            if (var.dirty) {
                // TODO: Actually apply the variable values to the script instance
                // This requires reflection or a way to set variables by name
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

        // Get the source path from the first script component
        const ScriptComponent* first = range.first->second;
        const std::string sourcePath = first->scriptGUID;

        std::string newDllPath;
        if (!CompileScript(sourcePath, "", newDllPath)) {
            std::cerr << "Failed to compile script for hot reload: " << scriptGUID << std::endl;
            return;
        }

        // Collect all components to reload
        std::vector<ScriptComponent*> toReload;
        for (auto it = range.first; it != range.second; ++it) {
            toReload.push_back(it->second);
        }

        // Reload each component
        for (ScriptComponent* comp : toReload) {
            // Save exposed variables
            std::vector<ScriptVariable> savedVars = comp->exposedVars;

            // Find the owner object
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

            // Unload old script
            UnloadScript(*comp);

            // Load new script
            if (LoadScript(scriptGUID, sourcePath, *comp)) {
                // Restore saved variables
                comp->exposedVars = std::move(savedVars);
                ApplyVariables(*comp);

                // Notify script of reload
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