// main.cpp  —  HonHon Engine IDE (version corrigée, éclairage fiable)
// =============================================================================
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <queue>
#include <deque>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <cstring>
#include <cstdio>
#include <shared_mutex>
#include <cctype>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
//included from path since vcpkg cannot find it
#include "imgui_impl_sdl2.h"
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>    

#include "GPURenderer.h"
#include "sceneManager.h"
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "pointlight.h"
#include "plane.h"
#include "sphere.h"
#include "rectangle.h"
#include "importer.h"
#include "settings.h"
#include "basicmovements.h"
#include "textureManager.h"
#include "animation.h"
#include "material.h"
#include "skinnedShader.h"
#include "script_manager.h"

// ── Project & Asset Management ────────────────────────────────────────────────
#include "ide_asset_database.h"
#include "ide_asset_browser.h"
#include "ide_package_manager.h"
#include "ide_theme.h"
#include "ide_global_search.h"
#include "ide_toast.h"
#include "ide_editor_settings.h"
#include "ide_undo_history.h"
#include "ide_layout.h"
#include "ide_progress.h"
#include "ide_project.h"
#include "ide_icons.h"
#include "ide_selection.h"
#include "ide_gizmo.h"
#include "ide_viewport_overlays.h"
#include "ide_camera_bookmarks.h"


using namespace HonHengine;

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Hierarchy drag & drop — lets users drag objects/lights from the Hierarchy
//  panel and drop them onto the Viewport to reposition them.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* kHierarchyDragPayload = "HONHON_HIERARCHY_ITEM";

struct HierarchyDragPayload {
    char name[128] = {};  // scene object / light name
    bool isLight = false; // true = BaseLight, false = BaseObject
};


struct IDEObject {
    std::string name; float px = 0, py = 0, pz = 0; float sx = 1, sy = 1, sz = 1; float rw = 1, rx = 0, ry = 0, rz = 0; bool visible = true; bool locked = false; std::string shader; std::string tag; int cr = 255, cg = 255, cb = 255, ca = 255;

    BaseObject toBaseObject() const {
        BaseObject obj;
        obj.transform.position = Vector3(px, py, pz);
        obj.transform.scale = Vector3(sx, sy, sz);
        obj.transform.rotation = Quaternion(rw, rx, ry, rz);
        obj.visible = visible;
        if (!shader.empty()) obj.render.shaderName = shader;
        if (!tag.empty()) obj.tag = tag;
        obj.material = new Material(0, 0, Color((double)cr, (double)cg, (double)cb, (double)ca));
        return obj;
    }
};
struct IDELight {
    std::string name; std::string lightType = "point"; float intensity = 1.f; int r = 255, g = 245, b = 209; float px = 0, py = 0, pz = 0; float rotx = 0, roty = 0, rotz = 0;

    BaseLight* toBaseLight() const {
        BaseLight* lt = nullptr;
        if (lightType == "directional") {
            auto* dl = new DirectionalLight();
            dl->transform.rotation = Quaternion::FromEuler(rotx, roty, rotz);
            lt = dl;
        }
        else {
            auto* pl = new PointLight();
            pl->transform.position = Vector3(px, py, pz);
            lt = pl;
        }
        lt->intensity = intensity;
        lt->color = Color((double)r, (double)g, (double)b);
        return lt;
    }
};


// =============================================================================
//  Name Validation & Auto-Sanitization (creates valid names automatically)
// =============================================================================
namespace NameValidator {
    // Reserved names that cannot be used
    static const std::unordered_set<std::string> ReservedNames = {
        "scene", "camera", "light", "object", "root", "none", "null", "undefined",
        "main", "editor", "game", "world", "level", "player", "system"
    };

    // Check if a name is valid (for internal use)
    static bool IsValidInternal(const std::string& n) {
        if (n.empty()) return false;
        if (!std::isalpha(static_cast<unsigned char>(n[0])) && n[0] != '_') return false;
        for (char c : n) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                return false;
        }
        std::string lower = n;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (ReservedNames.count(lower)) return false;
        return true;
    }

    // Sanitize a name (always returns a valid name)
    std::string Sanitize(const std::string& n) {
        if (n.empty()) return "Object";
        std::string result;
        result.reserve(n.size());
        // First character: must be letter or underscore
        if (n.size() > 0) {
            if (std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')
                result += n[0];
            else
                result += '_';
        }
        // Remaining characters: only alnum, _, -
        for (size_t i = 1; i < n.size(); ++i) {
            char c = n[i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                result += c;
            else if (c == ' ' || c == '\t')
                result += '_';
            // Skip other invalid chars (don't add anything)
        }
        if (result.empty()) result = "Object";

        // Check against reserved names
        std::string lowerResult = result;
        std::transform(lowerResult.begin(), lowerResult.end(), lowerResult.begin(), ::tolower);
        if (ReservedNames.count(lowerResult))
            result = "_" + result;

        return result;
    }

    // Generate a unique name (automatically handles duplicates)
    std::string MakeUnique(const std::string& base,
        const std::vector<IDEObject>& objects,
        const std::vector<IDELight>& lights) {
        std::string candidate = Sanitize(base);
        if (candidate.empty()) candidate = "Object";

        // Check if name exists in either vector
        auto nameExists = [&](const std::string& name) -> bool {
            for (const auto& obj : objects) {
                if (obj.name == name) return true;
            }
            for (const auto& lt : lights) {
                if (lt.name == name) return true;
            }
            return false;
            };

        if (!nameExists(candidate)) return candidate;

        int counter = 1;
        while (true) {
            std::string attempt = candidate + "_" + std::to_string(counter);
            if (!nameExists(attempt)) return attempt;
            ++counter;
        }
    }

    std::string MakeUnique(const std::string& base,
        const std::unordered_map<std::string, BaseObject*>& objects,
        const std::unordered_map<std::string, BaseLight*>& lights) {
        std::string candidate = Sanitize(base);
        if (candidate.empty()) candidate = "Object";

        // Check if name exists in either map
        auto nameExists = [&](const std::string& name) -> bool {
            return objects.find(name) != objects.end() ||
                lights.find(name) != lights.end();
            };

        if (!nameExists(candidate)) return candidate;

        int counter = 1;
        while (true) {
            std::string attempt = candidate + "_" + std::to_string(counter);
            if (!nameExists(attempt)) return attempt;
            ++counter;
        }
    }

    // Get final name (sanitized + unique) - one call does it all
    std::string GetFinalName(const std::string& requested,
        const std::unordered_map<std::string, BaseObject*>& objects,
        const std::unordered_map<std::string, BaseLight*>& lights) {
        return MakeUnique(Sanitize(requested), objects, lights);
    }

    // Get final name (sanitized + unique) - one call does it all
    std::string GetFinalName(const std::string& requested,
        const std::vector<IDEObject>& objects,
        const std::vector<IDELight>& lights) {
        return MakeUnique(Sanitize(requested), objects, lights);
    }
}

// =============================================================================
//  Mutex global protégeant les listes d'objets et de lumières
// =============================================================================
std::shared_mutex g_sceneMutex;

struct DeferredTask {
    std::string name;        // for the response
    std::string path;
    double x = 0, y = 0, z = 0;
};

static std::vector<DeferredTask> g_deferredGLTFTasks;
struct ConsoleLog;
static std::mutex g_deferredTasksMutex;
static ConsoleLog* g_deferredLog = nullptr;

// =============================================================================
//  Fonctions JSON utilitaires (inchangées)
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
static std::string MakeResponse(bool ok, const std::string& cmd, const std::string& msg, const std::string& extra = "") {
    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false") << ",\"cmd\":" << JStr(cmd) << ",\"msg\":" << JStr(msg);
    if (!extra.empty()) o << "," << extra;
    o << "}"; return o.str();
}
static std::string JVec3(const char* key, double x, double y, double z) {
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "\"" << key << "\":{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z << "}";
    return o.str();
}
static std::vector<std::string> Tokenize(const std::string& line) {
    std::istringstream ss(line); std::vector<std::string> t; std::string tok;
    while (ss >> tok) t.push_back(tok); return t;
}
static bool ParseDouble(const std::string& s, double& out) { try { out = std::stod(s); return true; } catch (...) { return false; } }
static bool ParseInt(const std::string& s, int& out) { try { out = std::stoi(s); return true; } catch (...) { return false; } }
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

static bool JsonOk(const std::string& json) { return JsonGet(json, "ok") == "true"; }

// =============================================================================
//  Couleurs nommées
// =============================================================================
static const Color colWhite{ 255,255,255,255 };
static const Color colBlack{ 0,0,0,255 };
static const Color colRed{ 220,60,60,255 };
static const Color colGreen{ 60,180,60,255 };
static const Color colBlue{ 60,100,220,255 };
static const Color colYellow{ 230,200,40,255 };
static const Color colGray{ 140,140,140,255 };
static const Color colOrange{ 230,130,40,255 };
static const Color colPurple{ 150,60,200,255 };
static const Color colCyan{ 60,200,210,255 };
static Color NameToColor(const std::string& n) {
    if (n == "white") return colWhite; if (n == "black") return colBlack;
    if (n == "red") return colRed; if (n == "green") return colGreen;
    if (n == "blue") return colBlue; if (n == "yellow") return colYellow;
    if (n == "gray") return colGray; if (n == "orange") return colOrange;
    if (n == "purple") return colPurple; if (n == "cyan") return colCyan;
    return colWhite;
}
static Quaternion EulerToQuat(double pitch, double yaw, double roll) {
    double p = pitch * M_PI / 180.0 * 0.5, y = yaw * M_PI / 180.0 * 0.5, r = roll * M_PI / 180.0 * 0.5;
    double cp = cos(p), sp = sin(p), cy = cos(y), sy = sin(y), cr = cos(r), sr = sin(r);
    return Quaternion(cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy);
}

// =============================================================================
//  Registres globaux (protégés par g_sceneMutex)
// =============================================================================
static std::unordered_map<std::string, BaseObject*> g_namedObjects;
static std::unordered_map<std::string, BaseLight*>  g_namedLights;

// =============================================================================
//  CommandRegistry et command handlers (tous avec verrouillage)
// =============================================================================

struct CmdContext {
    SceneManager* sm = nullptr;
    GPURenderer* renderer = nullptr;
    std::shared_mutex* sceneMutex = &g_sceneMutex;
};

struct CommandRegistry {
    struct Entry {
        std::string syntax, description;
        std::function<void(CmdContext&, const std::vector<std::string>&, std::ostream&)> handler;
    };
    std::unordered_map<std::string, Entry> cmds;
    void Register(const std::string& name, const std::string& syntax, const std::string& desc,
        std::function<void(CmdContext&, const std::vector<std::string>&, std::ostream&)> fn) {
        cmds[name] = { syntax, desc, std::move(fn) };
    }
    void Dispatch(const std::string& line, CmdContext& ctx, std::ostream& out);
    void PrintHelp(std::ostream& out) const;
};


void CommandRegistry::Dispatch(const std::string& line, CmdContext& ctx, std::ostream& out) {
    auto tokens = Tokenize(line);
    if (tokens.empty()) return;
    std::string cmdName = tokens[0];
    tokens.erase(tokens.begin());
    auto it = cmds.find(cmdName);
    if (it == cmds.end()) {
        out << MakeResponse(false, cmdName, "Unknown command. Type 'help'.") << "\n";
        return;
    }
    it->second.handler(ctx, tokens, out);
}

void CommandRegistry::PrintHelp(std::ostream& out) const {
    out << "{\"ok\":true,\"cmd\":\"help\",\"msg\":\"";
    for (auto& [n, e] : cmds) out << e.syntax << "  —  " << e.description << "\\n";
    out << "\"}\n";
}

static void BuildCommands(CommandRegistry& reg, GPURenderer* renderer,
    AssetDatabase* assetDb, SceneManager* sceneMgr) {
    // help
    reg.Register("help", "help", "List all commands.",
        [&reg](CmdContext&, const std::vector<std::string>&, std::ostream& out) { reg.PrintHelp(out); });
    // echo
    reg.Register("echo", "echo <text...>", "Echo arguments back.",
        [](CmdContext&, const std::vector<std::string>& args, std::ostream& out) {
            std::string m; for (size_t i = 0; i < args.size(); ++i) m += (i ? " " : "") + args[i];
            out << MakeResponse(true, "echo", m, "\"echo\":" + JStr(m)) << "\n";
        });
    // list (lecture seule mais verrou quand même)
    reg.Register("list", "list", "List all named objects and lights.",
        [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out) {
            std::shared_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << "\"objects\":[";
            bool first = true;
            for (auto& [name, obj] : g_namedObjects) {
                if (!first) d << ","; first = false;
                d << "{\"name\":" << JStr(name)
                    << ",\"visible\":" << (obj->visible ? "true" : "false")
                    << ",\"pos\":{\"x\":" << obj->transform.position.x
                    << ",\"y\":" << obj->transform.position.y
                    << ",\"z\":" << obj->transform.position.z << "}"
                    << ",\"scale\":{\"x\":" << obj->transform.scale.x
                    << ",\"y\":" << obj->transform.scale.y
                    << ",\"z\":" << obj->transform.scale.z << "}"
                    << ",\"shader\":" << JStr(obj->render.shaderName) << "}";
            }
            d << "],\"lights\":[";
            first = true;
            for (auto& [name, lt] : g_namedLights) {
                std::string lightType = "point";
                Vector3 lpos;
                if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                    lightType = "directional";
                    lpos = dl->transform.position;
                }
                else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                    lightType = "point";
                    lpos = pl->transform.position;
                }
                else {
                    lightType = "ambient"; // plain BaseLight — no transform
                }
                if (!first) d << ","; first = false;
                d << "{\"name\":" << JStr(name)
                    << ",\"intensity\":" << lt->intensity
                    << ",\"color\":{\"r\":" << (int)lt->color.r
                    << ",\"g\":" << (int)lt->color.g
                    << ",\"b\":" << (int)lt->color.b << "}"
                    << ",\"pos\":{\"x\":" << lpos.x << ",\"y\":" << lpos.y << ",\"z\":" << lpos.z << "}"
                    << ",\"type\":" << JStr(lightType) << "}";
            }
            d << "]";
            out << MakeResponse(true, "list",
                std::to_string(g_namedObjects.size()) + " objects, " + std::to_string(g_namedLights.size()) + " lights", d.str()) << "\n";
        });
    // inspect
    reg.Register("inspect", "inspect <name>", "Return transform and material.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "inspect", "Usage: inspect <name>") << "\n"; return; }
            std::shared_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                BaseObject* obj = it->second;
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << "\"name\":" << JStr(args[0]) << ",\"kind\":\"object\""
                    << ",\"visible\":" << (obj->visible ? "true" : "false")
                    << ",\"pos\":{\"x\":" << obj->transform.position.x
                    << ",\"y\":" << obj->transform.position.y
                    << ",\"z\":" << obj->transform.position.z << "}"
                    << ",\"scale\":{\"x\":" << obj->transform.scale.x
                    << ",\"y\":" << obj->transform.scale.y
                    << ",\"z\":" << obj->transform.scale.z << "}"
                    << ",\"rotation\":{\"w\":" << obj->transform.rotation.w
                    << ",\"x\":" << obj->transform.rotation.x
                    << ",\"y\":" << obj->transform.rotation.y
                    << ",\"z\":" << obj->transform.rotation.z << "}"
                    << ",\"shader\":" << JStr(obj->render.shaderName);
                if (obj->material)
                    d << ",\"color\":{\"r\":" << (int)obj->material->color.r
                    << ",\"g\":" << (int)obj->material->color.g
                    << ",\"b\":" << (int)obj->material->color.b
                    << ",\"a\":" << (int)obj->material->color.a << "}";
                out << MakeResponse(true, "inspect", "ok", d.str()) << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                BaseLight* lt = ltIt->second;
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                std::string lightType = "point";
                Vector3 lpos;
                if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                    lightType = "directional";
                    lpos = dl->transform.position;
                }
                else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                    lightType = "point";
                    lpos = pl->transform.position;
                }
                else {
                    lightType = "ambient"; // plain BaseLight — no transform
                }
                d << "\"name\":" << JStr(args[0]) << ",\"kind\":\"light\""
                    << ",\"type\":" << JStr(lightType)
                    << ",\"intensity\":" << lt->intensity
                    << ",\"color\":{\"r\":" << (int)lt->color.r
                    << ",\"g\":" << (int)lt->color.g
                    << ",\"b\":" << (int)lt->color.b << "}"
                    << ",\"pos\":{\"x\":" << lpos.x << ",\"y\":" << lpos.y << ",\"z\":" << lpos.z << "}";
                out << MakeResponse(true, "inspect", "ok", d.str()) << "\n";
                return;
            }
            out << MakeResponse(false, "inspect", "Unknown '" + args[0] + "'") << "\n";
        });
    reg.Register("plane", "plane <name> <x> <y> <z> [width] [height] [color]", "Spawn a plane primitive.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "plane", "Usage: plane <name> <x> <y> <z> [width=5] [height=5] [color]") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x, y, z, width = 5.0, height = 5.0;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "plane", "Bad coords") << "\n"; return;
            }
            if (args.size() >= 5) ParseDouble(args[4], width);
            if (args.size() >= 6) ParseDouble(args[5], height);
            Color col = args.size() >= 7 ? NameToColor(args[6]) : colWhite;
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            // Use Plane class
            BaseObject* obj = new HonHengine::Plane(Vector3(width, 1.0, height), Vector3(x, y, z), Quaternion(),
                new Material(0, 0, col, colBlack));
            ctx.sm->objects->push_back(obj);

            g_namedObjects[finalName] = obj;

            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"width\":" << width << ",\"height\":" << height;
            out << MakeResponse(true, "plane", "'" + finalName + "' spawned", d.str()) << "\n";
        });
    // addlight (avec verrou)
    reg.Register("addlight", "addlight <name> <intensity> <r> <g> <b> [type]",
        "Add a light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 5) { out << MakeResponse(false, "addlight", "Usage: ...") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double intensity; int r, g, b;
            if (!ParseDouble(args[1], intensity) || !ParseInt(args[2], r) || !ParseInt(args[3], g) || !ParseInt(args[4], b)) {
                out << MakeResponse(false, "addlight", "Bad values") << "\n"; return;
            }
            Color col{ (double)std::clamp(r,0,255),(double)std::clamp(g,0,255),(double)std::clamp(b,0,255),255.0 };
            std::string lightType = (args.size() >= 6) ? args[5] : "point";
            // Normalise aliases
            if (lightType == "directionallight") lightType = "directional";
            if (lightType == "pointlight")       lightType = "point";
            if (lightType == "ambientlight")     lightType = "ambient";
            BaseLight* light = nullptr;
            if (lightType == "directional") {
                DirectionalLight* dl = new DirectionalLight(intensity, col, Vector3(0, 0, 0), Quaternion());
                dl->transform.rotation = Quaternion::LookRotation(Vector3(-0.5, -0.8, -0.3));
                light = dl;
            }
            else if (lightType == "ambient") {
                light = new BaseLight(intensity, col); // type = AMBIENT_LIGHT by default
            }
            else {
                lightType = "point"; // enforce canonical name
                light = new PointLight(intensity, col, Vector3(0, 5, 0));
            }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            ctx.sm->lights->push_back(light);
            g_namedLights[finalName] = light;

            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << "\"intensity\":" << intensity << ",\"color\":{\"r\":" << r << ",\"g\":" << g << ",\"b\":" << b << "},\"lightType\":" << JStr(lightType);
            out << MakeResponse(true, "addlight", "'" + finalName + "' added", d.str()) << "\n";
        });
    // removelight
    reg.Register("removelight", "removelight <name>", "Remove a light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "removelight", "Usage: removelight <name>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedLights.find(args[0]);
            if (it == g_namedLights.end()) { out << MakeResponse(false, "removelight", "Unknown light") << "\n"; return; }
            auto& lights = *ctx.sm->lights;
            lights.erase(std::remove(lights.begin(), lights.end(), it->second), lights.end());
            delete it->second;
            g_namedLights.erase(it);
            out << MakeResponse(true, "removelight", "'" + args[0] + "' removed") << "\n";
        });
    // delete
    reg.Register("delete", "delete <name>", "Delete an object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "delete", "Usage: delete <name>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "delete", "Unknown object") << "\n"; return; }
            auto& objs = *ctx.sm->objects;
            objs.erase(std::remove(objs.begin(), objs.end(), it->second), objs.end());
            delete it->second;
            g_namedObjects.erase(it);
            out << MakeResponse(true, "delete", "'" + args[0] + "' deleted") << "\n";
        });
    // move
    reg.Register("move", "move <name> <x> <y> <z>", "Move object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "move", "Usage: move <name> <x> <y> <z>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            double x, y, z; if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "move", "Bad coords") << "\n"; return;
            }
            // Try object first, then light
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                it->second->transform.position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "move", "'" + args[0] + "' moved", d.str()) << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                if (auto* dl = dynamic_cast<DirectionalLight*>(ltIt->second)) dl->transform.position = Vector3(x, y, z);
                else if (auto* pl = dynamic_cast<PointLight*>(ltIt->second))  pl->transform.position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "move", "'" + args[0] + "' moved", d.str()) << "\n";
                return;
            }
            out << MakeResponse(false, "move", "Unknown '" + args[0] + "'") << "\n";
        });
    // scale
    reg.Register("scale", "scale <name> <sx> <sy> <sz>", "Scale object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "scale", "Usage: scale <name> <sx> <sy> <sz>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "scale", "Unknown object") << "\n"; return; }
            double sx, sy, sz; if (!ParseDouble(args[1], sx) || !ParseDouble(args[2], sy) || !ParseDouble(args[3], sz)) {
                out << MakeResponse(false, "scale", "Bad values") << "\n"; return;
            }
            it->second->transform.scale = Vector3(sx, sy, sz);
            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("scale", sx, sy, sz);
            out << MakeResponse(true, "scale", "'" + args[0] + "' scaled", d.str()) << "\n";
        });
    // rotate
    reg.Register("rotate", "rotate <name> <pitch> <yaw> <roll>", "Rotate object or light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "rotate", "Usage: rotate <name> <pitch> <yaw> <roll>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            double p, y, r; if (!ParseDouble(args[1], p) || !ParseDouble(args[2], y) || !ParseDouble(args[3], r)) {
                out << MakeResponse(false, "rotate", "Bad angles") << "\n"; return;
            }
            Quaternion q = EulerToQuat(p, y, r);
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                it->second->transform.rotation = q;
                out << MakeResponse(true, "rotate", "'" + args[0] + "' rotated") << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                if (auto* dl = dynamic_cast<DirectionalLight*>(ltIt->second)) dl->transform.rotation = q;
                else if (auto* pl = dynamic_cast<PointLight*>(ltIt->second))  pl->transform.rotation = q;
                out << MakeResponse(true, "rotate", "'" + args[0] + "' rotated") << "\n";
                return;
            }
            out << MakeResponse(false, "rotate", "Unknown '" + args[0] + "'") << "\n";
        });
    // color
    reg.Register("color", "color <name> <r> <g> <b> [a]", "Set object color.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) {
                out << MakeResponse(false, "color", "Usage: color <name> <r> <g> <b> [a]") << "\n";
                return;
            }

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "color", "Unknown object") << "\n";
                return;
            }
            int r, g, b, a = 255;
            if (!ParseInt(args[1], r) || !ParseInt(args[2], g) || !ParseInt(args[3], b)) {
                out << MakeResponse(false, "color", "Bad values") << "\n";
                return;
            }
            if (args.size() >= 5) ParseInt(args[4], a);

            BaseObject* obj = it->second;
            if (!obj->material) {
                obj->material = new Material(0, 0, colWhite, colBlack);
            }

            // Use setter that marks dirty
            obj->material->setColor(Color{
                (double)std::clamp(r, 0, 255),
                (double)std::clamp(g, 0, 255),
                (double)std::clamp(b, 0, 255),
                (double)std::clamp(a, 0, 255)
                });

            out << MakeResponse(true, "color", "Color set on '" + args[0] + "'") << "\n";
        });
    reg.Register("visible", "visible <name> <true/false>", "Set object visibility.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "visible", "Usage: visible <name> <true/false>") << "\n";
                return;
            }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "visible", "Unknown object") << "\n";
                return;
            }
            bool visible = (args[1] == "true" || args[1] == "1");
            it->second->visible = visible;
            out << MakeResponse(true, "visible", std::string(args[0]) + " visibility set to " + (visible ? "true" : "false")) << "\n";
        });
    // setshader
    reg.Register("setshader", "setshader <name> <shaderName>", "Assign shader.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "setshader", "Usage: setshader <name> <shaderName>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "setshader", "Unknown object") << "\n"; return; }
            it->second->render.shaderName = args[1];
            out << MakeResponse(true, "setshader", "Shader '" + args[1] + "' assigned") << "\n";
        });
    // registershader
    reg.Register("registershader", "registershader <name> <vertFile> <fragFile>", "Compile and register shader.",
        [renderer](CmdContext&, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 3) { out << MakeResponse(false, "registershader", "Usage: registershader <name> <vert> <frag>") << "\n"; return; }
            auto readFile = [](const std::string& path)->std::string {
                std::ifstream f(path); if (!f) return "";
                return{ std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>() };
                };
            std::string vert = readFile(args[1]), frag = readFile(args[2]);
            if (vert.empty()) { out << MakeResponse(false, "registershader", "Cannot read vert: " + args[1]) << "\n"; return; }
            if (frag.empty()) { out << MakeResponse(false, "registershader", "Cannot read frag: " + args[2]) << "\n"; return; }
            renderer->RegisterShader(args[0], vert.c_str(), frag.c_str());
            out << MakeResponse(true, "registershader", "Shader '" + args[0] + "' registered", "\"name\":" + JStr(args[0])) << "\n";
        });
    // clone
    reg.Register("clone", "clone <srcName> <newName>", "Duplicate an object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "clone", "Usage: clone <src> <new>") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[1], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[1]) {
                out << MakeResponse(true, "plane", "Name '" + args[1] + "' changed to '" + finalName + "'") << "\n";
            }

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "clone", "Unknown source") << "\n"; return; }
            if (g_namedObjects.count(args[1])) { out << MakeResponse(false, "clone", "Name already exists") << "\n"; return; }
            BaseObject* src = it->second;
            BaseObject* copy = new BaseObject(*src);
            copy->transform.position.x += 0.5;
            if (src->material) copy->material = new Material(*src->material);
            ctx.sm->objects->push_back(copy);

            g_namedObjects[finalName] = copy;

            out << MakeResponse(true, "clone", "'" + finalName + "' cloned as '" + args[1] + "'") << "\n";
        });
    // rename
    reg.Register("rename", "rename <oldName> <newName>", "Rename object/light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "rename", "Usage: rename <old> <new>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            const std::string& oldN = args[0], rawName = args[1];

            std::string finalName = NameValidator::GetFinalName(rawName, g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != rawName) {
                out << MakeResponse(true, "plane", "Name '" + rawName + "' changed to '" + finalName + "'") << "\n";
            }

            auto objIt = g_namedObjects.find(oldN);
            if (objIt != g_namedObjects.end()) {
                if (g_namedObjects.count(finalName)) { out << MakeResponse(false, "rename", "Name already in use") << "\n"; return; }
                BaseObject* obj = objIt->second;
                g_namedObjects.erase(objIt);
                g_namedObjects[finalName] = obj;
                out << MakeResponse(true, "rename", "'" + oldN + "' -> '" + finalName + "'") << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(oldN);
            if (ltIt != g_namedLights.end()) {
                if (g_namedLights.count(finalName)) { out << MakeResponse(false, "rename", "Name already in use") << "\n"; return; }
                BaseLight* lt = ltIt->second;
                g_namedLights.erase(ltIt);
                g_namedLights[finalName] = lt;
                out << MakeResponse(true, "rename", "'" + oldN + "' -> '" + finalName + "'") << "\n";
                return;
            }
            out << MakeResponse(false, "rename", "Unknown '" + oldN + "'") << "\n";
        });
    // clearscene
    reg.Register("clearscene", "clearscene", "Remove all objects and lights.",
        [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out) {
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            for (auto& [name, obj] : g_namedObjects) {
                auto& objs = *ctx.sm->objects;
                objs.erase(std::remove(objs.begin(), objs.end(), obj), objs.end());
                delete obj;
            }
            g_namedObjects.clear();
            for (auto& [name, lt] : g_namedLights) {
                auto& lights = *ctx.sm->lights;
                lights.erase(std::remove(lights.begin(), lights.end(), lt), lights.end());
                delete lt;
            }
            g_namedLights.clear();
            out << MakeResponse(true, "clearscene", "Scene cleared") << "\n";
        });
    // quit
    reg.Register("quit", "quit", "Exit the application.",
        [](CmdContext&, const std::vector<std::string>&, std::ostream& out) {
            out << MakeResponse(true, "quit", "Shutting down...") << "\n";
            SDL_Event qev{}; qev.type = SDL_QUIT; SDL_PushEvent(&qev);
        });

    // obj — import a Wavefront OBJ file into the scene
    reg.Register("obj", "obj <name> <path> [x] [y] [z]",
        "Import an OBJ mesh.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "obj", "Usage: obj <name> <path> [x] [y] [z]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x = 0, y = 0, z = 0;
            if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }
            // Strip surrounding quotes if present
            std::string path = args[1];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            BaseObject* obj = nullptr;
            try {
                obj = Importer::ImportFromOBJ(path);
            }
            catch (const std::exception& e) {
                out << MakeResponse(false, "obj", std::string("Import failed: ") + e.what()) << "\n";
                return;
            }
            if (!obj) {
                out << MakeResponse(false, "obj", "ImportFromOBJ returned null for: " + path) << "\n";
                return;
            }
            obj->transform.position = Vector3(x, y, z);
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            ctx.sm->objects->push_back(obj);

            g_namedObjects[finalName] = obj;

            out << MakeResponse(true, "obj", "'" + finalName + "' imported from " + path) << "\n";
        });

    // gltf — FIX 1 : on diffère l'import sur le thread principal
    reg.Register("gltf", "gltf <name> <path> [x] [y] [z]",
        "Import a glTF/GLB mesh.",
        [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "gltf", "Usage: gltf <name> <path> [x] [y] [z]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x = 0, y = 0, z = 0;
            if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }
            std::string path = args[1];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            {
                std::lock_guard<std::mutex> lk(g_deferredTasksMutex);
                g_deferredGLTFTasks.push_back({ finalName, path, x, y, z });
            }
            out << MakeResponse(true, "gltf", "'" + finalName + "' queued for import") << "\n";
        });

    // sphere — spawn a UV sphere approximated with a scaled cube (replace with real sphere mesh if available)
    // sphere — spawn a sphere primitive
    reg.Register("sphere", "sphere <name> <x> <y> <z> [radius=0.5] [rings=20] [segments=20] [color]",
        "Spawn a sphere primitive.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) {
                out << MakeResponse(false, "sphere", "Usage: sphere <name> <x> <y> <z> [radius=0.5] [rings=20] [segments=20] [color]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x, y, z, radius = 0.5;
            int rings = 20, segments = 20;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "sphere", "Bad coords") << "\n";
                return;
            }

            if (args.size() >= 5) ParseDouble(args[4], radius);
            if (args.size() >= 6) ParseInt(args[5], rings);
            if (args.size() >= 7) ParseInt(args[6], segments);
            Color col = args.size() >= 8 ? NameToColor(args[7]) : colWhite;

            // Clamp to reasonable values
            rings = std::clamp(rings, 3, 100);
            segments = std::clamp(segments, 3, 100);

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            // Use the proper Sphere class
            BaseObject* obj = new HonHengine::Sphere(Vector3(radius, radius, radius),
                Vector3(x, y, z),
                Quaternion(),
                new Material(0, 0, col, colBlack),
                segments, rings);
            ctx.sm->objects->push_back(obj);

            g_namedObjects[finalName] = obj;

            std::ostringstream d;
            d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"radius\":" << radius
                << ",\"rings\":" << rings << ",\"segments\":" << segments
                << ",\"color\":" << JStr(args.size() >= 8 ? args[7] : "white");
            out << MakeResponse(true, "sphere", "'" + finalName + "' spawned", d.str()) << "\n";
        });

    // rect — spawn a flat rectangle (thin slab)
    reg.Register("rect", "rect <name> <x> <y> <z> [w=5] [h=5] [color]",
        "Spawn a flat rectangle.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "rect", "Usage: rect <name> <x> <y> <z> [w=5] [h=5] [color]") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x, y, z, w = 5.0, h = 5.0;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "rect", "Bad coords") << "\n"; return;
            }
            if (args.size() >= 5) ParseDouble(args[4], w);
            if (args.size() >= 6) ParseDouble(args[5], h);
            Color col = args.size() >= 7 ? NameToColor(args[6]) : colWhite;
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            BaseObject* obj = new HonHengine::Rectangle(Vector3(w * 0.5, w * 0.5, h * 0.5), Vector3(x, y, z), Quaternion(),
                new Material(0, 0, col, colBlack));
            ctx.sm->objects->push_back(obj);
            g_namedObjects[finalName] = obj;
            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"w\":" << w << ",\"h\":" << h << ",\"color\":" << JStr(args.size() >= 7 ? args[6] : "white");
            out << MakeResponse(true, "rect", "'" + finalName + "' spawned", d.str()) << "\n";
        });

    // ── attachscript <objectName> <scriptGUID> ────────────────────────────────
    // Resolves the GUID to a source path via the captured AssetDatabase,
    // creates a ScriptComponent, and pushes it onto the target object's scripts
    // vector.  Thread-safe via g_sceneMutex.
    reg.Register("attachscript",
        "attachscript <objectName> <scriptGUID>",
        "Attach a script asset (by GUID) to a named scene object.",
        [assetDb, sceneMgr](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "attachscript",
                    "Usage: attachscript <objectName> <scriptGUID>") << "\n";
                return;
            }
            const std::string& objName = args[0];
            const std::string& guid = args[1];

            // Resolve GUID → source path via the captured AssetDatabase
            std::string srcPath;
            if (assetDb) {
                AssetRecord* rec = assetDb->FindByGUID(guid);
                if (rec) srcPath = rec->path;
            }
            if (srcPath.empty()) srcPath = guid;  // fallback: use the GUID string as path

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(objName);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "attachscript",
                    "Unknown object '" + objName + "'") << "\n";
                return;
            }
            BaseObject* obj = it->second;

            ScriptComponent newComp;
            newComp.scriptGUID = guid;
            newComp.compiledPath = "";

            ScriptManager* sm = sceneMgr ? sceneMgr->scriptManager.get() : nullptr;
            if (sm && sm->LoadScript(guid, srcPath, newComp)) {
                obj->scripts.push_back(newComp);
                out << MakeResponse(true, "attachscript",
                    "Script '" + guid + "' attached to '" + objName + "'") << "\n";
            }
            else {
                // ScriptManager unavailable or load failed — record the component
                // so the Inspector can still display and manage it.
                obj->scripts.push_back(newComp);
                out << MakeResponse(true, "attachscript",
                    "Script component recorded for '" + objName +
                    "' (runtime load skipped)") << "\n";
            }
        });

    // ── setmaterial <objectName> <materialPath> ───────────────────────────────
    // Replaces the object's material.  For .honmat files a placeholder white
    // material is created (a full material asset pipeline can extend this).
    // For any other path the same default is used.  Thread-safe.
    reg.Register("setmaterial",
        "setmaterial <objectName> <materialPath>",
        "Assign a material asset to a named scene object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "setmaterial",
                    "Usage: setmaterial <objectName> <materialPath>") << "\n";
                return;
            }
            const std::string& objName = args[0];
            const std::string& matPath = args[1];

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(objName);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "setmaterial",
                    "Unknown object '" + objName + "'") << "\n";
                return;
            }
            BaseObject* obj = it->second;

            // Delete the old material to avoid a leak
            delete obj->material;
            // Create a fresh default white material.  If a full .honmat loader
            // is added later, insert it here based on the file extension.
            obj->material = new Material(0, 0,
                Color(255.0, 255.0, 255.0, 255.0),
                Color(0.0, 0.0, 0.0, 255.0));

            out << MakeResponse(true, "setmaterial",
                "Material '" + matPath + "' assigned to '" + objName + "'") << "\n";
        });
}

// =============================================================================
//  Structures pour l'IDE 
// =============================================================================
struct ConsoleLog {
    enum Kind { CMD, REPLY_OK, REPLY_ERR, INFO };
    struct Entry { Kind kind; std::string text; };
    std::deque<Entry> entries;
    std::mutex mtx;
    bool autoScroll = true;
    void push(Kind k, const std::string& t) { std::lock_guard<std::mutex> lk(mtx); entries.push_back({ k,t }); if (entries.size() > 2000) entries.pop_front(); }
};

struct CommandBus {
    std::queue<std::string> pending;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<bool> alive{ true };
    void send(const std::string& cmd) { std::lock_guard<std::mutex> lk(mtx); pending.push(cmd); cv.notify_one(); }
    bool pop(std::string& out) {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait_for(lk, std::chrono::milliseconds(10), [this] {return !pending.empty() || !alive; });
        if (pending.empty()) return false;
        out = pending.front(); pending.pop(); return true;
    }
};

struct SceneFBO {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
    void init(int width, int height) {
        w = width; h = height;
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "FBO incomplete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    void resize(int nw, int nh) {
        if (nw == w && nh == h) return;
        w = nw; h = nh;
        glBindTexture(GL_TEXTURE_2D, color); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    }
    void bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h); }
    void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    void destroy() { if (fbo) glDeleteFramebuffers(1, &fbo); if (color) glDeleteTextures(1, &color); if (depth) glDeleteRenderbuffers(1, &depth); }
};

static void ShipGame(const std::string& projectDir,
    const std::string& outDir,
    ConsoleLog& log)
{
    log.push(ConsoleLog::INFO, "[Ship] Starting build packaging...");

    fs::path src(projectDir);
    fs::path dst(outDir);

    // Gather all files, skipping .git, build dirs, IDE-only source files.
    static const std::vector<std::string> skipDirs = {
        ".git","build","CMakeFiles",".vs","__pycache__","node_modules"
    };
    static const std::vector<std::string> skipExts = {
        ".o",".a",".d",".pdb",".ilk",".exp",".lib"
    };

    try {
        fs::create_directories(dst);
        size_t copied = 0;

        for (auto& entry : fs::recursive_directory_iterator(src,
            fs::directory_options::skip_permission_denied))
        {
            const fs::path& p = entry.path();

            // Skip hidden / build dirs
            bool skip = false;
            for (auto& part : p) {
                if (std::find(skipDirs.begin(), skipDirs.end(), part.string())
                    != skipDirs.end()) {
                    skip = true; break;
                }
            }
            if (skip) continue;

            std::string ext = p.extension().string();
            if (std::find(skipExts.begin(), skipExts.end(), ext) != skipExts.end())
                continue;

            fs::path rel = fs::relative(p, src);
            fs::path dest = dst / rel;

            if (entry.is_directory()) {
                fs::create_directories(dest);
            }
            else {
                fs::create_directories(dest.parent_path());
                fs::copy_file(p, dest, fs::copy_options::overwrite_existing);
                ++copied;
            }
        }

        // Write a launch script
        {
            fs::path launcher = dst / "RunGame.sh";
            std::ofstream lf(launcher);
            lf << "#!/bin/bash\n"
                << "cd \"$(dirname \"$0\")\"\n"
                << "./HonHonEngine \"$@\"\n";
            lf.close();
            // chmod +x via system() — portable enough for dev use
            std::string chmod_cmd = "chmod +x \"" + launcher.string() + "\"";
            std::system(chmod_cmd.c_str());

            fs::path winLauncher = dst / "RunGame.bat";
            std::ofstream wf(winLauncher);
            wf << "@echo off\n"
                << "cd /d \"%~dp0\"\n"
                << "HonHonEngine.exe %*\n";
        }

        // Optional: create a .tar.gz archive next to outDir
        std::string archiveName = dst.filename().string() + ".tar.gz";
        fs::path    archivePath = dst.parent_path() / archiveName;
        std::string cmd = "tar -czf \"" + archivePath.string()
            + "\" -C \"" + dst.parent_path().string()
            + "\" \"" + dst.filename().string() + "\"";
        if (std::system(cmd.c_str()) == 0) {
            log.push(ConsoleLog::REPLY_OK,
                "[Ship] Archive: " + archivePath.string());
        }

        log.push(ConsoleLog::REPLY_OK,
            "[Ship] Packaged " + std::to_string(copied)
            + " files → " + dst.string());
    }
    catch (const std::exception& e) {
        log.push(ConsoleLog::REPLY_ERR,
            std::string("[Ship] Error: ") + e.what());
    }
}


static const char* kColorNames[] = {
    "white","black","red","green","blue","yellow","gray","orange","purple","cyan"
};
static const int kNumColors = 10;

struct HierarchyNode {
    enum Kind { OBJECT, LIGHT, FOLDER };
    Kind kind = OBJECT; std::string name; bool folderOpen = true; std::vector<HierarchyNode> children;
};


struct IDEState {
    std::vector<IDEObject> objects;
    std::vector<IDELight> lights;
    // Non-owning pointer to the ScriptManager that lives in sm->scriptManager.
    // Ownership was moved there so GPURenderer::UpdateGameLogic can drive updates.
    ScriptManager* scriptManager = nullptr;
    float editorFov = 60.f; // editor viewport camera FOV

    // MODIFIED: sélection multiple au lieu de selectedObject
    MultiSelection selection;

    std::vector<HierarchyNode> hierRoots;
    std::string clipboardName;
    bool clipboardIsLight = false;
    SceneFBO sceneFBO;
    SceneManager* sm = nullptr;
    GPURenderer* renderer = nullptr;
    BasicMovements* player = nullptr;
    CommandBus bus;
    ConsoleLog log;
    SDL_Window* window = nullptr;
    static constexpr int kUndoMaxDepth = 64;
    char cmdInput[512] = {};

    ImGuiID mainDockspaceId = 0;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showConsole = true;
    bool showAssetBrowser = true;
    bool viewportHovered = false;
    bool showShipDialog = false;
    char shipSrcDir[512] = "./";
    char shipDstDir[512] = "./dist/";
    bool showAddObject = false;
    char newObjName[64] = "obj1";
    ObjectType newObjType = PLANE;
    char newObjFile[256] = "";
    float newObjPos[3] = {};
    float newObjHalf = 0.5f;
    int newObjColor = 0;
    bool showAddLight = false;
    float newLightPos[3] = {};
    char newLightName[64] = "light1";
    float newLightIntensity = 1.f;
    float newLightColor[3] = { 1.f,0.96f,0.82f };
    ObjectType newLightType = POINT_LIGHT;
    bool showAddCamera = false;
    char newCameraName[64] = "camera1";
    float newCameraPos[3] = { 0.f, 5.f, 15.f };
    float inspPos[3] = {};
    float inspScale[3] = { 1,1,1 };
    float inspRot[3] = {};
    float inspColor[4] = { 1,1,1,1 };
    char inspShader[64] = {};
    char inspTag[64] = {};
    bool playing = false;

    // Asset management
    AssetDatabase    assetDb;
    AssetBrowserState assetBrowser;

    // Project management
    ProjectSettings  project;
    ProjectUIState   projectUI;
    AutoSaveManager  autoSave;

    // Package manager
    PackageManager   packageMgr;
    PackageManagerUIState packageUI;

    // Global search
    GlobalSearchState globalSearch;

    // Toast notifications
    ToastManager toastMgr;

    // Editor settings
    EditorSettings editorSettings;

    // Selection history
    SelectionHistory selectionHistory;

    // Progress bar
    ProgressState progressState;

    // Inspector lock
    bool inspectorLocked = false;
    std::string lockedInspectorObject;

    char hierarchyFilter[128] = {};
    bool showStats = false;
    float statsTimer = 0.f;
    int statsFps = 0;
    int statsFrameCount = 0;
    bool showWorldSettings = false;
    float ambientIntensity = 0.15f;
    float fogDensity = 0.f;
    int fogColor[3] = { 200,210,230 };
    bool wireframe = false;
    bool showRenameModal = false;
    char renameOldName[64] = {};
    char renameNewName[64] = {};
    bool snapEnabled = false;
    float snapPosition = 0.25f;
    float snapRotation = 15.f;
    float snapScale = 0.1f;
    std::vector<std::string> cmdHistory;
    int cmdHistoryIdx = -1;
    std::vector<std::string> undoStack;
    bool sceneDirty = false;
    char sceneFilePath[512] = "scene.honscene";
    bool showSaveModal = false;
    bool showLoadModal = false;
    std::string pendingSelection;
    size_t logReadIdx = 0;
    bool showCreateFolder = false;
    char newFolderName[64] = "Group";
    bool showMoveToFolder = false;
    char moveTargetItem[64] = {};
    bool moveTargetIsLight = false;

    // Outils à la Unity
    int toolMode = 1;            // 0=View, 1=Translate, 2=Rotate, 3=Scale
    bool editorCamOrbit = false;
    ImVec2 editorCamLastMouse;

    // NEW: Gizmo state
    GizmoState gizmo;

    // NEW: Camera bookmarks
    CameraBookmarks bookmarks;

    // NEW: Focus transition
    FocusTransition focus;

    // NEW: Ortho/persp
    bool viewportOrtho = false;
    float orthoSize = 10.f;

    // NEW: Overlays
    bool showGrid = true;
    bool showAxes = true;
    bool showIcons3D = true;
    bool showFrustum = true;
    int gridPlane = 0;   // 0=XZ, 1=XY, 2=YZ
    int debugMode = 0;   // 0=shaded, 1=wireframe, 2=overdraw, 3=depth, 4=normals

    // NEW: FPS fly mode
    bool fpsFlyMode = false;

    // NEW: Box selection
    BoxSelectionState boxSelect;

    // ── Import Settings Overlay ───────────────────────────────────────────────
    // Floating temporary panel shown above the Inspector when an asset is
    // dropped onto the viewport (or focused in the browser).
    bool  showImportOverlay = false;
    std::string importOverlayGUID;          // which asset's settings to show
    bool  importOverlayHasImportBtn = false;// whether to show the "Import" button
    std::function<void()> importOverlayOnImport; // callback for Import button
    ImVec2 importOverlayAnchorPos = {};    // Inspector window top-left (updated each frame)
    ImVec2 importOverlayAnchorSize = {};    // Inspector window size     (updated each frame)
};

// =============================================================================
//  Fonctions de rafraîchissement de la hiérarchie (inchangées)
// =============================================================================
static void DrawWelcomeModal(IDEState& ide, bool& firstRun) {
    if (!firstRun) return;
    ImGui::OpenPopup("Welcome to HonHon Engine");
    firstRun = false;

    if (ImGui::BeginPopupModal("Welcome to HonHon Engine", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Welcome to the HonHon Engine IDE!");
        ImGui::Separator();
        ImGui::BulletText("Press Ctrl+F to search across objects, assets, and commands");
        ImGui::BulletText("Use Q/W/E/R to switch tools (View/Move/Rotate/Scale)");
        ImGui::BulletText("Right-click in Hierarchy for object options");
        ImGui::BulletText("Drag assets from Asset Browser into the Viewport to instantiate");
        ImGui::Spacing();
        if (ImGui::Button("Get Started", { 200, 0 })) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void PushSelectionHistory(IDEState& ide, const std::string& selection) {
    if (ide.inspectorLocked) return;
    ide.selectionHistory.Push(selection);
}

// Rebuild the flat+folder hierarchy.
static void RebuildHierarchy(IDEState& ide)
{
    std::unordered_map<std::string, bool> inTree; // name → isLight
    std::function<void(std::vector<HierarchyNode>&)> collect = [&](std::vector<HierarchyNode>& nodes) {
        for (auto& n : nodes) {
            if (n.kind == HierarchyNode::FOLDER) collect(n.children);
            else inTree[n.name] = (n.kind == HierarchyNode::LIGHT);
        }
        };
    collect(ide.hierRoots);

    // Ajouter les objets non présents dans l'arborescence
    for (auto& obj : ide.objects)
        if (!inTree.count(obj.name)) {
            HierarchyNode n;
            n.kind = HierarchyNode::OBJECT;
            n.name = obj.name;
            // Restaurer l'état d'ouverture du dossier si nécessaire
            ide.hierRoots.push_back(n);
        }
    for (auto& lt : ide.lights)
        if (!inTree.count(lt.name)) {
            HierarchyNode n;
            n.kind = HierarchyNode::LIGHT;
            n.name = lt.name;
            ide.hierRoots.push_back(n);
        }

    // Nettoyer les références invalides
    std::unordered_set<std::string> objNames, ltNames;
    for (auto& o : ide.objects) objNames.insert(o.name);
    for (auto& l : ide.lights)  ltNames.insert(l.name);

    std::function<void(std::vector<HierarchyNode>&)> prune = [&](std::vector<HierarchyNode>& nodes) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](HierarchyNode& n) -> bool {
            if (n.kind == HierarchyNode::FOLDER) {
                prune(n.children);
                // Supprimer les dossiers vides
                return n.children.empty() && n.name != "Root";
            }
            if (n.kind == HierarchyNode::OBJECT) return !objNames.count(n.name);
            if (n.kind == HierarchyNode::LIGHT)  return !ltNames.count(n.name);
            return false;
            }), nodes.end());
        };
    prune(ide.hierRoots);
}

static void ParseListReply(const std::string& json, IDEState& ide)
{
    // Sauvegarder la sélection actuelle avant de parser
    std::set<std::string> previousSelection = ide.selection.items;
    std::string oldPrimary = ide.selection.Primary();

    static std::vector<IDEObject> lastObjects;
    static std::vector<IDELight> lastLights;

    std::vector<IDEObject> newObjects;
    std::vector<IDELight> newLights;

    size_t objStart = json.find("\"objects\":[");
    if (objStart != std::string::npos) {
        size_t objEnd = json.find("],\"lights\"", objStart);
        if (objEnd == std::string::npos) objEnd = json.find("]}", objStart);

        std::string objectsStr = json.substr(objStart + 10, objEnd - objStart - 10);

        size_t pos = 0;
        while (true) {
            size_t namePos = objectsStr.find("\"name\":\"", pos);
            if (namePos == std::string::npos) break;

            size_t nameEnd = objectsStr.find("\"", namePos + 8);
            std::string name = objectsStr.substr(namePos + 8, nameEnd - namePos - 8);

            IDEObject obj;
            obj.name = name;
            obj.visible = true;
            obj.locked = false;
            obj.sx = obj.sy = obj.sz = 1.0f;
            obj.cr = obj.cg = obj.cb = 255;
            obj.ca = 255;
            obj.rw = 1.0f;
            obj.rx = obj.ry = obj.rz = 0.0f;
            obj.tag = "";
            obj.shader = "";

            // Extract position
            size_t xPos = objectsStr.find("\"x\":", nameEnd);
            if (xPos != std::string::npos) {
                sscanf_s(objectsStr.c_str() + xPos + 4, "%f", &obj.px);
            }
            size_t yPos = objectsStr.find("\"y\":", xPos + 1);
            if (yPos != std::string::npos) {
                sscanf_s(objectsStr.c_str() + yPos + 4, "%f", &obj.py);
            }
            size_t zPos = objectsStr.find("\"z\":", yPos + 1);
            if (zPos != std::string::npos) {
                sscanf_s(objectsStr.c_str() + zPos + 4, "%f", &obj.pz);
            }

            // Extract scale (if present)
            size_t scaleXPos = objectsStr.find("\"scale\":{\"x\":", nameEnd);
            if (scaleXPos != std::string::npos) {
                sscanf_s(objectsStr.c_str() + scaleXPos + 13, "%f", &obj.sx);
                size_t scaleYPos = objectsStr.find("\"y\":", scaleXPos + 13);
                if (scaleYPos != std::string::npos) {
                    sscanf_s(objectsStr.c_str() + scaleYPos + 4, "%f", &obj.sy);
                }
                size_t scaleZPos = objectsStr.find("\"z\":", scaleYPos + 4);
                if (scaleZPos != std::string::npos) {
                    sscanf_s(objectsStr.c_str() + scaleZPos + 4, "%f", &obj.sz);
                }
            }

            // Extract shader
            size_t shaderPos = objectsStr.find("\"shader\":\"", nameEnd);
            if (shaderPos != std::string::npos) {
                size_t shaderEnd = objectsStr.find("\"", shaderPos + 10);
                if (shaderEnd != std::string::npos) {
                    obj.shader = objectsStr.substr(shaderPos + 10, shaderEnd - shaderPos - 10);
                }
            }

            newObjects.push_back(obj);
            pos = nameEnd + 1;
        }
    }

    size_t lightStart = json.find("\"lights\":[");
    if (lightStart != std::string::npos) {
        size_t contentStart = lightStart + 10;
        size_t lightEnd = json.find("]", contentStart);
        if (lightEnd == std::string::npos) lightEnd = json.size();
        std::string lightsStr = json.substr(contentStart, lightEnd - contentStart);

        size_t pos = 0;
        while (true) {
            size_t namePos = lightsStr.find("\"name\":\"", pos);
            if (namePos == std::string::npos) break;

            size_t nameEnd = lightsStr.find("\"", namePos + 8);
            std::string name = lightsStr.substr(namePos + 8, nameEnd - namePos - 8);

            IDELight light;
            light.name = name;
            light.lightType = "point";
            light.intensity = 1.0f;
            light.r = light.g = light.b = 255;
            light.px = light.py = light.pz = 0.0f;
            light.rotx = light.roty = light.rotz = 0.0f;

            // Extract intensity
            size_t intPos = lightsStr.find("\"intensity\":", nameEnd);
            if (intPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + intPos + 12, "%f", &light.intensity);
            }

            // Extract light type
            size_t typePos = lightsStr.find("\"type\":\"", nameEnd);
            if (typePos != std::string::npos) {
                size_t typeEnd = lightsStr.find("\"", typePos + 8);
                if (typeEnd != std::string::npos) {
                    light.lightType = lightsStr.substr(typePos + 8, typeEnd - typePos - 8);
                }
            }

            // Extract position (if present)
            size_t posXPos = lightsStr.find("\"x\":", nameEnd);
            if (posXPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posXPos + 4, "%f", &light.px);
            }
            size_t posYPos = lightsStr.find("\"y\":", posXPos + 1);
            if (posYPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posYPos + 4, "%f", &light.py);
            }
            size_t posZPos = lightsStr.find("\"z\":", posYPos + 1);
            if (posZPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posZPos + 4, "%f", &light.pz);
            }

            newLights.push_back(light);
            pos = nameEnd + 1;
        }
    }

    // Check if scene changed
    bool changed = (newObjects.size() != lastObjects.size() || newLights.size() != lastLights.size());
    if (!changed) {
        for (size_t i = 0; i < newObjects.size(); i++) {
            if (newObjects[i].name != lastObjects[i].name) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        for (size_t i = 0; i < newLights.size(); i++) {
            if (newLights[i].name != lastLights[i].name ||
                newLights[i].lightType != lastLights[i].lightType) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        ide.objects = std::move(newObjects);
        ide.lights = std::move(newLights);
        lastObjects = ide.objects;
        lastLights = ide.lights;
        RebuildHierarchy(ide);
    }

    // Restaurer la sélection (pour les objets qui existent encore)
    std::set<std::string> restoredSelection;
    for (const auto& name : previousSelection) {
        bool found = false;
        // Vérifier dans les objets
        for (const auto& obj : ide.objects) {
            if (obj.name == name) {
                found = true;
                break;
            }
        }
        // Vérifier dans les lumières
        if (!found) {
            for (const auto& lt : ide.lights) {
                if (lt.name == name) {
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            restoredSelection.insert(name);
        }
        else {
            ide.log.push(ConsoleLog::INFO, "[Selection] Object '" + name + "' no longer exists, removed from selection");
        }
    }

    // Mettre à jour la sélection
    ide.selection.items = restoredSelection;

    // Si la sélection a changé, mettre à jour l'inspecteur
    if (restoredSelection.empty()) {
        if (!oldPrimary.empty()) {
            ide.log.push(ConsoleLog::INFO, "[Selection] Selection cleared");
        }
    }
    else if (restoredSelection.size() == 1) {
        std::string newPrimary = ide.selection.Primary();
        if (newPrimary != oldPrimary) {
            ide.bus.send("inspect " + newPrimary);
        }
    }
    else if (restoredSelection.size() > 1 && oldPrimary != ide.selection.Primary()) {
        // Pour la sélection multiple, on inspecte le primaire
        ide.bus.send("inspect " + ide.selection.Primary());
    }

    // Gérer la sélection en attente (après création d'un objet)
    if (!ide.pendingSelection.empty()) {
        if (ide.selection.Contains(ide.pendingSelection)) {
            ide.pendingSelection.clear();
        }
        else {
            // Attendre que l'objet apparaisse dans la liste
            bool pendingFound = false;
            for (const auto& obj : ide.objects) {
                if (obj.name == ide.pendingSelection) {
                    pendingFound = true;
                    break;
                }
            }
            if (!pendingFound) {
                for (const auto& lt : ide.lights) {
                    if (lt.name == ide.pendingSelection) {
                        pendingFound = true;
                        break;
                    }
                }
            }
            if (pendingFound) {
                ide.selection.SetSingle(ide.pendingSelection);
                ide.bus.send("inspect " + ide.pendingSelection);
                ide.pendingSelection.clear();
            }
        }
    }

    // Mettre à jour l'inspector si l'objet verrouillé existe toujours
    if (ide.inspectorLocked && !ide.lockedInspectorObject.empty()) {
        bool lockedExists = false;
        for (const auto& obj : ide.objects) {
            if (obj.name == ide.lockedInspectorObject) {
                lockedExists = true;
                break;
            }
        }
        if (!lockedExists) {
            for (const auto& lt : ide.lights) {
                if (lt.name == ide.lockedInspectorObject) {
                    lockedExists = true;
                    break;
                }
            }
        }
        if (!lockedExists) {
            ide.inspectorLocked = false;
            ide.lockedInspectorObject.clear();
        }
    }
}

static void ParseInspectReply(const std::string& json, IDEState& ide)
{
    ide.inspPos[0] = ide.inspPos[1] = ide.inspPos[2] = 0.f;
    ide.inspScale[0] = ide.inspScale[1] = ide.inspScale[2] = 1.f;
    ide.inspRot[0] = ide.inspRot[1] = ide.inspRot[2] = 0.f;
    ide.inspColor[0] = ide.inspColor[1] = ide.inspColor[2] = 1.f;
    ide.inspColor[3] = 1.f;
    ide.inspShader[0] = '\0';
    ide.inspTag[0] = '\0';

    auto posB = json.find("\"pos\":{");
    if (posB != std::string::npos) {
        std::string posS = json.substr(posB + 7);
        auto pe = posS.find('}');
        if (pe != std::string::npos) posS = posS.substr(0, pe);
        try { ide.inspPos[0] = std::stof(JsonGet(posS, "x")); }
        catch (...) {}
        try { ide.inspPos[1] = std::stof(JsonGet(posS, "y")); }
        catch (...) {}
        try { ide.inspPos[2] = std::stof(JsonGet(posS, "z")); }
        catch (...) {}
    }
    auto scB = json.find("\"scale\":{");
    if (scB != std::string::npos) {
        std::string scS = json.substr(scB + 9);
        auto se = scS.find('}');
        if (se != std::string::npos) scS = scS.substr(0, se);
        try { ide.inspScale[0] = std::stof(JsonGet(scS, "x")); }
        catch (...) {}
        try { ide.inspScale[1] = std::stof(JsonGet(scS, "y")); }
        catch (...) {}
        try { ide.inspScale[2] = std::stof(JsonGet(scS, "z")); }
        catch (...) {}
    }
    auto cB = json.find("\"color\":{");
    if (cB != std::string::npos) {
        std::string cS = json.substr(cB + 9);
        auto ce = cS.find('}');
        if (ce != std::string::npos) cS = cS.substr(0, ce);
        try { ide.inspColor[0] = std::stof(JsonGet(cS, "r")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[1] = std::stof(JsonGet(cS, "g")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[2] = std::stof(JsonGet(cS, "b")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[3] = std::stof(JsonGet(cS, "a")) / 255.f; }
        catch (...) {}
    }

    std::string inspectedName = JsonGet(json, "name");

    auto visB = json.find("\"visible\":");
    if (visB != std::string::npos) {
        std::string visStr = json.substr(visB + 10);
        bool visible = (visStr.find("true") != std::string::npos);
        // Mettre à jour l'objet dans ide.objects
        for (auto& obj : ide.objects) {
            if (obj.name == inspectedName) {
                obj.visible = visible;
                break;
            }
        }
    }
    std::string sh = JsonGet(json, "shader");
    strncpy_s(ide.inspShader, sizeof(ide.inspShader), sh.c_str(), sizeof(ide.inspShader) - 1);

    if (inspectedName.empty()) inspectedName = ide.selection.Primary();
    for (auto& lt : ide.lights) {
        if (lt.name == inspectedName) {
            try {
                std::string intensityStr = JsonGet(json, "intensity");
                if (!intensityStr.empty()) {
                    lt.intensity = std::stof(intensityStr);
                }
            }
            catch (...) {
                ide.log.push(ConsoleLog::REPLY_ERR,
                    "[ParseInspect] Failed to parse intensity for " + inspectedName);
            }
            auto cB2 = json.find("\"color\":{");
            if (cB2 != std::string::npos) {
                std::string cS2 = json.substr(cB2 + 9);
                auto ce2 = cS2.find('}');
                if (ce2 != std::string::npos) cS2 = cS2.substr(0, ce2);
                try { lt.r = (int)std::stof(JsonGet(cS2, "r")); }
                catch (...) {}
                try { lt.g = (int)std::stof(JsonGet(cS2, "g")); }
                catch (...) {}
                try { lt.b = (int)std::stof(JsonGet(cS2, "b")); }
                catch (...) {}
            }
            std::string lt_type = JsonGet(json, "type");
            if (!lt_type.empty()) lt.lightType = lt_type;
            lt.px = ide.inspPos[0];
            lt.py = ide.inspPos[1];
            lt.pz = ide.inspPos[2];
            break;
        }
    }
}

static void HandleReply(const std::string& json, IDEState& ide)
{
    std::string cmd = JsonGet(json, "cmd");
    bool ok = JsonOk(json);
    std::string msg = JsonGet(json, "msg");

    if (cmd == "list") { ParseListReply(json, ide); }
    if (cmd == "inspect") { ParseInspectReply(json, ide); }

    ConsoleLog::Kind k = ok ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR;
    ide.log.push(k, "[" + cmd + "] " + msg);
}

static void RefreshSceneList(IDEState& ide) { ide.bus.send("list"); }

static void ShellThread(CommandBus* bus, CommandRegistry* reg, CmdContext* ctx, std::atomic<bool>* running, ConsoleLog* log) {
    log->push(ConsoleLog::INFO, "[Shell] Engine shell ready.");
    while (*running) {
        std::string cmd;
        if (!bus->pop(cmd)) continue;
        if (cmd.empty()) continue;
        std::ostringstream reply;
        reg->Dispatch(cmd, *ctx, reply);
        std::string line = reply.str();
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) {
            bool ok = JsonOk(line);
            log->push(ok ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR, line);
        }
    }
}


static void ApplyIDEStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 6.f;
    s.ChildRounding = 4.f;
    s.FrameRounding = 4.f;
    s.PopupRounding = 5.f;
    s.ScrollbarRounding = 6.f;
    s.GrabRounding = 4.f;
    s.TabRounding = 4.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.PopupBorderSize = 1.f;

    s.FramePadding = { 8, 5 };
    s.ItemSpacing = { 8, 6 };
    s.ItemInnerSpacing = { 6, 4 };
    s.IndentSpacing = 18.f;
    s.ScrollbarSize = 11.f;
    s.GrabMinSize = 10.f;
    s.WindowTitleAlign = { 0.0f, 0.5f };
    s.SeparatorTextBorderSize = 1.f;

    const ImVec4 bg0 = { 0.078f, 0.082f, 0.094f, 1.f };
    const ImVec4 bg1 = { 0.110f, 0.114f, 0.129f, 1.f };
    const ImVec4 bg2 = { 0.145f, 0.150f, 0.168f, 1.f };
    const ImVec4 bg3 = { 0.190f, 0.196f, 0.216f, 1.f };
    const ImVec4 bg4 = { 0.240f, 0.248f, 0.270f, 1.f };

    const ImVec4 acc0 = { 0.22f, 0.47f, 0.80f, 1.f };
    const ImVec4 acc1 = { 0.30f, 0.55f, 0.88f, 1.f };
    const ImVec4 acc2 = { 0.18f, 0.40f, 0.70f, 1.f };

    const ImVec4 bdr = { 0.24f, 0.25f, 0.28f, 1.f };

    const ImVec4 txt = { 0.90f, 0.91f, 0.93f, 1.f };
    const ImVec4 txtD = { 0.48f, 0.50f, 0.54f, 1.f };

    auto* c = s.Colors;

    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg0;
    c[ImGuiCol_PopupBg] = { 0.13f, 0.135f, 0.15f, 0.98f };
    c[ImGuiCol_MenuBarBg] = bg0;

    c[ImGuiCol_Border] = bdr;
    c[ImGuiCol_BorderShadow] = { 0,0,0,0 };

    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = bg4;

    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = { 0.15f, 0.16f, 0.19f, 1.f };
    c[ImGuiCol_TitleBgCollapsed] = bg0;

    c[ImGuiCol_ScrollbarBg] = bg0;
    c[ImGuiCol_ScrollbarGrab] = bg4;
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.30f, 0.31f, 0.34f, 1.f };
    c[ImGuiCol_ScrollbarGrabActive] = acc0;

    c[ImGuiCol_CheckMark] = acc1;
    c[ImGuiCol_SliderGrab] = acc0;
    c[ImGuiCol_SliderGrabActive] = acc1;

    c[ImGuiCol_Button] = { acc0.x, acc0.y, acc0.z, 0.75f };
    c[ImGuiCol_ButtonHovered] = acc1;
    c[ImGuiCol_ButtonActive] = acc2;

    c[ImGuiCol_Header] = { acc0.x, acc0.y, acc0.z, 0.45f };
    c[ImGuiCol_HeaderHovered] = { acc0.x, acc0.y, acc0.z, 0.65f };
    c[ImGuiCol_HeaderActive] = acc0;

    c[ImGuiCol_Separator] = bdr;
    c[ImGuiCol_SeparatorHovered] = acc0;
    c[ImGuiCol_SeparatorActive] = acc1;

    c[ImGuiCol_ResizeGrip] = { acc0.x, acc0.y, acc0.z, 0.20f };
    c[ImGuiCol_ResizeGripHovered] = acc0;
    c[ImGuiCol_ResizeGripActive] = acc1;

    c[ImGuiCol_Tab] = { 0.13f, 0.135f, 0.15f, 1.f };
    c[ImGuiCol_TabHovered] = bg3;
    c[ImGuiCol_TabActive] = { 0.18f, 0.19f, 0.22f, 1.f };
    c[ImGuiCol_TabUnfocused] = bg0;
    c[ImGuiCol_TabUnfocusedActive] = bg1;

    c[ImGuiCol_PlotLines] = acc0;
    c[ImGuiCol_PlotLinesHovered] = acc1;
    c[ImGuiCol_PlotHistogram] = acc0;
    c[ImGuiCol_PlotHistogramHovered] = acc1;

    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = bdr;
    c[ImGuiCol_TableBorderLight] = { 0.18f, 0.19f, 0.21f, 1.f };

    c[ImGuiCol_TextSelectedBg] = { acc0.x, acc0.y, acc0.z, 0.35f };
    c[ImGuiCol_NavHighlight] = acc1;
    c[ImGuiCol_NavWindowingHighlight] = acc1;
    c[ImGuiCol_NavWindowingDimBg] = { 0,0,0,0.4f };
    c[ImGuiCol_ModalWindowDimBg] = { 0,0,0,0.5f };

    c[ImGuiCol_Text] = txt;
    c[ImGuiCol_TextDisabled] = txtD;
}


static void DrawHierarchyNodes(std::vector<HierarchyNode>& nodes, IDEState& ide,
    const std::string& filterLow)
{
    auto matchesFilter = [&](const std::string& name) {
        if (filterLow.empty()) return true;
        std::string low = name;
        for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        return low.find(filterLow) != std::string::npos;
        };

    for (auto& node : nodes)
    {
        if (node.kind == HierarchyNode::FOLDER)
        {
            bool hasMatch = filterLow.empty();
            if (!hasMatch) {
                std::function<bool(const std::vector<HierarchyNode>&)> anyMatch =
                    [&](const std::vector<HierarchyNode>& ch) -> bool {
                    for (auto& c : ch) {
                        if (c.kind == HierarchyNode::FOLDER && anyMatch(c.children)) return true;
                        if (matchesFilter(c.name)) return true;
                    }
                    return false;
                    };
                hasMatch = anyMatch(node.children);
            }
            if (!hasMatch) continue;

            ImGuiTreeNodeFlags folderFlags = ImGuiTreeNodeFlags_SpanFullWidth
                | ImGuiTreeNodeFlags_OpenOnArrow;
            if (node.folderOpen) folderFlags |= ImGuiTreeNodeFlags_DefaultOpen;

            ImGui::PushStyleColor(ImGuiCol_Text, { 0.85f, 0.75f, 0.5f, 1.f });
            bool open = ImGui::TreeNodeEx(("##folder_" + node.name).c_str(), folderFlags);
            node.folderOpen = open;
            ImGui::SameLine();
            ImGui::TextUnformatted((std::string(ICON_FA_FOLDER " ") + node.name).c_str());
            ImGui::PopStyleColor();

            if (ImGui::BeginPopupContextItem(("fctx_" + node.name).c_str()))
            {
                if (ImGui::MenuItem("Rename Folder...")) {
                    strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), node.name.c_str(), sizeof(ide.renameOldName) - 1);
                    strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), node.name.c_str(), sizeof(ide.renameNewName) - 1);
                    ide.showRenameModal = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Folder (keep children)")) {
                    for (auto& child : node.children)
                        ide.hierRoots.push_back(child);
                    node.children.clear();
                    node.name = "\x01__DELETE__";
                }
                ImGui::EndPopup();
            }

            if (open) {
                DrawHierarchyNodes(node.children, ide, filterLow);
                ImGui::TreePop();
            }
            continue;
        }

        if (!matchesFilter(node.name)) continue;

        bool isLight = (node.kind == HierarchyNode::LIGHT);

        IDEObject* pObj = nullptr;
        IDELight* pLt = nullptr;
        if (!isLight) for (auto& o : ide.objects) if (o.name == node.name) { pObj = &o; break; }
        else          for (auto& l : ide.lights)  if (l.name == node.name) { pLt = &l; break; }

        bool locked = pObj && pObj->locked;
        bool visible = pObj ? pObj->visible : true;

        // MODIFIED: multi-selection
        bool selected = ide.selection.Contains(node.name);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
            | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanFullWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        if (locked)        ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.5f,1.f });
        else if (isLight)  ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.9f,0.4f,1.f });
        else               ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

        std::string iconStr;
        if (isLight)
            iconStr = (pLt && pLt->lightType == "directional")
            ? "  " ICON_FA_SUN " "
            : (pLt && pLt->lightType == "ambient")
            ? "  " ICON_FA_GLOBE " "
            : "  " ICON_FA_CIRCLE_DOT " ";
        else
            iconStr = locked ? "  " ICON_FA_LOCK " " : "  " ICON_FA_CUBE " ";

        if (!visible) iconStr = "  " ICON_FA_EYE_SLASH " ";

        std::string label = iconStr + node.name;
        if (pObj && !pObj->tag.empty()) label += "  [" + pObj->tag + "]";

        ImGui::TreeNodeEx(("##hnode_" + node.name).c_str(), flags);
        bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !locked;

        // ── Drag source: drag this node to the Viewport to reposition it ────
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            HierarchyDragPayload hdp{};
            strncpy_s(hdp.name, sizeof(hdp.name),
                node.name.c_str(), sizeof(hdp.name) - 1);
            hdp.isLight = isLight;
            ImGui::SetDragDropPayload(kHierarchyDragPayload, &hdp, sizeof(hdp));
            ImGui::TextUnformatted(isLight ? "Move Light" : "Move Object");
            ImGui::SameLine();
            ImGui::TextUnformatted(node.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
        bool textClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !locked;
        ImGui::PopStyleColor();

        // MODIFIED: multi-selection with Ctrl
        if ((rowClicked || textClicked) && !locked) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                ide.selection.Toggle(node.name);
            }
            else {
                ide.selection.SetSingle(node.name);
            }
            ide.bus.send("inspect " + ide.selection.Primary());
        }

        // ── Drop target: accept asset payloads to attach scripts/materials ──
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* assetPayload =
                ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
                auto* ap = (AssetDragPayload*)assetPayload->Data;
                AssetRecord* rec = ide.assetDb.FindByGUID(std::string(ap->guidStr));
                if (rec) {
                    if (rec->type == AssetType::Script && !isLight) {
                        // Attach script to the BaseObject
                        BaseObject* baseObj = nullptr;
                        {
                            std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                            auto oit = g_namedObjects.find(node.name);
                            if (oit != g_namedObjects.end()) baseObj = oit->second;
                        }
                        if (baseObj) {
                            ScriptComponent newComp;
                            newComp.scriptGUID = ap->guidStr;
                            if (ide.scriptManager &&
                                ide.scriptManager->LoadScript(ap->guidStr, rec->path, newComp)) {
                                baseObj->scripts.push_back(newComp);
                                ide.log.push(ConsoleLog::REPLY_OK,
                                    "[Hierarchy] Script '" + rec->displayName
                                    + "' attached to '" + node.name + "'");
                            }
                            else {
                                ide.log.push(ConsoleLog::REPLY_ERR,
                                    "[Hierarchy] Failed to attach script " + rec->displayName);
                            }
                            ide.sceneDirty = true;
                        }
                    }
                    else if (rec->type == AssetType::Material) {
                        char cmd[512];
                        std::snprintf(cmd, sizeof(cmd), "setmaterial %s %s",
                            node.name.c_str(), rec->path.c_str());
                        ide.bus.send(cmd);
                        ide.sceneDirty = true;
                    }
                    else if (rec->type == AssetType::Model ||
                        rec->type == AssetType::Prefab) {
                        // Open import overlay for 3D assets
                        ide.importOverlayGUID = ap->guidStr;
                        ide.showImportOverlay = true;
                        ide.importOverlayHasImportBtn = false;
                        ide.importOverlayOnImport = nullptr;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu (identique, mais agit sur la sélection)
        if (ImGui::BeginPopupContextItem(("hctx_" + node.name).c_str()))
        {
            if (!ide.selection.Contains(node.name)) {
                ide.selection.SetSingle(node.name);
                ide.bus.send("inspect " + node.name);
            }

            ImGui::TextDisabled("  %s", node.name.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Rename...")) {
                strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), node.name.c_str(), sizeof(ide.renameOldName) - 1);
                strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), node.name.c_str(), sizeof(ide.renameNewName) - 1);
                ide.showRenameModal = true;
            }

            if (ImGui::MenuItem("Duplicate")) {
                std::string newName = node.name + "_copy";
                if (!isLight) {
                    ide.bus.send("clone " + node.name + " " + newName);
                }
                else {
                    if (pLt) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                            newName.c_str(), pLt->intensity, pLt->r, pLt->g, pLt->b,
                            pLt->lightType.c_str());
                        ide.bus.send(buf);
                    }
                }
                ide.sceneDirty = true;
                ide.pendingSelection = newName;
                RefreshSceneList(ide);
            }

            if (ImGui::MenuItem("Copy")) {
                ide.clipboardName = node.name;
                ide.clipboardIsLight = isLight;
            }

            bool canPaste = !ide.clipboardName.empty();
            if (ImGui::MenuItem("Paste", nullptr, false, canPaste)) {
                std::string newName = ide.clipboardName + "_copy";
                if (!ide.clipboardIsLight) {
                    ide.bus.send("clone " + ide.clipboardName + " " + newName);
                }
                else {
                    IDELight* src = nullptr;
                    for (auto& l : ide.lights) if (l.name == ide.clipboardName) { src = &l; break; }
                    if (src) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                            newName.c_str(), src->intensity, src->r, src->g, src->b,
                            src->lightType.c_str());
                        ide.bus.send(buf);
                    }
                }
                ide.sceneDirty = true;
                ide.pendingSelection = newName;
                RefreshSceneList(ide);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Focus")) {
                if (pObj) {
                    glm::vec3 target(pObj->px, pObj->py, pObj->pz);
                    ide.focus.Start(ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize, target, 5.f);
                }
            }

            ImGui::Separator();

            if (!isLight && pObj) {
                bool vis = pObj->visible;
                if (ImGui::MenuItem(vis ? "Hide" : "Show"))
                    ide.bus.send("visible " + node.name + " " + (vis ? "false" : "true"));
                if (ImGui::MenuItem(locked ? "Unlock" : "Lock"))
                    pObj->locked = !pObj->locked;
            }

            ImGui::Separator();

            ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.4f,0.4f,1.f });
            if (ImGui::MenuItem(isLight ? "Remove Light" : "Delete")) {
                if (isLight) {
                    ide.bus.send("removelight " + node.name);
                }
                else {
                    ide.undoStack.push_back("delete " + node.name);
                    if ((int)ide.undoStack.size() > IDEState::kUndoMaxDepth)
                        ide.undoStack.erase(ide.undoStack.begin());
                    ide.bus.send("delete " + node.name);
                }
                ide.sceneDirty = true;
                ide.selection.Remove(node.name);
                RefreshSceneList(ide);
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        }
    }

    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const HierarchyNode& n) {
        return n.kind == HierarchyNode::FOLDER && n.name == "\x01__DELETE__";
        }), nodes.end());
}


static void DrawTransformInspector(IDEState& ide, const std::string& target, IDEObject* pObj, bool isLight)
{
    if (ImGui::CollapsingHeader("  Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Copy/Paste buttons (à implémenter)

        if (isLight && pObj == nullptr) {
            IDELight* pLt = nullptr;
            for (auto& l : ide.lights) if (l.name == target) { pLt = &l; break; }
            if (pLt && pLt->lightType == "ambient") {
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.5f,1.f });
                ImGui::TextWrapped("  Ambient lights have no position or rotation.");
                ImGui::PopStyleColor();
            }
            else {
                float step = ide.snapEnabled ? ide.snapPosition : 0.05f;
                bool posChanged = ImGui::DragFloat3("Position##obj", ide.inspPos, step);
                if (posChanged) {
                    float* p = ide.inspPos;
                    if (ide.snapEnabled) {
                        for (int i = 0; i < 3; i++)
                            p[i] = std::round(p[i] / ide.snapPosition) * ide.snapPosition;
                    }
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f", target.c_str(), p[0], p[1], p[2]);
                    ide.bus.send(buf);
                    ide.sceneDirty = true;
                }

                float rotStep = ide.snapEnabled ? ide.snapRotation : 0.5f;
                bool rotChanged = ImGui::DragFloat3("Rotation##obj", ide.inspRot, rotStep);
                if (rotChanged) {
                    float* r = ide.inspRot;
                    if (ide.snapEnabled) {
                        for (int i = 0; i < 3; i++)
                            r[i] = std::round(r[i] / ide.snapRotation) * ide.snapRotation;
                    }
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f", target.c_str(), r[0], r[1], r[2]);
                    ide.bus.send(buf);
                    ide.sceneDirty = true;
                }
            }
        }
        else if (pObj) {
            float step = ide.snapEnabled ? ide.snapPosition : 0.05f;
            bool posChanged = ImGui::DragFloat3("Position##obj", ide.inspPos, step);
            if (posChanged) {
                float* p = ide.inspPos;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        p[i] = std::round(p[i] / ide.snapPosition) * ide.snapPosition;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f", target.c_str(), p[0], p[1], p[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
            }

            float rotStep = ide.snapEnabled ? ide.snapRotation : 0.5f;
            bool rotChanged = ImGui::DragFloat3("Rotation##obj", ide.inspRot, rotStep);
            if (rotChanged) {
                float* r = ide.inspRot;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        r[i] = std::round(r[i] / ide.snapRotation) * ide.snapRotation;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f", target.c_str(), r[0], r[1], r[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
            }

            float scStep = ide.snapEnabled ? ide.snapScale : 0.01f;
            bool scaleChanged = ImGui::DragFloat3("Scale##obj", ide.inspScale, scStep, 0.001f, 1000.f);
            if (scaleChanged) {
                float* s = ide.inspScale;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        s[i] = std::round(s[i] / ide.snapScale) * ide.snapScale;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "scale %s %.4f %.4f %.4f", target.c_str(), s[0], s[1], s[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
            }
        }

        // Reset buttons
        if (ImGui::SmallButton("Reset Pos")) {
            ide.inspPos[0] = ide.inspPos[1] = ide.inspPos[2] = 0.f;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "move %s 0 0 0", target.c_str());
            ide.bus.send(buf); ide.sceneDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Rot")) {
            ide.inspRot[0] = ide.inspRot[1] = ide.inspRot[2] = 0.f;
            ide.bus.send(std::string("rotate ") + target + " 0 0 0");
            ide.sceneDirty = true;
        }
        if (!isLight && pObj) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Scale")) {
                ide.inspScale[0] = ide.inspScale[1] = ide.inspScale[2] = 1.f;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "scale %s 1 1 1", target.c_str());
                ide.bus.send(buf); ide.sceneDirty = true;
            }
        }
    }
}

static void DrawLightInspector(IDEState& ide, IDELight* pLt)
{
    ImGui::PushID(pLt->name.c_str());

    if (ImGui::CollapsingHeader("  Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* kLightTypes[] = { "Point", "Directional", "Ambient" };
        int ltIdx = (pLt->lightType == "directional") ? 1
            : (pLt->lightType == "ambient") ? 2
            : 0;

        if (ImGui::Combo("Type##lt", &ltIdx, kLightTypes, 3)) {
            pLt->lightType = (ltIdx == 1) ? "directional"
                : (ltIdx == 2) ? "ambient"
                : "point";

            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        if (ImGui::DragFloat("Intensity##lt", &pLt->intensity, 0.05f, 0.f, 20.f)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        float lcol[3] = { pLt->r / 255.f, pLt->g / 255.f, pLt->b / 255.f };
        if (ImGui::ColorEdit3("Color##lt", lcol)) {
            pLt->r = (int)(lcol[0] * 255);
            pLt->g = (int)(lcol[1] * 255);
            pLt->b = (int)(lcol[2] * 255);

            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f,0.2f,0.2f,1.f });
        if (ImGui::Button("Remove Light")) {
            ide.bus.send("removelight " + pLt->name);
            ide.selection.Remove(pLt->name);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
        ImGui::PopStyleColor();
    }

    ImGui::PopID();
}

static void DrawMaterialInspector(IDEState& ide, const std::string& target, IDEObject* pObj)
{
    if (ImGui::CollapsingHeader("  Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool colorChanged = false;

        // Color Picker avec retour immédiat
        if (ImGui::ColorEdit4("Albedo##obj", ide.inspColor)) {
            colorChanged = true;
        }

        // Appliquer le changement quand l'utilisateur relâche la souris ou après modification
        if (ImGui::IsItemDeactivatedAfterEdit() || (colorChanged && !ImGui::IsItemActive())) {
            int r = (int)(ide.inspColor[0] * 255), g = (int)(ide.inspColor[1] * 255),
                b = (int)(ide.inspColor[2] * 255), a = (int)(ide.inspColor[3] * 255);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "color %s %d %d %d %d", target.c_str(), r, g, b, a);
            ide.bus.send(buf);
            ide.sceneDirty = true;
            // Forcer un rafraîchissement pour que la couleur s'affiche immédiatement
            RefreshSceneList(ide);
        }

        ImGui::TextDisabled("  Quick:");
        static const struct { const char* label; int r, g, b; } kQuickColors[] = {
            {"White",255,255,255},{"Gray",140,140,140},{"Black",0,0,0},
            {"Red",220,60,60},{"Green",60,180,60},{"Blue",60,100,220},
            {"Yellow",230,200,40},{"Orange",230,130,40},{"Cyan",60,200,210},
        };

        int quickIndex = 0;
        for (auto& qc : kQuickColors) {
            if (quickIndex % 3 != 0) ImGui::SameLine();
            ImVec4 col{ qc.r / 255.f, qc.g / 255.f, qc.b / 255.f, 1.f };
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
            if (ImGui::SmallButton(qc.label)) {
                ide.inspColor[0] = qc.r / 255.f; ide.inspColor[1] = qc.g / 255.f;
                ide.inspColor[2] = qc.b / 255.f; ide.inspColor[3] = 1.f;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "color %s %d %d %d 255", target.c_str(), qc.r, qc.g, qc.b);
                ide.bus.send(buf);
                ide.sceneDirty = true;
                // Forcer un rafraîchissement immédiat
                RefreshSceneList(ide);
            }
            ImGui::PopStyleColor(2);
            ++quickIndex;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Aperçu de la couleur actuelle
        ImVec4 previewCol(ide.inspColor[0], ide.inspColor[1], ide.inspColor[2], 1.0f);
        ImGui::TextUnformatted("Preview:");
        ImGui::SameLine();
        ImGui::ColorButton("##preview", previewCol, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 20));

        // Bouton Reset
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to White")) {
            ide.inspColor[0] = 1.0f; ide.inspColor[1] = 1.0f; ide.inspColor[2] = 1.0f; ide.inspColor[3] = 1.0f;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "color %s 255 255 255 255", target.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }
}

static void DrawRendererInspector(IDEState& ide, const std::string& target, IDEObject* pObj)
{
    if (ImGui::CollapsingHeader("  Renderer"))
    {
        ImGui::InputText("Shader##obj", ide.inspShader, sizeof(ide.inspShader));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ide.bus.send(std::string("setshader ") + target + " " + ide.inspShader);
            ide.sceneDirty = true;
        }
        ImGui::InputText("Tag##obj", ide.inspTag, sizeof(ide.inspTag));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ide.bus.send(std::string("settag ") + target + " " + ide.inspTag);
        }
    }

    if (pObj) {
        bool vis = pObj->visible;
        if (ImGui::Checkbox("Visible##obj", &vis)) {
            ide.bus.send("visible " + target + " " + (vis ? "true" : "false"));
            ide.sceneDirty = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Duplicate")) {
        std::string newName = target + "_copy";
        ide.bus.send("clone " + target + " " + newName);
        ide.sceneDirty = true;
        RefreshSceneList(ide);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename...")) {
        strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), target.c_str(), sizeof(ide.renameOldName) - 1);
        strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), target.c_str(), sizeof(ide.renameNewName) - 1);
        ide.showRenameModal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus")) {
        if (pObj) {
            glm::vec3 targetPos(pObj->px, pObj->py, pObj->pz);
            ide.focus.Start(ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize, targetPos, 5.f);
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f,0.2f,0.2f,1.f });
    if (ImGui::Button("Delete")) {
        ide.bus.send("delete " + target);
        ide.sceneDirty = true;
        ide.selection.Remove(target);
        RefreshSceneList(ide);
    }
    ImGui::PopStyleColor();
}

static void DrawHierarchyPanel(IDEState& ide)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.08f, 0.085f, 0.09f, 1.0f });
    ImGui::BeginChild("##hier", ImVec2(0, 0), false);

    // --- Barre d'outils compacte ---
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    // Groupe de boutons à gauche
    ImGui::BeginGroup();

    // Bouton Create (menu déroulant)
    if (ImGui::Button(ICON_FA_PLUS, ImVec2(28, 24))) {
        ImGui::OpenPopup("hier_create_menu");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create new object or light");

    if (ImGui::BeginPopup("hier_create_menu")) {
        ImGui::SeparatorText("3D Objects");
        if (ImGui::MenuItem(ICON_FA_SQUARE " Plane")) { ide.newObjType = PLANE; ide.showAddObject = true; }
        if (ImGui::MenuItem(ICON_FA_SQUARE " Rectangle")) { ide.newObjType = RECTANGLE; ide.showAddObject = true; }
        if (ImGui::MenuItem(ICON_FA_CIRCLE " Sphere")) { ide.newObjType = SPHERE; ide.showAddObject = true; }
        ImGui::SeparatorText("Lights");
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Ambient Light")) { ide.newLightType = AMBIENT_LIGHT; ide.showAddLight = true; }
        if (ImGui::MenuItem(ICON_FA_SUN " Directional Light")) { ide.newLightType = DIRECTIONAL_LIGHT; ide.showAddLight = true; }
        if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT " Point Light")) { ide.newLightType = POINT_LIGHT; ide.showAddLight = true; }
        ImGui::SeparatorText("Scene");
        if (ImGui::MenuItem(ICON_FA_VIDEO " Camera")) { ide.showAddCamera = true; }
        ImGui::SeparatorText("Organization");
        if (ImGui::MenuItem(ICON_FA_FOLDER " Folder")) { ide.showCreateFolder = true; }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button(ICON_FA_REDO_ALT, ImVec2(28, 24))) {
        RefreshSceneList(ide);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh hierarchy");

    // Paste button (si clipboard non vide)
    if (!ide.clipboardName.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LAYER_GROUP, ImVec2(28, 24))) {
            std::string newName = ide.clipboardName + "_copy";
            if (!ide.clipboardIsLight) {
                ide.bus.send("clone " + ide.clipboardName + " " + newName);
            }
            else {
                IDELight* src = nullptr;
                for (auto& l : ide.lights) if (l.name == ide.clipboardName) { src = &l; break; }
                if (src) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                        newName.c_str(), src->intensity, src->r, src->g, src->b,
                        src->lightType.c_str());
                    ide.bus.send(buf);
                }
            }
            ide.sceneDirty = true;
            ide.pendingSelection = newName;
            RefreshSceneList(ide);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste " ICON_FA_LAYER_GROUP);
    }

    ImGui::EndGroup();

    // Champ de recherche (prend l'espace restant)
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierfilter", ICON_FA_SEARCH " Search...",
        ide.hierarchyFilter, sizeof(ide.hierarchyFilter));

    ImGui::Separator();

    // Compteur compact
    ImGui::TextDisabled(" %zu objects | %zu lights", ide.objects.size(), ide.lights.size());
    ImGui::Separator();

    ImGui::PopStyleVar(); // ItemSpacing

    // Zone de défilement pour la hiérarchie
    ImGui::BeginChild("##hier_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::string filterLow(ide.hierarchyFilter);
    for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);
    DrawHierarchyNodes(ide.hierRoots, ide, filterLow);

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawScriptsInspector(IDEState& ide, const std::string& target, BaseObject* obj) {
    if (!obj) return;

    ImGui::SeparatorText("Scripts");

    // Drop a script asset directly onto this section
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            auto* ap = (AssetDragPayload*)payload->Data;
            if (ap->type == AssetType::Script) {
                AssetRecord* rec = ide.assetDb.FindByGUID(std::string(ap->guidStr));
                if (rec) {
                    ScriptComponent newComp;
                    newComp.scriptGUID = ap->guidStr;
                    if (ide.scriptManager && ide.scriptManager->LoadScript(ap->guidStr, rec->path, newComp))
                        obj->scripts.push_back(newComp);
                    else
                        ide.log.push(ConsoleLog::REPLY_ERR,
                            "[Inspector] Failed to attach script " + rec->displayName);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Add Script button
    if (ImGui::Button("+ Add Script", { -1, 0 })) {
        ImGui::OpenPopup("AddScriptPopup");
    }
    if (ImGui::BeginPopup("AddScriptPopup")) {
        // Show all script assets from the database
        for (auto& [guid, rec] : ide.assetDb.records) {
            if (rec.type == AssetType::Script) {
                if (ImGui::MenuItem(rec.displayName.c_str())) {
                    // Attach this script
                    ScriptComponent newComp;
                    newComp.scriptGUID = guid;
                    newComp.compiledPath = ""; // will be set by LoadScript
                    if (ide.scriptManager->LoadScript(guid, rec.path, newComp)) {
                        obj->scripts.push_back(newComp);
                        // Set game object pointer on the script instance
                        // We need a wrapper – for simplicity we store the BaseObject pointer.
                        // Extend IScript with SetGameObject or add a member.
                    }
                    else {
                        ide.log.push(ConsoleLog::REPLY_ERR,
                            "Failed to load script " + rec.displayName);
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    // List attached scripts
    for (size_t i = 0; i < obj->scripts.size(); ++i) {
        auto& comp = obj->scripts[i];
        // Get asset record for display name
        AssetRecord* rec = ide.assetDb.FindByGUID(comp.scriptGUID);
        std::string name = rec ? rec->displayName : comp.scriptGUID;

        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.2f, 0.3f, 0.4f, 1.0f });
        if (ImGui::CollapsingHeader(name.c_str())) {
            ImGui::PopStyleColor();
            // Exposed variables editing
            for (auto& var : comp.exposedVars) {
                ImGui::PushID(&var);
                switch (var.type) {
                case ScriptVarType::Int:
                    if (ImGui::DragInt(var.name.c_str(), &var.value.i, 0.1f))
                        var.dirty = true;
                    break;
                case ScriptVarType::Float:
                    if (ImGui::DragFloat(var.name.c_str(), &var.value.f, 0.01f))
                        var.dirty = true;
                    break;
                case ScriptVarType::Bool:
                    if (ImGui::Checkbox(var.name.c_str(), &var.value.b))
                        var.dirty = true;
                    break;
                case ScriptVarType::String: {
                    char buf[256];
                    strncpy_s(buf, sizeof(buf), var.value.s ? var.value.s : "", _TRUNCATE);
                    if (ImGui::InputText(var.name.c_str(), buf, sizeof(buf))) {
                        delete[] var.value.s;
                        var.value.s = new char[strlen(buf) + 1];
                        strcpy_s(var.value.s, strlen(buf) + 1, buf);
                        var.dirty = true;
                    }
                    break;
                }
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Remove Script")) {
                ide.scriptManager->UnloadScript(comp);
                obj->scripts.erase(obj->scripts.begin() + i);
                i--;
            }
        }
        else {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
}

static void DrawInspectorPanel(IDEState& ide)
{
    ImGui::BeginChild("##insp", { 0,0 }, false);

    ImGui::PushID("insp_lock");
    if (ImGui::Button(ide.inspectorLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK, { 28, 24 })) {
        if (!ide.inspectorLocked && !ide.selection.Empty()) {
            ide.lockedInspectorObject = ide.selection.Primary();
            ide.inspectorLocked = true;
        }
        else {
            ide.inspectorLocked = false;
            ide.lockedInspectorObject.clear();
        }
    }
    ImGui::PopID();
    ImGui::SameLine();

    std::string inspectionTarget = ide.inspectorLocked ? ide.lockedInspectorObject : ide.selection.Primary();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload))
        {
            IM_ASSERT(payload->DataSize == sizeof(AssetDragPayload));
            const AssetDragPayload& drop = *static_cast<const AssetDragPayload*>(payload->Data);
            std::string guidStr(drop.guidStr);
            std::string path(drop.path);

            if (drop.type == AssetType::Script && !inspectionTarget.empty())
            {
                // Drop a script directly onto a scene object in the Inspector header
                BaseObject* baseObj = nullptr;
                {
                    std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                    auto it = g_namedObjects.find(inspectionTarget);
                    if (it != g_namedObjects.end()) baseObj = it->second;
                }
                if (baseObj)
                {
                    AssetRecord* rec = ide.assetDb.FindByGUID(guidStr);
                    if (rec)
                    {
                        ScriptComponent newComp;
                        newComp.scriptGUID = guidStr;
                        if (ide.scriptManager && ide.scriptManager->LoadScript(guidStr, rec->path, newComp))
                            baseObj->scripts.push_back(newComp);
                        else
                            ide.log.push(ConsoleLog::REPLY_ERR,
                                "[Inspector] Failed to attach script " + rec->displayName);
                    }
                }
            }
            else
            {
                // For any other asset type, open the import overlay
                ide.assetBrowser.focusedGUID = guidStr;
                ide.assetBrowser.selectedGUIDs = { guidStr };
                ide.importOverlayGUID = guidStr;
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = false;
                ide.importOverlayOnImport = nullptr;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ide.snapEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f,0.9f,0.4f,1.f });
        ImGui::TextUnformatted("  SNAP ON");
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    // MODIFIED: multi-selection message
    if (ide.selection.Size() > 1) {
        ImGui::TextColored({ 0.8f, 0.9f, 1.0f, 1.f }, "  %zu objects selected", ide.selection.Size());
        ImGui::TextDisabled("  Multi-selection editing limited");
        ImGui::Separator();
        if (ImGui::Button("Clear selection")) {
            ide.selection.Clear();
        }
        ImGui::EndChild();
        return;
    }

    if (inspectionTarget.empty())
    {
        ImGui::TextDisabled("  Select an object or light in the Hierarchy.");
        ImGui::EndChild();
        return;
    }

    IDEObject* pObj = nullptr;
    for (auto& o : ide.objects) if (o.name == inspectionTarget) { pObj = &o; break; }

    IDELight* pLt = nullptr;
    for (auto& l : ide.lights) if (l.name == inspectionTarget) { pLt = &l; break; }

    bool isLight = (pObj == nullptr && pLt != nullptr);
    bool isUnknown = (pObj == nullptr && pLt == nullptr);

    if (isUnknown)
    {
        ImGui::TextDisabled("  '%s' not found in scene.", inspectionTarget.c_str());
        ImGui::Spacing();
        if (ImGui::SmallButton("Clear selection")) ide.selection.Clear();
        ImGui::EndChild();
        return;
    }

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, { 0.85f,0.95f,1.0f,1.f });
    if (isLight) {
        const char* lightIcon = (pLt->lightType == "directional") ? ICON_FA_SUN
            : (pLt->lightType == "ambient") ? ICON_FA_GLOBE
            : ICON_FA_CIRCLE_DOT;
        ImGui::Text("  %s  %s  [Light]", lightIcon, inspectionTarget.c_str());
    }
    else {
        const char* lockIcon = (pObj && pObj->locked) ? " " ICON_FA_LOCK : "";
        ImGui::Text("  %s%s", inspectionTarget.c_str(), lockIcon);
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (pObj && pObj->locked) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.6f,0.6f,0.6f,1.f });
        ImGui::TextWrapped("  Object is locked. Right-click in Hierarchy to unlock.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    // Transform component
    DrawTransformInspector(ide, inspectionTarget, pObj, isLight);

    // Light component
    if (isLight && pLt) {
        DrawLightInspector(ide, pLt);
    }

    // Material component (objects only)
    if (!isLight && pObj) {
        DrawMaterialInspector(ide, inspectionTarget, pObj);
    }

    // Renderer component
    if (!isLight && pObj) {
        DrawRendererInspector(ide, inspectionTarget, pObj);
    }

    // Scripts component
    if (!isLight && pObj) {
        BaseObject* baseObj = nullptr;
        std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
        auto it = g_namedObjects.find(inspectionTarget);
        if (it != g_namedObjects.end()) baseObj = it->second;
        lock.unlock();
        DrawScriptsInspector(ide, inspectionTarget, baseObj);
    }

    // ── Asset Import Settings ─────────────────────────────────────────────────
    // Import settings are now shown in the floating ImportSettingsOverlay panel
    // above the Inspector (triggered by asset focus / drag-drop).
    // The overlay is drawn in MainScene_Run after all panels.

    ImGui::EndChild();
}

// =============================================================================
//  FPS Fly Camera (editor, not play mode)
// =============================================================================
static void UpdateFPSCamera(IDEState& ide, float dt)
{
    if (!ide.fpsFlyMode || ide.playing) return;
    if (!ide.viewportHovered && !ide.fpsFlyMode) return;

    Camera* cam = ide.sm->currentCamera;
    if (!cam) return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float speed = 5.0f * dt;

    // Allow speed boost with Shift
    if (keys[SDL_SCANCODE_LSHIFT]) speed *= 3.0f;

    // ZQSD / WASD forward/back
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z]) {
        Vector3 fwd = cam->transform.forward();
        cam->transform.position = cam->transform.position + fwd * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
        Vector3 fwd = cam->transform.forward();
        cam->transform.position = cam->transform.position - fwd * speed;
    }
    // Strafe
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_Q]) {
        Vector3 right = cam->transform.right();
        cam->transform.position = cam->transform.position - right * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
        Vector3 right = cam->transform.right();
        cam->transform.position = cam->transform.position + right * speed;
    }
    // Vertical
    if (keys[SDL_SCANCODE_SPACE]) {
        cam->transform.position.y += speed;
    }
    if (keys[SDL_SCANCODE_LCTRL]) {
        cam->transform.position.y -= speed;
    }

    // Mouse look (relative mode is already on when fpsFlyMode is true)
    int mx = 0, my = 0;
    SDL_GetRelativeMouseState(&mx, &my);
    if (mx != 0 || my != 0) {
        float sensitivity = 0.002f;
        // Yaw (Y axis)
        if (mx != 0) {
            float angle = -mx * sensitivity;
            Vector3 up(0, 1, 0);
            cam->transform.rotation = Quaternion::FromAxisAngle(
                up,
                angle * (180.0f / (float)M_PI)) * cam->transform.rotation;
        }
        // Pitch (local X axis) — right-multiply to stay in local space,
        // otherwise diagonal yaw inverts the vertical axis.
        if (my != 0) {
            float angle = -my * sensitivity;
            Vector3 localRight(1, 0, 0);
            cam->transform.rotation = cam->transform.rotation * Quaternion::FromAxisAngle(
                localRight,
                angle * (180.0f / (float)M_PI));
        }
    }
}

// =============================================================================
//  Camera Controls (Orbit, Pan, Zoom) pour l'éditeur
// =============================================================================
static void UpdateEditorCamera(IDEState& ide, float dt, int vw, int vh,
    const ImVec2& imagePos, const ImVec2& imageSize) {

    // Ne pas interférer avec les modes FPS ou Play
    if (ide.fpsFlyMode || ide.playing) return;

    Camera* cam = ide.sm->currentCamera;
    if (!cam) return;

    ImGuiIO& io = ImGui::GetIO();

    // Vérifier si la souris est dans la zone du viewport
    bool mouseInViewport = (io.MousePos.x >= imagePos.x && io.MousePos.x <= imagePos.x + imageSize.x &&
        io.MousePos.y >= imagePos.y && io.MousePos.y <= imagePos.y + imageSize.y);

    if (!mouseInViewport) return;

    // --- Point focal : centre de l'objet sélectionné ou centre de la scène ---
    glm::vec3 pivotPoint(0.0f, 0.0f, 0.0f);

    if (!ide.selection.Empty()) {
        std::string primary = ide.selection.Primary();
        // Chercher l'objet sélectionné
        for (auto& obj : ide.objects) {
            if (obj.name == primary) {
                pivotPoint = glm::vec3(obj.px, obj.py, obj.pz);
                break;
            }
        }
        // Si non trouvé dans les objets, chercher dans les lumières
        if (pivotPoint == glm::vec3(0.0f)) {
            for (auto& light : ide.lights) {
                if (light.name == primary) {
                    pivotPoint = glm::vec3(light.px, light.py, light.pz);
                    break;
                }
            }
        }
    }

    // Si pas de sélection, utiliser le centre de la scène
    if (pivotPoint == glm::vec3(0.0f) && !ide.objects.empty()) {
        for (auto& obj : ide.objects) {
            pivotPoint += glm::vec3(obj.px, obj.py, obj.pz);
        }
        pivotPoint /= (float)ide.objects.size();
    }

    // --- ZOOM : Molette de souris (SLOWER) ---
    float wheel = io.MouseWheel;
    if (wheel != 0.0f && mouseInViewport) {
        // Move camera along its forward direction
        float speed = 2.0f;  // adjust to taste
        Vector3 forward = cam->transform.forward();
        cam->transform.position = cam->transform.position + forward * (wheel * speed);
    }

    // --- Gestion des états de souris ---
    static bool wasRightDragging = false;
    static bool wasMiddleDragging = false;
    static ImVec2 lastMousePos;

    bool rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // Calculer le delta souris
    ImVec2 currentMousePos = io.MousePos;
    ImVec2 mouseDelta = { 0, 0 };

    // Pour le clic droit (orbit)
    if (rightMouseDown) {
        if (wasRightDragging) {
            mouseDelta.x = currentMousePos.x - lastMousePos.x;
            mouseDelta.y = (currentMousePos.y - lastMousePos.y) * -1;
        }
        lastMousePos = currentMousePos;
        wasRightDragging = true;
    }
    else {
        wasRightDragging = false;
    }

    // Pour le clic milieu (pan)
    if (middleMouseDown) {
        if (wasMiddleDragging) {
            mouseDelta.x = currentMousePos.x - lastMousePos.x;
            mouseDelta.y = currentMousePos.y - lastMousePos.y;
        }
        lastMousePos = currentMousePos;
        wasMiddleDragging = true;
    }
    else {
        wasMiddleDragging = false;
    }

    // --- ROTATION : Clic droit (rotation autour de la caméra, pas du pivot) ---
    if (rightMouseDown && !io.KeyAlt && (mouseDelta.x != 0 || mouseDelta.y != 0)) {
        // Accelerative rotation: slow for small movements, faster for quick flicks.
        // ~0.17°/px at 1 px, scales to ~4° at 20 px;
        auto accel = [](float raw) -> float {
            float sign = raw < 0.f ? -1.f : 1.f;
            float mag = std::abs(raw);
            return sign * 0.003f * std::pow(mag, 1.1f);
            };
        float deltaX = accel(mouseDelta.x);
        float deltaY = accel(mouseDelta.y);

        // Rotation around camera's own position (not around a pivot)
        // Get current camera forward direction
        glm::vec3 forward = cam->transform.forward().ToGLM();
        glm::vec3 right = cam->transform.right().ToGLM();
        glm::vec3 up = cam->transform.up().ToGLM();

        // Horizontal rotation (yaw) around world Y axis
        glm::vec3 axisY(0.0f, 1.0f, 0.0f);
        glm::quat rotY = glm::angleAxis(-deltaX, axisY);
        forward = rotY * forward;
        right = rotY * right;
        up = glm::normalize(glm::cross(right, forward));

        // Vertical rotation (pitch) around camera's right axis
        glm::quat rotX = glm::angleAxis(-deltaY, right);
        forward = rotX * forward;
        up = rotX * up;

        // Normalize to prevent drift
        forward = glm::normalize(forward);

        // Limiter l'angle vertical pour éviter le flip
        float pitch = glm::asin(glm::clamp(forward.y, -1.f, 1.f));
        const float maxPitch = glm::radians(89.0f);
        if (std::abs(pitch) > maxPitch) {
            float sign = forward.y > 0.f ? 1.f : -1.f;
            forward.y = glm::sin(sign * maxPitch);
            float horizMag = glm::sqrt(forward.x * forward.x + forward.z * forward.z);
            float targetHoriz = glm::cos(sign * maxPitch);
            if (horizMag > 0.001f) {
                forward.x = (forward.x / horizMag) * targetHoriz;
                forward.z = (forward.z / horizMag) * targetHoriz;
            }
            else {
                forward.x = targetHoriz;
                forward.z = 0;
            }
            forward = glm::normalize(forward);
        }

        // Update camera rotation to look in the new forward direction
        cam->transform.rotation = Quaternion::LookRotation(
            Vector3(forward.x, forward.y, forward.z));
    }

    else if (middleMouseDown && (mouseDelta.x != 0 || mouseDelta.y != 0)) {
        glm::vec3 camPos = cam->transform.position.ToGLM();
        glm::vec3 forward = cam->transform.forward().ToGLM();
        glm::vec3 right = cam->transform.right().ToGLM();
        glm::vec3 up = cam->transform.up().ToGLM();

        float distanceToPivot = glm::distance(camPos, pivotPoint);
        float panSpeed;
        if (ide.viewportOrtho) {
            panSpeed = ide.orthoSize * 0.002f;
        }
        else {
            panSpeed = std::clamp(distanceToPivot * 0.002f, 0.5f, 2.0f);
        }

        // INVERTED: horizontal follows mouse, vertical is flipped
        float deltaX = -mouseDelta.x * panSpeed;
        float deltaY = -mouseDelta.y * panSpeed;

        glm::vec3 panDelta = right * deltaX + up * deltaY;

        cam->transform.position = Vector3(
            camPos.x + panDelta.x,
            camPos.y + panDelta.y,
            camPos.z + panDelta.z
        );
        pivotPoint += panDelta;
    }
}

// =============================================================================
//  Viewport  — avec contrôles de caméra éditeur (Unity-style)
// =============================================================================
static void HandleShortcuts(IDEState& ide, bool& quitRequested)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    // Ctrl+N : New scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
        ide.bus.send("clearscene");
        ide.sceneDirty = false;
        RefreshSceneList(ide);
    }
    // Ctrl+S : Save scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        ide.showSaveModal = true;
    }
    // Ctrl+O : Load scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        ide.showLoadModal = true;
    }
    // Ctrl+Shift+S : Toggle snap
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        ide.snapEnabled = !ide.snapEnabled;
    }
    // Ctrl+Z : Undo
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (!ide.undoStack.empty()) {
            ide.undoStack.pop_back();
            ide.log.push(ConsoleLog::INFO, "[Edit] Undo");
        }
    }
    // Ctrl+D : Duplicate
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        if (!ide.selection.Empty()) {
            std::string newName = ide.selection.Primary() + "_copy";
            ide.bus.send("clone " + ide.selection.Primary() + " " + newName);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }
    // Delete key
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (!ide.selection.Empty()) {
            for (auto& name : ide.selection.items) {
                ide.bus.send("delete " + name);
            }
            ide.selection.Clear();
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }

    // Ctrl+O : Ortho/persp toggle
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        ide.viewportOrtho = !ide.viewportOrtho;
        ide.log.push(ConsoleLog::INFO, std::string("Camera: ") + (ide.viewportOrtho ? "Orthographic" : "Perspective"));
    }

    // F : Focus on selected with smooth transition
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl && !ide.selection.Empty()) {
        std::string primary = ide.selection.Primary();
        for (auto& obj : ide.objects) {
            if (obj.name == primary) {
                glm::vec3 target(obj.px, obj.py, obj.pz);

                // Calculate appropriate distance based on object size
                float objSize = (std::max)({ obj.sx, obj.sy, obj.sz });
                float distance = (objSize * 1.5f) + 3.0f;  // 1.5x size + 3 units margin

                // Use the fixed focus method
                ide.focus.Start(ide.sm->currentCamera, ide.editorFov,
                    ide.viewportOrtho, ide.orthoSize, target, distance);
                break;
            }
        }
    }

    // Tab : FPS fly mode toggle
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyCtrl) {
        ide.fpsFlyMode = !ide.fpsFlyMode;
        SDL_SetRelativeMouseMode(ide.fpsFlyMode ? SDL_TRUE : SDL_FALSE);
        ide.log.push(ConsoleLog::INFO, std::string("FPS fly mode ") + (ide.fpsFlyMode ? "ON" : "OFF"));
    }

    // Ctrl+1..9 : Restore bookmarks
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        for (int i = 0; i < 9; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey)((int)ImGuiKey_1 + i))) {
                ide.bookmarks.Restore(i, ide.sm->currentCamera, ide.viewportOrtho, ide.orthoSize, ide.editorFov);
                break;
            }
        }
    }
    // Ctrl+Alt+1..9 : Save bookmarks
    if (io.KeyCtrl && io.KeyAlt && !io.KeyShift) {
        for (int i = 0; i < 9; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey)((int)ImGuiKey_1 + i))) {
                ide.bookmarks.Save(i, ide.sm->currentCamera, ide.viewportOrtho, ide.editorFov, ide.orthoSize);
                ide.log.push(ConsoleLog::INFO, std::string("Bookmark ") + std::to_string(i + 1) + " saved");
                break;
            }
        }
    }

    // Alt+Left / Alt+Right : selection history
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ide.selectionHistory.CanGoBack()) {
        std::string prev = ide.selectionHistory.Back();
        if (!prev.empty()) {
            ide.selection.SetSingle(prev);
            ide.bus.send("inspect " + prev);
        }
    }
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ide.selectionHistory.CanGoForward()) {
        std::string next = ide.selectionHistory.Forward();
        if (!next.empty()) {
            ide.selection.SetSingle(next);
            ide.bus.send("inspect " + next);
        }
    }

    // Q/W/E/R : Tools
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q)) ide.toolMode = 0;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W)) ide.toolMode = 1;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E)) ide.toolMode = 2;
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_R)) ide.toolMode = 3;

    // Ctrl+P : Play / Stop
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) {
        ide.playing = !ide.playing;
        SDL_SetRelativeMouseMode(ide.playing ? SDL_TRUE : SDL_FALSE);
        ide.renderer->SetPlayMode(ide.playing);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
        static bool fullscreen = false;
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(ide.window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }

    // Escape : deselect or stop playing or exit FPS mode
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (ide.fpsFlyMode) {
            ide.fpsFlyMode = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
        }
        else if (ide.playing) {
            ide.playing = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            ide.renderer->SetPlayMode(false);
        }
        else if (!ide.selection.Empty()) {
            ide.selection.Clear();
        }
    }
}

// =============================================================================
//  Console
// =============================================================================
static void DrawViewportToolbar(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    ImGui::SetNextWindowPos(ImVec2(imagePos.x + 12, imagePos.y + 12));
    ImGui::SetNextWindowSize(ImVec2(480, 40));  // Légèrement élargi pour les nouveaux boutons
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{ 0.10f, 0.10f, 0.14f, 0.85f });
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{ 0.35f, 0.38f, 0.45f, 0.9f });

    ImGui::Begin("##ViewportToolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.22f, 0.22f, 0.28f, 0.95f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.35f, 0.35f, 0.45f, 0.95f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.5f, 0.65f, 0.85f, 0.95f });

    // View (Q)
    if (ide.toolMode == 0) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
    if (ImGui::Button(" Q ", ImVec2(44, 30))) ide.toolMode = 0;
    if (ide.toolMode == 0) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View mode (Q) - Orbit camera with right click + box selection");
    ImGui::SameLine();

    // Move (W)
    if (ide.toolMode == 1) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
    if (ImGui::Button(" W ", ImVec2(44, 30))) ide.toolMode = 1;
    if (ide.toolMode == 1) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move mode (W) - Translate selected object");
    ImGui::SameLine();

    // Rotate (E)
    if (ide.toolMode == 2) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
    if (ImGui::Button(" E ", ImVec2(44, 30))) ide.toolMode = 2;
    if (ide.toolMode == 2) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate mode (E) - Rotate selected object");
    ImGui::SameLine();

    // Scale (R)
    if (ide.toolMode == 3) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
    if (ImGui::Button(" R ", ImVec2(44, 30))) ide.toolMode = 3;
    if (ide.toolMode == 3) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale mode (R) - Scale selected object");
    ImGui::SameLine();

    ImGui::Dummy(ImVec2(8, 0));
    ImGui::SameLine();

    // Local/World toggle
    static bool localMode = true;
    if (localMode) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.3f, 0.95f });
    if (ImGui::Button(localMode ? " LOCAL " : " WORLD ", ImVec2(56, 30))) {
        localMode = !localMode;
        ide.gizmo.mode = localMode ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }
    if (localMode) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local / World transform space");
    ImGui::SameLine();

    // Pivot / Center toggle
    if (ide.gizmo.pivotCenter) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.3f, 0.95f });
    if (ImGui::Button(ide.gizmo.pivotCenter ? " PIVOT " : " CENTER ", ImVec2(56, 30))) {
        ide.gizmo.pivotCenter = !ide.gizmo.pivotCenter;
    }
    if (ide.gizmo.pivotCenter) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Pivot / Center manipulation");
    ImGui::SameLine();

    // Snap
    if (ide.snapEnabled) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.6f, 0.3f, 0.95f });
    if (ImGui::Button(ide.snapEnabled ? " SNAP " : " Snap ", ImVec2(56, 30))) {
        ide.snapEnabled = !ide.snapEnabled;
    }
    if (ide.snapEnabled) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to grid (Ctrl+Shift+S)");
    ImGui::SameLine();

    // Grid toggle
    if (ide.showGrid) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.5f, 0.95f });
    if (ImGui::Button(" GRID ", ImVec2(56, 30))) ide.showGrid = !ide.showGrid;
    if (ide.showGrid) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle grid overlay");
    ImGui::SameLine();

    // Frustum toggle (nouveau)
    if (ide.showFrustum) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.5f, 0.3f, 0.5f, 0.95f });
    if (ImGui::Button(" FRUSTUM ", ImVec2(60, 30))) ide.showFrustum = !ide.showFrustum;
    if (ide.showFrustum) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle camera frustum visualization");
    ImGui::SameLine();

    // Icons 3D toggle (nouveau)
    if (ide.showIcons3D) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.4f, 0.6f, 0.95f });
    if (ImGui::Button(" ICONS ", ImVec2(56, 30))) ide.showIcons3D = !ide.showIcons3D;
    if (ide.showIcons3D) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 3D icons for lights and cameras");

    // IMPORTANT: Pop styles in reverse order of pushes
    // 3 pushes de style var (ItemSpacing, FrameRounding, FramePadding)
    ImGui::PopStyleVar(3);
    // 3 pushes de style color (Button, ButtonHovered, ButtonActive)
    ImGui::PopStyleColor(3);

    ImGui::End();

    // 2 pushes de style color au début (WindowBg, Border)
    ImGui::PopStyleColor(2);
    // 2 pushes de style var au début (WindowRounding, WindowBorderSize)
    ImGui::PopStyleVar(2);
}

static void DrawViewportStats(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    std::string statsText;
    if (ide.playing) statsText = " " ICON_FA_PLAY " PLAYING ";
    if (ide.showStats) {
        if (!statsText.empty()) statsText += " | ";
        char fpsBuf[64];
        snprintf(fpsBuf, sizeof(fpsBuf), " %d FPS ", ide.statsFps > 0 ? ide.statsFps : 0);
        statsText += fpsBuf;
        char objBuf[64];
        snprintf(objBuf, sizeof(objBuf), " %zu objs ", ide.objects.size());
        statsText += objBuf;
        char lightBuf[64];
        snprintf(lightBuf, sizeof(lightBuf), " %zu lights ", ide.lights.size());
        statsText += lightBuf;
        if (ide.selection.Size() > 1) {
            char selBuf[64];
            snprintf(selBuf, sizeof(selBuf), " %zu selected ", ide.selection.Size());
            statsText += selBuf;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(statsText.c_str());
    ImVec2 statsPos = ImVec2(imagePos.x + imageSize.x - textSize.x - 16, imagePos.y + 12);
    dl->AddRectFilled(statsPos, ImVec2(statsPos.x + textSize.x + 12, statsPos.y + textSize.y + 8),
        IM_COL32(20, 20, 30, 200), 6.0f);
    dl->AddRect(statsPos, ImVec2(statsPos.x + textSize.x + 12, statsPos.y + textSize.y + 8),
        IM_COL32(60, 65, 80, 200), 6.0f);
    dl->AddText(ImVec2(statsPos.x + 6, statsPos.y + 4),
        ide.playing ? IM_COL32(100, 220, 100, 255) : IM_COL32(180, 185, 210, 255),
        statsText.c_str());
}

static void DrawSelectionToast(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::string selText = " Selected: " + ide.selection.Primary();
    if (ide.selection.Size() > 1) {
        selText += " (+" + std::to_string(ide.selection.Size() - 1) + " more)";
    }
    ImVec2 textSize = ImGui::CalcTextSize(selText.c_str());
    ImVec2 toastPos = ImVec2(imagePos.x + 12, imagePos.y + imageSize.y - textSize.y - 12);
    dl->AddRectFilled(toastPos, ImVec2(toastPos.x + textSize.x + 12, toastPos.y + textSize.y + 6),
        IM_COL32(40, 45, 60, 200), 4.0f);
    dl->AddText(ImVec2(toastPos.x + 6, toastPos.y + 3),
        IM_COL32(220, 220, 240, 255), selText.c_str());
}


static glm::vec3 GetCameraSpawnPos(IDEState& ide, float dist = 6.f)
{
    if (!ide.sm || !ide.sm->currentCamera) return glm::vec3(0.f);
    Camera* cam = ide.sm->currentCamera;
    glm::vec3 pos = cam->transform.position.ToGLM();
    glm::vec3 fwd = glm::normalize(cam->transform.forward().ToGLM());
    return pos + fwd * dist;
}


static void DrawCameraPositionOverlay(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    Camera* cam = ide.sm ? ide.sm->currentCamera : nullptr;
    if (!cam) return;

    glm::vec3 p = cam->transform.position.ToGLM();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "  CAM  X %.2f  Y %.2f  Z %.2f  ", p.x, p.y, p.z);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    float padX = 10.f, padY = 10.f;
    ImVec2 boxMin = ImVec2(imagePos.x + imageSize.x - textSize.x - padX * 2.f,
        imagePos.y + imageSize.y - textSize.y - padY * 2.f);
    ImVec2 boxMax = ImVec2(imagePos.x + imageSize.x - padX,
        imagePos.y + imageSize.y - padY);

    dl->AddRectFilled(boxMin, boxMax, IM_COL32(20, 22, 32, 210), 5.f);
    dl->AddRect(boxMin, boxMax, IM_COL32(70, 80, 110, 180), 5.f, 0, 1.f);
    dl->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY * 0.5f),
        IM_COL32(140, 200, 255, 255), buf);
}

static void DrawViewportPanel(IDEState& ide, float dt)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0,0 });
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int vw = (int)avail.x, vh = (int)avail.y;
    if (vw < 8) vw = 8; if (vh < 8) vh = 8;

    if (vw != ide.sceneFBO.w || vh != ide.sceneFBO.h)
    {
        ide.sceneFBO.resize(vw, vh);
        Settings::canvasWidth = vw;
        Settings::canvasHeight = vh;
    }

    glDisable(GL_SCISSOR_TEST);

    ide.sceneFBO.bind();
    ide.renderer->Render(dt, ide.sceneFBO.fbo);

    // Overlays 3D (grille, axes, icônes) après le rendu principal
    if (!ide.playing) {
        Camera* cam = ide.sm->currentCamera;
        glm::mat4 view = cam->transform.GetViewMatrix();
        glm::mat4 proj;
        if (ide.viewportOrtho) {
            float aspect = (float)vw / (float)vh;
            proj = glm::ortho(-ide.orthoSize * aspect, ide.orthoSize * aspect,
                -ide.orthoSize, ide.orthoSize, 0.1f, 1000.f);
        }
        else {
            proj = glm::perspective(glm::radians(ide.editorFov), (float)vw / (float)vh, 0.1f, 1000.f);
        }

        if (ide.showGrid) {
            DrawGrid(view, proj, 20.f, 20, ide.gridPlane);
        }

        if (ide.showIcons3D) {
            // Icônes pour les lumières
            for (auto& light : ide.lights) {
                if (light.lightType == "ambient") continue;
                glm::vec3 pos(light.px, light.py, light.pz);
                glm::vec3 color(light.r / 255.f, light.g / 255.f, light.b / 255.f);
                DrawBillboardIcon(pos, 0.3f, color, view, proj);
            }

            // Icônes pour les caméras (objets avec tag "Camera")
            for (auto& obj : ide.objects) {
                if (obj.tag == "Camera") {
                    glm::vec3 pos(obj.px, obj.py, obj.pz);
                    glm::vec3 color(0.2f, 0.8f, 0.8f); // Cyan pour caméras
                    DrawBillboardIcon(pos, 0.25f, color, view, proj);
                }
            }
        }

        if (ide.showFrustum && ide.sm->currentCamera) {
            Camera* cam = ide.sm->currentCamera;
            float fovRad = glm::radians(ide.editorFov);
            float aspect = (float)vw / (float)vh;
            DrawFrustum(cam->transform.position.ToGLM(),
                cam->transform.forward().ToGLM(),
                fovRad, aspect, 100.f,
                cam->transform.up().ToGLM(),
                view, proj);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_SCISSOR_TEST);

    // Afficher l'image du viewport
    ImGui::Image(
        (ImTextureID)(uintptr_t)ide.sceneFBO.color,
        avail,
        { 0,1 }, { 1,0 }
    );

    ImVec2 imagePos = ImGui::GetItemRectMin();
    ImVec2 imageSize = ImGui::GetItemRectSize();
    ide.viewportHovered = ImGui::IsItemHovered();

    // Gizmo (après l'image, avant les overlays UI)
    if (!ide.selection.Empty() && !ide.playing && (ide.toolMode >= 1 && ide.toolMode <= 3)) {
        Camera* cam = ide.sm->currentCamera;
        glm::mat4 view = cam->transform.GetViewMatrix();
        glm::mat4 proj;
        if (ide.viewportOrtho) {
            float aspect = (float)vw / (float)vh;
            proj = glm::ortho(-ide.orthoSize * aspect, ide.orthoSize * aspect,
                -ide.orthoSize, ide.orthoSize, 0.1f, 1000.f);
        }
        else {
            proj = glm::perspective(glm::radians(ide.editorFov), (float)vw / (float)vh, 0.1f, 1000.f);
        }

        // Mettre à jour les snaps du gizmo
        ide.gizmo.SetSnapFromIDE(ide.snapEnabled, ide.snapPosition, ide.snapRotation, ide.snapScale);

        std::string primary = ide.selection.Primary();

        // Chercher dans les objets
        bool found = false;
        for (auto& obj : ide.objects) {
            if (obj.name == primary) {
                glm::vec3 pos(obj.px, obj.py, obj.pz);
                glm::vec3 scale(obj.sx, obj.sy, obj.sz);
                glm::quat rot(obj.rw, obj.rx, obj.ry, obj.rz);

                bool changed = DrawGizmoForObject(primary, view, proj, ide.toolMode, ide.gizmo,
                    pos, rot, scale,
                    [&](const std::string& cmd) {
                        ide.bus.send(cmd);
                        ide.sceneDirty = true;
                    },
                    true);

                if (changed) {
                    // Mettre à jour les valeurs locales
                    obj.px = pos.x; obj.py = pos.y; obj.pz = pos.z;
                    obj.sx = scale.x; obj.sy = scale.y; obj.sz = scale.z;
                    obj.rw = rot.w; obj.rx = rot.x; obj.ry = rot.y; obj.rz = rot.z;
                    // Forcer un rafraîchissement de l'inspecteur
                    ide.bus.send("inspect " + primary);
                }
                found = true;
                break;
            }
        }

        // Si non trouvé dans les objets, chercher dans les lumières
        if (!found) {
            for (auto& light : ide.lights) {
                if (light.name == primary) {
                    // Pour les lumières, seul le déplacement est supporté
                    if (ide.toolMode == 1) {
                        glm::vec3 pos(light.px, light.py, light.pz);
                        glm::vec3 scale(1.0f, 1.0f, 1.0f);
                        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);

                        bool changed = DrawGizmoForObject(primary, view, proj, ide.toolMode, ide.gizmo,
                            pos, rot, scale,
                            [&](const std::string& cmd) {
                                ide.bus.send(cmd);
                                ide.sceneDirty = true;
                            },
                            true);

                        if (changed) {
                            light.px = pos.x; light.py = pos.y; light.pz = pos.z;
                            ide.bus.send("inspect " + primary);
                        }
                    }
                    break;
                }
            }
        }
    }

    // Box selection handling
    if (ide.viewportHovered && ide.toolMode == 0 && !ide.playing &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing()) {
        ide.boxSelect.Begin(ImGui::GetMousePos());
    }
    if (ide.boxSelect.active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        ide.boxSelect.Update(ImGui::GetMousePos());
        // Draw rectangle via ImDrawList
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto [mn, mx] = ide.boxSelect.GetRect();
        dl->AddRectFilled(mn, mx, IM_COL32(100, 160, 240, 40));
        dl->AddRect(mn, mx, IM_COL32(120, 180, 255, 200), 0.f, 0, 1.5f);
    }
    if (ide.boxSelect.active && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (ide.boxSelect.IsSignificant()) {
            // Raycast candidates from objects
            std::vector<RaycastCandidate> candidates;
            for (auto& obj : ide.objects) {
                RaycastCandidate rc;
                rc.name = obj.name;
                rc.center = glm::vec3(obj.px, obj.py, obj.pz);
                rc.halfSize = glm::vec3(obj.sx * 0.5f, obj.sy * 0.5f, obj.sz * 0.5f);
                candidates.push_back(rc);
            }
            Camera* cam = ide.sm->currentCamera;
            glm::mat4 view = cam->transform.GetViewMatrix();
            glm::mat4 proj;
            if (ide.viewportOrtho) {
                float aspect = (float)vw / (float)vh;
                proj = glm::ortho(-ide.orthoSize * aspect, ide.orthoSize * aspect,
                    -ide.orthoSize, ide.orthoSize, 0.1f, 1000.f);
            }
            else {
                proj = glm::perspective(glm::radians(ide.editorFov), (float)vw / (float)vh, 0.1f, 1000.f);
            }
            std::set<std::string> boxHits = BoxSelectObjects(ide.boxSelect, proj, view,
                imagePos.x, imagePos.y, imageSize.x, imageSize.y, candidates);
            if (ImGui::GetIO().KeyCtrl) {
                for (auto& name : boxHits) ide.selection.Toggle(name);
            }
            else {
                ide.selection.items = boxHits;
            }
            if (!boxHits.empty()) ide.bus.send("inspect " + *boxHits.begin());
        }
        ide.boxSelect.End();
    }

    // Raycast selection (clic simple)
    if (ide.viewportHovered && !ide.boxSelect.active && !ide.playing &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing() && ide.toolMode == 0) {

        std::vector<RaycastCandidate> candidates;
        for (auto& obj : ide.objects) {
            RaycastCandidate rc;
            rc.name = obj.name;
            rc.center = glm::vec3(obj.px, obj.py, obj.pz);
            rc.halfSize = glm::vec3(obj.sx * 0.5f, obj.sy * 0.5f, obj.sz * 0.5f);
            candidates.push_back(rc);
        }

        Camera* cam = ide.sm->currentCamera;
        glm::mat4 view = cam->transform.GetViewMatrix();
        glm::mat4 proj;
        if (ide.viewportOrtho) {
            float aspect = (float)vw / (float)vh;
            proj = glm::ortho(-ide.orthoSize * aspect, ide.orthoSize * aspect,
                -ide.orthoSize, ide.orthoSize, 0.1f, 1000.f);
        }
        else {
            proj = glm::perspective(glm::radians(ide.editorFov), (float)vw / (float)vh, 0.1f, 1000.f);
        }

        ImVec2 mousePos = ImGui::GetMousePos();
        std::string hit = RaycastObjects(mousePos.x, mousePos.y,
            imagePos.x, imagePos.y, imageSize.x, imageSize.y,
            proj, view, cam->transform.position.ToGLM(), candidates);

        if (!hit.empty()) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                ide.selection.Toggle(hit);
            }
            else {
                ide.selection.SetSingle(hit);
            }
            ide.bus.send("inspect " + hit);
        }
        else if (!ImGui::GetIO().KeyCtrl) {
            ide.selection.Clear();
        }
    }

    // ── Viewport drop targets ─────────────────────────────────────────────────
    // Must be called on the Image item that was just rendered so that ImGui
    // correctly reports the correct drop region.
    if (ImGui::BeginDragDropTarget()) {
        // ── 1. Hierarchy item → move object/light to world position ──────────
        if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload(kHierarchyDragPayload)) {
            auto* hp = (HierarchyDragPayload*)payload->Data;
            Camera* cam = ide.sm->currentCamera;
            glm::mat4 view = cam->transform.GetViewMatrix();
            glm::mat4 proj;
            if (ide.viewportOrtho) {
                float aspect = (float)vw / (float)vh;
                proj = glm::ortho(-ide.orthoSize * aspect, ide.orthoSize * aspect,
                    -ide.orthoSize, ide.orthoSize, 0.1f, 1000.f);
            }
            else {
                proj = glm::perspective(glm::radians(ide.editorFov),
                    (float)vw / (float)vh, 0.1f, 1000.f);
            }

            // Build a ray from the camera through the pixel under the mouse.
            // NDC coords: x in [-1,1] left→right, y in [-1,1] bottom→top.
            ImVec2 mousePos = ImGui::GetMousePos();
            float ndcX = (mousePos.x - imagePos.x) / imageSize.x * 2.f - 1.f;
            float ndcY = 1.f - (mousePos.y - imagePos.y) / imageSize.y * 2.f;

            // Unproject two NDC points (near/far) into world space.
            glm::mat4 invVP = glm::inverse(proj * view);
            auto unproject = [&](float nx, float ny, float nz) {
                glm::vec4 clip(nx, ny, nz, 1.f);
                glm::vec4 world = invVP * clip;
                if (std::abs(world.w) > 1e-7f) world /= world.w;
                return glm::vec3(world);
                };
            glm::vec3 rayNear = unproject(ndcX, ndcY, -1.f);
            glm::vec3 rayFar = unproject(ndcX, ndcY, 1.f);
            glm::vec3 rayDir = glm::normalize(rayFar - rayNear);
            glm::vec3 rayOrig = cam->transform.position.ToGLM();

            // Intersect with horizontal ground plane Y = 0.
            // ray(t) = rayOrig + t * rayDir  →  y=0  →  t = -rayOrig.y / rayDir.y
            if (std::abs(rayDir.y) > 1e-5f) {
                float t = -rayOrig.y / rayDir.y;
                if (t > 0.f) {
                    glm::vec3 hit = rayOrig + rayDir * t;
                    char cmd[256];
                    std::snprintf(cmd, sizeof(cmd), "move %s %.3f %.3f %.3f",
                        hp->name, hit.x, hit.y, hit.z);
                    ide.bus.send(cmd);
                    ide.sceneDirty = true;
                    RefreshSceneList(ide);
                }
            }
        }

        // ── 2. Asset payload → instantiate or show import overlay ────────────
        if (const ImGuiPayload* p =
            ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            auto* ap = (AssetDragPayload*)p->Data;
            std::string path(ap->path);
            std::string ext = fs::path(path).extension().string();
            std::string stem = fs::path(path).stem().string();

            // Generate unique name
            std::string objName = stem;
            {
                int n = 1;
                while (g_namedObjects.count(objName) || g_namedLights.count(objName))
                    objName = stem + "_" + std::to_string(n++);
            }

            glm::vec3 dropPos = GetCameraSpawnPos(ide);
            char posBuf[64];
            std::snprintf(posBuf, sizeof(posBuf), "%.3f %.3f %.3f",
                dropPos.x, dropPos.y, dropPos.z);

            if (ap->type == AssetType::Prefab || ext == ".honprefab") {
                // Prefab: add to scene immediately
                std::string cmd = "gltf " + objName + " \"" + path + "\" " + posBuf;
                ide.log.push(ConsoleLog::CMD, "> " + cmd);
                ide.bus.send(cmd);
                ide.pendingSelection = objName;
                ide.sceneDirty = true;
                if (!ide.assetDb.FindByPath(path))
                    ide.assetDb.Register(path);
                ide.assetBrowser.hasPendingDrop = true;
                ide.assetBrowser.pendingDrop = *ap;
                RefreshSceneList(ide);
            }
            else if (ap->type == AssetType::Model ||
                ext == ".obj" || ext == ".gltf" || ext == ".glb" ||
                ext == ".fbx" || ext == ".dae")
            {
                // 3-D model: open the Import Settings overlay with an Import button
                AssetRecord* rec = ide.assetDb.FindByPath(path);
                if (!rec) rec = &ide.assetDb.Register(path);

                ide.importOverlayGUID = rec->guid.ToString();
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = true;

                // Capture everything needed for the deferred import
                std::string capturedCmd_base = ext == ".obj"
                    ? "obj " + objName + " \"" + path + "\" " + posBuf
                    : "gltf " + objName + " \"" + path + "\" " + posBuf;
                std::string capturedName = objName;
                std::string capturedPath = path;
                AssetDragPayload capturedPayload = *ap;

                ide.importOverlayOnImport = [&ide, capturedCmd_base, capturedName,
                    capturedPath, capturedPayload]() mutable
                    {
                        AssetRecord* r = ide.assetDb.FindByPath(capturedPath);
                        float scale = (r && r->type == AssetType::Model)
                            ? r->modelSettings.importScale : 1.0f;

                        ide.log.push(ConsoleLog::CMD, "> " + capturedCmd_base);
                        ide.bus.send(capturedCmd_base);
                        if (scale != 1.0f) {
                            char sb[128];
                            std::snprintf(sb, sizeof(sb), "scale %s %.4f %.4f %.4f",
                                capturedName.c_str(), scale, scale, scale);
                            ide.bus.send(sb);
                        }
                        ide.pendingSelection = capturedName;
                        ide.sceneDirty = true;
                        if (!r) r = &ide.assetDb.Register(capturedPath);
                        ide.assetBrowser.hasPendingDrop = true;
                        ide.assetBrowser.pendingDrop = capturedPayload;
                        RefreshSceneList(ide);
                    };
            }
            else if (ap->type == AssetType::Scene || ext == ".honscene") {
                ide.bus.send("clearscene");
                ide.bus.send("loadscene " + path);
                ide.sceneDirty = false;
                RefreshSceneList(ide);
            }
            // Other types: just focus in the browser for now
            else {
                ide.assetBrowser.focusedGUID = std::string(ap->guidStr);
                ide.assetBrowser.selectedGUIDs = { std::string(ap->guidStr) };
            }
        }

        ImGui::EndDragDropTarget();
    }

    // Toolbar overlay
    DrawViewportToolbar(ide, imagePos, imageSize);

    // Stats overlay
    if (ide.showStats || ide.playing) {
        DrawViewportStats(ide, imagePos, imageSize);
    }

    // Selected object info toast
    if (!ide.selection.Empty() && !ide.playing) {
        DrawSelectionToast(ide, imagePos, imageSize);
    }

    // Camera position overlay (always visible in edit mode)
    if (!ide.playing) {
        DrawCameraPositionOverlay(ide, imagePos, imageSize);
    }

    // Update editor camera (orbite, pan, zoom)
    UpdateEditorCamera(ide, dt, vw, vh, imagePos, imageSize);

    // FPS fly mode indicator
    if (ide.fpsFlyMode && !ide.playing) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        std::string fpsText = " FPS FLY MODE (TAB to exit) ";
        ImVec2 textSize = ImGui::CalcTextSize(fpsText.c_str());
        ImVec2 pos = ImVec2(imagePos.x + imageSize.x * 0.5f - textSize.x * 0.5f, imagePos.y + imageSize.y - 50);
        dl->AddRectFilled(pos, ImVec2(pos.x + textSize.x + 16, pos.y + textSize.y + 8), IM_COL32(200, 100, 50, 200), 8.f);
        dl->AddText(ImVec2(pos.x + 8, pos.y + 4), IM_COL32(255, 200, 100, 255), fpsText.c_str());
    }

    ImGui::PopStyleVar();
}

static void DrawConsolePanel(IDEState& ide)
{
    ImGui::BeginChild("##console_log", { 0,-70 }, false, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lk(ide.log.mtx);
        for (auto& e : ide.log.entries)
        {
            ImVec4 col;
            switch (e.kind) {
            case ConsoleLog::CMD:       col = { 0.70f,0.85f,1.00f,1.f }; break;
            case ConsoleLog::REPLY_OK:  col = { 0.70f,0.95f,0.70f,1.f }; break;
            case ConsoleLog::REPLY_ERR: col = { 1.00f,0.50f,0.45f,1.f }; break;
            default:                    col = { 0.80f,0.80f,0.80f,1.f }; break;
            }
            ImGui::TextColored(col, "%s", e.text.c_str());
        }
        if (ide.log.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        if (ImGui::BeginPopupContextWindow("ConsoleContext")) {
            if (ImGui::MenuItem("Clear Console")) {
                std::lock_guard<std::mutex> lk(ide.log.mtx);
                ide.log.entries.clear();
                ide.logReadIdx = 0;
            }
            ImGui::Separator();
            ImGui::Checkbox("Auto-scroll", &ide.log.autoScroll);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // Multi-line input area (3 lines tall)
    ImGui::PushItemWidth(-80.f);
    bool enter = ImGui::InputTextMultiline("##cmdinput", ide.cmdInput, sizeof(ide.cmdInput),
        ImVec2(-1, 60),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Send button
    bool send = ImGui::Button("Send", ImVec2(60, 60));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Execute all commands in the input box\nPress Enter to execute");
    }

    // Process commands when Enter is pressed (without Ctrl) or Send button clicked
    if (enter || send) {
        std::string input(ide.cmdInput);
        if (!input.empty()) {
            // Split by newline and process each command
            std::vector<std::string> commands;
            std::stringstream ss(input);
            std::string line;
            while (std::getline(ss, line, '\n')) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (!line.empty()) {
                    commands.push_back(line);
                }
            }

            // Execute all commands
            for (const auto& cmd : commands) {
                ide.log.push(ConsoleLog::CMD, std::string("> ") + cmd);
                ide.bus.send(cmd);
            }

            // Clear input after execution
            ide.cmdInput[0] = '\0';
        }
        ImGui::SetKeyboardFocusHere(-1);
    }

    // Help button
    ImGui::SameLine();
    if (ImGui::Button("?", ImVec2(30, 60))) {
        ide.bus.send("help");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show command list");
    }

    // Clear console button
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(50, 60))) {
        std::lock_guard<std::mutex> lk(ide.log.mtx);
        ide.log.entries.clear();
        ide.logReadIdx = 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear console output");
    }

    // Additional hint text below buttons
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.6f, 0.6f, 0.7f, 1.0f });
    ImGui::TextDisabled("  (Ctrl+Enter = new line)");
    ImGui::PopStyleColor();
}

// =============================================================================
//  Modals
// =============================================================================
static void DrawModals(IDEState& ide)
{
    // Add Object modal
    if (ide.showAddObject) {
        ImGui::OpenPopup("Add Object");
        ide.showAddObject = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newObjPos[0] = sp.x; ide.newObjPos[1] = sp.y; ide.newObjPos[2] = sp.z;
    }
    if (ImGui::BeginPopupModal("Add Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static const char* kTypes[] = { "Plane", "OBJ mesh", "glTF / GLB", "Rectangle", "Sphere" };
        static const ObjectType kObjTypeMap[] = { PLANE, OBJ_MESH, GLTF_MESH, RECTANGLE, SPHERE };
        ImGui::InputText("Name", ide.newObjName, sizeof(ide.newObjName));
        {
            int comboObjIdx = 0;
            for (int i = 0; i < 6; ++i) if (kObjTypeMap[i] == ide.newObjType) { comboObjIdx = i; break; }
            if (ImGui::Combo("Type", &comboObjIdx, kTypes, 6)) ide.newObjType = kObjTypeMap[comboObjIdx];
        }
        ImGui::DragFloat3("Position", ide.newObjPos, 0.1f);
        if (ide.newObjType == CUBE)
            ImGui::DragFloat("Half-extent", &ide.newObjHalf, 0.01f, 0.01f, 100.f);
        if (ide.newObjType == SPHERE)
            ImGui::DragFloat("Radius", &ide.newObjHalf, 0.01f, 0.01f, 100.f);
        if (ide.newObjType == OBJ_MESH || ide.newObjType == GLTF_MESH)
            ImGui::InputText("File path", ide.newObjFile, sizeof(ide.newObjFile));
        ImGui::Combo("Color", &ide.newObjColor, kColorNames, kNumColors);

        if (ImGui::Button("Add", { 120,0 })) {
            char buf[512];
            std::string nm = NameValidator::GetFinalName(ide.newObjName, ide.objects, ide.lights);
            float* p = ide.newObjPos;
            switch (ide.newObjType) {
            case CUBE:
                std::snprintf(buf, sizeof(buf), "cube %s %.3f %.3f %.3f %.3f %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjHalf,
                    kColorNames[ide.newObjColor]);
                break;
            case PLANE:
                std::snprintf(buf, sizeof(buf), "plane %s %.3f %.3f %.3f 5.000 5.000 %s",
                    nm.c_str(), p[0], p[1], p[2], kColorNames[ide.newObjColor]);
                break;
            case OBJ_MESH:
                std::snprintf(buf, sizeof(buf), "obj %s \"%s\" %.3f %.3f %.3f",
                    nm.c_str(), ide.newObjFile, p[0], p[1], p[2]);
                break;
            case GLTF_MESH:
                std::snprintf(buf, sizeof(buf), "gltf %s \"%s\" %.3f %.3f %.3f",
                    nm.c_str(), ide.newObjFile, p[0], p[1], p[2]);
                break;
            case RECTANGLE:
                std::snprintf(buf, sizeof(buf), "rect %s %.3f %.3f %.3f 5.000 5.000 %s",
                    nm.c_str(), p[0], p[1], p[2],
                    kColorNames[ide.newObjColor]);
                break;
            case SPHERE:
                std::snprintf(buf, sizeof(buf), "sphere %s %.3f %.3f %.3f %.3f %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjHalf,
                    kColorNames[ide.newObjColor]);
                break;
            default: break;
            }
            ide.log.push(ConsoleLog::CMD, std::string("> ") + buf);
            ide.bus.send(buf);
            ide.pendingSelection = nm;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Add Light modal

    if (ide.showAddLight) {
        ImGui::OpenPopup("Add Light");
        ide.showAddLight = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newLightPos[0] = sp.x; ide.newLightPos[1] = sp.y; ide.newLightPos[2] = sp.z;
    }
    if (ImGui::BeginPopupModal("Add Light", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", ide.newLightName, sizeof(ide.newLightName));

        static const char* lightTypes[] = { "Point", "Directional", "Ambient" };
        static const ObjectType kLightTypeMap[] = { POINT_LIGHT, DIRECTIONAL_LIGHT, AMBIENT_LIGHT };
        {
            int comboLightIdx = 0;
            for (int i = 0; i < 3; ++i) if (kLightTypeMap[i] == ide.newLightType) { comboLightIdx = i; break; }
            if (ImGui::Combo("Type", &comboLightIdx, lightTypes, 3)) ide.newLightType = kLightTypeMap[comboLightIdx];
        }

        ImGui::DragFloat("Intensity", &ide.newLightIntensity, 0.05f, 0.f, 20.f);
        ImGui::ColorEdit3("Color", ide.newLightColor);

        if (ImGui::Button("Add", { 120,0 })) {
            char buf[256];
            int r = (int)(ide.newLightColor[0] * 255),
                g = (int)(ide.newLightColor[1] * 255),
                b = (int)(ide.newLightColor[2] * 255);
            const char* typeStr = (ide.newLightType == DIRECTIONAL_LIGHT) ? "directional"
                : (ide.newLightType == AMBIENT_LIGHT) ? "ambient"
                : "point";
            std::snprintf(buf, sizeof(buf),
                "addlight %s %.2f %d %d %d %s",
                ide.newLightName, ide.newLightIntensity, r, g, b, typeStr);
            ide.log.push(ConsoleLog::CMD, std::string("> ") + buf);
            ide.bus.send(buf);
            ide.pendingSelection = ide.newLightName;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Add Camera modal
    if (ide.showAddCamera) {
        ImGui::OpenPopup("Add Camera");
        ide.showAddCamera = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newCameraPos[0] = sp.x; ide.newCameraPos[1] = sp.y; ide.newCameraPos[2] = sp.z;
    }    if (ImGui::BeginPopupModal("Add Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", ide.newCameraName, sizeof(ide.newCameraName));
        ImGui::DragFloat3("Position", ide.newCameraPos, 0.1f);

        if (ImGui::Button("Add", { 120,0 })) {
            // Spawn a named camera by moving/duplicating the current editor camera
            // The engine doesn't expose addcamera over the bus, so we log a note and
            // register the name in the hierarchy via a lightweight object placeholder.
            char buf[256];
            // Use a zero-scale cube as a scene-graph placeholder; real camera spawning
            // should be wired to your SceneManager's cameras list in a future command.
            std::snprintf(buf, sizeof(buf), "cube %s %.3f %.3f %.3f 0.001 white",
                ide.newCameraName, ide.newCameraPos[0], ide.newCameraPos[1], ide.newCameraPos[2]);
            ide.log.push(ConsoleLog::CMD, std::string("> [camera] ") + ide.newCameraName);
            ide.bus.send(buf);
            ide.pendingSelection = ide.newCameraName;
            ide.sceneDirty = true;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Create Folder modal (UPDATED for multi-selection)
    if (ide.showCreateFolder) { ImGui::OpenPopup("Create Folder"); ide.showCreateFolder = false; }
    if (ImGui::BeginPopupModal("Create Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Folder name:");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##foldername", ide.newFolderName, sizeof(ide.newFolderName),
            ImGuiInputTextFlags_AutoSelectAll);

        // Option: include selected objects in the folder
        if (!ide.selection.Empty()) {
            ImGui::TextDisabled("  %zu selected item(s) will be moved into folder", ide.selection.Size());
        }

        ImGui::Spacing();
        if (ImGui::Button("Create", { 120,0 })) {
            std::string fname(ide.newFolderName);
            if (!fname.empty()) {
                bool dup = false;
                for (auto& r : ide.hierRoots) {
                    if (r.kind == HierarchyNode::FOLDER && r.name == fname) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    HierarchyNode folder;
                    folder.kind = HierarchyNode::FOLDER;
                    folder.name = fname;
                    folder.folderOpen = true;

                    // Move all selected objects into the folder
                    if (!ide.selection.Empty()) {
                        std::set<std::string> toMove = ide.selection.items;
                        for (auto it = ide.hierRoots.begin(); it != ide.hierRoots.end(); ) {
                            bool isMatch = false;
                            if (it->kind == HierarchyNode::OBJECT || it->kind == HierarchyNode::LIGHT) {
                                if (toMove.count(it->name)) {
                                    folder.children.push_back(*it);
                                    it = ide.hierRoots.erase(it);
                                    isMatch = true;
                                }
                            }
                            if (!isMatch) {
                                ++it;
                            }
                        }
                        // Clear selection after moving
                        ide.selection.Clear();
                    }
                    ide.hierRoots.push_back(folder);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Rename modal (UPDATED for multi-selection)
    if (ide.showRenameModal) { ImGui::OpenPopup("Rename Object"); ide.showRenameModal = false; }
    if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("New name for object:");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##rnname", ide.renameNewName, sizeof(ide.renameNewName),
            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Spacing();
        if (ImGui::Button("Rename", { 120,0 })) {
            std::string oldN(ide.renameOldName), newN(ide.renameNewName);
            if (!newN.empty() && newN != oldN) {
                ide.bus.send("rename " + oldN + " " + newN);
                // Update selection if the renamed object was selected
                if (ide.selection.Contains(oldN)) {
                    ide.selection.Remove(oldN);
                    ide.selection.Add(newN);
                }
                ide.sceneDirty = true;
                RefreshSceneList(ide);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Save Scene modal
    if (ide.showSaveModal) { ImGui::OpenPopup("Save Scene"); ide.showSaveModal = false; }
    if (ImGui::BeginPopupModal("Save Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save scene to file:");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##savepath", ide.sceneFilePath, sizeof(ide.sceneFilePath));
        ImGui::Spacing();
        if (ImGui::Button("Save", { 120,0 })) {
            ide.bus.send(std::string("savescene ") + ide.sceneFilePath);
            ide.sceneDirty = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Load Scene modal
    if (ide.showLoadModal) { ImGui::OpenPopup("Load Scene"); ide.showLoadModal = false; }
    if (ImGui::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Load scene from file:");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##loadpath", ide.sceneFilePath, sizeof(ide.sceneFilePath));
        ImGui::Spacing();
        ImGui::TextColored({ 1.f,0.6f,0.2f,1.f }, "  Warning: current scene will be replaced.");
        ImGui::Spacing();
        if (ImGui::Button("Load", { 120,0 })) {
            ide.bus.send("clearscene");
            ide.bus.send(std::string("loadscene ") + ide.sceneFilePath);
            ide.selection.Clear();  // Clear selection when loading new scene
            ide.sceneDirty = false;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Ship Game modal
    if (ide.showShipDialog) { ImGui::OpenPopup("Ship Game"); ide.showShipDialog = false; }
    if (ImGui::BeginPopupModal("Ship Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Package your project for distribution.");
        ImGui::Separator();
        ImGui::InputText("Source dir", ide.shipSrcDir, sizeof(ide.shipSrcDir));
        ImGui::InputText("Output dir", ide.shipDstDir, sizeof(ide.shipDstDir));
        ImGui::Spacing();
        ImGui::TextDisabled("The build will copy all assets, shaders, and the\n"
            "compiled binary. A .tar.gz archive is created alongside.");

        ImGui::Spacing();
        if (ImGui::Button("  Build & Ship  ", { 180,0 })) {
            ImGui::CloseCurrentPopup();
            std::string src(ide.shipSrcDir), dst(ide.shipDstDir);
            ConsoleLog* logPtr = &ide.log;
            std::thread([src, dst, logPtr] {
                ShipGame(src, dst, *logPtr);
                }).detach();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
// =============================================================================
//  Menu Bar  (Unity/UE-style: File, Edit, Scene, View, Build, Help,
//                  centred Play/Pause/Step, right-side stats badge)
// =============================================================================
static void DrawMenuBar(IDEState& ide, bool& quitRequested)
{
    if (!ImGui::BeginMainMenuBar()) return;
    // Utiliser des largeurs fixes pour éviter l'étalement
    const float SECTION_WIDTH = 80.0f;
    const float BUTTON_WIDTH = 70.0f;

    // --- FILE (section fixe) ---
    if (ImGui::BeginMenu("File", true))
    {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            ide.bus.send("clearscene");
            ide.sceneDirty = false;
            RefreshSceneList(ide);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) ide.showSaveModal = true;
        if (ImGui::MenuItem("Load", "Ctrl+O")) ide.showLoadModal = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Ship...")) ide.showShipDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) quitRequested = true;
        ImGui::EndMenu();
    }

    // --- PROJECT (new) ---
    if (ImGui::BeginMenu("Project", true))
    {
        if (ImGui::MenuItem("New Project...")) ide.projectUI.showNewProject = true;
        if (ImGui::MenuItem("Save Project...")) ide.projectUI.showSaveProjModal = true;
        if (ImGui::MenuItem("Load Project...")) ide.projectUI.showLoadProjModal = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Project Settings...")) ide.projectUI.showProjectSettings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Package Manager...")) ide.packageUI.show = true;
        ImGui::EndMenu();
    }

    // --- EDIT ---
    if (ImGui::BeginMenu("Edit", true))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ide.undoStack.empty())) {
            if (!ide.undoStack.empty()) ide.undoStack.pop_back();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !ide.selection.Empty())) {
            if (!ide.selection.Empty()) {
                std::string newName = ide.selection.Primary() + "_copy";
                ide.bus.send("clone " + ide.selection.Primary() + " " + newName);
                ide.sceneDirty = true;
                RefreshSceneList(ide);
            }
        }
        if (ImGui::MenuItem("Delete", "Del", false, !ide.selection.Empty())) {
            ide.bus.send("delete " + ide.selection.Primary());
            ide.selection.Clear();
            RefreshSceneList(ide);
        }
        ImGui::EndMenu();
    }

    // --- GAMEOBJECT ---
    if (ImGui::BeginMenu("GameObject", true))
    {
        if (ImGui::BeginMenu("3D Objects"))
        {
            if (ImGui::MenuItem(ICON_FA_SQUARE " Plane")) {
                ide.newObjType = PLANE;
                ide.showAddObject = true;
            }
            if (ImGui::MenuItem(ICON_FA_SQUARE " Rectangle")) {
                ide.newObjType = RECTANGLE;
                ide.showAddObject = true;
            }
            if (ImGui::MenuItem(ICON_FA_CIRCLE " Sphere")) {
                ide.newObjType = SPHERE;
                ide.showAddObject = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Lights"))
        {
            if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Ambient Light")) {
                ide.newLightType = AMBIENT_LIGHT;
                ide.showAddLight = true;
            }
            if (ImGui::MenuItem(ICON_FA_SUN " Directional Light")) {
                ide.newLightType = DIRECTIONAL_LIGHT;
                ide.showAddLight = true;
            }
            if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT " Point Light")) {
                ide.newLightType = POINT_LIGHT;
                ide.showAddLight = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem(ICON_FA_VIDEO " Camera")) {
                ide.showAddCamera = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- OUTILS ---
    if (ImGui::BeginMenu("Tools", true))
    {
        if (ImGui::MenuItem("View", "Q", ide.toolMode == 0)) ide.toolMode = 0;
        if (ImGui::MenuItem("Move", "W", ide.toolMode == 1)) ide.toolMode = 1;
        if (ImGui::MenuItem("Rotate", "E", ide.toolMode == 2)) ide.toolMode = 2;
        if (ImGui::MenuItem("Scale", "R", ide.toolMode == 3)) ide.toolMode = 3;
        ImGui::EndMenu();
    }

    // --- VIEW ---
    if (ImGui::BeginMenu("View", true))
    {
        if (ImGui::MenuItem("Wireframe", nullptr, ide.wireframe)) {
            ide.wireframe = !ide.wireframe;
            ide.bus.send(ide.wireframe ? "wireframe on" : "wireframe off");
        }
        if (ImGui::MenuItem("Stats", nullptr, ide.showStats)) {
            ide.showStats = !ide.showStats;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Debug Mode")) {
            if (ImGui::MenuItem("Shaded", nullptr, ide.debugMode == 0)) { ide.debugMode = 0; ide.renderer->debugMode = 0; }
            if (ImGui::MenuItem("Wireframe", nullptr, ide.debugMode == 1)) { ide.debugMode = 1; ide.renderer->debugMode = 1; }
            if (ImGui::MenuItem("Overdraw", nullptr, ide.debugMode == 2)) { ide.debugMode = 2; ide.renderer->debugMode = 2; }
            if (ImGui::MenuItem("Depth", nullptr, ide.debugMode == 3)) { ide.debugMode = 3; ide.renderer->debugMode = 3; }
            if (ImGui::MenuItem("Normals", nullptr, ide.debugMode == 4)) { ide.debugMode = 4; ide.renderer->debugMode = 4; }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- LAYOUT ---
    if (ImGui::BeginMenu("Layout", true)) {
        if (ImGui::MenuItem("Save Layout")) {
            SaveDockLayout(ide.editorSettings.dockLayoutFile);
            ide.toastMgr.Push("Layout saved", Toast::Success, 2.0f);
        }
        if (ImGui::MenuItem("Preferences...")) {
            // Show preferences window
            static bool prefsOpen = true;
            prefsOpen = true;
            DrawPreferencesWindow(ide.editorSettings, prefsOpen);
        }
        ImGui::EndMenu();
    }

    // --- HELP ---
    if (ImGui::BeginMenu("Help", true))
    {
        if (ImGui::MenuItem("Commands")) ide.bus.send("help");
        if (ImGui::MenuItem("Shortcuts")) {
            ide.log.push(ConsoleLog::INFO,
                "F1=Help  Ctrl+N=New  Ctrl+S=Save  Ctrl+O=Load  Ctrl+D=Duplicate  Del=Delete  Q/W/E/R=Tools");
        }
        ImGui::EndMenu();
    }

    // --- Espacement centralisé pour Play/Stop ---
    float menuWidth = ImGui::GetContentRegionAvail().x;
    float playWidth = 100.0f;
    float statsWidth = ide.showStats ? 120.0f : 60.0f;
    float leftSpacer = (menuWidth - playWidth - statsWidth) * 0.45f;

    if (leftSpacer > 0) {
        ImGui::Dummy(ImVec2(leftSpacer, 0.0f));
        ImGui::SameLine();
    }

    // --- Bouton Play/Stop (style compact) ---
    ImGui::PushStyleColor(ImGuiCol_Button,
        ide.playing ? ImVec4{ 0.15f, 0.55f, 0.15f, 1.0f } : ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f });
    if (ImGui::Button(ide.playing ? " STOP " : " PLAY ", ImVec2(playWidth, 0))) {
        ide.playing = !ide.playing;
        SDL_SetRelativeMouseMode(ide.playing ? SDL_TRUE : SDL_FALSE);
        // Reset editor camera position when stopping
        if (!ide.playing && ide.sm->currentCamera) {
            ide.sm->currentCamera->transform.position = Vector3(0, 5, 15);
        }
        // Delegate Start()/OnDestroy() calls and play-state tracking to the
        // renderer so all game-logic lifecycle is in one place.
        ide.renderer->SetPlayMode(ide.playing);
        if (ide.playing)
            ide.log.push(ConsoleLog::REPLY_OK, "Play mode started");
        else
            ide.log.push(ConsoleLog::REPLY_OK, "Play mode stopped");
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // --- Stats (display compact) ---
    if (ide.showStats && ide.statsFps > 0) {
        ImGui::TextDisabled("%d FPS | %zu obj", ide.statsFps, ide.objects.size());
    }
    else {
        ImGui::Dummy(ImVec2(statsWidth - 20, 0.0f));
    }

    // --- Unsaved indicator (position fixe à droite) ---
    float rightMargin = ImGui::GetContentRegionAvail().x - 70.0f;
    if (rightMargin > 0 && ide.sceneDirty) {
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(rightMargin, 0.0f));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0f, 0.7f, 0.2f, 1.0f });
        ImGui::Text("*");
        ImGui::PopStyleColor();
    }

    ImGui::EndMainMenuBar();
}

// =============================================================================
//  Configuration du layout fixe (Unity-style)
// =============================================================================
static void SetupDockSpace(IDEState& ide) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceWindow", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ide.mainDockspaceId = dockspaceId;

    // Appliquer le layout par défaut UNIQUEMENT si c'est la première fois
    static bool firstRun = true;
    if (firstRun && ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
        firstRun = false;


        // Vérifier si des fenêtres sont déjà dockées
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        if (node && node->IsLeafNode() && node->Windows.Size == 0) {
            // Aucune fenêtre dockée - configurer le layout par défaut
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockLeftAndCenter;
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.22f, nullptr, &dockLeftAndCenter);

            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockLeftAndCenter, ImGuiDir_Left, 0.25f, nullptr, &dockLeftAndCenter);
            ImGuiID dockCenter = dockLeftAndCenter;

            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.27f, nullptr, &dockCenter);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            ImGui::DockBuilderDockWindow("Console", dockBottom);
            ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    ImGui::End();
}


// =============================================================================
//  MainScene_Run — Boucle principale corrigée
// =============================================================================
void MainScene_Run() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) { std::cerr << "SDL_Init failed\n"; return; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    int ideW = 1600, ideH = 960;
    SDL_Window* win = SDL_CreateWindow("HonHon Engine IDE", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ideW, ideH, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { std::cerr << "SDL_CreateWindow failed\n"; SDL_Quit(); return; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { std::cerr << "GLAD failed\n"; SDL_DestroyWindow(win); SDL_Quit(); return; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    std::string fontPaths = "assets/JetBrainsMono-Medium.ttf";
    const std::string faPaths = "assets/fa-solid-900.ttf";

    std::string jetbrainsPath;
    std::ifstream f(fontPaths);
    if (f.good()) {
        jetbrainsPath = fontPaths;
    }

    std::string faPath;
    std::ifstream f2(faPaths);
    if (f2.good()) {
        faPath = faPaths;
    }

    static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    float defaultFontSize = 16.0f;
    float iconFontSize = 16.0f;

    ImFont* defaultFont = nullptr;

    if (!jetbrainsPath.empty()) {
        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;

        defaultFont = io.Fonts->AddFontFromFileTTF(
            jetbrainsPath.c_str(),
            defaultFontSize,
            &fontConfig,
            io.Fonts->GetGlyphRangesDefault()
        );
        if (!defaultFont) {
            defaultFont = io.Fonts->AddFontDefault();
        }
    }
    else {
        defaultFont = io.Fonts->AddFontDefault();
    }

    if (!faPath.empty()) {
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.FontDataOwnedByAtlas = false;
        iconConfig.GlyphMinAdvanceX = iconFontSize;
        io.Fonts->AddFontFromFileTTF(
            faPath.c_str(),
            iconFontSize,
            &iconConfig,
            iconRanges
        );
    }

    if (defaultFont) {
        ImFontConfig titleConfig;
        titleConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromFileTTF(
            jetbrainsPath.c_str(),
            20.0f,
            &titleConfig,
            io.Fonts->GetGlyphRangesDefault()
        );
    }

    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ApplyIDEStyle();

    Settings::canvasWidth = 900; Settings::canvasHeight = 600;
    SceneManager* sm = new SceneManager();
    Camera* camera = new Camera(Vector3(0.0, 5.0, 15.0), Quaternion::LookRotation(Vector3(0, 0, -1)));
    sm->cameras->push_back(camera);
    sm->currentCamera = camera;
    GPURenderer renderer(sm, win, ctx);

    renderer.RegisterShader("skinned", SKINNED_VERT, SKINNED_FRAG);


    BasicMovements player(sm);
    player.eyeHeight = 1.7;
    player.moveSpeed = 0.12;
    player.flySpeed = 0.3;
    player.sdlKeys = SdlBindings::WASD();

    IDEState ide;
    ide.sm = sm;
    ide.renderer = &renderer;
    ide.player = &player;
    ide.sceneFBO.init(Settings::canvasWidth, Settings::canvasHeight);
    ide.bookmarks.InitFromCurrent(camera, false, 60.f, 10.f);
    ide.window = win;

    sm->scriptManager = std::make_unique<ScriptManager>(sm);

    ScriptManager::SetToolchainPath("./tools/mingw64");
    ide.scriptManager = sm->scriptManager.get();

    ImGuizmo::SetRect(0, 0, (float)Settings::canvasWidth, (float)Settings::canvasHeight);

    // Load editor settings
    ide.editorSettings.Load("ide_settings.ini");

    static bool firstRun = true;

    // ── Project: check for recovery files on startup ──────────────────────────
    {
        auto recoveries = AutoSaveManager::ListRecovery(ide.project.rootFolder);
        if (!recoveries.empty()) {
            ide.projectUI.recoveryFiles = recoveries;
            ide.projectUI.showRecoveryToast = true;
        }
    }

    // ── Asset browser: initial scan ───────────────────────────────────────────
    ide.assetBrowser.currentDir = "./assets/";
    ide.assetBrowser.dirDirty = true;
    // Load persisted asset database, then rescan to catch new/changed files
    ide.assetDb.Load(".honassets");
    if (fs::exists("./assets/"))
        ide.assetDb.ScanDirectory("./assets/");

    // ── Package manager: seed built-in packages ───────────────────────────────
    {
        PackageRecord core;
        core.id = "honhon.core"; core.displayName = "HonHon Core";
        core.description = "Core engine runtime (always present).";
        core.type = PackageType::Library; core.isBuiltIn = true;
        core.version = { 1, 0, 0 }; core.installedVersion = { 1, 0, 0 };
        core.status = PackageStatus::UpToDate;
        ide.packageMgr.packages.push_back(core);

        PackageRecord physics;
        physics.id = "honhon.physics"; physics.displayName = "Physics Plugin";
        physics.description = "Rigid body simulation and collision detection.";
        physics.type = PackageType::PhysicsPlugin;
        physics.version = { 0, 9, 2 };
        physics.status = PackageStatus::Available;
        ide.packageMgr.packages.push_back(physics);

        PackageRecord audio;
        audio.id = "honhon.audio"; audio.displayName = "Audio Plugin";
        audio.description = "3D positional audio and streaming support.";
        audio.type = PackageType::AudioPlugin;
        audio.version = { 0, 7, 0 };
        audio.status = PackageStatus::Available;
        ide.packageMgr.packages.push_back(audio);

        // Load persisted package list if it exists
        ide.packageMgr.Load(".honpackages");
    }

    CommandRegistry reg;
    BuildCommands(reg, &renderer, &ide.assetDb, sm);
    CmdContext cmdCtx{ sm, &renderer, &g_sceneMutex };
    std::atomic<bool> running{ true };
    std::thread shellThr(ShellThread, &ide.bus, &reg, &cmdCtx, &running, &ide.log);
    ide.bus.send("list");
    // Avant d'entrer dans la boucle, on s'assure que g_deferredLog pointe vers le log de l'IDE
    g_deferredLog = &ide.log;

    using Clock = std::chrono::high_resolution_clock;
    auto tPrev = Clock::now();
    bool quit = false;
    while (!quit) {
        auto tNow = Clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        if (dt > 0.1f) dt = 0.1f;

        // --- Exécuter les tâches GLTF différées --------------------------------
        {
            std::vector<DeferredTask> tasksToProcess;
            {
                std::lock_guard<std::mutex> lk(g_deferredTasksMutex);
                tasksToProcess.swap(g_deferredGLTFTasks); // vide la file localement
            }
            for (const auto& task : tasksToProcess) {
                // L'import s'exécute maintenant sur le thread principal, donc avec un contexte GL valide.
                auto result = Importer::ImportFromGLTF<TextureManager>(task.path, &ide.renderer->texManager);
                if (!result.ok) {
                    ide.log.push(ConsoleLog::REPLY_ERR, "[GLTF] " + task.name + " failed: " + result.error);
                    continue;
                }
                BaseObject* obj = result.object;
                if (!obj) {
                    ide.log.push(ConsoleLog::REPLY_ERR, "[GLTF] " + task.name + " returned null");
                    continue;
                }
                obj->transform.position = Vector3(task.x, task.y, task.z);
                {
                    std::unique_lock<std::shared_mutex> lock(g_sceneMutex);
                    ide.sm->objects->push_back(obj);
                    g_namedObjects[task.name] = obj;
                }
                ide.log.push(ConsoleLog::REPLY_OK, "[GLTF] " + task.name + " imported successfully");
                ide.sceneDirty = true;
                ide.pendingSelection = task.name; // on veut sélectionner le nouvel objet
                RefreshSceneList(ide);            // demande une mise à jour de la hiérarchie
            }
        }
        // -----------------------------------------------------------------------

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) quit = true;
        }

        // Process keyboard shortcuts after ImGui has captured its own keys
        HandleShortcuts(ide, quit);
        UpdateFPSCamera(ide, dt);

        if (ide.playing) {
            int mx, my; SDL_GetRelativeMouseState(&mx, &my);
            if (mx || my) player.TickMouse(-mx, -my);
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            player.TickKeys(ks, dt);
        }

        {
            std::lock_guard<std::mutex> lk(ide.log.mtx);
            size_t total = ide.log.entries.size();
            if (ide.logReadIdx > total) ide.logReadIdx = total;
            for (size_t ei = ide.logReadIdx; ei < total; ++ei) {
                auto& e = ide.log.entries[ei];
                if (e.kind == ConsoleLog::REPLY_OK) {
                    std::string c = JsonGet(e.text, "cmd");
                    if (c == "list") {
                        // Snapshot object names before parse
                        std::vector<std::string> prevNames;
                        for (auto& o : ide.objects) prevNames.push_back(o.name);

                        ParseListReply(e.text, ide);

                        // ── Sub-object population ────────────────────────────
                        // If a model was just dropped, find newly-added objects
                        // and register them as sub-objects on the asset record.
                        if (ide.assetBrowser.hasPendingDrop) {
                            ide.assetBrowser.hasPendingDrop = false;
                            std::string dropPath(ide.assetBrowser.pendingDrop.path);
                            AssetRecord* rec = ide.assetDb.FindByPath(dropPath);
                            if (rec) {
                                // Collect names that are new since last list
                                std::unordered_set<std::string> prevSet(prevNames.begin(), prevNames.end());
                                for (auto& obj : ide.objects) {
                                    if (!prevSet.count(obj.name)) {
                                        // Check it isn't already a sub-object
                                        bool already = false;
                                        for (auto& so : rec->subObjects)
                                            if (so.name == obj.name) { already = true; break; }
                                        if (!already) {
                                            AssetSubObject sub;
                                            sub.name = obj.name;
                                            sub.index = (int)rec->subObjects.size();
                                            sub.type = AssetType::Model;
                                            sub.enabled = true;
                                            rec->subObjects.push_back(sub);
                                        }
                                    }
                                }
                                ide.assetDb.Save(".honassets");
                            }
                        }
                    }
                    if (c == "inspect") ParseInspectReply(e.text, ide);
                }
            }
            ide.logReadIdx = total;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        SetupDockSpace(ide);

        // Draw welcome modal on first run
        DrawWelcomeModal(ide, firstRun);


        if (ide.focus.active) {
            ide.focus.Update(dt, ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize);
        }

        ImGui::Begin("Viewport");
        DrawViewportPanel(ide, dt);
        ImGui::End();

        ImGui::Begin("Hierarchy");
        DrawHierarchyPanel(ide);
        ImGui::End();
        ImGui::Begin("Inspector");
        // Record Inspector window pos/size so the overlay can anchor above it
        ide.importOverlayAnchorPos = ImGui::GetWindowPos();
        ide.importOverlayAnchorSize = ImGui::GetWindowSize();
        DrawInspectorPanel(ide);
        ImGui::End();
        ImGui::Begin("Console");
        DrawConsolePanel(ide);
        ImGui::End();
        ImGui::Begin("Asset Browser");
        DrawAssetBrowserPanel(
            ide.assetBrowser,
            ide.assetDb,
            dt,
            [&](const std::string& path, AssetType t) {
                std::string ext = fs::path(path).extension().string();
                std::string stem = fs::path(path).stem().string();

                // Ensure unique object name
                std::string objName = stem;
                {
                    int n = 1;
                    while (g_namedObjects.count(objName) || g_namedLights.count(objName))
                        objName = stem + "_" + std::to_string(n++);
                }

                // Fetch import settings
                AssetRecord* rec = ide.assetDb.FindByPath(path);
                float importScale = 1.0f;
                if (rec && rec->type == AssetType::Model)
                    importScale = rec->modelSettings.importScale;

                std::string cmd;
                glm::vec3 dropPos = GetCameraSpawnPos(ide);
                char posBuf[64];
                std::snprintf(posBuf, sizeof(posBuf), "%.3f %.3f %.3f", dropPos.x, dropPos.y, dropPos.z);
                if (ext == ".obj")
                    cmd = "obj " + objName + " \"" + path + "\" " + posBuf;
                else if (ext == ".gltf" || ext == ".glb")
                    cmd = "gltf " + objName + " \"" + path + "\" " + posBuf;
                else if (ext == ".honscene") {
                    ide.bus.send("clearscene");
                    ide.bus.send("loadscene " + path);
                    ide.sceneDirty = false;
                    RefreshSceneList(ide);
                    return;
                }

                if (!cmd.empty()) {
                    ide.log.push(ConsoleLog::CMD, "> " + cmd);
                    ide.bus.send(cmd);
                    if (importScale != 1.0f) {
                        char scaleBuf[128];
                        std::snprintf(scaleBuf, sizeof(scaleBuf),
                            "scale %s %.4f %.4f %.4f",
                            objName.c_str(), importScale, importScale, importScale);
                        ide.bus.send(scaleBuf);
                    }
                    ide.pendingSelection = objName;
                    ide.sceneDirty = true;
                    // Register and mark sub-object scan pending
                    if (!rec) rec = &ide.assetDb.Register(path);
                    ide.assetBrowser.hasPendingDrop = true;
                    AssetDragPayload fake{};
                    strncpy_s(fake.path, sizeof(fake.path), path.c_str(), _TRUNCATE);                    fake.type = t;
                    ide.assetBrowser.pendingDrop = fake;
                    RefreshSceneList(ide);
                }
            });
        ImGui::End();

        // ── Open import overlay whenever the browser focuses a new asset ──────
        if (!ide.assetBrowser.focusedGUID.empty() &&
            ide.assetBrowser.focusedGUID != ide.importOverlayGUID)
        {
            AssetRecord* focusedRec = ide.assetDb.FindByGUID(ide.assetBrowser.focusedGUID);
            if (focusedRec) {
                ide.importOverlayGUID = ide.assetBrowser.focusedGUID;
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = false;
                ide.importOverlayOnImport = nullptr;
            }
        }

        // ── Draw the floating Import Settings Overlay ─────────────────────────
        {
            AssetRecord* ovRec = ide.importOverlayGUID.empty()
                ? nullptr
                : ide.assetDb.FindByGUID(ide.importOverlayGUID);
            std::function<void()> importCb = ide.importOverlayHasImportBtn
                ? ide.importOverlayOnImport
                : std::function<void()>(nullptr);
            DrawImportSettingsOverlay(
                ovRec,
                ide.showImportOverlay,
                ide.importOverlayAnchorPos,
                ide.importOverlayAnchorSize,
                importCb);
        }

        // ── Project / Package manager windows (floating, non-docked) ─────────
        DrawProjectSettingsWindow(ide.projectUI, ide.project);
        DrawPackageManagerWindow(ide.packageUI, ide.packageMgr);

        // ── Project modals ────────────────────────────────────────────────────
        DrawNewProjectModal(ide.projectUI, ide.project);
        DrawProjectSaveLoadModals(ide.projectUI, ide.project);

        // ── Auto-save recovery toast ──────────────────────────────────────────
        std::string recovered = DrawRecoveryToast(ide.projectUI, dt);
        if (!recovered.empty()) {
            ide.bus.send("clearscene");
            ide.bus.send("loadscene " + recovered);
            ide.sceneDirty = false;
            RefreshSceneList(ide);
        }

        DrawMenuBar(ide, quit);
        DrawModals(ide);
        DrawGlobalSearch(ide.globalSearch, ide,
            [&](const std::string& name) {
                ide.selection.SetSingle(name);
                ide.bus.send("inspect " + name);
            },
            [&](const std::string& guid) {
                ide.assetBrowser.focusedGUID = guid;
                ide.assetBrowser.selectedGUIDs = { guid };
            });

        // Toast notifications at end of frame
        ide.toastMgr.DrawToasts(dt);

        // Progress bar
        ide.progressState.Draw(dt);

        ImGui::Render();
        glViewport(0, 0, ideW, ideH);
        glClearColor(0.07f, 0.075f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window* bkWin = SDL_GL_GetCurrentWindow();
            SDL_GLContext bkCtx = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(bkWin, bkCtx);
        }
        SDL_GL_SwapWindow(win);

        static float refreshTimer = 0.f;
        refreshTimer += dt;
        if (refreshTimer > 2.0f) { ide.bus.send("list"); refreshTimer = 0.f; }

        // ── Deleted-asset scene notification ──────────────────────────────────
        // If any DB record's file was deleted from disk (e.g. via bulk delete in
        // the browser), remove scene objects whose names match sub-objects of that
        // asset and unregister the record.
        {
            static float deleteScanTimer = 0.f;
            deleteScanTimer += dt;
            if (deleteScanTimer >= 1.5f) {
                deleteScanTimer = 0.f;
                std::vector<std::string> toUnregister;
                for (auto& [gstr, rec] : ide.assetDb.records) {
                    // Skip if file still exists
                    if (fs::exists(fs::path(rec.path))) continue;
                    // File gone — remove any sub-objects from scene
                    for (auto& so : rec.subObjects) {
                        bool inScene = false;
                        for (auto& obj : ide.objects)
                            if (obj.name == so.name) { inScene = true; break; }
                        if (inScene) {
                            ide.bus.send("delete " + so.name);
                            if (ide.selection.Contains(so.name))
                                ide.selection.Remove(so.name);
                            ide.sceneDirty = true;
                        }
                    }
                    toUnregister.push_back(gstr);
                }
                if (!toUnregister.empty()) {
                    for (auto& g : toUnregister)
                        ide.assetDb.Unregister(g);
                    ide.assetDb.Save(".honassets");
                    RefreshSceneList(ide);
                }
            }
        }

        // ── Auto-save tick ────────────────────────────────────────────────────
        ide.autoSave.Tick(dt, ide.project, ide.sceneDirty, ide.sceneFilePath,
            [&](const std::string& slot) -> bool {
                ide.bus.send("savescene " + slot);
                return true;
            });

        // ── Script file watcher (polls every 3 s, same cadence as asset rescan)
        {
            static float scriptWatchTimer = 0.f;
            scriptWatchTimer += dt;
            if (scriptWatchTimer >= 3.0f) {
                scriptWatchTimer = 0.f;
                for (auto& [gstr, rec] : ide.assetDb.records) {
                    if (rec.type != AssetType::Script) continue;
                    if (!rec.scriptSettings.autoRecompile) continue;
                    if (rec.scriptSettings.treatAsHeader) continue;
                    try {
                        auto mtime = fs::last_write_time(fs::path(rec.path));
                        // Store mtime as a 64-bit count in fileSize temporarily
                        // (we use a parallel static map so we don't corrupt fileSize)
                        static std::unordered_map<std::string, fs::file_time_type> s_mtimes;
                        auto it = s_mtimes.find(gstr);
                        if (it == s_mtimes.end()) {
                            s_mtimes[gstr] = mtime; // first observation
                        }
                        else if (mtime != it->second) {
                            it->second = mtime;
                            // File changed — hot reload via ScriptManager
                            if (ide.scriptManager) {
                                ide.scriptManager->HotReloadScript(gstr);
                                ide.log.push(ConsoleLog::INFO,
                                    "[Script] Hot reload triggered for: " + rec.path);
                            }
                            else {
                                // Fallback: plain recompile
                                std::string compileCmd = rec.scriptSettings.compileCommand;
                                if (compileCmd.empty())
                                    compileCmd = "g++ -std=c++17 -c \"" + rec.path + "\" 2>&1";
                                ide.log.push(ConsoleLog::INFO,
                                    "[Script] File changed, recompiling: " + rec.path);
                                ConsoleLog* logPtr = &ide.log;
                                std::string cmdCopy = compileCmd;
                                std::string pathCopy = rec.path;
                                std::thread([cmdCopy, pathCopy, logPtr] {
                                    int ret = std::system(cmdCopy.c_str());
                                    logPtr->push(
                                        ret == 0 ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR,
                                        "[Script] " + pathCopy
                                        + (ret == 0 ? " compiled OK" : " compile FAILED"));
                                    }).detach();
                            }
                        }
                    }
                    catch (...) {}
                }
            }
        }
    }

    running = false;
    ide.bus.alive = false;
    if (shellThr.joinable()) shellThr.join();
    ide.sceneFBO.destroy();
    ide.packageMgr.Save(".honpackages");
    ide.assetDb.Save(".honassets");
    ide.editorSettings.Save("ide_settings.ini");  // Save editor settings
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    renderer.cleanup();
    delete sm;
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    MainScene_Run();
    return 0;
}