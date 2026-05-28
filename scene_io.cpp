// scene_io.cpp — Shared scene serialisation / deserialisation.
// =============================================================================
// Implements SceneSave() and SceneLoad() declared in scene_io.h.
// Both functions operate on the engine's global scene state (g_namedObjects,
// g_namedLights, g_deferredGLTFTasks) which are defined in main.cpp.
//
// Color note: HonHon's Color stores channels as `double` in the 0–255 range.
// All channel reads/writes use `(double)` casts accordingly.
// =============================================================================

#include "scene_io.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <cctype>
#include <algorithm>

// Engine headers — needed to create Plane / Sphere / Rectangle / Light objects
#include "sceneManager.h"
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "pointlight.h"
#include "plane.h"
#include "sphere.h"
#include "rectangle.h"
#include "importer.h"
#include "material.h"
#include "script_manager.h"
#include "ide_asset_database.h"

// =============================================================================
//  Forward declarations of globals defined in main.cpp (global namespace)
// =============================================================================
extern std::shared_mutex g_sceneMutex;
extern std::unordered_map<std::string, HonHengine::BaseObject*> g_namedObjects;
extern std::unordered_map<std::string, HonHengine::BaseLight*>  g_namedLights;

// DeferredTask struct must match the one in main.cpp (global namespace)
struct DeferredTask {
    std::string name;
    std::string path;
    double x = 0, y = 0, z = 0;
};
extern std::vector<DeferredTask> g_deferredGLTFTasks;
extern std::mutex g_deferredTasksMutex;
extern struct ConsoleLog* g_deferredLog;  // from main.cpp

namespace HonHengine {
    namespace fs = std::filesystem;

    // =============================================================================
    //  Helper functions (mirrored from main.cpp)
    // =============================================================================
    static std::string JStr(const std::string& s) {
        std::string o; o.reserve(s.size() + 2); o += '"';
        for (char c : s) {
            if (c == '"') o += "\\\"";
            else if (c == '\\') o += "\\\\";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') o += "\\r";
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        o += '"'; return o;
    }

    static std::string JsonGet(const std::string& json, const std::string& key) {
        auto pos = json.find("\"" + key + "\":");
        if (pos == std::string::npos) return "";
        pos += key.size() + 3;
        if (pos >= json.size()) return "";
        if (json[pos] == '"') {
            ++pos; std::string v;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
                v += json[pos++];
            }
            return v;
        }
        std::string v;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') v += json[pos++];
        return v;
    }

    // Colour constants (mirrors the ones in main.cpp).
    static const Color kBlack{ 0.0, 0.0, 0.0, 255.0 };
    static const Color kWhite{ 255.0, 255.0, 255.0, 255.0 };

    // =============================================================================
    //  SceneSave
    // =============================================================================
    SceneSaveResult SceneSave(const std::string& filePath)
    {
        SceneSaveResult result;

        // Create parent directory if needed.
        try {
            auto parent = fs::path(filePath).parent_path();
            if (!parent.empty()) fs::create_directories(parent);
        }
        catch (const std::exception& e) {
            result.errorMsg = std::string("Cannot create directories: ") + e.what();
            return result;
        }

        // Serialise scene under a shared (read) lock.
        std::string sceneJson;
        {
            std::shared_lock<std::shared_mutex> lock(::g_sceneMutex);
            std::ostringstream oss;
            oss << "{\"objects\":[";
            bool firstObj = true;
            for (auto& pair : ::g_namedObjects) {
                const std::string& name = pair.first;
                BaseObject* obj = pair.second;
                if (!firstObj) oss << ",";
                firstObj = false;
                // Color channels are double in the 0–255 range.
                double cr = obj->material ? obj->material->color.r : 255.0;
                double cg = obj->material ? obj->material->color.g : 255.0;
                double cb = obj->material ? obj->material->color.b : 255.0;
                double ca = obj->material ? obj->material->color.a : 255.0;
                oss << "{\"name\":" << JStr(name)
                    << ",\"px\":" << obj->transform.position.x
                    << ",\"py\":" << obj->transform.position.y
                    << ",\"pz\":" << obj->transform.position.z
                    << ",\"sx\":" << obj->transform.scale.x
                    << ",\"sy\":" << obj->transform.scale.y
                    << ",\"sz\":" << obj->transform.scale.z
                    << ",\"rw\":" << obj->transform.rotation.w
                    << ",\"rx\":" << obj->transform.rotation.x
                    << ",\"ry\":" << obj->transform.rotation.y
                    << ",\"rz\":" << obj->transform.rotation.z
                    << ",\"visible\":" << (obj->visible ? "true" : "false")
                    << ",\"cr\":" << cr
                    << ",\"cg\":" << cg
                    << ",\"cb\":" << cb
                    << ",\"ca\":" << ca
                    << ",\"shader\":" << JStr(obj->render.shaderName)
                    << ",\"tag\":" << JStr(obj->tag)
                    << ",\"scripts\":[";
                bool firstScript = true;
                for (auto& sc : obj->scripts) {
                    if (!firstScript) oss << ",";
                    firstScript = false;
                    oss << JStr(sc.scriptGUID);
                }
                oss << "]}";
            }
            oss << "],\"lights\":[";
            bool firstLight = true;
            for (auto& pair : ::g_namedLights) {
                const std::string& name = pair.first;
                BaseLight* lt = pair.second;
                if (!firstLight) oss << ",";
                firstLight = false;
                std::string ltype = "point";
                float lpx = 0.f, lpy = 0.f, lpz = 0.f;
                if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                    ltype = "directional";
                    lpx = dl->transform.position.x;
                    lpy = dl->transform.position.y;
                    lpz = dl->transform.position.z;
                }
                else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                    lpx = pl->transform.position.x;
                    lpy = pl->transform.position.y;
                    lpz = pl->transform.position.z;
                }
                // Color channels are double in the 0–255 range.
                oss << "{\"name\":" << JStr(name)
                    << ",\"type\":" << JStr(ltype)
                    << ",\"intensity\":" << lt->intensity
                    << ",\"r\":" << lt->color.r
                    << ",\"g\":" << lt->color.g
                    << ",\"b\":" << lt->color.b
                    << ",\"px\":" << lpx
                    << ",\"py\":" << lpy
                    << ",\"pz\":" << lpz << "}";
            }
            oss << "]}";
            sceneJson = oss.str();
        }

        // Write to disk.
        std::ofstream sf(filePath);
        if (!sf.is_open()) {
            result.errorMsg = "Cannot write file: " + filePath;
            return result;
        }
        sf << sceneJson;
        sf.close();
        result.ok = true;
        return result;
    }

    // =============================================================================
    //  SceneLoad — internal helpers
    // =============================================================================

    // Walk a JSON string finding the next balanced { ... } element starting at or
    // after `pos`, handling nested brackets/braces correctly.
    // Returns {start, end} of the element (inclusive), or {npos, npos} if none.
    static std::pair<size_t, size_t> NextJsonObject(const std::string& json, size_t pos)
    {
        pos = json.find('{', pos);
        if (pos == std::string::npos) return { std::string::npos, std::string::npos };

        size_t depth = 0, end = pos;
        for (; end < json.size(); ++end) {
            char c = json[end];
            if (c == '{') ++depth;
            else if (c == '}') { if (--depth == 0) break; }
        }
        if (depth != 0) return { std::string::npos, std::string::npos }; // malformed
        return { pos, end };
    }

    // Parse the "scripts":["guid",...] sub-array from an object element string.
    static std::vector<std::string> ParseScriptGUIDs(const std::string& elem)
    {
        std::vector<std::string> guids;
        size_t arrStart = elem.find("\"scripts\":[");
        if (arrStart == std::string::npos) return guids;
        arrStart += 11; // skip past "scripts":[
        size_t arrEnd = elem.find(']', arrStart);
        if (arrEnd == std::string::npos) arrEnd = elem.size();
        const std::string arr = elem.substr(arrStart, arrEnd - arrStart);

        size_t p = 0;
        while (p < arr.size()) {
            size_t q1 = arr.find('"', p);
            if (q1 == std::string::npos) break;
            size_t q2 = arr.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string guid = arr.substr(q1 + 1, q2 - q1 - 1);
            if (!guid.empty()) guids.push_back(guid);
            p = q2 + 1;
        }
        return guids;
    }

    // =============================================================================
    //  SceneLoad
    // =============================================================================
    SceneLoadResult SceneLoad(const std::string& filePath,
        SceneManager* sm,
        AssetDatabase* assetDb,
        SceneManager* sceneMgr)
    {
        SceneLoadResult result;

        if (!sm) {
            result.errorMsg = "SceneManager is null";
            return result;
        }

        // Read file.
        std::ifstream f(filePath);
        if (!f.is_open()) {
            result.errorMsg = "Cannot open file: " + filePath;
            return result;
        }
        const std::string json((std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>());
        f.close();

        std::ostringstream warnings;

        // ── Objects ──────────────────────────────────────────────────────────────
        {
            size_t arrStart = json.find("\"objects\":[");
            if (arrStart != std::string::npos) {
                size_t pos = arrStart + 11; // skip past "objects":[
                // Stop scanning once we hit the lights array marker.
                size_t lightsMarker = json.find("\"lights\":[", pos);

                while (pos < json.size()) {
                    auto [elemStart, elemEnd] = NextJsonObject(json, pos);
                    if (elemStart == std::string::npos) break;
                    // Stop if this object starts after the lights array.
                    if (lightsMarker != std::string::npos && elemStart > lightsMarker) break;
                    pos = elemEnd + 1;

                    const std::string elem = json.substr(elemStart, elemEnd - elemStart + 1);

                    std::string name = JsonGet(elem, "name");
                    if (name.empty()) continue;
                    const std::string tag = JsonGet(elem, "tag");

                    // Transform
                    double px = 0, py = 0, pz = 0;
                    double sx = 1, sy = 1, sz = 1;
                    double rw = 1, rx = 0, ry = 0, rz = 0;
                    try { px = std::stod(JsonGet(elem, "px")); }
                    catch (...) {}
                    try { py = std::stod(JsonGet(elem, "py")); }
                    catch (...) {}
                    try { pz = std::stod(JsonGet(elem, "pz")); }
                    catch (...) {}
                    try { sx = std::stod(JsonGet(elem, "sx")); }
                    catch (...) {}
                    try { sy = std::stod(JsonGet(elem, "sy")); }
                    catch (...) {}
                    try { sz = std::stod(JsonGet(elem, "sz")); }
                    catch (...) {}
                    try { rw = std::stod(JsonGet(elem, "rw")); }
                    catch (...) {}
                    try { rx = std::stod(JsonGet(elem, "rx")); }
                    catch (...) {}
                    try { ry = std::stod(JsonGet(elem, "ry")); }
                    catch (...) {}
                    try { rz = std::stod(JsonGet(elem, "rz")); }
                    catch (...) {}

                    // Color — channels stored as double (0–255 range) in .honscene JSON.
                    double cr = 255, cg = 255, cb = 255, ca = 255;
                    try { cr = std::stod(JsonGet(elem, "cr")); }
                    catch (...) {}
                    try { cg = std::stod(JsonGet(elem, "cg")); }
                    catch (...) {}
                    try { cb = std::stod(JsonGet(elem, "cb")); }
                    catch (...) {}
                    try { ca = std::stod(JsonGet(elem, "ca")); }
                    catch (...) {}

                    const bool visible = (JsonGet(elem, "visible") != "false");
                    const std::string shaderName = JsonGet(elem, "shader");

                    // Color uses double.
                    Color col{ cr, cg, cb, ca };

                    // ── Reconstruct mesh from tag ──────────────────────────────
                    BaseObject* obj = nullptr;

                    if (tag.rfind("OBJ:", 0) == 0) {
                        // OBJ mesh — import synchronously (OK from any thread).
                        const std::string meshPath = tag.substr(4);
                        try {
                            obj = Importer::ImportFromOBJ(meshPath);
                        }
                        catch (const std::exception& e) {
                            warnings << "OBJ import failed for '" << name
                                << "': " << e.what() << "; ";
                        }
                        if (obj) obj->tag = tag;
                    }
                    else if (tag.rfind("GLTF:", 0) == 0) {
                        // GLTF — must be processed on the main GL thread.
                        // Push to the deferred queue; the main loop will finish it.
                        const std::string meshPath = tag.substr(5);
                        {
                            std::lock_guard<std::mutex> lk(::g_deferredTasksMutex);
                            ::g_deferredGLTFTasks.push_back({ name, meshPath, px, py, pz });
                        }
                        // Position/rotation will be applied by the deferred handler.
                        // Count it so the caller knows it was queued.
                        ++result.objCount;
                        continue;
                    }
                    else if (tag.find("sphere") != std::string::npos) {
                        // Sphere primitive — identified by tag content.
                        obj = new HonHengine::Sphere(
                            Vector3((float)sx, (float)sy, (float)sz),
                            Vector3((float)px, (float)py, (float)pz),
                            Quaternion(),
                            new Material(0, 0, col, kBlack));
                        obj->tag = tag;
                    }
                    else if (tag.find("rect") != std::string::npos) {
                        obj = new HonHengine::Rectangle(
                            Vector3((float)sx, (float)sy, (float)sz),
                            Vector3((float)px, (float)py, (float)pz),
                            Quaternion(),
                            new Material(0, 0, col, kBlack));
                        obj->tag = tag;
                    }
                    else {
                        // Default: Plane (covers hand-placed planes and any unknown primitives).
                        obj = new HonHengine::Plane(
                            Vector3((float)sx, (float)sy, (float)sz),
                            Vector3((float)px, (float)py, (float)pz),
                            Quaternion(),
                            new Material(0, 0, col, kBlack));
                        obj->tag = tag;
                    }

                    if (!obj) continue;

                    // Apply full transform.
                    obj->transform.position = Vector3((float)px, (float)py, (float)pz);
                    obj->transform.scale = Vector3((float)sx, (float)sy, (float)sz);
                    obj->transform.rotation = Quaternion((float)rw, (float)rx, (float)ry, (float)rz);
                    obj->visible = visible;
                    if (obj->material) obj->material->color = col;
                    if (!shaderName.empty()) obj->render.shaderName = shaderName;

                    // ── Scripts ───────────────────────────────────────────────
                    for (const std::string& guid : ParseScriptGUIDs(elem)) {
                        std::string srcPath;
                        if (assetDb) {
                            AssetRecord* rec = assetDb->FindByGUID(guid);
                            if (rec) srcPath = rec->path;
                        }
                        if (srcPath.empty()) srcPath = guid; // fallback: treat GUID as path

                        ScriptComponent newComp;
                        newComp.scriptGUID = guid;
                        ScriptManager* scriptMgr = sceneMgr ? sceneMgr->scriptManager.get() : nullptr;
                        if (scriptMgr) scriptMgr->LoadScript(guid, srcPath, newComp);
                        obj->scripts.push_back(newComp);
                    }

                    // ── Insert into global scene state ────────────────────────
                    {
                        std::unique_lock<std::shared_mutex> lock(::g_sceneMutex);
                        sm->objects->push_back(obj);
                        ::g_namedObjects[name] = obj;
                    }
                    ++result.objCount;
                }
            }
        }

        // ── Lights ───────────────────────────────────────────────────────────────
        {
            size_t arrStart = json.find("\"lights\":[");
            if (arrStart != std::string::npos) {
                size_t pos = arrStart + 10; // skip past "lights":[
                while (pos < json.size()) {
                    auto [elemStart, elemEnd] = NextJsonObject(json, pos);
                    if (elemStart == std::string::npos) break;
                    pos = elemEnd + 1;

                    const std::string elem = json.substr(elemStart, elemEnd - elemStart + 1);

                    std::string name = JsonGet(elem, "name");
                    if (name.empty()) continue;
                    const std::string ltype = JsonGet(elem, "type");

                    float intensity = 1.0f;
                    // Color channels stored as double (0–255) in JSON.
                    double lr = 255, lg = 255, lb = 255;
                    double lpx = 0, lpy = 0, lpz = 0;
                    try { intensity = std::stof(JsonGet(elem, "intensity")); }
                    catch (...) {}
                    try { lr = std::stod(JsonGet(elem, "r")); }
                    catch (...) {}
                    try { lg = std::stod(JsonGet(elem, "g")); }
                    catch (...) {}
                    try { lb = std::stod(JsonGet(elem, "b")); }
                    catch (...) {}
                    try { lpx = std::stod(JsonGet(elem, "px")); }
                    catch (...) {}
                    try { lpy = std::stod(JsonGet(elem, "py")); }
                    catch (...) {}
                    try { lpz = std::stod(JsonGet(elem, "pz")); }
                    catch (...) {}

                    // Color uses double.
                    Color lcol{ lr, lg, lb, 255.0 };

                    BaseLight* light = nullptr;
                    {
                        std::unique_lock<std::shared_mutex> lock(::g_sceneMutex);
                        if (ltype == "directional") {
                            auto* dl = new DirectionalLight(intensity, lcol,
                                Vector3((float)lpx, (float)lpy, (float)lpz), Quaternion());
                            light = dl;
                        }
                        else {
                            auto* pl = new PointLight(intensity, lcol,
                                Vector3((float)lpx, (float)lpy, (float)lpz));
                            light = pl;
                        }
                        sm->lights->push_back(light);
                        ::g_namedLights[name] = light;
                    }
                    ++result.lightCount;
                }
            }
        }

        std::string warnStr = warnings.str();
        if (!warnStr.empty()) result.warnings = warnStr;
        result.ok = true;
        return result;
    }
}