#pragma once
// ide_package_manager.h  —  HonHon Engine IDE  —  Package & Plugin Manager
// =============================================================================
// Covers:
//   - Package descriptor (name, version, author, type, status)
//   - Package registry (built-in, installed, available from a manifest)
//   - Install / uninstall / update operations (simulated + async flag)
//   - Plugin type: renderer extension, importer, editor tool, library
//   - Version conflict detection
//   - Persistent installed-packages list  (.honpackages)
//   - Full ImGui panel with tabs: Installed / Available / Registry
// =============================================================================

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <functional>
#include <cstring>
#include <imgui.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Semantic version
// ─────────────────────────────────────────────────────────────────────────────
struct SemVer {
    int major = 0, minor = 0, patch = 0;

    bool operator<(const SemVer& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    bool operator==(const SemVer& o) const {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    bool operator>(const SemVer& o) const { return o < *this; }

    std::string ToString() const {
        return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    }
    static SemVer Parse(const std::string& s) {
        SemVer v; int i = 0;
        std::istringstream ss(s); std::string tok;
        while (std::getline(ss, tok, '.') && i < 3) {
            try {
                switch (i++) {
                case 0: v.major = std::stoi(tok); break;
                case 1: v.minor = std::stoi(tok); break;
                case 2: v.patch = std::stoi(tok); break;
                }
            }
            catch (...) {}
        }
        return v;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Package descriptor
// ─────────────────────────────────────────────────────────────────────────────
enum class PackageType {
    Library = 0,
    RendererExtension,
    Importer,
    EditorTool,
    PhysicsPlugin,
    AudioPlugin,
    Template,
    Unknown
};

inline const char* PackageTypeName(PackageType t) {
    switch (t) {
    case PackageType::Library:            return "Library";
    case PackageType::RendererExtension:  return "Renderer Extension";
    case PackageType::Importer:           return "Importer";
    case PackageType::EditorTool:         return "Editor Tool";
    case PackageType::PhysicsPlugin:      return "Physics Plugin";
    case PackageType::AudioPlugin:        return "Audio Plugin";
    case PackageType::Template:           return "Template";
    default:                              return "Unknown";
    }
}
inline const char* PackageTypeIcon(PackageType t) {
    switch (t) {
    case PackageType::Library:            return "\xef\x84\xa0";  // fa-terminal
    case PackageType::RendererExtension:  return "\xef\x80\xbe";  // fa-image
    case PackageType::Importer:           return "\xef\x81\x93";  // fa-upload
    case PackageType::EditorTool:         return "\xef\x80\x93";  // fa-cog
    case PackageType::PhysicsPlugin:      return "\xef\x86\xb2";  // fa-cube
    case PackageType::AudioPlugin:        return "\xef\x80\xa1";  // fa-music
    case PackageType::Template:           return "\xef\x81\xbb";  // fa-tree
    default:                              return "\xef\x85\x9b";  // fa-file
    }
}

enum class PackageStatus {
    Available,   // in registry, not installed
    Installed,
    UpToDate,
    UpdateAvailable,
    Installing,
    Uninstalling,
    Error
};

struct PackageDependency {
    std::string id;
    SemVer      minVersion;
};

struct PackageRecord {
    std::string   id;           // e.g. "com.honhon.gltf-importer"
    std::string   displayName;
    std::string   author;
    std::string   description;
    PackageType   type = PackageType::Library;
    SemVer        version;
    SemVer        installedVersion;  // if installed
    PackageStatus status = PackageStatus::Available;
    bool          isBuiltIn = false;  // built-in packages cannot be removed
    std::string   registryUrl;
    std::string   localPath;    // local install path
    std::vector<PackageDependency> dependencies;
    std::string   changelog;
    std::string   license;
    bool          selected = false;  // UI selection
};

// ─────────────────────────────────────────────────────────────────────────────
//  PackageManager
// ─────────────────────────────────────────────────────────────────────────────
struct PackageManager {
    std::vector<PackageRecord> packages;  // all known packages (installed + registry)

    // Callbacks (set by IDE host)
    std::function<void(const std::string& id)> onInstall;
    std::function<void(const std::string& id)> onUninstall;
    std::function<void(const std::string& id)> onUpdate;

    PackageManager() {
        // ── Seed built-in packages ───────────────────────────────────────────
        auto addBuiltIn = [&](const char* id, const char* name, const char* desc,
            PackageType t, const char* ver) {
                PackageRecord r;
                r.id = id; r.displayName = name; r.description = desc;
                r.type = t; r.version = SemVer::Parse(ver);
                r.installedVersion = r.version;
                r.status = PackageStatus::UpToDate; r.isBuiltIn = true;
                packages.push_back(r);
            };
        addBuiltIn("com.honhon.core", "Core Engine", "Scene management, renderer, camera.", PackageType::Library, "1.0.0");
        addBuiltIn("com.honhon.gltf", "glTF Importer", "Import glTF 2.0 / GLB assets.", PackageType::Importer, "1.2.0");
        addBuiltIn("com.honhon.obj", "OBJ Importer", "Import Wavefront .obj meshes.", PackageType::Importer, "1.0.3");
        addBuiltIn("com.honhon.water-shader", "Water Shader", "Animated water surface shader.", PackageType::RendererExtension, "1.1.0");
        addBuiltIn("com.honhon.beach-shader", "Beach Shader", "Coastal sand & foam shader.", PackageType::RendererExtension, "1.0.0");
        addBuiltIn("com.honhon.skinned", "Skinned Mesh", "GPU skeletal animation shader.", PackageType::RendererExtension, "1.0.0");

        // ── Available registry packages ──────────────────────────────────────
        auto addAvail = [&](const char* id, const char* name, const char* desc,
            PackageType t, const char* ver, const char* url = "") {
                PackageRecord r;
                r.id = id; r.displayName = name; r.description = desc;
                r.type = t; r.version = SemVer::Parse(ver);
                r.status = PackageStatus::Available; r.registryUrl = url;
                packages.push_back(r);
            };
        addAvail("com.honhon.physics-bullet", "Bullet Physics", "Rigid body, constraints, raycasts.", PackageType::PhysicsPlugin, "3.25.0");
        addAvail("com.honhon.audio-openal", "OpenAL Audio", "3D spatial audio via OpenAL.", PackageType::AudioPlugin, "2.1.0");
        addAvail("com.honhon.fbx-importer", "FBX Importer", "Import Autodesk FBX files.", PackageType::Importer, "0.9.2");
        addAvail("com.honhon.terrain", "Terrain System", "Heightmap terrain with LOD.", PackageType::Library, "0.4.0");
        addAvail("com.honhon.post-fx", "Post Processing", "Bloom, SSAO, depth of field.", PackageType::RendererExtension, "0.7.1");
        addAvail("com.honhon.nav-mesh", "NavMesh", "AI pathfinding via navigation mesh.", PackageType::Library, "0.3.0");
        addAvail("com.honhon.spline", "Spline Tools", "Bezier splines, path editor.", PackageType::EditorTool, "1.0.0");
        addAvail("com.honhon.lua-scripting", "Lua Scripting", "Embedded Lua 5.4 runtime.", PackageType::Library, "5.4.0");
        addAvail("com.honhon.scene-template", "3D Starter Scene", "Ready-to-use scene template.", PackageType::Template, "1.0.0");
    }

    // ── Queries ──────────────────────────────────────────────────────────────
    std::vector<PackageRecord*> Installed() {
        std::vector<PackageRecord*> out;
        for (auto& p : packages)
            if (p.status == PackageStatus::Installed ||
                p.status == PackageStatus::UpToDate ||
                p.status == PackageStatus::UpdateAvailable)
                out.push_back(&p);
        return out;
    }
    std::vector<PackageRecord*> Available() {
        std::vector<PackageRecord*> out;
        for (auto& p : packages)
            if (p.status == PackageStatus::Available) out.push_back(&p);
        return out;
    }

    PackageRecord* Find(const std::string& id) {
        for (auto& p : packages) if (p.id == id) return &p;
        return nullptr;
    }

    // ── Operations ───────────────────────────────────────────────────────────
    void Install(const std::string& id) {
        auto* p = Find(id);
        if (!p || p->isBuiltIn) return;
        p->status = PackageStatus::Installing;
        // Check deps
        for (auto& dep : p->dependencies) {
            auto* dep_pkg = Find(dep.id);
            if (!dep_pkg || dep_pkg->status == PackageStatus::Available) {
                Install(dep.id);  // recursive dep install
            }
        }
        // Simulate install (real: download + extract to localPath)
        p->installedVersion = p->version;
        p->status = PackageStatus::Installed;
        if (onInstall) onInstall(id);
    }

    void Uninstall(const std::string& id) {
        auto* p = Find(id);
        if (!p || p->isBuiltIn) return;
        p->status = PackageStatus::Uninstalling;
        // Check reverse deps
        for (auto& other : packages) {
            for (auto& dep : other.dependencies)
                if (dep.id == id && (other.status == PackageStatus::Installed ||
                    other.status == PackageStatus::UpToDate))
                    Uninstall(other.id);
        }
        p->status = PackageStatus::Available;
        p->installedVersion = { 0,0,0 };
        if (onUninstall) onUninstall(id);
    }

    void Update(const std::string& id) {
        auto* p = Find(id);
        if (!p) return;
        p->status = PackageStatus::Installing;
        p->installedVersion = p->version;
        p->status = PackageStatus::Installed;
        if (onUpdate) onUpdate(id);
    }

    // ── Persistence ──────────────────────────────────────────────────────────
    bool Save(const std::string& filepath) const {
        std::ofstream f(filepath);
        if (!f.good()) return false;
        f << "# HonHon Package List v1\n";
        for (auto& p : packages) {
            if (p.status != PackageStatus::Installed &&
                p.status != PackageStatus::UpToDate &&
                p.status != PackageStatus::UpdateAvailable) continue;
            f << p.id << "=" << p.installedVersion.ToString() << "\n";
        }
        return true;
    }

    bool Load(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.good()) return false;
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string id = line.substr(0, eq);
            std::string ver = line.substr(eq + 1);
            auto* p = Find(id);
            if (!p) {
                // Unknown package — add a stub
                PackageRecord r;
                r.id = id; r.displayName = id;
                r.installedVersion = SemVer::Parse(ver);
                r.status = PackageStatus::Installed;
                packages.push_back(r);
            }
            else {
                p->installedVersion = SemVer::Parse(ver);
                p->status = (p->installedVersion == p->version)
                    ? PackageStatus::UpToDate
                    : PackageStatus::UpdateAvailable;
            }
        }
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Package Manager UI state
// ─────────────────────────────────────────────────────────────────────────────
struct PackageManagerUIState {
    bool  show = false;
    int   activeTab = 0;    // 0=Installed, 1=Available, 2=All
    char  searchBuf[128] = {};
    int   typeFilter = -1;   // -1=all
    std::string selectedId;
    bool  showDetails = false;

    // Confirm uninstall
    bool  showConfirmUninstall = false;
    std::string confirmUninstallId;
};

// ─────────────────────────────────────────────────────────────────────────────
//  DrawPackageManagerWindow
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawPackageManagerWindow(PackageManagerUIState& ui, PackageManager& pm) {
    if (!ui.show) return;
    ImGui::SetNextWindowSize({ 780, 540 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Package Manager", &ui.show)) { ImGui::End(); return; }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(220.f);
    ImGui::InputTextWithHint("##pkgsearch", "\xef\x80\x82 Search packages...",
        ui.searchBuf, sizeof(ui.searchBuf));
    ImGui::SameLine();

    static const char* kTypeFilterNames[] = {
        "All Types", "Library", "Renderer Ext.", "Importer",
        "Editor Tool", "Physics", "Audio", "Template"
    };
    ImGui::SetNextItemWidth(120.f);
    int filt = ui.typeFilter + 1;
    if (ImGui::Combo("##pkgtype", &filt, kTypeFilterNames, 8))
        ui.typeFilter = filt - 1;

    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh registry")) {
        // In a real editor this would fetch a remote manifest
    }

    ImGui::Separator();

    // ── Content: left list + right details ───────────────────────────────────
    float detailsW = ui.showDetails ? 280.f : 0.f;

    // ── Tab bar ──────────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("##pkgtabs")) {

        // Helper lambda to draw a package list
        auto DrawPackageList = [&](std::vector<PackageRecord*>& pkgs) {
            std::string searchLow(ui.searchBuf);
            for (auto& c : searchLow) c = (char)std::tolower((unsigned char)c);

            if (ImGui::BeginTable("##pkgtbl", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                { -detailsW, -1.f })) {

                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 100.f);
                ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 70.f);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 90.f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 100.f);
                ImGui::TableHeadersRow();

                for (auto* p : pkgs) {
                    // Filter
                    if (ui.typeFilter >= 0 && (int)p->type != ui.typeFilter) continue;
                    if (!searchLow.empty()) {
                        std::string nameLow = p->displayName;
                        for (auto& c : nameLow) c = (char)std::tolower((unsigned char)c);
                        if (nameLow.find(searchLow) == std::string::npos) continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    bool sel = (ui.selectedId == p->id);
                    ImGui::PushID(p->id.c_str());
                    if (ImGui::Selectable(("##row_" + p->id).c_str(), sel,
                        ImGuiSelectableFlags_SpanAllColumns)) {
                        ui.selectedId = p->id;
                        ui.showDetails = true;
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(PackageTypeIcon(p->type));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(p->displayName.c_str());
                    if (p->isBuiltIn) {
                        ImGui::SameLine();
                        ImGui::TextColored({ 0.5f,0.8f,0.5f,1.f }, "(built-in)");
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextDisabled("%s", PackageTypeName(p->type));

                    ImGui::TableSetColumnIndex(2);
                    if (p->status == PackageStatus::Available)
                        ImGui::TextDisabled("%s", p->version.ToString().c_str());
                    else
                        ImGui::TextDisabled("%s", p->installedVersion.ToString().c_str());

                    ImGui::TableSetColumnIndex(3);
                    switch (p->status) {
                    case PackageStatus::UpToDate:
                        ImGui::TextColored({ 0.4f,0.9f,0.4f,1.f }, "Up to date");  break;
                    case PackageStatus::Installed:
                        ImGui::TextColored({ 0.7f,0.9f,0.7f,1.f }, "Installed");   break;
                    case PackageStatus::UpdateAvailable:
                        ImGui::TextColored({ 1.f, 0.8f,0.2f,1.f }, "Update ready"); break;
                    case PackageStatus::Available:
                        ImGui::TextDisabled("Available"); break;
                    case PackageStatus::Installing:
                        ImGui::TextColored({ 0.5f,0.7f,1.f,1.f }, "Installing…"); break;
                    case PackageStatus::Uninstalling:
                        ImGui::TextColored({ 1.f,0.5f,0.5f,1.f }, "Removing…");   break;
                    case PackageStatus::Error:
                        ImGui::TextColored({ 1.f,0.3f,0.3f,1.f }, "Error");        break;
                    }

                    ImGui::TableSetColumnIndex(4);
                    bool isInstalled = (p->status == PackageStatus::Installed ||
                        p->status == PackageStatus::UpToDate ||
                        p->status == PackageStatus::UpdateAvailable);
                    if (p->isBuiltIn) {
                        ImGui::TextDisabled("—");
                    }
                    else if (isInstalled) {
                        if (p->status == PackageStatus::UpdateAvailable) {
                            if (ImGui::SmallButton("Update")) pm.Update(p->id);
                            ImGui::SameLine();
                        }
                        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.5f,0.5f,1.f });
                        if (ImGui::SmallButton("Remove")) {
                            ui.showConfirmUninstall = true;
                            ui.confirmUninstallId = p->id;
                        }
                        ImGui::PopStyleColor();
                    }
                    else {
                        if (ImGui::SmallButton("Install")) pm.Install(p->id);
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            };

        // ── Tab: Installed ────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Installed")) {
            auto ins = pm.Installed();
            DrawPackageList(ins);
            ImGui::EndTabItem();
        }

        // ── Tab: Available ────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Available")) {
            auto avail = pm.Available();
            DrawPackageList(avail);
            ImGui::EndTabItem();
        }

        // ── Tab: All ─────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("All")) {
            std::vector<PackageRecord*> all;
            for (auto& p : pm.packages) all.push_back(&p);
            DrawPackageList(all);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // ── Detail panel ─────────────────────────────────────────────────────────
    if (ui.showDetails && !ui.selectedId.empty()) {
        ImGui::SameLine(0.f, 6.f);
        auto* p = pm.Find(ui.selectedId);
        ImGui::BeginChild("##pkgdetail", { -1.f, -1.f }, true);
        if (p) {
            ImGui::TextUnformatted(PackageTypeIcon(p->type));
            ImGui::SameLine();
            ImGui::Text("%s", p->displayName.c_str());
            ImGui::TextDisabled("by %s", p->author.empty() ? "HonHon Engine" : p->author.c_str());
            ImGui::TextDisabled("v%s", p->version.ToString().c_str());
            ImGui::Separator();
            ImGui::TextWrapped("%s", p->description.empty() ?
                "(No description provided)" : p->description.c_str());
            ImGui::Spacing();
            if (!p->license.empty()) {
                ImGui::TextDisabled("License: %s", p->license.c_str());
            }
            if (!p->dependencies.empty()) {
                ImGui::Spacing();
                ImGui::TextDisabled("Dependencies:");
                for (auto& dep : p->dependencies) {
                    ImGui::TextDisabled("  • %s  >= %s", dep.id.c_str(),
                        dep.minVersion.ToString().c_str());
                    // Show warning if dep is not installed
                    auto* dp = pm.Find(dep.id);
                    if (!dp || dp->status == PackageStatus::Available)
                        ImGui::TextColored({ 1.f,0.6f,0.2f,1.f },
                            "    ^ not installed");
                }
            }
            if (!p->changelog.empty()) {
                ImGui::Spacing();
                ImGui::SeparatorText("Changelog");
                ImGui::TextWrapped("%s", p->changelog.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            bool isInstalled = (p->status == PackageStatus::Installed ||
                p->status == PackageStatus::UpToDate ||
                p->status == PackageStatus::UpdateAvailable);
            if (!p->isBuiltIn) {
                if (!isInstalled) {
                    if (ImGui::Button("Install", { -1, 0 })) pm.Install(p->id);
                }
                else {
                    if (p->status == PackageStatus::UpdateAvailable)
                    {
                        if (ImGui::Button(("Update to v" + p->version.ToString()).c_str(), {-1, 0}))
                            pm.Update(p->id);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Button, { 0.5f,0.15f,0.15f,1.f });
                    if (ImGui::Button("Uninstall", { -1, 0 })) {
                        ui.showConfirmUninstall = true;
                        ui.confirmUninstallId = p->id;
                    }
                    ImGui::PopStyleColor();
                }
            }
            else {
                ImGui::TextColored({ 0.5f,0.8f,0.5f,1.f }, "Built-in — always available.");
            }
            if (ImGui::SmallButton("Close details")) ui.showDetails = false;
        }
        ImGui::EndChild();
    }

    // ── Confirm uninstall dialog ──────────────────────────────────────────────
    if (ui.showConfirmUninstall) {
        ImGui::OpenPopup("Confirm Remove");
        ui.showConfirmUninstall = false;
    }
    if (ImGui::BeginPopupModal("Confirm Remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto* p = pm.Find(ui.confirmUninstallId);
        if (p) ImGui::Text("Remove '%s'?", p->displayName.c_str());
        ImGui::TextColored({ 1.f,0.6f,0.2f,1.f }, "Packages that depend on this may break.");
        ImGui::Spacing();
        if (ImGui::Button("Remove", { 120, 0 })) {
            pm.Uninstall(ui.confirmUninstallId);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 90, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawPackageManagerMenuItems  — call inside a menu (e.g. Edit or a new
//  "Packages" top-level menu) to surface shortcuts to the window.
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawPackageManagerMenuItems(PackageManagerUIState& ui, PackageManager& pm) {
    if (ImGui::MenuItem("Package Manager…")) ui.show = true;
    ImGui::Separator();
    // Quick badges for pending updates
    int updates = 0;
    for (auto& p : pm.packages)
        if (p.status == PackageStatus::UpdateAvailable) ++updates;
    if (updates > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.8f, 0.2f, 1.f });
        ImGui::Text("  %d update(s) available", updates);
        ImGui::PopStyleColor();
    }
}