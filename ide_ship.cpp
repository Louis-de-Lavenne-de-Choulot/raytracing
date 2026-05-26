// ide_ship.cpp — HonHon Engine real shipping pipeline
// =============================================================================
#include "ide_ship.h"
#include "script_manager.h"  // for ScriptManager::Get*() toolchain accessors

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cstdio>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/wait.h>
#endif

namespace fs = std::filesystem;
namespace HonHengine {

    // =============================================================================
    //  Utility helpers
    // =============================================================================

    std::string ShipBuilder::QuotePath(const fs::path& p) const {
        return "\"" + p.string() + "\"";
    }

    // Run a shell command, capture stdout+stderr, return combined output.
    std::string ShipBuilder::RunCommand(const std::string& cmd, int& exitCode) const {
        std::string output;

#ifdef _WIN32
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE hRead, hWrite;
        if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
            exitCode = -1;
            return "CreatePipe failed";
        }
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hWrite;
        si.hStdError = hWrite;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        PROCESS_INFORMATION pi{};
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');

        if (!CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr,
            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hWrite);
            CloseHandle(hRead);
            exitCode = -1;
            return "CreateProcess failed";
        }
        CloseHandle(hWrite);

        char buf[4096];
        DWORD read;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0) {
            buf[read] = '\0';
            output += buf;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD ec;
        GetExitCodeProcess(pi.hProcess, &ec);
        exitCode = (int)ec;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hRead);
#else
        // POSIX: popen is simpler; capture stderr too via 2>&1
        FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
        if (!pipe) { exitCode = -1; return "popen failed"; }
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) output += buf;
        int status = pclose(pipe);
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
        return output;
    }

    std::string ShipBuilder::BuildIncludeFlags(const ShipConfig& cfg) const {
        std::string s;

        // Mirror ScriptManager::CompileScript include order exactly:
        // 1. engineRoot/include  (same as CompileScript line 188)
        fs::path engineRoot = fs::current_path();
        s += " -I" + QuotePath(engineRoot / "include");

        // 2. SDL2 include dir — derived from sdl2Path the same way the editor does:
        //    sdl2Path = tools/sdl2/x64/SDL2.dll  ->  include = tools/sdl2/include
        fs::path sdl2Path = ScriptManager::GetSDL2Path();
        if (!sdl2Path.empty()) {
            fs::path sdl2Inc = sdl2Path.parent_path().parent_path() / "include";
            if (fs::exists(sdl2Inc))
                s += " -I" + QuotePath(sdl2Inc);
            else {
                // Fallback: one level up (flat layout tools/sdl2/SDL2.dll)
                sdl2Inc = sdl2Path.parent_path() / "include";
                if (fs::exists(sdl2Inc))
                    s += " -I" + QuotePath(sdl2Inc);
            }
        }
        else if (!cfg.sdl2LibDir.empty()) {
            // Manual override from ShipConfig
            fs::path sdl2Inc = fs::path(cfg.sdl2LibDir).parent_path() / "include";
            if (fs::exists(sdl2Inc))
                s += " -I" + QuotePath(sdl2Inc);
        }

        // 3. additionalIncludePaths (same as CompileScript lines 191-194)
        for (const auto& inc : ScriptManager::GetIncludePaths())
            s += " -I" + QuotePath(inc);

        // 4. Any extra paths from ShipConfig (set manually in the Ship modal)
        for (auto& inc : cfg.includeDirs)
            s += " -I" + QuotePath(fs::path(inc));

        return s;
    }

    std::string ShipBuilder::BuildLibFlags(const ShipConfig& cfg) const {
        std::string s;

        // Mirror ScriptManager::CompileScript lib order exactly:
        // 1. additionalLibraryPaths (lines 199-201)
        for (const auto& lp : ScriptManager::GetLibraryPaths())
            s += " -L" + QuotePath(lp);

        // 2. GameEngine.a parent dir (lines 204-207)
        fs::path engLib = ScriptManager::GetEngineLibraryPath();
        if (engLib.empty() && !cfg.engineLibPath.empty())
            engLib = fs::path(cfg.engineLibPath);
        if (!engLib.empty() && fs::exists(engLib))
            s += " -L" + QuotePath(engLib.parent_path());

        fs::path sdl2Path = ScriptManager::GetSDL2Path();
        if (!sdl2Path.empty())
            s += " -L" + QuotePath(sdl2Path);
        else if (!cfg.sdl2LibDir.empty())
            s += " -L" + QuotePath(fs::path(cfg.sdl2LibDir));

        // 4. Extra lib dirs from ShipConfig
        for (auto& ld : cfg.libraryDirs)
            s += " -L" + QuotePath(fs::path(ld));

        // 5. Engine static lib (comes AFTER source objects, critical for symbol resolution)
        if (!engLib.empty() && fs::exists(engLib))
            s += " " + QuotePath(engLib);

        // 6. additionalLibraries
        for (const auto& lib : ScriptManager::GetLinkLibraries())
            s += " -l" + lib;

        // 7. ShipConfig extra libs
        for (auto& lib : cfg.linkLibraries)
            s += " -l" + lib;

        // 8. Platform system libs FIRST
#ifdef _WIN32
        s += " -lSDL2main -lSDL2 -lopengl32 -lgdi32 -lwinmm -limm32 -lversion -lole32 -loleaut32 -lsetupapi";
#else
        s += " -lSDL2 -lGL -ldl -lpthread";
#endif

        // 9. After-libs LAST (e.g. -lglad must follow GameEngine.a which references gladLoadGLLoader)
        for (const auto& lp : ScriptManager::GetLibraryPathsAfter())
            s += " -L" + QuotePath(lp);
        for (const auto& lib : ScriptManager::GetLinkLibrariesAfter())
            s += " -l" + lib;
        return s;
    }

    // =============================================================================
    //  Phase 1 — Collect script GUIDs from all .honscene files
    // =============================================================================
    // .honscene is JSON-ish.  Scripts are stored inside objects as:
    //   "scripts": ["guid1", "guid2"]
    // We do a simple substring scan — no full JSON parser needed.

    static std::vector<std::string> ParseScriptGUIDsFromScene(const fs::path& scenePath) {
        std::vector<std::string> guids;
        std::ifstream f(scenePath);
        if (!f) return guids;
        std::string text((std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());

        // Find every "scripts":[ ... ] block
        size_t pos = 0;
        while (true) {
            size_t arrStart = text.find("\"scripts\":[", pos);
            if (arrStart == std::string::npos) break;
            arrStart += 11; // skip past the [
            size_t arrEnd = text.find(']', arrStart);
            if (arrEnd == std::string::npos) break;

            std::string arr = text.substr(arrStart, arrEnd - arrStart);
            // Extract quoted strings from the array
            size_t p2 = 0;
            while (true) {
                size_t q1 = arr.find('"', p2);
                if (q1 == std::string::npos) break;
                size_t q2 = arr.find('"', q1 + 1);
                if (q2 == std::string::npos) break;
                std::string g = arr.substr(q1 + 1, q2 - q1 - 1);
                if (!g.empty()) guids.push_back(g);
                p2 = q2 + 1;
            }
            pos = arrEnd + 1;
        }
        return guids;
    }

    std::vector<std::string> ShipBuilder::CollectSceneScriptGUIDs(
        const std::string& projectDir,
        const std::string& /*honassetsPath*/,
        std::string& /*errorOut*/) const
    {
        std::unordered_set<std::string> seen;
        std::vector<std::string> guids;

        for (auto& entry : fs::recursive_directory_iterator(
            fs::path(projectDir),
            fs::directory_options::skip_permission_denied))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".honscene") continue;

            for (auto& g : ParseScriptGUIDsFromScene(entry.path())) {
                if (seen.insert(g).second)
                    guids.push_back(g);
            }
        }
        return guids;
    }

    // =============================================================================
    //  Phase 2 — Resolve GUIDs to source paths via .honassets
    // =============================================================================
    // .honassets is a simple JSON array of records:
    //   { "guid":"...", "path":"...", "type":4, ... }
    // type 4 == AssetType::Script (matches ide_asset_database.h enum).

    static const int kAssetTypeScript = 4;

    std::vector<ScriptSource> ShipBuilder::ResolveGUIDsToSources(
        const std::vector<std::string>& guids,
        const std::string& honassetsPath,
        std::string& errorOut) const
    {
        std::vector<ScriptSource> result;

        std::ifstream f(honassetsPath);
        if (!f) {
            errorOut = "Cannot open asset database: " + honassetsPath;
            return result;
        }
        std::string text((std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());

        // Build a map guid → path by scanning the JSON records
        std::unordered_map<std::string, std::string> guidToPath;
        std::unordered_map<std::string, int> guidToType;

        // Simple field extractor — finds "key":"value" near pos
        auto findField = [&](const std::string& src, const std::string& key, size_t from) -> std::pair<std::string, size_t> {
            size_t k = src.find("\"" + key + "\":", from);
            if (k == std::string::npos) return { "", std::string::npos };
            size_t v = src.find_first_not_of(" \t\r\n", k + key.size() + 3);
            if (v == std::string::npos) return { "", std::string::npos };
            if (src[v] == '"') {
                size_t end = src.find('"', v + 1);
                if (end == std::string::npos) return { "", std::string::npos };
                return { src.substr(v + 1, end - v - 1), end };
            }
            else {
                size_t end = v;
                while (end < src.size() && src[end] != ',' && src[end] != '}' && src[end] != ']')
                    ++end;
                return { src.substr(v, end - v), end };
            }
            };

        // Scan record objects
        size_t pos = 0;
        while (true) {
            size_t recStart = text.find('{', pos);
            if (recStart == std::string::npos) break;
            size_t recEnd = text.find('}', recStart);
            if (recEnd == std::string::npos) break;

            std::string rec = text.substr(recStart, recEnd - recStart + 1);
            auto [guid, _1] = findField(rec, "guid", 0);
            auto [path, _2] = findField(rec, "path", 0);
            auto [typeStr, _3] = findField(rec, "type", 0);

            if (!guid.empty() && !path.empty()) {
                guidToPath[guid] = path;
                try { guidToType[guid] = std::stoi(typeStr); }
                catch (...) {}
            }
            pos = recEnd + 1;
        }

        for (auto& g : guids) {
            auto it = guidToPath.find(g);
            if (it == guidToPath.end()) {
                errorOut += "Warning: GUID not found in asset database: " + g + "\n";
                continue;
            }
            if (guidToType[g] != kAssetTypeScript) continue;

            fs::path srcPath = fs::path(it->second);
            if (!fs::exists(srcPath)) {
                errorOut += "Warning: Script source not found: " + srcPath.string() + "\n";
                continue;
            }

            ScriptSource ss;
            ss.guid = g;
            ss.sourcePath = fs::absolute(srcPath).string();
            ss.className = srcPath.stem().string();
            result.push_back(std::move(ss));
        }
        return result;
    }

    // =============================================================================
    //  Phase 3 — Generate game_entry.cpp
    // =============================================================================
    // This file is the main() of the shipped game.  It:
    //   1. Forward-declares every script class (each must export CreateScript_ClassName).
    //   2. Builds a SceneManager, loads the serialised scene, attaches scripts.
    //   3. Creates a GPURenderer and calls RunGameLoop() (blocking).
    //
    // The generated file uses the same engine API the editor uses, so it compiles
    // cleanly against GameEngine.a without any dynamic-loading shims.

    bool ShipBuilder::WriteGameEntry(
        const ShipConfig& cfg,
        const std::vector<ScriptSource>& scripts,
        const fs::path& buildDir,
        fs::path& outEntryPath,
        std::string& errorOut) const
    {
        outEntryPath = buildDir / "game_entry.cpp";
        std::ofstream f(outEntryPath);
        if (!f) {
            errorOut = "Cannot write game_entry.cpp to " + outEntryPath.string();
            return false;
        }

        // ── Header ──────────────────────────────────────────────────────────────
        f << "// AUTO-GENERATED by HonHon Engine ShipBuilder — do not edit.\n"
            << "#define SDL_MAIN_HANDLED\n"
            << "#include <glad/glad.h>\n"  // must precede SDL.h to satisfy gladLoadGLLoader refs in GameEngine.a
            << "#include <SDL.h>\n"
            << "#include \"scenemanager.h\"\n"
            << "#include \"GPURenderer.h\"\n"
            << "#include \"camera.h\"\n"
            << "#include \"settings.h\"\n"
            << "#include \"IScript.h\"\n"
            << "#include <string>\n"
            << "#include <iostream>\n"
            << "#include <fstream>\n"
            << "#include <sstream>\n"
            << "#include <unordered_map>\n"
            << "#include <functional>\n\n"
            << "using namespace HonHengine;\n\n";

        // ── Script factory registry ──────────────────────────────────────────────
        // Each script .cpp must define:
        //   extern "C" IScript* CreateScript_<ClassName>() { return new ClassName(); }
        //   extern "C" void     DestroyScript_<ClassName>(IScript* s) { delete s; }
        // (ShipBuilder generates these declarations; scripts must implement them.)

        f << "// Script factory declarations\n";
        for (auto& ss : scripts) {
            f << "extern \"C\" IScript* CreateScript_" << ss.className << "();\n";
            f << "extern \"C\" void     DestroyScript_" << ss.className << "(IScript*);\n";
        }
        f << "\n";

        // ── Script factory map ───────────────────────────────────────────────────
        f << "static std::unordered_map<std::string,\n"
            << "    std::function<IScript*()>> g_scriptFactories = {\n";
        for (auto& ss : scripts) {
            f << "    { \"" << ss.guid << "\", []()->IScript*{ return CreateScript_"
                << ss.className << "(); } },\n";
        }
        f << "};\n\n";

        // ── Minimal scene loader ─────────────────────────────────────────────────
        // Parses the same .honscene JSON the editor writes, re-creates objects,
        // lights, and wires scripts through the factory map above.
        // A full scene loader lives in the engine; this is the bootstrap shim.
        f << R"CPP(
// ── Minimal scene bootstrap loader ──────────────────────────────────────────
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "pointlight.h"
#include "plane.h"
#include "sphere.h"
#include "material.h"
#include "script_component.h"
#include <vector>
#include <cstring>

static std::string jsonGet(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return {};
    pos += key.size() + 3;
    if (pos >= json.size()) return {};
    if (json[pos] == '"') {
        ++pos; std::string v;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos+1 < json.size()) ++pos;
            v += json[pos++];
        }
        return v;
    }
    std::string v;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
        v += json[pos++];
    return v;
}

static void LoadSceneFile(const std::string& path, SceneManager& sm,
    const std::unordered_map<std::string, std::function<IScript*()>>& factories)
{
    std::ifstream f(path);
    if (!f) { std::cerr << "[Game] Cannot open scene: " << path << "\n"; return; }
    std::string text((std::istreambuf_iterator<char>(f)), {});

    // ── Objects ──
    size_t pos = text.find("\"objects\":[");
    if (pos != std::string::npos) {
        pos += 11;
        while (true) {
            size_t ob = text.find('{', pos);
            size_t cb = text.find('}', ob == std::string::npos ? 0 : ob);
            if (ob == std::string::npos || cb == std::string::npos) break;
            if (ob > text.find("\"lights\":[", pos)) break; // past objects array
            std::string rec = text.substr(ob, cb - ob + 1);

            std::string name = jsonGet(rec, "name");
            if (name.empty()) { pos = cb + 1; continue; }

            float px = 0, py = 0, pz = 0;
            float sx = 1, sy = 1, sz = 1;
            try { px = std::stof(jsonGet(rec, "px")); } catch(...) {}
            try { py = std::stof(jsonGet(rec, "py")); } catch(...) {}
            try { pz = std::stof(jsonGet(rec, "pz")); } catch(...) {}
            try { sx = std::stof(jsonGet(rec, "sx")); } catch(...) {}
            try { sy = std::stof(jsonGet(rec, "sy")); } catch(...) {}
            try { sz = std::stof(jsonGet(rec, "sz")); } catch(...) {}
            float r = 255, g = 255, b = 255, a = 255;
            try { r = std::stof(jsonGet(rec, "cr")); } catch(...) {}
            try { g = std::stof(jsonGet(rec, "cg")); } catch(...) {}
            try { b = std::stof(jsonGet(rec, "cb")); } catch(...) {}
            try { a = std::stof(jsonGet(rec, "ca")); } catch(...) {}

            BaseObject* obj = new Plane(
                Vector3(sx, sy, sz), Vector3(px, py, pz), Quaternion(),
                new Material(0, 0, Color(r,g,b,a)));
            sm.objects->push_back(obj);

            // ── Scripts ──
            size_t sa = rec.find("\"scripts\":[");
            if (sa != std::string::npos) {
                sa += 11;
                size_t ea = rec.find(']', sa);
                std::string arr = rec.substr(sa, ea - sa);
                size_t p2 = 0;
                while (true) {
                    size_t q1 = arr.find('"', p2);
                    if (q1 == std::string::npos) break;
                    size_t q2 = arr.find('"', q1 + 1);
                    if (q2 == std::string::npos) break;
                    std::string guid = arr.substr(q1+1, q2-q1-1);
                    auto fit = factories.find(guid);
                    if (fit != factories.end()) {
                        ScriptComponent comp;
                        comp.scriptGUID = guid;
                        comp.instance   = fit->second();
                        if (comp.instance) comp.instance->Start();
                        obj->scripts.push_back(std::move(comp));
                    }
                    p2 = q2 + 1;
                }
            }
            pos = cb + 1;
        }
    }

    // ── Lights ──
    pos = text.find("\"lights\":[");
    if (pos != std::string::npos) {
        pos += 10;
        while (true) {
            size_t ob = text.find('{', pos);
            if (ob == std::string::npos) break;
            size_t cb = text.find('}', ob);
            if (cb == std::string::npos) break;
            std::string rec = text.substr(ob, cb - ob + 1);

            std::string ltype = jsonGet(rec, "type");
            float intensity = 1.f;
            float r = 255, g = 255, b = 255;
            float px = 0, py = 5, pz = 0;
            try { intensity = std::stof(jsonGet(rec, "intensity")); } catch(...) {}
            try { r = std::stof(jsonGet(rec, "r")); } catch(...) {}
            try { g = std::stof(jsonGet(rec, "g")); } catch(...) {}
            try { b = std::stof(jsonGet(rec, "b")); } catch(...) {}
            try { px = std::stof(jsonGet(rec, "px")); } catch(...) {}
            try { py = std::stof(jsonGet(rec, "py")); } catch(...) {}
            try { pz = std::stof(jsonGet(rec, "pz")); } catch(...) {}

            BaseLight* lt = nullptr;
            if (ltype == "directional") {
                auto* dl = new DirectionalLight(intensity, Color(r,g,b));
                dl->transform.rotation = Quaternion::LookRotation(Vector3(-0.5,-0.8,-0.3));
                lt = dl;
            } else {
                auto* pl = new PointLight(intensity, Color(r,g,b), Vector3(px,py,pz));
                lt = pl;
            }
            if (lt) sm.lights->push_back(lt);
            pos = cb + 1;
        }
    }
}
)CPP";

        // ── main() ──────────────────────────────────────────────────────────────
        f << "\nint main(int argc, char* argv[]) {\n"
            << "    SDL_SetMainReady();\n"
            << "    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {\n"
            << "        std::cerr << \"SDL_Init failed: \" << SDL_GetError() << \"\\n\";\n"
            << "        return 1;\n"
            << "    }\n\n"
            << "    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);\n"
            << "    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);\n"
            << "    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);\n"
            << "    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);\n"
            << "    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);\n\n"
            << "    SDL_Window* win = SDL_CreateWindow(\n"
            << "        \"" << cfg.gameName << "\",\n"
            << "        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,\n"
            << "        1280, 720,\n"
            << "        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);\n"
            << "    if (!win) { std::cerr << \"SDL_CreateWindow failed\\n\"; SDL_Quit(); return 1; }\n\n"
            << "    SDL_GLContext ctx = SDL_GL_CreateContext(win);\n"
            << "    SDL_GL_MakeCurrent(win, ctx);\n"
            << "    SDL_GL_SetSwapInterval(1);\n\n"
            << "    Settings::canvasWidth  = 1280;\n"
            << "    Settings::canvasHeight = 720;\n\n"
            << "    SceneManager sm;\n"
            << "    Camera* cam = new Camera(Vector3(0,5,15), Quaternion::LookRotation(Vector3(0,0,-1)));\n"
            << "    sm.cameras->push_back(cam);\n"
            << "    sm.currentCamera = cam;\n\n"
            << "    // Load entry scene\n"
            << "    std::string scenePath = \"" << cfg.entryScene << "\";\n"
            << "    // Allow override via first command-line argument\n"
            << "    if (argc >= 2) scenePath = argv[1];\n"
            << "    LoadSceneFile(scenePath, sm, g_scriptFactories);\n\n"
            << "    GPURenderer renderer(&sm, win, ctx);\n"
            << "    renderer.SetPlayMode(true);\n\n"
            << "    // ── Game loop ──\n"
            << "    bool quit = false;\n"
            << "    Uint32 prev = SDL_GetTicks();\n"
            << "    while (!quit) {\n"
            << "        SDL_Event e;\n"
            << "        while (SDL_PollEvent(&e)) {\n"
            << "            if (e.type == SDL_QUIT) quit = true;\n"
            << "        }\n"
            << "        Uint32 now = SDL_GetTicks();\n"
            << "        float dt = (now - prev) / 1000.f;\n"
            << "        prev = now;\n"
            << "        if (sm.scriptManager) sm.scriptManager->Update(dt);\n"
            << "        renderer.Render(dt);\n"
            << "        SDL_GL_SwapWindow(win);\n"
            << "    }\n\n"
            << "    SDL_GL_DeleteContext(ctx);\n"
            << "    SDL_DestroyWindow(win);\n"
            << "    SDL_Quit();\n"
            << "    return 0;\n"
            << "}\n";

        f.close();
        return true;
    }

    // =============================================================================
    //  Phase 4 — Compile scripts to .o (not .dll/.so)
    // =============================================================================
    // Each script .cpp must define:
    //   extern "C" IScript* CreateScript_ClassName() { return new ClassName(); }
    //   extern "C" void DestroyScript_ClassName(IScript* s) { delete s; }
    // This lets game_entry.cpp call them without dlopen.

    bool ShipBuilder::CompileScripts(
        const ShipConfig& cfg,
        const std::vector<ScriptSource>& scripts,
        const fs::path& buildDir,
        std::vector<fs::path>& outObjects,
        const ShipProgressCb& progress,
        std::atomic<bool>& cancelFlag,
        std::string& errorOut) const
    {
        std::string inc = BuildIncludeFlags(cfg);
        std::string opts = cfg.debugBuild ? " -g -O0" : " -O2 -DNDEBUG";
        std::string compiler = cfg.compilerPath.empty() ? ScriptManager::GetCompilerPath().string() : cfg.compilerPath;

        float base = 0.05f;
        float share = scripts.empty() ? 0.f : 0.40f / (float)scripts.size();

        for (size_t i = 0; i < scripts.size(); ++i) {
            if (cancelFlag) return false;

            auto& ss = scripts[i];
            fs::path obj = buildDir / (ss.className + ".o");

            progress(base + share * (float)i,
                "[Ship] Compiling " + ss.className + ".cpp ...", false);

            // -c = compile only (no link), -DHON_SHIP_BUILD skips DLL exports
            std::string cmd =
                "\"" + compiler + "\" -std=c++20 -c" + opts + inc +
                " -DHON_SHIP_BUILD" +
                " -o " + QuotePath(obj) +
                " " + QuotePath(fs::path(ss.sourcePath));

            int ec;
            std::string out = RunCommand(cmd, ec);
            if (ec != 0) {
                errorOut = "Script compile failed: " + ss.className + "\n" + out;
                return false;
            }
            outObjects.push_back(obj);
        }
        return true;
    }

    bool ShipBuilder::CompileGameEntry(
        const ShipConfig& cfg,
        const fs::path& entryPath,
        const fs::path& buildDir,
        fs::path& outObject,
        std::string& errorOut) const
    {
        outObject = buildDir / "game_entry.o";
        std::string inc = BuildIncludeFlags(cfg);
        std::string opts = cfg.debugBuild ? " -g -O0" : " -O2 -DNDEBUG";
        std::string compiler = cfg.compilerPath.empty() ? ScriptManager::GetCompilerPath().string() : cfg.compilerPath;

        std::string cmd =
            "\"" + compiler + "\" -std=c++20 -c" + opts + inc +
            " -DHON_SHIP_BUILD" +
            " -o " + QuotePath(outObject) +
            " " + QuotePath(entryPath);

#ifdef _WIN32
        cmd = "\"" + compiler + "\" -std=c++20 -c -mwindows" + opts + inc +
            " -DHON_SHIP_BUILD" +
            " -o " + QuotePath(outObject) +
            " " + QuotePath(entryPath);
#endif

        std::cout << "[CompileGameEntry] " << cmd << std::endl;

        int ec;
        std::string out = RunCommand(cmd, ec);
        if (ec != 0) {
            errorOut = "game_entry.cpp compile failed:\n" + out;
            return false;
        }
        return true;
    }

    // =============================================================================
    //  Phase 5 — Link into a single executable
    // =============================================================================
    bool ShipBuilder::LinkExecutable(
        const ShipConfig& cfg,
        const std::vector<fs::path>& objects,
        const fs::path& exePath,
        std::string& errorOut) const
    {
        std::string libs = BuildLibFlags(cfg);
        std::string compiler = cfg.compilerPath.empty() ? ScriptManager::GetCompilerPath().string() : cfg.compilerPath;
        std::string opts = cfg.debugBuild ? " -g" : " -O2 -s";

        std::string objs;
        for (auto& o : objects) objs += " " + QuotePath(o);

        // Static linking to avoid missing DLLs (libstdc++, libgcc, winpthread)
        std::string cmd =
            "\"" + compiler + "\" -std=c++20" + opts + " -static " +
            objs + libs + " -o " + QuotePath(exePath);

        int ec;
        std::cout << "[Ship Command] " << cmd << "\n";
        std::string out = RunCommand(cmd, ec);
        if (ec != 0) {
            errorOut = "Link failed:\n" + out;
            return false;
        }
        return true;
    }

    // =============================================================================
    //  Phase 6 — Copy assets
    // =============================================================================

    static const std::vector<std::string> kSkipDirs = {
        ".git","build","CMakeFiles",".vs","__pycache__","node_modules"
    };
    static const std::vector<std::string> kAssetExts = {
        ".png",".jpg",".jpeg",".bmp",".tga",".hdr",
        ".obj",".mtl",".gltf",".glb",".fbx",
        ".honscene",".honassets",".honproject",
        ".vert",".frag",".glsl",
        ".ttf",".otf",".wav",".ogg",".mp3"
    };

    size_t ShipBuilder::CopyAssets(
        const ShipConfig& cfg,
        const fs::path& outputDir,
        const ShipProgressCb& progress,
        std::string& errorOut) const
    {
        fs::path projectRoot(cfg.projectDir);
        fs::path sourceAssets = projectRoot / "assets";
        size_t copied = 0;

        // 1. Clean previous assets folder to avoid leftover files
        try {
            fs::remove_all(outputDir / "assets");
        }
        catch (...) {}

        // 2. Copy the project's assets/ folder
        if (fs::exists(sourceAssets) && fs::is_directory(sourceAssets)) {
            try {
                for (auto& entry : fs::recursive_directory_iterator(
                    sourceAssets, fs::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file()) continue;

                    fs::path rel = fs::relative(entry.path(), sourceAssets);
                    fs::path dest = outputDir / "assets" / rel;
                    fs::create_directories(dest.parent_path());
                    fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
                    ++copied;
                }
                progress(0.80f, "[Ship] Copied assets folder (" + std::to_string(copied) + " files)", false);
            }
            catch (const std::exception& e) {
                errorOut += "Asset copy error: " + std::string(e.what()) + "\n";
            }
        }
        else {
            errorOut += "Warning: source 'assets/' folder not found in project.\n";
            fs::create_directories(outputDir / "assets");
        }

        // 3. Copy scene and asset database files from project root
        for (const auto& entry : fs::directory_iterator(projectRoot)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            if (ext == ".honscene" || ext == ".honassets") {
                fs::path dest = outputDir / entry.path().filename();
                fs::copy_file(entry.path(), dest, fs::copy_options::overwrite_existing);
                ++copied;
                progress(0.82f, "[Ship] Copied " + entry.path().filename().string(), false);
            }
        }

        // 4. Copy SDL2.dll - construct proper DLL path from ScriptManager's SDL2 directory
#ifdef _WIN32
        {
            fs::path sdl2DllPath;

            // Priority 1: Use cfg.sdl2DllPath if provided (full path to SDL2.dll)
            if (!cfg.sdl2DllPath.empty() && fs::exists(cfg.sdl2DllPath) && fs::is_regular_file(cfg.sdl2DllPath)) {
                sdl2DllPath = cfg.sdl2DllPath;
            }
            // Priority 2: Use cfg.sdl2LibDir + "SDL2.dll"
            else if (!cfg.sdl2LibDir.empty() && fs::exists(cfg.sdl2LibDir)) {
                fs::path candidate = fs::path(cfg.sdl2LibDir) / "SDL2.dll";
                if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
                    sdl2DllPath = candidate;
                }
            }
            // Priority 3: Use ScriptManager's SDL2 path (which is a directory, not a file)
            else {
                fs::path smSDL2Dir = ScriptManager::GetSDL2Path();
                if (!smSDL2Dir.empty() && fs::exists(smSDL2Dir)) {
                    // Try common SDL2.dll locations within that directory
                    std::vector<fs::path> candidates = {
                        smSDL2Dir / "SDL2.dll",                           // direct
                        smSDL2Dir.parent_path() / "SDL2.dll",             // one level up
                        smSDL2Dir.parent_path().parent_path() / "SDL2.dll", // two levels up
                        smSDL2Dir / "x64" / "SDL2.dll",                   // typical vcpkg layout
                        smSDL2Dir / "lib" / "x64" / "SDL2.dll",           // typical MinGW layout
                    };
                    for (const auto& cand : candidates) {
                        if (fs::exists(cand) && fs::is_regular_file(cand)) {
                            sdl2DllPath = cand;
                            break;
                        }
                    }
                }
            }

            // Priority 4: Try common system locations
            if (sdl2DllPath.empty()) {
                fs::path exePath = fs::current_path();
                std::vector<fs::path> candidates = {
                    exePath / "tools" / "sdl2" / "x64" / "SDL2.dll",
                    exePath / "tools" / "sdl2" / "lib" / "x64" / "SDL2.dll",
                    exePath / "SDL2.dll",
                    "C:/msys64/mingw64/bin/SDL2.dll",
                    "C:/mingw64/bin/SDL2.dll"
                };
                for (const auto& cand : candidates) {
                    if (fs::exists(cand) && fs::is_regular_file(cand)) {
                        sdl2DllPath = cand;
                        break;
                    }
                }
            }

            if (!sdl2DllPath.empty() && fs::exists(sdl2DllPath) && fs::is_regular_file(sdl2DllPath)) {
                fs::path dllDest = outputDir / "SDL2.dll";
                try {
                    // If destination exists and is read-only, remove it first
                    if (fs::exists(dllDest)) {
                        fs::permissions(dllDest, fs::perms::owner_write, fs::perm_options::add);
                        fs::remove(dllDest);
                    }
                    fs::copy_file(sdl2DllPath, dllDest, fs::copy_options::overwrite_existing);
                    ++copied;
                    progress(0.85f, "[Ship] Copied SDL2.dll from " + sdl2DllPath.string(), false);
                }
                catch (const std::exception& e) {
                    errorOut += "Failed to copy SDL2.dll: " + std::string(e.what()) + "\n";
                }
            }
            else {
                errorOut += "Warning: SDL2.dll not found. The game may not run.\n";
                progress(0.85f, "[Ship] WARNING: SDL2.dll not found", false);
            }
        }
#endif

        // 5. Write launcher scripts
        {
            std::ofstream sh(outputDir / "RunGame.sh");
            sh << "#!/bin/bash\ncd \"$(dirname \"$0\")\"\n./" << cfg.gameName << " \"$@\"\n";
            sh.close();
#ifndef _WIN32
            std::system(("chmod +x \"" + (outputDir / "RunGame.sh").string() + "\"").c_str());
#endif

            std::ofstream bat(outputDir / "RunGame.bat");
            bat << "@echo off\ncd /d \"%~dp0\"\n" << cfg.gameName << ".exe %*\n";
            bat.close();

            progress(0.90f, "[Ship] Created launcher scripts", false);
        }

        return copied;
    }

    // =============================================================================
    //  Phase 7 — Optional archive
    // =============================================================================

    void ShipBuilder::CreateArchive(
        const fs::path& outputDir,
        const std::string& gameName,
        const ShipProgressCb& progress) const
    {
        progress(0.97f, "[Ship] Creating archive...", false);
        std::string archiveName = gameName + "_dist.tar.gz";
        fs::path archivePath = outputDir.parent_path() / archiveName;
        std::string cmd = "tar -czf \"" + archivePath.string() +
            "\" -C \"" + outputDir.parent_path().string() +
            "\" \"" + outputDir.filename().string() + "\"";
        std::system(cmd.c_str());
    }

    // =============================================================================
    //  Top-level Build()
    // =============================================================================
    ShipResult ShipBuilder::Build(
        const ShipConfig& cfg,
        const ShipProgressCb& progress,
        std::atomic<bool>& cancelFlag)
    {
        ShipResult result;

        auto fail = [&](const std::string& msg) -> ShipResult {
            result.success = false;
            result.errorLog = msg;
            progress(1.f, "[Ship] FAILED: " + msg, true);
            return result;
            };

        progress(0.0f, "[Ship] Starting build for '" + cfg.gameName + "'...", false);

        // ── Validate config ──────────────────────────────────────────────────────
        if (cfg.projectDir.empty() || !fs::exists(cfg.projectDir))
            return fail("Project directory not found: " + cfg.projectDir);
        if (cfg.outputDir.empty())
            return fail("Output directory not specified.");
        if (cfg.entryScene.empty())
            return fail("Entry scene not specified.");

        fs::path outputDir = fs::path(cfg.outputDir);
        fs::path buildDir = outputDir / ".build_tmp";
        try {
            fs::create_directories(buildDir);
            fs::create_directories(outputDir);
        }
        catch (const std::exception& e) {
            return fail(std::string("Cannot create output dirs: ") + e.what());
        }

        // ── Phase 1: Collect script GUIDs ────────────────────────────────────────
        progress(0.02f, "[Ship] Scanning scenes for scripts...", false);
        std::string honassets = cfg.projectDir + "/.honassets";
        std::string warnings;
        std::vector<std::string> guids = CollectSceneScriptGUIDs(cfg.projectDir, honassets, warnings);
        if (!warnings.empty()) progress(0.03f, "[Ship] " + warnings, false);

        progress(0.04f,
            "[Ship] Found " + std::to_string(guids.size()) + " script GUID(s) in scenes.", false);

        // ── Phase 2: Resolve GUIDs → sources ─────────────────────────────────────
        std::string resolveErr;
        std::vector<ScriptSource> scripts = ResolveGUIDsToSources(guids, honassets, resolveErr);
        if (!resolveErr.empty()) progress(0.05f, "[Ship] " + resolveErr, false);
        progress(0.05f,
            "[Ship] Resolved " + std::to_string(scripts.size()) + " script source(s).", false);

        if (cancelFlag) return fail("Cancelled by user.");

        // ── Phase 3: Compile scripts ──────────────────────────────────────────────
        std::vector<fs::path> objects;
        std::string compileErr;
        if (!scripts.empty()) {
            if (!CompileScripts(cfg, scripts, buildDir, objects, progress, cancelFlag, compileErr))
                return fail(compileErr);
        }
        result.errorLog = resolveErr; // carry warnings

        if (cancelFlag) return fail("Cancelled by user.");

        // ── Phase 4: Generate & compile game_entry.cpp ───────────────────────────
        progress(0.48f, "[Ship] Generating game_entry.cpp...", false);
        fs::path entryPath;
        std::string entryErr;
        if (!WriteGameEntry(cfg, scripts, buildDir, entryPath, entryErr))
            return fail(entryErr);

        progress(0.50f, "[Ship] Compiling game_entry.cpp...", false);
        fs::path entryObj;
        std::string entryCompErr;
        if (!CompileGameEntry(cfg, entryPath, buildDir, entryObj, entryCompErr))
            return fail(entryCompErr);
        objects.push_back(entryObj);

        if (cancelFlag) return fail("Cancelled by user.");

        // ── Phase 5: Link ─────────────────────────────────────────────────────────
        progress(0.65f, "[Ship] Linking executable...", false);
#ifdef _WIN32
        fs::path exePath = outputDir / (cfg.gameName + ".exe");
#else
        fs::path exePath = outputDir / cfg.gameName;
#endif
        std::string linkErr;
        if (!LinkExecutable(cfg, objects, exePath, linkErr))
            return fail(linkErr);

        result.executablePath = exePath.string();
        progress(0.75f, "[Ship] Executable: " + exePath.string(), false);

        if (cancelFlag) return fail("Cancelled by user.");

        // ── Phase 6: Copy assets ──────────────────────────────────────────────────
        progress(0.78f, "[Ship] Copying assets...", false);
        fs::remove_all(outputDir / "assets");
        std::string assetErr;
        result.filesWritten = CopyAssets(cfg, outputDir, progress, assetErr);
        if (!assetErr.empty()) progress(0.90f, "[Ship] Asset warnings: " + assetErr, false);

        // ── Phase 7: Archive ──────────────────────────────────────────────────────
        if (cfg.createArchive)
            CreateArchive(outputDir, cfg.gameName, progress);

        // ── Cleanup build temp ────────────────────────────────────────────────────
        try { fs::remove_all(buildDir); }
        catch (...) {}

        progress(1.0f,
            "[Ship] Done!  " + std::to_string(result.filesWritten) + " assets, exe: " + exePath.string(),
            false);

        result.success = true;
        return result;
    }

} // namespace HonHengine