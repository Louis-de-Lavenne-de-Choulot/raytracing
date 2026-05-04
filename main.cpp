// main.cpp — HonHon Graphics Engine Scene Shell
// ─────────────────────────────────────────────────────────────────────────────
// Interactive runtime command shell driven from stdin.
// Designed to be driven by an IDE or tool: every command emits a single-line
// JSON response on stdout so the host can parse results without screen-scraping.
//
// Response envelope (always one line, always valid JSON):
//   {"ok":true,  "cmd":"move", "msg":"'box' moved to (1,2,3)", "data":{...}}
//   {"ok":false, "cmd":"move", "msg":"Unknown object 'box'"}
//
// The shell runs on a background thread — the render loop is never blocked.
// Add new commands in BuildCommands() only; no other file needs changing.
// ─────────────────────────────────────────────────────────────────────────────
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <cmath>
#include <chrono>
#include <iomanip>   // std::quoted

#include <SDL.h>

// ── Engine headers ────────────────────────────────────────────────────────────
#include "GPURenderer.h"
#include "sceneManager.h"
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "pointlight.h"
#include "rectangle.h"
#include "importer.h"
#include "settings.h"
#include "basicmovements.h"
#include "textureManager.h"
#include "animation.h"
#include "material.h"

#include "exampleScene.h"

namespace HonHengine
{

    // ─────────────────────────────────────────────────────────────────────────────
    //  JSON helpers
    //  All shell output is machine-readable single-line JSON.
    //  Use J() to build a response and emit it with Emit().
    // ─────────────────────────────────────────────────────────────────────────────

    // Escape a string for JSON embedding (handles \, ", newline, tab).
    static std::string JStr(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 2);
        out += '"';
        for (char c : s)
        {
            if (c == '"')  out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else                out += c;
        }
        out += '"';
        return out;
    }

    // Build a minimal JSON response.
    // extra = optional key:value pairs already formatted, e.g. R"("x":1,"y":2)"
    static std::string MakeResponse(bool ok, const std::string& cmd,
        const std::string& msg,
        const std::string& extra = "")
    {
        std::ostringstream o;
        o << "{\"ok\":" << (ok ? "true" : "false")
            << ",\"cmd\":" << JStr(cmd)
            << ",\"msg\":" << JStr(msg);
        if (!extra.empty()) o << "," << extra;
        o << "}";
        return o.str();
    }

    // Convenience: format a vec3 fragment for the JSON data field.
    static std::string JVec3(const char* key, double x, double y, double z)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(4);
        o << "\"" << key << "\":{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z << "}";
        return o.str();
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Token helpers
    // ─────────────────────────────────────────────────────────────────────────────

    static std::vector<std::string> Tokenize(const std::string& line)
    {
        std::istringstream ss(line);
        std::vector<std::string> tokens;
        std::string tok;
        while (ss >> tok) tokens.push_back(tok);
        return tokens;
    }

    static bool ParseDouble(const std::string& s, double& out)
    {
        try { out = std::stod(s); return true; }
        catch (...) { return false; }
    }

    static bool ParseInt(const std::string& s, int& out)
    {
        try { out = std::stoi(s); return true; }
        catch (...) { return false; }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  CommandRegistry
    // ─────────────────────────────────────────────────────────────────────────────

    struct CmdContext
    {
        SceneManager* sm;
        GPURenderer* renderer;
        std::mutex* sceneMutex;
    };

    using CmdFn = std::function<void(CmdContext&,
        const std::vector<std::string>& args,
        std::ostream& out)>;

    struct CommandEntry
    {
        std::string usage;
        std::string help;
        CmdFn       fn;
    };

    class CommandRegistry
    {
    public:
        void Register(const std::string& name, const std::string& usage,
            const std::string& help, CmdFn fn)
        {
            order_.push_back(name);
            cmds_[name] = { usage, help, std::move(fn) };
        }

        void Dispatch(const std::string& line, CmdContext& ctx, std::ostream& out) const
        {
            auto toks = Tokenize(line);
            if (toks.empty()) return;

            auto it = cmds_.find(toks[0]);
            if (it == cmds_.end())
            {
                out << MakeResponse(false, toks[0],
                    "Unknown command. Send 'help' for the command list.") << "\n";
                return;
            }
            std::vector<std::string> args(toks.begin() + 1, toks.end());
            it->second.fn(ctx, args, out);
        }

        // Emit the help table as a JSON array of command descriptors.
        void PrintHelp(std::ostream& out) const
        {
            out << "{\"ok\":true,\"cmd\":\"help\",\"msg\":\"command list\",\"commands\":[";
            bool first = true;
            for (const auto& name : order_)
            {
                auto& e = cmds_.at(name);
                if (!first) out << ",";
                first = false;
                out << "{\"name\":" << JStr(name)
                    << ",\"usage\":" << JStr(e.usage)
                    << ",\"help\":" << JStr(e.help) << "}";
            }
            out << "]}\n";
        }

    private:
        std::unordered_map<std::string, CommandEntry> cmds_;
        std::vector<std::string>                      order_;   // insertion order for help
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Named-object / light registries
    // ─────────────────────────────────────────────────────────────────────────────

    static std::unordered_map<std::string, BaseObject*> g_namedObjects;
    static std::unordered_map<std::string, BaseLight*>  g_namedLights;

    // ─────────────────────────────────────────────────────────────────────────────
    //  Named colours
    // ─────────────────────────────────────────────────────────────────────────────

    static const Color colWhite{ 255, 255, 255, 255 };
    static const Color colBlack{ 0,   0,   0, 255 };
    static const Color colRed{ 220,  60,  60, 255 };
    static const Color colGreen{ 60, 180,  60, 255 };
    static const Color colBlue{ 60, 100, 220, 255 };
    static const Color colYellow{ 230, 200,  40, 255 };
    static const Color colGray{ 140, 140, 140, 255 };
    static const Color colOrange{ 230, 130,  40, 255 };
    static const Color colPurple{ 150,  60, 200, 255 };
    static const Color colCyan{ 60, 200, 210, 255 };

    static Color NameToColor(const std::string& name)
    {
        if (name == "white")  return colWhite;
        if (name == "black")  return colBlack;
        if (name == "red")    return colRed;
        if (name == "green")  return colGreen;
        if (name == "blue")   return colBlue;
        if (name == "yellow") return colYellow;
        if (name == "gray")   return colGray;
        if (name == "orange") return colOrange;
        if (name == "purple") return colPurple;
        if (name == "cyan")   return colCyan;
        return colWhite;
    }

    static const char* ColorToName(const Color& c)
    {
        // Best-effort reverse lookup for display purposes.
        if (c.r == 255 && c.g == 255 && c.b == 255) return "white";
        if (c.r == 0 && c.g == 0 && c.b == 0) return "black";
        if (c.r == 220 && c.g == 60 && c.b == 60) return "red";
        if (c.r == 60 && c.g == 180 && c.b == 60) return "green";
        if (c.r == 60 && c.g == 100 && c.b == 220) return "blue";
        if (c.r == 230 && c.g == 200 && c.b == 40) return "yellow";
        if (c.r == 140 && c.g == 140 && c.b == 140) return "gray";
        if (c.r == 230 && c.g == 130 && c.b == 40) return "orange";
        if (c.r == 150 && c.g == 60 && c.b == 200) return "purple";
        if (c.r == 60 && c.g == 200 && c.b == 210) return "cyan";
        return "custom";
    }

    // Build a Quaternion from Euler angles (degrees, XYZ order).
    static Quaternion EulerToQuat(double pitch, double yaw, double roll)
    {
        double p = pitch * M_PI / 180.0 * 0.5;
        double y = yaw * M_PI / 180.0 * 0.5;
        double r = roll * M_PI / 180.0 * 0.5;

        double cp = std::cos(p), sp = std::sin(p);
        double cy = std::cos(y), sy = std::sin(y);
        double cr = std::cos(r), sr = std::sin(r);

        return Quaternion(
            cr * cp * cy + sr * sp * sy,  // w
            sr * cp * cy - cr * sp * sy,  // x
            cr * sp * cy + sr * cp * sy,  // y
            cr * cp * sy - sr * sp * cy   // z
        );
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  BuildCommands — every public engine option is exposed here.
    // ─────────────────────────────────────────────────────────────────────────────

    static void BuildCommands(CommandRegistry& reg, GPURenderer* renderer)
    {

        // ── UTILITY ──────────────────────────────────────────────────────────────

        reg.Register("help", "help",
            "List all commands with usage and description.",
            [&reg](CmdContext&, const std::vector<std::string>&, std::ostream& out)
            { reg.PrintHelp(out); });

        reg.Register("echo", "echo <text...>",
            "Echo arguments back. Useful for IDE round-trip / ping tests.",
            [](CmdContext&, const std::vector<std::string>& args, std::ostream& out)
            {
                std::string msg;
                for (size_t i = 0; i < args.size(); ++i)
                    msg += (i ? " " : "") + args[i];
                out << MakeResponse(true, "echo", msg, "\"echo\":" + JStr(msg)) << "\n";
            });

        // ── SCENE QUERIES ─────────────────────────────────────────────────────────

        reg.Register("list", "list",
            "List all named objects and lights with positions.",
            [](CmdContext&, const std::vector<std::string>&, std::ostream& out)
            {
                std::ostringstream data;
                data << std::fixed << std::setprecision(4);
                data << "\"objects\":[";
                bool first = true;
                for (auto& [name, obj] : g_namedObjects)
                {
                    if (!first) data << ",";
                    first = false;
                    data << "{\"name\":" << JStr(name)
                        << ",\"visible\":" << (obj->visible ? "true" : "false")
                        << ",\"pos\":{\"x\":" << obj->transform.position.x
                        << ",\"y\":" << obj->transform.position.y
                        << ",\"z\":" << obj->transform.position.z << "}"
                        << ",\"scale\":{\"x\":" << obj->transform.scale.x
                        << ",\"y\":" << obj->transform.scale.y
                        << ",\"z\":" << obj->transform.scale.z << "}"
                        << ",\"shader\":" << JStr(obj->render.shaderName) << "}";
                }
                data << "],\"lights\":[";
                first = true;
                for (auto& [name, l] : g_namedLights)
                {
                    if (!first) data << ",";
                    first = false;
                    data << "{\"name\":" << JStr(name)
                        << ",\"intensity\":" << l->intensity
                        << ",\"color\":{\"r\":" << (int)l->color.r
                        << ",\"g\":" << (int)l->color.g
                        << ",\"b\":" << (int)l->color.b << "}}";
                }
                data << "]";
                out << MakeResponse(true, "list",
                    std::to_string(g_namedObjects.size()) + " objects, " +
                    std::to_string(g_namedLights.size()) + " lights", data.str()) << "\n";
            });

        reg.Register("inspect", "inspect <name>",
            "Return full transform, material, shader, and visibility for a named object.",
            [](CmdContext&, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "inspect", "Usage: inspect <name>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end())
                {
                    out << MakeResponse(false, "inspect", "Unknown object '" + args[0] + "'") << "\n"; return;
                }

                BaseObject* obj = it->second;
                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << "\"name\":" << JStr(args[0])
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
                {
                    d << ",\"color\":{\"r\":" << (int)obj->material->color.r
                        << ",\"g\":" << (int)obj->material->color.g
                        << ",\"b\":" << (int)obj->material->color.b
                        << ",\"a\":" << (int)obj->material->color.a << "}";
                }
                out << MakeResponse(true, "inspect", "ok", d.str()) << "\n";
            });

        // ── SPAWNING ──────────────────────────────────────────────────────────────

        reg.Register("cube",
            "cube <name> <x> <y> <z> [half=0.5] [color=white]",
            "Spawn a coloured axis-aligned cube. half = half-extent on each axis.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "cube", "Usage: cube <name> <x> <y> <z> [half=0.5] [color=white]") << "\n"; return; }
                std::string name = args[0];
                double x, y, z;
                if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z))
                {
                    out << MakeResponse(false, "cube", "Bad coordinates") << "\n"; return;
                }

                double half = 0.5;
                Color  col = colWhite;
                if (args.size() >= 5) ParseDouble(args[4], half);
                if (args.size() >= 6) col = NameToColor(args[5]);

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                BaseObject* obj = new Rectangle(
                    Vector3(half, half, half), Vector3(x, y, z), Quaternion(),
                    new Material(0.0, 0.0, col, colBlack));
                ctx.sm->objects->push_back(obj);
                g_namedObjects[name] = obj;

                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z) << ",\"half\":" << half
                    << ",\"color\":" << JStr(args.size() >= 6 ? args[5] : "white");
                out << MakeResponse(true, "cube", "'" + name + "' spawned", d.str()) << "\n";
            });

        reg.Register("slab",
            "slab <name> <x> <y> <z> <hx> <hy> <hz> [color=gray]",
            "Spawn a box with independent half-extents on each axis.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 7) { out << MakeResponse(false, "slab", "Usage: slab <name> <x> <y> <z> <hx> <hy> <hz> [color]") << "\n"; return; }
                std::string name = args[0];
                double x, y, z, hx, hy, hz;
                if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z) ||
                    !ParseDouble(args[4], hx) || !ParseDouble(args[5], hy) || !ParseDouble(args[6], hz))
                {
                    out << MakeResponse(false, "slab", "Bad arguments") << "\n"; return;
                }

                Color col = args.size() >= 8 ? NameToColor(args[7]) : colGray;

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                BaseObject* obj = new Rectangle(
                    Vector3(hx, hy, hz), Vector3(x, y, z), Quaternion(),
                    new Material(0.0, 0.0, col, colBlack));
                ctx.sm->objects->push_back(obj);
                g_namedObjects[name] = obj;

                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z) << "," << JVec3("halfExtents", hx, hy, hz);
                out << MakeResponse(true, "slab", "'" + name + "' spawned", d.str()) << "\n";
            });

        reg.Register("obj",
            "obj <name> <file.obj> [x=0] [y=0] [z=0]",
            "Import an OBJ mesh and place it in the scene. Default shader: 'beach'.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "obj", "Usage: obj <name> <file.obj> [x] [y] [z]") << "\n"; return; }
                std::string name = args[0], path = args[1];
                double x = 0, y = 0, z = 0;
                if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }

                BaseObject* imported = nullptr;
                try { imported = Importer::ImportFromOBJ(path); }
                catch (const std::exception& e)
                {
                    out << MakeResponse(false, "obj", std::string("Import exception: ") + e.what()) << "\n"; return;
                }

                if (!imported) { out << MakeResponse(false, "obj", "Failed to import '" + path + "'") << "\n"; return; }
                imported->transform.position = Vector3(x, y, z);

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                ctx.sm->objects->push_back(imported);
                g_namedObjects[name] = imported;

                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << "\"path\":" << JStr(path) << "," << JVec3("pos", x, y, z);
                out << MakeResponse(true, "obj", "'" + name + "' imported", d.str()) << "\n";
            });

        reg.Register("gltf",
            "gltf <name> <file.gltf|file.glb> [x=0] [y=0] [z=0]",
            "Import a glTF/GLB skinned mesh with animations. Default shader: 'skinned'.",
            [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "gltf", "Usage: gltf <name> <file> [x] [y] [z]") << "\n"; return; }
                std::string name = args[0], path = args[1];
                double x = 0, y = 0, z = 0;
                if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }

                auto res = Importer::ImportFromGLTF(path, &renderer->texManager);
                if (!res.ok) { out << MakeResponse(false, "gltf", "Import failed: " + res.error) << "\n"; return; }

                res.object->transform.position = Vector3(x, y, z);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                ctx.sm->objects->push_back(res.object);
                g_namedObjects[name] = res.object;

                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << "\"path\":" << JStr(path)
                    << "," << JVec3("pos", x, y, z)
                    << ",\"clips\":" << res.clips.size()
                    << ",\"albedoKey\":" << JStr(res.albedoTexKey);
                out << MakeResponse(true, "gltf", "'" + name + "' imported (" + std::to_string(res.clips.size()) + " clips)", d.str()) << "\n";
            });

        // ── TRANSFORM ────────────────────────────────────────────────────────────

        reg.Register("move",
            "move <name> <x> <y> <z>",
            "Teleport a named object to an absolute world position.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "move", "Usage: move <name> <x> <y> <z>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "move", "Unknown object '" + args[0] + "'") << "\n"; return; }
                double x, y, z;
                if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z))
                {
                    out << MakeResponse(false, "move", "Bad coordinates") << "\n"; return;
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->transform.position = Vector3(x, y, z);

                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "move", "'" + args[0] + "' moved", d.str()) << "\n";
            });

        reg.Register("translate",
            "translate <name> <dx> <dy> <dz>",
            "Move a named object by a relative offset (delta).",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "translate", "Usage: translate <name> <dx> <dy> <dz>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "translate", "Unknown object '" + args[0] + "'") << "\n"; return; }
                double dx, dy, dz;
                if (!ParseDouble(args[1], dx) || !ParseDouble(args[2], dy) || !ParseDouble(args[3], dz))
                {
                    out << MakeResponse(false, "translate", "Bad delta") << "\n"; return;
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->transform.position.x += dx;
                it->second->transform.position.y += dy;
                it->second->transform.position.z += dz;
                auto& p = it->second->transform.position;

                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("delta", dx, dy, dz) << "," << JVec3("newPos", p.x, p.y, p.z);
                out << MakeResponse(true, "translate", "'" + args[0] + "' translated", d.str()) << "\n";
            });

        reg.Register("scale",
            "scale <name> <sx> <sy> <sz>",
            "Set the absolute world scale of a named object.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "scale", "Usage: scale <name> <sx> <sy> <sz>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "scale", "Unknown object '" + args[0] + "'") << "\n"; return; }
                double sx, sy, sz;
                if (!ParseDouble(args[1], sx) || !ParseDouble(args[2], sy) || !ParseDouble(args[3], sz))
                {
                    out << MakeResponse(false, "scale", "Bad scale values") << "\n"; return;
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->transform.scale = Vector3(sx, sy, sz);

                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("scale", sx, sy, sz);
                out << MakeResponse(true, "scale", "'" + args[0] + "' scaled", d.str()) << "\n";
            });

        reg.Register("rotate",
            "rotate <name> <pitch> <yaw> <roll>   (degrees, Euler XYZ)",
            "Set the rotation of a named object using Euler angles in degrees.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "rotate", "Usage: rotate <name> <pitch> <yaw> <roll>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "rotate", "Unknown object '" + args[0] + "'") << "\n"; return; }
                double pitch, yaw, roll;
                if (!ParseDouble(args[1], pitch) || !ParseDouble(args[2], yaw) || !ParseDouble(args[3], roll))
                {
                    out << MakeResponse(false, "rotate", "Bad angle values") << "\n"; return;
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->transform.rotation = EulerToQuat(pitch, yaw, roll);

                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << "\"pitch\":" << pitch << ",\"yaw\":" << yaw << ",\"roll\":" << roll;
                out << MakeResponse(true, "rotate", "'" + args[0] + "' rotated", d.str()) << "\n";
            });

        // ── MATERIAL / APPEARANCE ────────────────────────────────────────────────

        reg.Register("color",
            "color <name> <colorName|r g b>",
            "Change the diffuse colour of a named object. Accepts a color name or three 0-255 integers.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "color", "Usage: color <name> <colorName>  OR  color <name> <r> <g> <b>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "color", "Unknown object '" + args[0] + "'") << "\n"; return; }

                Color col;
                std::string colDesc;
                if (args.size() >= 4)
                {
                    double r, g, b;
                    ParseDouble(args[1], r); ParseDouble(args[2], g); ParseDouble(args[3], b);
                    col = Color(
                        static_cast<uint8_t>(std::clamp(r, 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(g, 0.0, 255.0)),
                        static_cast<uint8_t>(std::clamp(b, 0.0, 255.0)), 255);
                    colDesc = "rgb(" + args[1] + "," + args[2] + "," + args[3] + ")";
                }
                else
                {
                    col = NameToColor(args[1]);
                    colDesc = args[1];
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                if (it->second->material)
                    it->second->material->color = col;

                std::ostringstream d;
                d << "\"color\":{\"r\":" << (int)col.r << ",\"g\":" << (int)col.g << ",\"b\":" << (int)col.b << "}";
                out << MakeResponse(true, "color", "'" + args[0] + "' color set to " + colDesc, d.str()) << "\n";
            });

        reg.Register("shader",
            "shader <name> <shaderName>",
            "Change the shader used by a named object (e.g. 'beach', 'skinned', or any registered shader).",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "shader", "Usage: shader <name> <shaderName>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "shader", "Unknown object '" + args[0] + "'") << "\n"; return; }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->render.shaderName = args[1];

                out << MakeResponse(true, "shader",
                    "'" + args[0] + "' shader set to '" + args[1] + "'",
                    "\"shader\":" + JStr(args[1])) << "\n";
            });

        reg.Register("texture",
            "texture <name> <textureName> [slot=0]",
            "Assign a loaded texture to a named object's material slot.",
            [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "texture", "Usage: texture <name> <textureName> [slot=0]") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "texture", "Unknown object '" + args[0] + "'") << "\n"; return; }

                std::string texName = args[1];
                int slot = 0;
                if (args.size() >= 3) ParseInt(args[2], slot);

                // Verify texture exists
                if (renderer->texManager.get(texName) == 0)
                {
                    out << MakeResponse(false, "texture", "Texture '" + texName + "' not loaded. Use 'loadtex' first.") << "\n"; return;
                }

                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                // Replace or add texture binding in the render component
                auto* mat = it->second->material;
                if (!mat)
                {
                    out << MakeResponse(false, "texture", "Object has no material") << "\n";
                    return;
                }

                // Crée une texture
                Texture tex(texName, ""); // path optionnel ici

                // Ajoute comme layer
                mat->addLayer(TextureLayer(tex));

                std::ostringstream d;
                d << "\"texture\":" << JStr(texName) << ",\"slot\":" << slot;
                out << MakeResponse(true, "texture", "'" + args[0] + "' texture set", d.str()) << "\n";
            });

        reg.Register("loadtex",
            "loadtex <textureName> <filepath>",
            "Load an image file into the texture manager so it can be assigned with 'texture'.",
            [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "loadtex", "Usage: loadtex <textureName> <filepath>") << "\n"; return; }
                GLuint id = renderer->texManager.load(args[0], args[1]);
                if (id == 0)
                {
                    out << MakeResponse(false, "loadtex", "Failed to load '" + args[1] + "'") << "\n"; return;
                }

                std::ostringstream d;
                d << "\"textureName\":" << JStr(args[0]) << ",\"path\":" << JStr(args[1]) << ",\"glId\":" << id;
                out << MakeResponse(true, "loadtex", "'" + args[0] + "' loaded (id=" + std::to_string(id) + ")", d.str()) << "\n";
            });

        reg.Register("cullface",
            "cullface <name> <on|off>",
            "Enable or disable backface culling on a named object.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "cullface", "Usage: cullface <name> <on|off>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "cullface", "Unknown object '" + args[0] + "'") << "\n"; return; }

                bool enable = (args[1] == "on" || args[1] == "true" || args[1] == "1");
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->render.cullFace = enable;
                out << MakeResponse(true, "cullface",
                    "'" + args[0] + "' cull face " + (enable ? "on" : "off"),
                    "\"cullFace\":" + std::string(enable ? "true" : "false")) << "\n";
            });

        // ── VISIBILITY / LIFECYCLE ───────────────────────────────────────────────

        reg.Register("hide", "hide <name>",
            "Make a named object invisible (stays in scene graph, no draw call).",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "hide", "Usage: hide <name>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "hide", "Unknown object '" + args[0] + "'") << "\n"; return; }
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->visible = false;
                out << MakeResponse(true, "hide", "'" + args[0] + "' hidden") << "\n";
            });

        reg.Register("show", "show <name>",
            "Make a previously hidden object visible again.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "show", "Usage: show <name>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "show", "Unknown object '" + args[0] + "'") << "\n"; return; }
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->visible = true;
                out << MakeResponse(true, "show", "'" + args[0] + "' visible") << "\n";
            });

        reg.Register("remove", "remove <name>",
            "Remove and delete a named object from the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "remove", "Usage: remove <name>") << "\n"; return; }
                auto it = g_namedObjects.find(args[0]);
                if (it == g_namedObjects.end()) { out << MakeResponse(false, "remove", "Unknown object '" + args[0] + "'") << "\n"; return; }
                BaseObject* obj = it->second;
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                auto& vec = *ctx.sm->objects;
                vec.erase(std::remove(vec.begin(), vec.end(), obj), vec.end());
                delete obj;
                g_namedObjects.erase(it);
                out << MakeResponse(true, "remove", "'" + args[0] + "' removed") << "\n";
            });

        reg.Register("clear", "clear",
            "Remove all shell-spawned objects from the scene (leaves scene-setup objects).",
            [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out)
            {
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                auto& vec = *ctx.sm->objects;
                int removed = 0;
                for (auto& [name, obj] : g_namedObjects)
                {
                    vec.erase(std::remove(vec.begin(), vec.end(), obj), vec.end());
                    delete obj;
                    ++removed;
                }
                g_namedObjects.clear();
                out << MakeResponse(true, "clear",
                    std::to_string(removed) + " object(s) removed",
                    "\"removed\":" + std::to_string(removed)) << "\n";
            });

        // ── PROCEDURAL HELPERS ───────────────────────────────────────────────────

        reg.Register("grid",
            "grid <cols> <rows> <spacing> [color=gray]",
            "Spawn a flat grid of tiles on the Y=0 ground plane.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 3) { out << MakeResponse(false, "grid", "Usage: grid <cols> <rows> <spacing> [color]") << "\n"; return; }
                double cols, rows, spacing;
                if (!ParseDouble(args[0], cols) || !ParseDouble(args[1], rows) || !ParseDouble(args[2], spacing))
                {
                    out << MakeResponse(false, "grid", "Bad arguments") << "\n"; return;
                }

                Color col = args.size() >= 4 ? NameToColor(args[3]) : colGray;
                int ic = static_cast<int>(cols), ir = static_cast<int>(rows);
                double half = spacing * 0.5;
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                int count = 0;
                for (int r = 0; r < ir; ++r)
                    for (int c = 0; c < ic; ++c)
                    {
                        double x = c * spacing - (ic * spacing * 0.5);
                        double z = r * spacing - (ir * spacing * 0.5);
                        ctx.sm->objects->push_back(new Rectangle(
                            Vector3(half * 0.9, half * 0.1, half * 0.9),
                            Vector3(x, 0, z), Quaternion(),
                            new Material(0, 0, col, colBlack)));
                        ++count;
                    }
                out << MakeResponse(true, "grid",
                    std::to_string(count) + " tiles spawned",
                    "\"tiles\":" + std::to_string(count)) << "\n";
            });

        // ── LIGHTING ─────────────────────────────────────────────────────────────

        reg.Register("listlights", "listlights",
            "List all named lights with intensity and colour.",
            [](CmdContext&, const std::vector<std::string>&, std::ostream& out)
            {
                std::ostringstream d;
                d << "\"lights\":[";
                bool first = true;
                for (auto& [name, l] : g_namedLights)
                {
                    if (!first) d << ",";
                    first = false;
                    d << "{\"name\":" << JStr(name)
                        << ",\"intensity\":" << l->intensity
                        << ",\"color\":{\"r\":" << (int)l->color.r
                        << ",\"g\":" << (int)l->color.g
                        << ",\"b\":" << (int)l->color.b << "}}";
                }
                d << "]";
                out << MakeResponse(true, "listlights",
                    std::to_string(g_namedLights.size()) + " lights", d.str()) << "\n";
            });

        reg.Register("suncolor",
            "suncolor <r> <g> <b>   (0-255)",
            "Set the colour of all sun/directional lights currently in the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 3) { out << MakeResponse(false, "suncolor", "Usage: suncolor <r> <g> <b>") << "\n"; return; }
                double r, g, b;
                if (!ParseDouble(args[0], r) || !ParseDouble(args[1], g) || !ParseDouble(args[2], b))
                {
                    out << MakeResponse(false, "suncolor", "Bad values") << "\n"; return;
                }
                Color col(
                    static_cast<uint8_t>(std::clamp(r, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(g, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(b, 0.0, 255.0)), 255);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                for (BaseLight* l : *ctx.sm->lights) l->color = col;
                std::ostringstream d;
                d << "\"color\":{\"r\":" << (int)col.r << ",\"g\":" << (int)col.g << ",\"b\":" << (int)col.b << "}";
                out << MakeResponse(true, "suncolor", "Sun colour updated", d.str()) << "\n";
            });

        reg.Register("sunintensity",
            "sunintensity <value>",
            "Set the intensity of all sun/directional lights in the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "sunintensity", "Usage: sunintensity <value>") << "\n"; return; }
                double v; ParseDouble(args[0], v);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                for (BaseLight* l : *ctx.sm->lights) l->intensity = static_cast<float>(v);
                out << MakeResponse(true, "sunintensity",
                    "Intensity set to " + args[0],
                    "\"intensity\":" + args[0]) << "\n";
            });

        reg.Register("ambient",
            "ambient <intensity> <r> <g> <b>",
            "Add a global ambient light to the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "ambient", "Usage: ambient <intensity> <r> <g> <b>") << "\n"; return; }
                double intensity, r, g, b;
                ParseDouble(args[0], intensity); ParseDouble(args[1], r); ParseDouble(args[2], g); ParseDouble(args[3], b);
                Color col(
                    static_cast<uint8_t>(std::clamp(r, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(g, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(b, 0.0, 255.0)), 255);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                BaseLight* al = new BaseLight(static_cast<float>(intensity), col);
                ctx.sm->lights->push_back(al);
                g_namedLights["global_ambient"] = al;
                std::ostringstream d;
                d << "\"intensity\":" << intensity << ",\"color\":{\"r\":" << (int)col.r
                    << ",\"g\":" << (int)col.g << ",\"b\":" << (int)col.b << "}";
                out << MakeResponse(true, "ambient", "Ambient light set", d.str()) << "\n";
            });

        reg.Register("pointlight",
            "pointlight <name> <x> <y> <z> <intensity> [r=255] [g=200] [b=100]",
            "Add a named point light to the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 5) { out << MakeResponse(false, "pointlight", "Usage: pointlight <name> <x> <y> <z> <intensity> [r] [g] [b]") << "\n"; return; }
                std::string name = args[0];
                double x, y, z, intensity;
                if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z) || !ParseDouble(args[4], intensity))
                {
                    out << MakeResponse(false, "pointlight", "Bad arguments") << "\n"; return;
                }
                double r = 255, g = 200, b = 100;
                if (args.size() >= 8) { ParseDouble(args[5], r); ParseDouble(args[6], g); ParseDouble(args[7], b); }
                Color col(
                    static_cast<uint8_t>(std::clamp(r, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(g, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(b, 0.0, 255.0)), 255);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                PointLight* pl = new PointLight(static_cast<float>(intensity), col, Vector3(x, y, z));
                ctx.sm->lights->push_back(pl);
                g_namedLights[name] = pl;
                std::ostringstream d;
                d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z)
                    << ",\"intensity\":" << intensity
                    << ",\"color\":{\"r\":" << (int)col.r << ",\"g\":" << (int)col.g << ",\"b\":" << (int)col.b << "}";
                out << MakeResponse(true, "pointlight", "'" + name + "' added", d.str()) << "\n";
            });

        reg.Register("lightmove",
            "lightmove <name> <x> <y> <z>",
            "Teleport a named point light to a new world position.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "lightmove", "Usage: lightmove <name> <x> <y> <z>") << "\n"; return; }
                auto it = g_namedLights.find(args[0]);
                if (it == g_namedLights.end()) { out << MakeResponse(false, "lightmove", "Unknown light '" + args[0] + "'") << "\n"; return; }
                PointLight* pl = dynamic_cast<PointLight*>(it->second);
                if (!pl) { out << MakeResponse(false, "lightmove", "'" + args[0] + "' is not a PointLight") << "\n"; return; }
                double x, y, z;
                if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z))
                {
                    out << MakeResponse(false, "lightmove", "Bad coordinates") << "\n"; return;
                }
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                pl->position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "lightmove", "'" + args[0] + "' moved", d.str()) << "\n";
            });

        reg.Register("lightcolor",
            "lightcolor <name> <r> <g> <b>   (0-255)",
            "Change the colour of a named light.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 4) { out << MakeResponse(false, "lightcolor", "Usage: lightcolor <name> <r> <g> <b>") << "\n"; return; }
                auto it = g_namedLights.find(args[0]);
                if (it == g_namedLights.end()) { out << MakeResponse(false, "lightcolor", "Unknown light '" + args[0] + "'") << "\n"; return; }
                double r, g, b;
                ParseDouble(args[1], r); ParseDouble(args[2], g); ParseDouble(args[3], b);
                Color col(
                    static_cast<uint8_t>(std::clamp(r, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(g, 0.0, 255.0)),
                    static_cast<uint8_t>(std::clamp(b, 0.0, 255.0)), 255);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->color = col;
                std::ostringstream d;
                d << "\"color\":{\"r\":" << (int)col.r << ",\"g\":" << (int)col.g << ",\"b\":" << (int)col.b << "}";
                out << MakeResponse(true, "lightcolor", "'" + args[0] + "' colour updated", d.str()) << "\n";
            });

        reg.Register("lightintensity",
            "lightintensity <name> <value>",
            "Set the intensity of a named light.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 2) { out << MakeResponse(false, "lightintensity", "Usage: lightintensity <name> <value>") << "\n"; return; }
                auto it = g_namedLights.find(args[0]);
                if (it == g_namedLights.end()) { out << MakeResponse(false, "lightintensity", "Unknown light '" + args[0] + "'") << "\n"; return; }
                double v; ParseDouble(args[1], v);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                it->second->intensity = static_cast<float>(v);
                out << MakeResponse(true, "lightintensity",
                    "'" + args[0] + "' intensity set to " + args[1],
                    "\"intensity\":" + args[1]) << "\n";
            });

        reg.Register("removelight", "removelight <name>",
            "Remove a named light from the scene.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "removelight", "Usage: removelight <name>") << "\n"; return; }
                auto it = g_namedLights.find(args[0]);
                if (it == g_namedLights.end()) { out << MakeResponse(false, "removelight", "Unknown light '" + args[0] + "'") << "\n"; return; }
                BaseLight* l = it->second;
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                auto& vec = *ctx.sm->lights;
                vec.erase(std::remove(vec.begin(), vec.end(), l), vec.end());
                delete l;
                g_namedLights.erase(it);
                out << MakeResponse(true, "removelight", "'" + args[0] + "' removed") << "\n";
            });

        // ── CAMERA ───────────────────────────────────────────────────────────────

        reg.Register("camera",
            "camera <x> <y> <z>",
            "Teleport the active camera to a world position.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 3) { out << MakeResponse(false, "camera", "Usage: camera <x> <y> <z>") << "\n"; return; }
                double x, y, z;
                if (!ParseDouble(args[0], x) || !ParseDouble(args[1], y) || !ParseDouble(args[2], z))
                {
                    out << MakeResponse(false, "camera", "Bad coordinates") << "\n"; return;
                }
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                if (ctx.sm->currentCamera)
                    ctx.sm->currentCamera->transform.position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "camera", "Camera moved", d.str()) << "\n";
            });

        reg.Register("camerarot",
            "camerarot <pitch> <yaw> <roll>   (degrees)",
            "Set the active camera rotation via Euler angles.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 3) { out << MakeResponse(false, "camerarot", "Usage: camerarot <pitch> <yaw> <roll>") << "\n"; return; }
                double pitch, yaw, roll;
                if (!ParseDouble(args[0], pitch) || !ParseDouble(args[1], yaw) || !ParseDouble(args[2], roll))
                {
                    out << MakeResponse(false, "camerarot", "Bad angles") << "\n"; return;
                }
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                if (ctx.sm->currentCamera)
                    ctx.sm->currentCamera->transform.rotation = EulerToQuat(pitch, yaw, roll);
                std::ostringstream d;
                d << "\"pitch\":" << pitch << ",\"yaw\":" << yaw << ",\"roll\":" << roll;
                out << MakeResponse(true, "camerarot", "Camera rotated", d.str()) << "\n";
            });

        reg.Register("fov",
            "fov <degrees>",
            "Set the active camera field of view in degrees.",
            [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "fov", "Usage: fov <degrees>") << "\n"; return; }
                double v; ParseDouble(args[0], v);
                std::lock_guard<std::mutex> lk(*ctx.sceneMutex);
                if (ctx.sm->currentCamera)
                    ctx.sm->currentCamera->fov = v;
                out << MakeResponse(true, "fov",
                    "FOV set to " + args[0],
                    "\"fov\":" + args[0]) << "\n";
            });

        reg.Register("getcamera", "getcamera",
            "Return the current camera position, rotation, and FOV.",
            [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out)
            {
                if (!ctx.sm->currentCamera)
                {
                    out << MakeResponse(false, "getcamera", "No active camera") << "\n"; return;
                }
                auto* cam = ctx.sm->currentCamera;
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", cam->transform.position.x, cam->transform.position.y, cam->transform.position.z)
                    << ",\"rotation\":{\"w\":" << cam->transform.rotation.w
                    << ",\"x\":" << cam->transform.rotation.x
                    << ",\"y\":" << cam->transform.rotation.y
                    << ",\"z\":" << cam->transform.rotation.z << "}"
                    << ",\"fov\":" << cam->fov;
                out << MakeResponse(true, "getcamera", "ok", d.str()) << "\n";
            });

        // ── RENDER SETTINGS ───────────────────────────────────────────────────────

        reg.Register("shading",
            "shading <phong|gouraud>",
            "Switch the global shading model.",
            [](CmdContext&, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.empty()) { out << MakeResponse(false, "shading", "Usage: shading <phong|gouraud>") << "\n"; return; }
                if (args[0] == "phong")
                    Settings::shadingMode = Settings::ShadingMode::PHONG;
                else if (args[0] == "gouraud")
                    Settings::shadingMode = Settings::ShadingMode::GOURAUD;
                else
                {
                    out << MakeResponse(false, "shading", "Unknown mode '" + args[0] + "'. Options: phong, gouraud") << "\n"; return;
                }
                out << MakeResponse(true, "shading",
                    "Shading set to " + args[0],
                    "\"mode\":" + JStr(args[0])) << "\n";
            });

        reg.Register("registershader",
            "registershader <name> <vertFile> <fragFile>",
            "Compile and register a GLSL shader from two source files.",
            [renderer](CmdContext&, const std::vector<std::string>& args, std::ostream& out)
            {
                if (args.size() < 3) { out << MakeResponse(false, "registershader", "Usage: registershader <name> <vertFile> <fragFile>") << "\n"; return; }

                // Read files
                auto readFile = [](const std::string& path) -> std::string {
                    std::ifstream f(path); if (!f) return "";
                    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
                    };
                std::string vert = readFile(args[1]);
                std::string frag = readFile(args[2]);
                if (vert.empty()) { out << MakeResponse(false, "registershader", "Cannot read vert file: " + args[1]) << "\n"; return; }
                if (frag.empty()) { out << MakeResponse(false, "registershader", "Cannot read frag file: " + args[2]) << "\n"; return; }

                renderer->RegisterShader(args[0], vert.c_str(), frag.c_str());
                out << MakeResponse(true, "registershader",
                    "Shader '" + args[0] + "' registered",
                    "\"name\":" + JStr(args[0])) << "\n";
            });

        // ── ENGINE ────────────────────────────────────────────────────────────────

        reg.Register("quit", "quit",
            "Send SDL_QUIT to cleanly close the engine window.",
            [](CmdContext&, const std::vector<std::string>&, std::ostream& out)
            {
                out << MakeResponse(true, "quit", "Shutting down...") << "\n";
                SDL_Event qev{};
                qev.type = SDL_QUIT;
                SDL_PushEvent(&qev);
            });
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  Shell thread — reads stdin, dispatches, emits JSON to stdout
    // ─────────────────────────────────────────────────────────────────────────────

    static void ShellThread(CommandRegistry* reg, CmdContext* ctx,
        std::atomic<bool>* running)
    {
        // IDE-mode: a clean marker so the host knows the shell is ready.
        std::cout << "{\"ok\":true,\"cmd\":\"init\",\"msg\":\"HonHon Scene Shell ready. Send JSON-line commands on stdin.\"}\n"
            << std::flush;

        std::string line;
        while (*running && std::getline(std::cin, line))
        {
            // Trim CR (Windows line endings)
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty())
                reg->Dispatch(line, *ctx, std::cout);
            std::cout << std::flush;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────────
    //  MainScene_Run
    // ─────────────────────────────────────────────────────────────────────────────

    void MainScene_Run()
    {
        // ── Scene ─────────────────────────────────────────────────────────────────
        SceneManager* sm = new SceneManager();

        Camera* camera = new Camera(Vector3(0.0, 5.0, 15.0), Quaternion());
        sm->cameras->push_back(camera);
        sm->currentCamera = camera;

        BaseLight* sun = new BaseLight();
        sun->color = Color(255, 245, 209, 255);
        sun->intensity = 1.1f;
        sm->lights->push_back(sun);

        // ── Renderer ──────────────────────────────────────────────────────────────
        GPURenderer renderer(sm);
        renderer.FlushGLErrors();

        // ── Floor plane ───────────────────────────────────────────────────────────
        {
            BaseObject* floor = new Rectangle(
                Vector3(50.0, 0.05, 50.0), Vector3(0.0, -0.05, 0.0), Quaternion(),
                new Material(0.0, 0.0, Color(90, 130, 80, 255)));
            floor->render.cullFace = false;
            sm->objects->push_back(floor);
        }

        // ── Player controller ─────────────────────────────────────────────────────
        BasicMovements player(sm);
        player.eyeHeight = 1.7;
        player.moveSpeed = 0.12;
        player.flySpeed = 0.3;
        player.sdlKeys = SdlBindings::WASD();

        // ── Command system ────────────────────────────────────────────────────────
        CommandRegistry reg;
        BuildCommands(reg, &renderer);

        std::mutex  sceneMutex;
        CmdContext  cmdCtx{ sm, &renderer, &sceneMutex };

        std::atomic<bool> running{ true };
        std::thread shellThread(ShellThread, &reg, &cmdCtx, &running);

        // ── Main loop ─────────────────────────────────────────────────────────────
        using Clock = std::chrono::high_resolution_clock;
        auto tPrev = Clock::now();

        while (running)
        {
            auto  tNow = Clock::now();
            float dt = static_cast<float>(
                std::chrono::duration<double>(tNow - tPrev).count());
            tPrev = tNow;
            if (dt > 0.1f) dt = 0.1f;

            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_QUIT)                                    running = false;
                if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
            }

            int mx = 0, my = 0;
            renderer.Get_MouseState(&mx, &my);
            if (mx != 0 || my != 0) player.TickMouse(-mx, -my);

            {
                std::lock_guard<std::mutex> lk(sceneMutex);
                const Uint8* ks = SDL_GetKeyboardState(nullptr);
                if (!player.TickKeys(ks, dt)) running = false;
                renderer.Render(dt);
            }

            renderer.Present();
        }

        // ── Cleanup ───────────────────────────────────────────────────────────────
        running = false;
        shellThread.detach();   // stdin is blocking — detach is safe at process exit

        renderer.cleanup();
        for (BaseObject* o : *sm->objects) delete o;
        sm->objects->clear();
        for (BaseLight* l : *sm->lights)  delete l;
        sm->lights->clear();
        delete sm;
    }

} // namespace HonHengine

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/)
{
    HonHengine::MainScene_Run();
    return 0;
}