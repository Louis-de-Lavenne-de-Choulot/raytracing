#pragma once
// ide_project.h  —  HonHon Engine IDE  —  Project & Settings Management
// =============================================================================
// Covers:
//   - Project creation (name, folder, template)
//   - Project settings (physics, quality, input, tags, layers, time, render)
//   - Save / Load with format version  (*.honproject)
//   - Auto-save and crash recovery
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Version stamp embedded in every .honproject file
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kProjectFormatVersion = 3;

// ─────────────────────────────────────────────────────────────────────────────
//  Axis / input binding
// ─────────────────────────────────────────────────────────────────────────────
struct InputAxis {
    char name[64] = "Horizontal";
    char positiveKey[32] = "d";
    char negativeKey[32] = "a";
    float sensitivity = 3.0f;
    float gravity = 3.0f;
    float deadZone = 0.001f;
    bool  snap = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Quality level descriptor
// ─────────────────────────────────────────────────────────────────────────────
struct QualityLevel {
    char  name[64] = "Medium";
    int   shadowResolution = 1024;   // 0=off, 512,1024,2048,4096
    int   msaaSamples = 4;      // 1,2,4,8
    bool  softShadows = true;
    bool  bloom = false;
    bool  ao = false;
    float renderScale = 1.0f;   // 0.5 – 2.0
    int   anisotropicFilter = 4;      // 1,2,4,8,16
};

// ─────────────────────────────────────────────────────────────────────────────
//  Physics settings
// ─────────────────────────────────────────────────────────────────────────────
struct PhysicsSettings {
    float gravityX = 0.f;
    float gravityY = -9.81f;
    float gravityZ = 0.f;
    float fixedTimestep = 0.02f;   // seconds (50 Hz)
    int   solverIterations = 6;
    float bounceThreshold = 2.0f;
    float sleepThreshold = 0.005f;
    bool  queriesHitTriggers = true;
    bool  autoSimulation = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Time settings
// ─────────────────────────────────────────────────────────────────────────────
struct TimeSettings {
    float timeScale = 1.0f;
    float maxDeltaTime = 0.333f;
    float fixedDeltaTime = 0.02f;
    int   targetFrameRate = -1;      // -1 = unlimited
    bool  vsync = true;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Render settings (IDE-side, not touching GPURenderer internals)
// ─────────────────────────────────────────────────────────────────────────────
struct RenderSettings {
    int   colorSpace = 0;       // 0=Gamma, 1=Linear
    int   renderPath = 0;       // 0=Forward, 1=Deferred
    float ambientIntensity = 0.15f;
    float fogDensity = 0.0f;
    float fogColor[3] = { 0.78f, 0.82f, 0.90f };
    bool  fogEnabled = false;
    bool  wireframe = false;
    int   skyboxMode = 0;       // 0=Solid color, 1=Gradient, 2=Cubemap
    float skyColor[3] = { 0.18f, 0.25f, 0.40f };
};

// ─────────────────────────────────────────────────────────────────────────────
//  Project descriptor
// ─────────────────────────────────────────────────────────────────────────────
struct ProjectSettings {
    // Identity
    char  name[128] = "Untitled";
    char  rootFolder[512] = "./";
    char  version[32] = "0.1.0";
    char  author[128] = "";
    int   templateId = 0;       // 0=Empty 3D, 1=Basic 3D, 2=Physics demo

    // Subsystems
    PhysicsSettings physics;
    TimeSettings    time;
    RenderSettings  render;

    // Quality levels (up to 6, index 0=Low … 5=Ultra)
    std::vector<QualityLevel> qualityLevels;
    int  currentQualityLevel = 1;

    // Input axes
    std::vector<InputAxis> inputAxes;

    // Tags / Layers (like Unity)
    std::vector<std::string> tags;
    std::vector<std::string> layers;

    // Auto-save
    bool  autoSave = true;
    float autoSaveInterval = 120.f;   // seconds

    bool loaded = false;   // true once a project has been opened / created

    ProjectSettings() {
        // Default quality levels
        qualityLevels.resize(4);
        strncpy_s(qualityLevels[0].name, sizeof(QualityLevel::name), "Low", sizeof(QualityLevel::name) - 1);
        qualityLevels[0].shadowResolution = 512; qualityLevels[0].msaaSamples = 1;
        strncpy_s(qualityLevels[1].name, sizeof(QualityLevel::name), "Medium", sizeof(QualityLevel::name) - 1);
        qualityLevels[1].shadowResolution = 1024; qualityLevels[1].msaaSamples = 4;
        strncpy_s(qualityLevels[2].name, sizeof(QualityLevel::name), "High", sizeof(QualityLevel::name) - 1);
        qualityLevels[2].shadowResolution = 2048; qualityLevels[2].msaaSamples = 4; qualityLevels[2].bloom = true;
        strncpy_s(qualityLevels[3].name, sizeof(QualityLevel::name), "Ultra", sizeof(QualityLevel::name) - 1);
        qualityLevels[3].shadowResolution = 4096; qualityLevels[3].msaaSamples = 8;
        qualityLevels[3].bloom = true; qualityLevels[3].ao = true;

        // Default input axes
        {
            InputAxis h; strncpy_s(h.name, sizeof(h.name), "Horizontal", sizeof(h.name) - 1);
            strncpy_s(h.positiveKey, sizeof(h.positiveKey), "d", sizeof(h.positiveKey) - 1);
            strncpy_s(h.negativeKey, sizeof(h.negativeKey), "a", sizeof(h.negativeKey) - 1);
            inputAxes.push_back(h);
        }
        {
            InputAxis v; strncpy_s(v.name, sizeof(v.name), "Vertical", sizeof(v.name) - 1);
            strncpy_s(v.positiveKey, sizeof(v.positiveKey), "w", sizeof(v.positiveKey) - 1);
            strncpy_s(v.negativeKey, sizeof(v.negativeKey), "s", sizeof(v.negativeKey) - 1);
            inputAxes.push_back(v);
        }

        // Default tags & layers
        tags = { "Untagged", "Player", "Enemy", "Terrain", "Trigger" };
        layers = { "Default", "TransparentFX", "IgnoreRaycast", "UI", "Water" };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Simple key=value serialiser (no external dependency)
// ─────────────────────────────────────────────────────────────────────────────
namespace ProjectIO {

    inline void WriteKV(std::ostream& f, const std::string& key, const std::string& val) {
        f << key << "=" << val << "\n";
    }
    inline void WriteKV(std::ostream& f, const std::string& key, float v) {
        f << key << "=" << v << "\n";
    }
    inline void WriteKV(std::ostream& f, const std::string& key, int v) {
        f << key << "=" << v << "\n";
    }
    inline void WriteKV(std::ostream& f, const std::string& key, bool v) {
        f << key << "=" << (v ? "1" : "0") << "\n";
    }

    inline std::unordered_map<std::string, std::string> ParseKV(std::istream& f) {
        std::unordered_map<std::string, std::string> m;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            m[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return m;
    }

    inline float KVFloat(const std::unordered_map<std::string, std::string>& m,
        const std::string& k, float def = 0.f) {
        auto it = m.find(k);
        if (it == m.end()) return def;
        try { return std::stof(it->second); }
        catch (...) { return def; }
    }
    inline int KVInt(const std::unordered_map<std::string, std::string>& m,
        const std::string& k, int def = 0) {
        auto it = m.find(k);
        if (it == m.end()) return def;
        try { return std::stoi(it->second); }
        catch (...) { return def; }
    }
    inline bool KVBool(const std::unordered_map<std::string, std::string>& m,
        const std::string& k, bool def = false) {
        auto it = m.find(k);
        if (it == m.end()) return def;
        return it->second == "1";
    }
    inline std::string KVStr(const std::unordered_map<std::string, std::string>& m,
        const std::string& k, const std::string& def = "") {
        auto it = m.find(k);
        return (it == m.end()) ? def : it->second;
    }

    // ── Save ──────────────────────────────────────────────────────────────────
    inline bool Save(const ProjectSettings& p, const std::string& filepath) {
        std::ofstream f(filepath);
        if (!f.good()) return false;

        f << "# HonHon Engine Project — format " << kProjectFormatVersion << "\n";
        WriteKV(f, "format", kProjectFormatVersion);
        WriteKV(f, "name", std::string(p.name));
        WriteKV(f, "rootFolder", std::string(p.rootFolder));
        WriteKV(f, "version", std::string(p.version));
        WriteKV(f, "author", std::string(p.author));
        WriteKV(f, "templateId", p.templateId);

        // Physics
        f << "[physics]\n";
        WriteKV(f, "gx", p.physics.gravityX);
        WriteKV(f, "gy", p.physics.gravityY);
        WriteKV(f, "gz", p.physics.gravityZ);
        WriteKV(f, "fixedStep", p.physics.fixedTimestep);
        WriteKV(f, "solverIter", p.physics.solverIterations);
        WriteKV(f, "bounceThresh", p.physics.bounceThreshold);
        WriteKV(f, "sleepThresh", p.physics.sleepThreshold);
        WriteKV(f, "queriesHitTrig", p.physics.queriesHitTriggers);
        WriteKV(f, "autoSim", p.physics.autoSimulation);

        // Time
        f << "[time]\n";
        WriteKV(f, "timeScale", p.time.timeScale);
        WriteKV(f, "maxDelta", p.time.maxDeltaTime);
        WriteKV(f, "fixedDelta", p.time.fixedDeltaTime);
        WriteKV(f, "targetFPS", p.time.targetFrameRate);
        WriteKV(f, "vsync", p.time.vsync);

        // Render
        f << "[render]\n";
        WriteKV(f, "colorSpace", p.render.colorSpace);
        WriteKV(f, "renderPath", p.render.renderPath);
        WriteKV(f, "ambientInt", p.render.ambientIntensity);
        WriteKV(f, "fogDensity", p.render.fogDensity);
        WriteKV(f, "fogR", p.render.fogColor[0]);
        WriteKV(f, "fogG", p.render.fogColor[1]);
        WriteKV(f, "fogB", p.render.fogColor[2]);
        WriteKV(f, "fogEnabled", p.render.fogEnabled);
        WriteKV(f, "wireframe", p.render.wireframe);
        WriteKV(f, "skyboxMode", p.render.skyboxMode);
        WriteKV(f, "skyR", p.render.skyColor[0]);
        WriteKV(f, "skyG", p.render.skyColor[1]);
        WriteKV(f, "skyB", p.render.skyColor[2]);

        // Quality
        WriteKV(f, "currentQuality", p.currentQualityLevel);
        WriteKV(f, "qualityCount", (int)p.qualityLevels.size());
        for (int i = 0; i < (int)p.qualityLevels.size(); ++i) {
            auto& q = p.qualityLevels[i];
            std::string pfx = "q" + std::to_string(i) + "_";
            WriteKV(f, pfx + "name", std::string(q.name));
            WriteKV(f, pfx + "shadowRes", q.shadowResolution);
            WriteKV(f, pfx + "msaa", q.msaaSamples);
            WriteKV(f, pfx + "soft", q.softShadows);
            WriteKV(f, pfx + "bloom", q.bloom);
            WriteKV(f, pfx + "ao", q.ao);
            WriteKV(f, pfx + "scale", q.renderScale);
            WriteKV(f, pfx + "aniso", q.anisotropicFilter);
        }

        // Input
        WriteKV(f, "axisCount", (int)p.inputAxes.size());
        for (int i = 0; i < (int)p.inputAxes.size(); ++i) {
            auto& a = p.inputAxes[i];
            std::string pfx = "axis" + std::to_string(i) + "_";
            WriteKV(f, pfx + "name", std::string(a.name));
            WriteKV(f, pfx + "pos", std::string(a.positiveKey));
            WriteKV(f, pfx + "neg", std::string(a.negativeKey));
            WriteKV(f, pfx + "sens", a.sensitivity);
            WriteKV(f, pfx + "grav", a.gravity);
            WriteKV(f, pfx + "dead", a.deadZone);
            WriteKV(f, pfx + "snap", a.snap);
        }

        // Tags
        WriteKV(f, "tagCount", (int)p.tags.size());
        for (int i = 0; i < (int)p.tags.size(); ++i)
            WriteKV(f, "tag" + std::to_string(i), p.tags[i]);

        // Layers
        WriteKV(f, "layerCount", (int)p.layers.size());
        for (int i = 0; i < (int)p.layers.size(); ++i)
            WriteKV(f, "layer" + std::to_string(i), p.layers[i]);

        // Auto-save
        WriteKV(f, "autoSave", p.autoSave);
        WriteKV(f, "autoSaveInt", p.autoSaveInterval);

        return true;
    }

    // ── Load ──────────────────────────────────────────────────────────────────
    inline bool Load(ProjectSettings& p, const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.good()) return false;
        auto m = ParseKV(f);

        int fmt = KVInt(m, "format", 1);
        // Future migration hooks would go here (fmt < kProjectFormatVersion)

        strncpy_s(p.name, sizeof(p.name), KVStr(m, "name", "Untitled").c_str(), sizeof(p.name) - 1);
        strncpy_s(p.rootFolder, sizeof(p.rootFolder), KVStr(m, "rootFolder", "./").c_str(), sizeof(p.rootFolder) - 1);
        strncpy_s(p.version, sizeof(p.version), KVStr(m, "version", "0.1.0").c_str(), sizeof(p.version) - 1);
        strncpy_s(p.author, sizeof(p.author), KVStr(m, "author", "").c_str(), sizeof(p.author) - 1);
        p.templateId = KVInt(m, "templateId", 0);

        p.physics.gravityX = KVFloat(m, "gx", 0.f);
        p.physics.gravityY = KVFloat(m, "gy", -9.81f);
        p.physics.gravityZ = KVFloat(m, "gz", 0.f);
        p.physics.fixedTimestep = KVFloat(m, "fixedStep", 0.02f);
        p.physics.solverIterations = KVInt(m, "solverIter", 6);
        p.physics.bounceThreshold = KVFloat(m, "bounceThresh", 2.0f);
        p.physics.sleepThreshold = KVFloat(m, "sleepThresh", 0.005f);
        p.physics.queriesHitTriggers = KVBool(m, "queriesHitTrig", true);
        p.physics.autoSimulation = KVBool(m, "autoSim", true);

        p.time.timeScale = KVFloat(m, "timeScale", 1.f);
        p.time.maxDeltaTime = KVFloat(m, "maxDelta", 0.333f);
        p.time.fixedDeltaTime = KVFloat(m, "fixedDelta", 0.02f);
        p.time.targetFrameRate = KVInt(m, "targetFPS", -1);
        p.time.vsync = KVBool(m, "vsync", true);

        p.render.colorSpace = KVInt(m, "colorSpace", 0);
        p.render.renderPath = KVInt(m, "renderPath", 0);
        p.render.ambientIntensity = KVFloat(m, "ambientInt", 0.15f);
        p.render.fogDensity = KVFloat(m, "fogDensity", 0.f);
        p.render.fogColor[0] = KVFloat(m, "fogR", 0.78f);
        p.render.fogColor[1] = KVFloat(m, "fogG", 0.82f);
        p.render.fogColor[2] = KVFloat(m, "fogB", 0.90f);
        p.render.fogEnabled = KVBool(m, "fogEnabled", false);
        p.render.wireframe = KVBool(m, "wireframe", false);
        p.render.skyboxMode = KVInt(m, "skyboxMode", 0);
        p.render.skyColor[0] = KVFloat(m, "skyR", 0.18f);
        p.render.skyColor[1] = KVFloat(m, "skyG", 0.25f);
        p.render.skyColor[2] = KVFloat(m, "skyB", 0.40f);

        p.currentQualityLevel = KVInt(m, "currentQuality", 1);
        int qCount = KVInt(m, "qualityCount", 0);
        if (qCount > 0) {
            p.qualityLevels.clear();
            p.qualityLevels.resize(qCount);
            for (int i = 0; i < qCount; ++i) {
                auto& q = p.qualityLevels[i];
                std::string pfx = "q" + std::to_string(i) + "_";
                strncpy_s(q.name, sizeof(q.name), KVStr(m, pfx + "name", "Level").c_str(), sizeof(q.name) - 1);
                q.shadowResolution = KVInt(m, pfx + "shadowRes", 1024);
                q.msaaSamples = KVInt(m, pfx + "msaa", 4);
                q.softShadows = KVBool(m, pfx + "soft", true);
                q.bloom = KVBool(m, pfx + "bloom", false);
                q.ao = KVBool(m, pfx + "ao", false);
                q.renderScale = KVFloat(m, pfx + "scale", 1.0f);
                q.anisotropicFilter = KVInt(m, pfx + "aniso", 4);
            }
        }

        int axisCount = KVInt(m, "axisCount", 0);
        if (axisCount > 0) {
            p.inputAxes.clear();
            p.inputAxes.resize(axisCount);
            for (int i = 0; i < axisCount; ++i) {
                auto& a = p.inputAxes[i];
                std::string pfx = "axis" + std::to_string(i) + "_";
                strncpy_s(a.name, sizeof(a.name), KVStr(m, pfx + "name", "Axis").c_str(), sizeof(a.name) - 1);
                strncpy_s(a.positiveKey, sizeof(a.positiveKey), KVStr(m, pfx + "pos", "d").c_str(), sizeof(a.positiveKey) - 1);
                strncpy_s(a.negativeKey, sizeof(a.negativeKey), KVStr(m, pfx + "neg", "a").c_str(), sizeof(a.negativeKey) - 1);
                a.sensitivity = KVFloat(m, pfx + "sens", 3.f);
                a.gravity = KVFloat(m, pfx + "grav", 3.f);
                a.deadZone = KVFloat(m, pfx + "dead", 0.001f);
                a.snap = KVBool(m, pfx + "snap", true);
            }
        }

        int tagCount = KVInt(m, "tagCount", 0);
        p.tags.clear();
        for (int i = 0; i < tagCount; ++i)
            p.tags.push_back(KVStr(m, "tag" + std::to_string(i), ""));

        int layerCount = KVInt(m, "layerCount", 0);
        p.layers.clear();
        for (int i = 0; i < layerCount; ++i)
            p.layers.push_back(KVStr(m, "layer" + std::to_string(i), ""));

        p.autoSave = KVBool(m, "autoSave", true);
        p.autoSaveInterval = KVFloat(m, "autoSaveInt", 120.f);

        p.loaded = true;
        return true;
    }

} // namespace ProjectIO

// ─────────────────────────────────────────────────────────────────────────────
//  Auto-save manager — call Tick() once per frame with dt
// ─────────────────────────────────────────────────────────────────────────────
struct AutoSaveManager {
    float timer = 0.f;
    int   generation = 0;   // cycles through 3 recovery slots

    // Returns the path that was written, or "" if nothing was saved yet
    std::string Tick(float dt, const ProjectSettings& proj,
        bool sceneDirty, const std::string& sceneFilePath,
        const std::function<bool(const std::string&)>& saveSceneFn)
    {
        if (!proj.autoSave || !proj.loaded) return "";
        timer += dt;
        if (timer < proj.autoSaveInterval) return "";
        timer = 0.f;

        // Write to rotating backup slot
        std::string slot = std::string(proj.rootFolder) + "/.autosave/recovery"
            + std::to_string(generation % 3) + ".honscene";
        fs::create_directories(fs::path(slot).parent_path());

        if (saveSceneFn(slot)) {
            ++generation;
            return slot;
        }
        return "";
    }

    // Returns sorted list of existing recovery files
    static std::vector<fs::path> ListRecovery(const std::string& rootFolder) {
        std::vector<fs::path> out;
        fs::path dir = fs::path(rootFolder) / ".autosave";
        if (!fs::exists(dir)) return out;
        for (auto& e : fs::directory_iterator(dir)) {
            if (e.path().extension() == ".honscene")
                out.push_back(e.path());
        }
        std::sort(out.begin(), out.end(),
            [](const fs::path& a, const fs::path& b) {
                return fs::last_write_time(a) > fs::last_write_time(b);
            });
        return out;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  UI state for all project-related modals / windows
// ─────────────────────────────────────────────────────────────────────────────
struct ProjectUIState {
    // New-project wizard
    bool  showNewProject = false;
    char  newProjName[128] = "MyGame";
    char  newProjFolder[512] = "./projects/";
    int   newProjTemplate = 0;   // 0=Empty 3D, 1=Basic 3D, 2=Physics demo

    // Project settings window
    bool  showProjectSettings = false;
    int   settingsTab = 0;  // 0=General,1=Physics,2=Quality,3=Input,4=Tags,5=Layers,6=Time,7=Render

    // Save/Load project
    bool  showSaveProjModal = false;
    bool  showLoadProjModal = false;
    char  projFilePath[512] = "project.honproject";

    // Auto-save recovery toast
    bool  showRecoveryToast = false;
    std::vector<fs::path> recoveryFiles;
    float recoveryToastTimer = 0.f;

    // For inline tag/layer editing
    char  editTagBuf[64] = "";
    char  editLayerBuf[64] = "";

    // Quality level editing (which one is selected in the list)
    int   qualityEditIdx = 0;

    // Input axis editing
    int   axisEditIdx = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  DrawNewProjectModal
// ─────────────────────────────────────────────────────────────────────────────
// Returns true when the user clicked "Create" (caller should initialise the project)
inline bool DrawNewProjectModal(ProjectUIState& ui, ProjectSettings& proj) {
    if (ui.showNewProject) { ImGui::OpenPopup("New Project"); ui.showNewProject = false; }
    bool created = false;
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Create a new HonHon Engine project");
        ImGui::Separator();

        ImGui::InputText("Project name", ui.newProjName, sizeof(ui.newProjName));
        ImGui::InputText("Root folder", ui.newProjFolder, sizeof(ui.newProjFolder));
        ImGui::SameLine();
        if (ImGui::SmallButton("...")) {
            // Placeholder: real implementation would open a native folder picker
        }

        static const char* kTemplates[] = {
            "Empty 3D",
            "Basic 3D (default cube + ambient light)",
            "Physics Demo (boxes + gravity)"
        };
        ImGui::Combo("Template", &ui.newProjTemplate, kTemplates, 3);

        ImGui::Spacing();
        if (ImGui::Button("Create", { 140, 0 })) {
            strncpy_s(proj.name, sizeof(proj.name), ui.newProjName, sizeof(proj.name) - 1);
            strncpy_s(proj.rootFolder, sizeof(proj.rootFolder), ui.newProjFolder, sizeof(proj.rootFolder) - 1);
            proj.templateId = ui.newProjTemplate;
            proj.loaded = true;

            // Create folder structure
            try {
                fs::create_directories(fs::path(proj.rootFolder) / "assets");
                fs::create_directories(fs::path(proj.rootFolder) / "scenes");
                fs::create_directories(fs::path(proj.rootFolder) / "scripts");
                fs::create_directories(fs::path(proj.rootFolder) / ".autosave");
            }
            catch (...) {}

            // Save initial project file
            ProjectIO::Save(proj, std::string(proj.rootFolder) + "/" + proj.name + ".honproject");

            created = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return created;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawProjectSettingsWindow  (tabbed, non-modal)
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawProjectSettingsWindow(ProjectUIState& ui, ProjectSettings& proj) {
    if (!ui.showProjectSettings) return;
    ImGui::SetNextWindowSize({ 660, 520 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Settings", &ui.showProjectSettings)) { ImGui::End(); return; }

    // Tab bar
    static const char* kTabs[] = {
        "General", "Physics", "Quality", "Input", "Tags & Layers", "Time", "Render"
    };
    if (ImGui::BeginTabBar("ProjSettingsTabs")) {

        // ── General ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("General")) {
            ImGui::InputText("Project name", proj.name, sizeof(proj.name));
            ImGui::InputText("Version", proj.version, sizeof(proj.version));
            ImGui::InputText("Author", proj.author, sizeof(proj.author));
            ImGui::InputText("Root folder", proj.rootFolder, sizeof(proj.rootFolder));
            ImGui::Separator();
            ImGui::Checkbox("Auto-save", &proj.autoSave);
            if (proj.autoSave)
                ImGui::DragFloat("Interval (s)", &proj.autoSaveInterval, 5.f, 10.f, 3600.f, "%.0f s");
            ImGui::Spacing();
            if (ImGui::Button("Save project")) {
                ProjectIO::Save(proj, std::string(proj.rootFolder) + "/" + proj.name + ".honproject");
            }
            ImGui::EndTabItem();
        }

        // ── Physics ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Physics")) {
            ImGui::SeparatorText("Gravity");
            float g[3] = { proj.physics.gravityX, proj.physics.gravityY, proj.physics.gravityZ };
            if (ImGui::DragFloat3("Gravity (m/s²)", g, 0.1f))
            {
                proj.physics.gravityX = g[0]; proj.physics.gravityY = g[1]; proj.physics.gravityZ = g[2];
            }

            ImGui::SeparatorText("Solver");
            ImGui::DragFloat("Fixed timestep (s)", &proj.physics.fixedTimestep, 0.001f, 0.001f, 0.1f, "%.4f");
            ImGui::DragInt("Solver iterations", &proj.physics.solverIterations, 1, 1, 32);
            ImGui::DragFloat("Bounce threshold", &proj.physics.bounceThreshold, 0.1f, 0.f, 20.f);
            ImGui::DragFloat("Sleep threshold", &proj.physics.sleepThreshold, 0.001f, 0.f, 1.f, "%.4f");

            ImGui::SeparatorText("Behaviour");
            ImGui::Checkbox("Queries hit triggers", &proj.physics.queriesHitTriggers);
            ImGui::Checkbox("Auto simulation", &proj.physics.autoSimulation);
            ImGui::EndTabItem();
        }

        // ── Quality ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Quality")) {
            // Left list of quality levels
            ImGui::BeginChild("QualityList", { 150, 0 }, true);
            for (int i = 0; i < (int)proj.qualityLevels.size(); ++i) {
                bool sel = (ui.qualityEditIdx == i);
                if (ImGui::Selectable(proj.qualityLevels[i].name, sel))
                    ui.qualityEditIdx = i;
                if (i == proj.currentQualityLevel) {
                    ImGui::SameLine();
                    ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.f }, "*");
                }
            }
            ImGui::Separator();
            if (ImGui::SmallButton("+")) {
                QualityLevel q; strncpy_s(q.name, sizeof(q.name), "New Level", sizeof(q.name) - 1);
                proj.qualityLevels.push_back(q);
                ui.qualityEditIdx = (int)proj.qualityLevels.size() - 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-") && proj.qualityLevels.size() > 1) {
                proj.qualityLevels.erase(proj.qualityLevels.begin() + ui.qualityEditIdx);
                ui.qualityEditIdx = (std::max)(0, ui.qualityEditIdx - 1);
                proj.currentQualityLevel = (std::min)(proj.currentQualityLevel,
                    (int)proj.qualityLevels.size() - 1);
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right panel — edit selected level
            ImGui::BeginChild("QualityEdit", { 0, 0 }, false);
            if (ui.qualityEditIdx < (int)proj.qualityLevels.size()) {
                auto& q = proj.qualityLevels[ui.qualityEditIdx];
                ImGui::InputText("Name##ql", q.name, sizeof(q.name));

                static const char* kShadowRes[] = { "Off", "512", "1024", "2048", "4096" };
                int shadowIdx = 0;
                const int kShadowVals[] = { 0, 512, 1024, 2048, 4096 };
                for (int i = 0; i < 5; ++i) if (kShadowVals[i] == q.shadowResolution) shadowIdx = i;
                if (ImGui::Combo("Shadow resolution##ql", &shadowIdx, kShadowRes, 5))
                    q.shadowResolution = kShadowVals[shadowIdx];

                static const char* kMSAA[] = { "x1 (off)", "x2", "x4", "x8" };
                const int kMSAAVals[] = { 1, 2, 4, 8 };
                int msaaIdx = 0;
                for (int i = 0; i < 4; ++i) if (kMSAAVals[i] == q.msaaSamples) msaaIdx = i;
                if (ImGui::Combo("MSAA##ql", &msaaIdx, kMSAA, 4))
                    q.msaaSamples = kMSAAVals[msaaIdx];

                ImGui::Checkbox("Soft shadows##ql", &q.softShadows);
                ImGui::Checkbox("Bloom##ql", &q.bloom);
                ImGui::Checkbox("Ambient Occlusion##ql", &q.ao);
                ImGui::DragFloat("Render scale##ql", &q.renderScale, 0.05f, 0.25f, 2.f, "%.2fx");

                static const char* kAniso[] = { "x1", "x2", "x4", "x8", "x16" };
                const int kAnisoVals[] = { 1, 2, 4, 8, 16 };
                int anisoIdx = 0;
                for (int i = 0; i < 5; ++i) if (kAnisoVals[i] == q.anisotropicFilter) anisoIdx = i;
                if (ImGui::Combo("Anisotropic filter##ql", &anisoIdx, kAniso, 5))
                    q.anisotropicFilter = kAnisoVals[anisoIdx];

                ImGui::Spacing();
                bool isCurrent = (ui.qualityEditIdx == proj.currentQualityLevel);
                if (!isCurrent && ImGui::Button("Set as current"))
                    proj.currentQualityLevel = ui.qualityEditIdx;
                else if (isCurrent)
                    ImGui::TextColored({ 0.4f, 0.9f, 0.4f, 1.f }, "  (current)");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ── Input ─────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Input")) {
            ImGui::BeginChild("AxisList", { 160, 0 }, true);
            for (int i = 0; i < (int)proj.inputAxes.size(); ++i) {
                bool sel = (ui.axisEditIdx == i);
                if (ImGui::Selectable(proj.inputAxes[i].name, sel))
                    ui.axisEditIdx = i;
            }
            ImGui::Separator();
            if (ImGui::SmallButton("+")) {
                InputAxis a; strncpy_s(a.name, sizeof(a.name), "NewAxis", sizeof(a.name) - 1);
                proj.inputAxes.push_back(a);
                ui.axisEditIdx = (int)proj.inputAxes.size() - 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("-") && !proj.inputAxes.empty()) {
                proj.inputAxes.erase(proj.inputAxes.begin() + ui.axisEditIdx);
                if (ui.axisEditIdx >= (int)proj.inputAxes.size())
                    ui.axisEditIdx = (int)proj.inputAxes.size() - 1;
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("AxisEdit", { 0, 0 }, false);
            if (ui.axisEditIdx < (int)proj.inputAxes.size()) {
                auto& a = proj.inputAxes[ui.axisEditIdx];
                ImGui::InputText("Name##ax", a.name, sizeof(a.name));
                ImGui::InputText("Positive key##ax", a.positiveKey, sizeof(a.positiveKey));
                ImGui::InputText("Negative key##ax", a.negativeKey, sizeof(a.negativeKey));
                ImGui::DragFloat("Sensitivity##ax", &a.sensitivity, 0.1f, 0.1f, 20.f);
                ImGui::DragFloat("Gravity##ax", &a.gravity, 0.1f, 0.1f, 20.f);
                ImGui::DragFloat("Dead zone##ax", &a.deadZone, 0.001f, 0.f, 0.5f, "%.4f");
                ImGui::Checkbox("Snap##ax", &a.snap);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ── Tags & Layers ────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Tags & Layers")) {
            ImGui::BeginChild("TagsCol", { 0.f, 0.f }, false, ImGuiWindowFlags_None);
            ImGui::SeparatorText("Tags");
            for (int i = 0; i < (int)proj.tags.size(); ++i) {
                ImGui::PushID(i + 1000);
                char buf[64]; strncpy_s(buf, sizeof(buf), proj.tags[i].c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("##tag", buf, sizeof(buf)))
                    proj.tags[i] = buf;
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    proj.tags.erase(proj.tags.begin() + i);
                    ImGui::PopID(); break;
                }
                ImGui::PopID();
            }
            ImGui::InputText("##newTag", ui.editTagBuf, sizeof(ui.editTagBuf));
            ImGui::SameLine();
            if (ImGui::SmallButton("Add Tag") && ui.editTagBuf[0] != '\0') {
                proj.tags.push_back(ui.editTagBuf);
                ui.editTagBuf[0] = '\0';
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Layers");
            for (int i = 0; i < (int)proj.layers.size(); ++i) {
                ImGui::PushID(i + 2000);
                char buf[64]; strncpy_s(buf, sizeof(buf), proj.layers[i].c_str(), sizeof(buf) - 1);
                if (ImGui::InputText("##layer", buf, sizeof(buf)))
                    proj.layers[i] = buf;
                ImGui::SameLine();
                if (i > 0 && ImGui::SmallButton("x")) {   // keep "Default"
                    proj.layers.erase(proj.layers.begin() + i);
                    ImGui::PopID(); break;
                }
                ImGui::PopID();
            }
            ImGui::InputText("##newLayer", ui.editLayerBuf, sizeof(ui.editLayerBuf));
            ImGui::SameLine();
            if (ImGui::SmallButton("Add Layer") && ui.editLayerBuf[0] != '\0') {
                proj.layers.push_back(ui.editLayerBuf);
                ui.editLayerBuf[0] = '\0';
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ── Time ─────────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Time")) {
            ImGui::DragFloat("Time scale", &proj.time.timeScale, 0.01f, 0.f, 100.f);
            ImGui::DragFloat("Max delta time (s)", &proj.time.maxDeltaTime, 0.01f, 0.01f, 1.f, "%.3f");
            ImGui::DragFloat("Fixed delta time (s)", &proj.time.fixedDeltaTime, 0.001f, 0.001f, 0.1f, "%.4f");
            ImGui::DragInt("Target frame rate", &proj.time.targetFrameRate, 1, -1, 300,
                proj.time.targetFrameRate < 0 ? "Unlimited" : "%d fps");
            ImGui::Checkbox("VSync", &proj.time.vsync);
            ImGui::EndTabItem();
        }

        // ── Render ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Render")) {
            static const char* kColorSpace[] = { "Gamma", "Linear" };
            ImGui::Combo("Color space", &proj.render.colorSpace, kColorSpace, 2);
            static const char* kRenderPath[] = { "Forward", "Deferred" };
            ImGui::Combo("Render path", &proj.render.renderPath, kRenderPath, 2);
            ImGui::DragFloat("Ambient intensity", &proj.render.ambientIntensity, 0.01f, 0.f, 5.f);

            ImGui::Separator();
            ImGui::Checkbox("Fog enabled", &proj.render.fogEnabled);
            if (proj.render.fogEnabled) {
                ImGui::DragFloat("Fog density", &proj.render.fogDensity, 0.001f, 0.f, 1.f, "%.4f");
                ImGui::ColorEdit3("Fog color", proj.render.fogColor);
            }

            ImGui::Separator();
            static const char* kSkybox[] = { "Solid color", "Gradient", "Cubemap (path)" };
            ImGui::Combo("Skybox mode", &proj.render.skyboxMode, kSkybox, 3);
            ImGui::ColorEdit3("Sky color", proj.render.skyColor);

            ImGui::Separator();
            ImGui::Checkbox("Wireframe (editor)", &proj.render.wireframe);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawRecoveryToast  — call once per frame, returns chosen recovery path or ""
// ─────────────────────────────────────────────────────────────────────────────
inline std::string DrawRecoveryToast(ProjectUIState& ui, float dt) {
    if (!ui.showRecoveryToast || ui.recoveryFiles.empty()) return "";
    ui.recoveryToastTimer += dt;
    // Auto-dismiss after 30 s
    if (ui.recoveryToastTimer > 30.f) { ui.showRecoveryToast = false; return ""; }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = { io.DisplaySize.x - 400.f, io.DisplaySize.y - 160.f };
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ 390.f, 0.f });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, { 0.15f, 0.12f, 0.05f, 0.95f });
    ImGui::PushStyleColor(ImGuiCol_Border, { 0.80f, 0.60f, 0.10f, 1.00f });
    ImGui::Begin("##RecoveryToast", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextColored({ 1.f, 0.8f, 0.2f, 1.f }, " Auto-save recovery available");
    ImGui::TextDisabled("Most recent: %s",
        ui.recoveryFiles[0].filename().string().c_str());

    std::string chosen;
    if (ImGui::SmallButton("Restore newest")) {
        chosen = ui.recoveryFiles[0].string();
        ui.showRecoveryToast = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) ui.showRecoveryToast = false;

    ImGui::End();
    ImGui::PopStyleColor(2);
    return chosen;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawProjectSaveLoadModals  — call from DrawModals()
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawProjectSaveLoadModals(ProjectUIState& ui, ProjectSettings& proj) {
    if (ui.showSaveProjModal) { ImGui::OpenPopup("Save Project"); ui.showSaveProjModal = false; }
    if (ImGui::BeginPopupModal("Save Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save project file:");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##saveProjPath", ui.projFilePath, sizeof(ui.projFilePath));
        ImGui::Spacing();
        if (ImGui::Button("Save", { 120, 0 })) {
            ProjectIO::Save(proj, ui.projFilePath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (ui.showLoadProjModal) { ImGui::OpenPopup("Load Project"); ui.showLoadProjModal = false; }
    if (ImGui::BeginPopupModal("Load Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Load project file:");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##loadProjPath", ui.projFilePath, sizeof(ui.projFilePath));
        ImGui::Spacing();
        ImGui::TextColored({ 1.f, 0.6f, 0.2f, 1.f }, "  Current project settings will be replaced.");
        ImGui::Spacing();
        if (ImGui::Button("Load", { 120, 0 })) {
            ProjectIO::Load(proj, ui.projFilePath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}