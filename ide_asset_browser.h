// ide_asset_browser.h  —  HonHon Engine IDE  —  Asset Browser Panel
// =============================================================================
// Covers:
//   - List / Grid view toggle
//   - Search (name, type filter), favorites filter
//   - Breadcrumb navigation + back button, clamped to project asset folder
//   - Thumbnail display (checkerboard fallback, type-icon overlay)
//   - Drag & drop payload from browser → scene (auto-imports as command)
//   - Context menu: open, rename, duplicate, delete, show in explorer
//   - Sub-objects expandable under compound assets
//   - Import settings inline in browser inspector strip
//   - Status bar: selected count, total size
//   - Navigation restricted to project asset root (never above)
//   - Folder and Scene icons improved
//   - Right-click empty area to create new assets (script, scene, material, folder)
// =============================================================================

#pragma once

#include "ide_asset_database.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <glad/glad.h>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#include <windows.h>    // for ShellExecuteA
#include <shellapi.h>   // for SW_SHOWNORMAL
#endif
#include "ide_icons.h"

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Drag & drop payload tag
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* kAssetDragPayload = "HONHON_ASSET";

struct AssetDragPayload {
    char  guidStr[40] = {};   // GUID of the dragged asset
    AssetType type = AssetType::Unknown;
    char  path[512] = {};   // absolute path
};

// ─────────────────────────────────────────────────────────────────────────────
//  Placeholder checkerboard texture  (generated once on GPU)
// ─────────────────────────────────────────────────────────────────────────────
static GLuint g_checkerTex = 0;

inline void EnsureCheckerTex() {
    if (g_checkerTex) return;
    constexpr int SZ = 16;
    uint8_t data[SZ * SZ * 4];
    for (int y = 0; y < SZ; ++y)
        for (int x = 0; x < SZ; ++x) {
            bool dark = ((x / 4 + y / 4) % 2) == 0;
            int idx = (y * SZ + x) * 4;
            data[idx + 0] = dark ? 80 : 140;
            data[idx + 1] = dark ? 80 : 140;
            data[idx + 2] = dark ? 80 : 140;
            data[idx + 3] = 255;
        }
    glGenTextures(1, &g_checkerTex);
    glBindTexture(GL_TEXTURE_2D, g_checkerTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SZ, SZ, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Open a file with the OS default application
// ─────────────────────────────────────────────────────────────────────────────
static void OpenWithDefaultProgram(const std::string& path) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + path + "\"";
    system(cmd.c_str());
#else
    // Linux / freedesktop
    std::string cmd = "xdg-open \"" + path + "\" &";
    system(cmd.c_str());
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: ensure path stays inside project asset root
// ─────────────────────────────────────────────────────────────────────────────
static bool IsPathInsideRoot(const std::string& path, const std::string& root) {
    if (root.empty()) return true;
    fs::path normalizedPath = fs::weakly_canonical(fs::path(path));
    fs::path normalizedRoot = fs::weakly_canonical(fs::path(root));

    // Walk up the path tree looking for root — fully cross-platform
    fs::path p = normalizedPath;
    while (true) {
        if (p == normalizedRoot) return true;
        fs::path parent = p.parent_path();
        if (parent == p) break;  // reached filesystem root
        p = parent;
    }
    return false;
}

static std::string ClampToRoot(const std::string& path, const std::string& root) {
    if (root.empty()) return path;
    if (IsPathInsideRoot(path, root))
        return path;
    return root;
}

// ─────────────────────────────────────────────────────────────────────────────
//  AssetBrowserState  — per-panel UI state
// ─────────────────────────────────────────────────────────────────────────────
struct AssetBrowserState {
    // Navigation – rootPath must be set from IDE (project asset folder)
    std::string currentDir;          // will be clamped to rootPath
    std::string rootPath;            // immutable root – no navigation above this
    std::vector<std::string> navStack;   // breadcrumb back-stack

    // View
    enum ViewMode { List, Grid } viewMode = Grid;
    float iconSize = 80.f;  // grid cell size
    bool  showFavoritesOnly = false;

    // Filter
    char  searchBuf[128] = {};
    int   typeFilter = -1;  // -1 = all, else (int)AssetType

    // Selection
    std::vector<std::string> selectedGUIDs;   // GUID strings
    std::string focusedGUID;                  // single focused (for inspector)
    std::string lastClickedGUID;              // anchor for Shift-range selection

    // Rename modal
    bool  showRenameModal = false;
    char  renameTargetGUID[40] = {};
    char  renameTargetPath[512] = {};   // fallback when GUID is unknown
    char  renameNewName[128] = {};

    // Path-based selection (for folders and unregistered files)
    std::vector<std::string> selectedPaths;

    // Script wizard modal
    bool  showScriptWizard = false;
    char  wizardScriptName[128] = "NewScript";
    int   wizardScriptTemplate = 0;   // 0=Empty, 1=Start/Update, 2=Full Example

    // Shader create-from-sources wizard
    bool  showShaderWizard = false;
    char  shaderWizardName[128] = "NewShader";
    char  shaderWizardVert[512] = {};   // absolute path to .vert file
    char  shaderWizardFrag[512] = {};   // absolute path to .frag file

    // Folder rename modal
    bool  showFolderRenameModal = false;
    char  folderRenameOldPath[512] = {};
    char  folderRenameNewName[128] = {};

    // Import settings panel (shown when exactly one asset is selected)
    bool  showImportSettings = true;

    // Directory cache
    std::vector<fs::directory_entry> dirEntries;
    bool  dirDirty = true;
    float rescanTimer = 0.f;

    // Drag state
    AssetDragPayload pendingDrop;
    bool hasPendingDrop = false;

    // Status
    std::string statusMsg;

    // Helper to set the project asset root (must be called before any navigation)
    void SetRootPath(const std::string& path) {
        rootPath = path;
        if (currentDir.empty()) currentDir = rootPath;
        else currentDir = ClampToRoot(currentDir, rootPath);
        dirDirty = true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: get background color tint per type (for non-folder assets)
// ─────────────────────────────────────────────────────────────────────────────
static ImVec4 AssetTypeTint(AssetType t) {
    switch (t) {
    case AssetType::Texture:      return { 0.3f, 0.7f, 1.0f, 0.25f };
    case AssetType::Model:        return { 0.6f, 0.9f, 0.5f, 0.25f };
    case AssetType::Audio:        return { 0.9f, 0.6f, 0.3f, 0.25f };
    case AssetType::Script:       return { 0.8f, 0.5f, 0.9f, 0.25f };
    case AssetType::Scene:        return { 1.0f, 0.85f, 0.2f, 0.25f };
    case AssetType::Prefab:       return { 0.2f, 0.9f, 0.85f, 0.30f };  // cyan-teal
    case AssetType::ShaderSource: return { 0.9f, 0.3f, 0.3f, 0.25f };
    default:                      return { 0.5f, 0.5f, 0.5f, 0.20f };
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: format bytes as human-readable
// ─────────────────────────────────────────────────────────────────────────────
static std::string FormatBytes(int64_t bytes) {
    if (bytes < 1024)       return std::to_string(bytes) + " B";
    if (bytes < 1 << 20)      return std::to_string(bytes >> 10) + " KB";
    if (bytes < 1 << 30)      return std::to_string(bytes >> 20) + " MB";
    return std::to_string(bytes >> 30) + " GB";
}

// ─────────────────────────────────────────────────────────────────────────────
//  Create new file helpers (right-click create)
// ─────────────────────────────────────────────────────────────────────────────
static bool CreateNewScript(const std::string& dir, const std::string& name, int templateIdx = 0) {
    fs::path filepath = fs::path(dir) / (name + ".cpp");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;

    f << "#include \"iscript.h\"\n";
    f << "#include \"baseobject.h\"\n\n";
    f << "class " << name << " : public IScript {\n";
    f << "public:\n";
    if (templateIdx >= 1) {
        f << "    void Start() override {}\n";
        f << "    void Update(float dt) override {}\n";
    }
    if (templateIdx == 2) {
        f << "    void OnDestroy() override {}\n";
        f << "    void OnCollision(BaseObject* other) {}\n";
    }
    f << "};\n\n";
    f << "extern \"C\" IScript* CreateScript() { return new " << name << "(); }\n";
    f << "extern \"C\" void DestroyScript(IScript* s) { delete s; }\n";
    f.close();
    return true;
}

static bool CreateNewScene(const std::string& dir, const std::string& name) {
    fs::path filepath = fs::path(dir) / (name + ".honscene");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;
    f << "# HonHon Scene File\n";
    f << "version=1\n";
    f.close();
    return true;
}

static bool CreateNewMaterial(const std::string& dir, const std::string& name) {
    fs::path filepath = fs::path(dir) / (name + ".honmat");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;
    f << "material\n{\n";
    f << "    shader = \"default\";\n";
    f << "    albedo = [1,1,1,1];\n";
    f << "    roughness = 0.5;\n";
    f << "    metalness = 0.0;\n";
    f << "}\n";
    f.close();
    return true;
}

static bool CreateNewFolder(const std::string& dir, const std::string& name) {
    fs::path folderpath = fs::path(dir) / name;
    if (fs::exists(folderpath)) return false;
    return fs::create_directory(folderpath);
}

// Creates a .honshader sidecar that records the vert/frag source paths.
// The engine's ShaderLibrary reads this file to compile + cache the program.
static bool CreateShaderAsset(const std::string& dir, const std::string& name,
    const std::string& vertPath, const std::string& fragPath)
{
    fs::path filepath = fs::path(dir) / (name + ".honshader");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;
    f << "shader\n{\n";
    f << "    name    = \"" << name << "\";\n";
    f << "    vertex  = \"" << vertPath << "\";\n";
    f << "    fragment = \"" << fragPath << "\";\n";
    f << "}\n";
    f.close();
    return true;
}

// Creates an empty .honprefab stub that can be filled in later
static bool CreateNewPrefab(const std::string& dir, const std::string& name) {
    fs::path filepath = fs::path(dir) / (name + ".honprefab");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;
    f << "{\n"
        << "  \"name\": \"" << name << "\",\n"
        << "  \"objects\": [],\n"
        << "  \"lights\": []\n"
        << "}\n";
    f.close();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: scan selectedGUIDs for .vert / .frag and fill wizard fields.
//  Returns true if at least one slot was filled.
// ─────────────────────────────────────────────────────────────────────────────
static bool FillShaderWizardFromSelection(AssetBrowserState& ab, AssetDatabase& db,
    const std::string& clickedPath = "")
{
    ab.shaderWizardVert[0] = '\0';
    ab.shaderWizardFrag[0] = '\0';
    bool gotVert = false, gotFrag = false;
    std::string stemName;

    // First check all selected GUIDs
    for (auto& g : ab.selectedGUIDs) {
        auto* r = db.FindByGUID(g);
        if (!r) continue;
        std::string ext = fs::path(r->path).extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".vert" && !gotVert) {
            strncpy_s(ab.shaderWizardVert, sizeof(ab.shaderWizardVert),
                r->path.c_str(), sizeof(ab.shaderWizardVert) - 1);
            gotVert = true;
            if (stemName.empty()) stemName = fs::path(r->path).stem().string();
        }
        else if (ext == ".frag" && !gotFrag) {
            strncpy_s(ab.shaderWizardFrag, sizeof(ab.shaderWizardFrag),
                r->path.c_str(), sizeof(ab.shaderWizardFrag) - 1);
            gotFrag = true;
            if (stemName.empty()) stemName = fs::path(r->path).stem().string();
        }
        if (gotVert && gotFrag) break;
    }

    // Fall back to the clicked file path if a slot is still empty
    if (!clickedPath.empty()) {
        std::string ext = fs::path(clickedPath).extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".vert" && !gotVert) {
            strncpy_s(ab.shaderWizardVert, sizeof(ab.shaderWizardVert),
                clickedPath.c_str(), sizeof(ab.shaderWizardVert) - 1);
            gotVert = true;
            if (stemName.empty()) stemName = fs::path(clickedPath).stem().string();
        }
        else if (ext == ".frag" && !gotFrag) {
            strncpy_s(ab.shaderWizardFrag, sizeof(ab.shaderWizardFrag),
                clickedPath.c_str(), sizeof(ab.shaderWizardFrag) - 1);
            gotFrag = true;
            if (stemName.empty()) stemName = fs::path(clickedPath).stem().string();
        }
    }

    if (!stemName.empty())
        strncpy_s(ab.shaderWizardName, sizeof(ab.shaderWizardName),
            stemName.c_str(), sizeof(ab.shaderWizardName) - 1);

    return gotVert || gotFrag;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Opens the host OS file explorer pointed at the specified item's folder
// ─────────────────────────────────────────────────────────────────────────────
static void OpenInFileExplorer(const fs::path& targetPath) {
    // Determine the directory containing the item if it's a file
    fs::path folderPath = fs::is_directory(targetPath) ? targetPath : targetPath.parent_path();
    std::string pathStr = folderPath.make_preferred().string();

#if defined(_WIN32)
    // On Windows, opens the folder directly in File Explorer
    ShellExecuteA(NULL, "open", "explorer.exe", pathStr.c_str(), NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string cmd = "open \"" + pathStr + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + pathStr + "\"";
    std::system(cmd.c_str());
#endif
}

//────────────────────────────────────────────────────────────────────────────
//  Context menu drawn per-item or globally on empty space
// ─────────────────────────────────────────────────────────────────────────────
static void DrawAssetContextMenu(AssetBrowserState& ab, AssetDatabase& db,
    const std::string& guidStr,
    const fs::directory_entry* entry,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset = nullptr)
{
    AssetRecord* rec = guidStr.empty() ? nullptr : db.FindByGUID(guidStr);

    bool isDir = entry && entry->is_directory();
    bool isPrefabEntry = entry && !isDir &&
        ExtToAssetType(entry->path().extension().string()) == AssetType::Prefab;

    // ── File/Type Specific Actions ───────────────────────────────────────────
    if (!isDir) {
        if (isPrefabEntry) {
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.3f, 1.f, 0.9f, 1.f });
            if (ImGui::MenuItem("\xef\x86\xb2  Edit Prefab")) {
                if (entry && onImportAsset)
                    onImportAsset(entry->path().string(), AssetType::Prefab);
            }
            ImGui::PopStyleColor();
            if (ImGui::MenuItem("Instantiate in Scene")) {
                if (entry && onImportAsset)
                    onImportAsset(entry->path().string() + "?instantiate", AssetType::Prefab);
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Open")) {
            if (entry) OpenWithDefaultProgram(entry->path().string());
        }
        if (rec && ImGui::MenuItem("Reimport")) { rec->needsReimport = true; }
        ImGui::Separator();
    }

    if (rec) {
        if (ImGui::MenuItem(rec->isFavorite ? "Remove from Favorites" : "Add to Favorites"))
            rec->isFavorite = !rec->isFavorite;
    }

    if (!isDir && rec) {
        if (ImGui::MenuItem("Duplicate")) {
            std::string newGuid = db.Duplicate(guidStr);
            if (!newGuid.empty()) ab.dirDirty = true;
        }
    }

    // Shader creation helper for source codes
    if (!isDir && entry) {
        AssetType et = ExtToAssetType(entry->path().extension().string());
        if (et == AssetType::ShaderSource) {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f, 0.5f, 0.5f, 1.f });
            if (ImGui::MenuItem("\xef\x81\x9b  Create Shader from Sources...")) {
                ab.showShaderWizard = true;
                FillShaderWizardFromSelection(ab, db, entry->path().string());
            }
            ImGui::PopStyleColor();
        }
    }

    // ── Mandatory Cross-Target Actions (Files & Folders) ────────────────────
    if (entry) {
        ImGui::Separator();

        // 1. Open in Explorer
        if (ImGui::MenuItem("Open in Explorer")) {
            // Open parent directory if it's a file, or open the folder itself
            OpenInFileExplorer(entry->path());
        }

        // 2. Copy Path
        if (ImGui::MenuItem("Copy Path")) {
            ImGui::SetClipboardText(entry->path().string().c_str());
        }

        // 3. Rename
        if (ImGui::MenuItem("Rename...")) {
            if (isDir) {
                ab.showFolderRenameModal = true;
                std::string oldPath = entry->path().string();
                std::string oldName = entry->path().filename().string();

                // Clean buffers and assign
                std::memset(ab.folderRenameOldPath, 0, sizeof(ab.folderRenameOldPath));
                std::memset(ab.folderRenameNewName, 0, sizeof(ab.folderRenameNewName));
                strncpy_s(ab.folderRenameOldPath, oldPath.c_str(), sizeof(ab.folderRenameOldPath) - 1);
                strncpy_s(ab.folderRenameNewName, oldName.c_str(), sizeof(ab.folderRenameNewName) - 1);
            }
            else {
                // Treat as a database tracked asset file
                ab.showRenameModal = true;
                std::string oldName = entry->path().filename().string();

                std::memset(ab.renameTargetGUID, 0, sizeof(ab.renameTargetGUID));
                std::memset(ab.renameTargetPath, 0, sizeof(ab.renameTargetPath));
                std::memset(ab.renameNewName, 0, sizeof(ab.renameNewName));
                strncpy_s(ab.renameTargetGUID, guidStr.c_str(), sizeof(ab.renameTargetGUID) - 1);
                strncpy_s(ab.renameTargetPath, entry->path().string().c_str(), sizeof(ab.renameTargetPath) - 1);
                strncpy_s(ab.renameNewName, oldName.c_str(), sizeof(ab.renameNewName) - 1);
            }
        }

        // 4. Delete
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.4f, 0.4f, 1.f });
        if (ImGui::MenuItem("Delete")) {
            try {
                if (isDir) {
                    fs::remove_all(entry->path());
                }
                else {
                    // If tracked in DB, run bulk delete cleanup, otherwise directly remove file
                    if (!guidStr.empty() && rec) {
                        db.BulkDelete({ guidStr });
                        ab.selectedGUIDs.erase(
                            std::remove(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), guidStr),
                            ab.selectedGUIDs.end()
                        );
                    }
                    else {
                        fs::remove(entry->path());
                    }
                }
                ab.dirDirty = true;
            }
            catch (const std::exception& e) {
                // Fallback fail-safe logging if a file system lock is active
                printf("Error executing delete: %s\n", e.what());
            }
        }
        ImGui::PopStyleColor();
    }

    // Multi-selection bulk ops (unchanged)
    if (ab.selectedGUIDs.size() > 1) {
        ImGui::Separator();
        ImGui::TextDisabled("%zu selected", ab.selectedGUIDs.size());

        {
            bool hasVert = false, hasFrag = false;
            for (auto& g : ab.selectedGUIDs) {
                auto* r = db.FindByGUID(g);
                if (!r) continue;
                std::string ext = fs::path(r->path).extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".vert") hasVert = true;
                else if (ext == ".frag") hasFrag = true;
            }
            if (hasVert && hasFrag) {
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f, 0.5f, 0.5f, 1.f });
                if (ImGui::MenuItem("\xef\x81\x9b  Create Shader from Sources...")) {
                    ab.showShaderWizard = true;
                    FillShaderWizardFromSelection(ab, db, entry ? entry->path().string() : "");
                }
                ImGui::PopStyleColor();
                ImGui::Separator();
            }
        }

        if (ImGui::MenuItem("Delete all selected")) {
            db.BulkDelete(ab.selectedGUIDs);
            ab.selectedGUIDs.clear();
            ab.dirDirty = true;
        }
        if (ImGui::MenuItem("Duplicate all selected")) {
            std::vector<std::string> toAdd;
            for (auto& g : ab.selectedGUIDs) {
                std::string ng = db.Duplicate(g);
                if (!ng.empty()) toAdd.push_back(ng);
            }
            ab.dirDirty = true;
        }
        ImGui::Separator();
        static char pfxBuf[64] = "", sfxBuf[64] = "";
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputText("Prefix##bc", pfxBuf, sizeof(pfxBuf));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f);
        ImGui::InputText("Suffix##bc", sfxBuf, sizeof(sfxBuf));
        if (ImGui::MenuItem("Rename selected (add prefix/suffix)"))
            db.BulkRename(ab.selectedGUIDs, pfxBuf, sfxBuf);
    }

    ImGui::EndPopup();
}

static void EnsureAssetRegistered(AssetDatabase& db, const fs::directory_entry& entry) {
    if (entry.is_directory()) return;
    std::string path = entry.path().string();
    if (!db.FindByPath(path)) {
        db.Register(path);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Draw a single grid cell; returns true if double-clicked (open dir / import)
//  Improved: folders show folder icon, scenes show scene icon
// ─────────────────────────────────────────────────────────────────────────────
static bool DrawGridCell(AssetBrowserState& ab, AssetDatabase& db,
    const fs::directory_entry& entry,
    float cellSize, bool& ctxOpen,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset = nullptr)
{
    bool dblClicked = false;
    std::string name = entry.path().filename().string();
    bool isDir = entry.is_directory();
    AssetType atype = isDir ? AssetType::Unknown
        : ExtToAssetType(entry.path().extension().string());
    std::string path = entry.path().string();

    EnsureAssetRegistered(db, entry);
    AssetRecord* rec = isDir ? nullptr : db.FindByPath(path);
    std::string gstr = (rec) ? rec->guid.ToString() : "";

    bool selected = (!gstr.empty() &&
        std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr)
        != ab.selectedGUIDs.end())
        || (gstr.empty() &&
            std::find(ab.selectedPaths.begin(), ab.selectedPaths.end(), path)
            != ab.selectedPaths.end());

    // Outer cell frame
    ImVec2 cellStart = ImGui::GetCursorScreenPos();
    ImVec4 bg = selected ? ImVec4{ 0.22f,0.47f,0.80f,0.45f }
        : (isDir ? ImVec4{ 0.25f,0.20f,0.15f,0.25f }   // warm tint for folders
    : AssetTypeTint(atype));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cellStart,
        { cellStart.x + cellSize, cellStart.y + cellSize + 20.f },
        ImGui::ColorConvertFloat4ToU32(bg), 6.f);
    if (selected)
        dl->AddRect(cellStart,
            { cellStart.x + cellSize, cellStart.y + cellSize + 20.f },
            IM_COL32(80, 140, 220, 200), 6.f, 0, 2.f);
    // Prefab gets a distinctive cyan border even when not selected
    else if (atype == AssetType::Prefab)
        dl->AddRect(cellStart,
            { cellStart.x + cellSize, cellStart.y + cellSize + 20.f },
            IM_COL32(50, 220, 200, 180), 6.f, 0, 1.5f);

    // Invisible button for interaction
    ImGui::PushID(name.c_str());
    ImGui::InvisibleButton("##cell", { cellSize, cellSize + 20.f });
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    dblClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (ImGui::BeginPopupContextItem("##assetctx")) {
        ctxOpen = true;
        DrawAssetContextMenu(ab, db, gstr, &entry, onImportAsset);   // calls EndPopup() internally
    }

    // Favorite star
    if (rec && rec->isFavorite) {
        dl->AddText({ cellStart.x + cellSize - 18.f, cellStart.y + 4.f },
            IM_COL32(255, 200, 40, 255), "\xef\x80\x85");  // fa-star
    }

    if (rec && rec->isDirty) {
        dl->AddCircleFilled({ cellStart.x + cellSize - 8.f, cellStart.y + 8.f }, 4.f,
            IM_COL32(255, 200, 40, 220));
    }

    // Thumbnail or icon
    float iconY = cellStart.y + 8.f;
    float iconX = cellStart.x + (cellSize - 48.f) * 0.5f;
    EnsureCheckerTex();

    if (isDir) {
        // Folder icon with nice background
        dl->AddRectFilled({ iconX, iconY }, { iconX + 48.f, iconY + 48.f },
            IM_COL32(70, 65, 55, 220), 8.f);
        dl->AddText(ImGui::GetFont(), 32.f,
            { iconX + (48.f - ImGui::CalcTextSize("\xef\x81\xbb").x) * 0.5f,
              iconY + (48.f - ImGui::GetFontSize()) * 0.5f },
            IM_COL32(210, 190, 140, 255), "\xef\x81\xbb");  // fa-folder
    }
    else if (atype == AssetType::Prefab) {
        // Prefab: dark teal background + cubes icon in bright cyan + "P" badge
        dl->AddRectFilled({ iconX, iconY }, { iconX + 48.f, iconY + 48.f },
            IM_COL32(20, 60, 65, 230), 8.f);
        // Outer cube outline ring
        dl->AddRect({ iconX + 2.f, iconY + 2.f }, { iconX + 46.f, iconY + 46.f },
            IM_COL32(50, 210, 200, 80), 6.f, 0, 1.f);
        // Cubes icon centered
        const char* prefabIcon = "\xef\x86\xb2";  // fa-cubes
        ImVec2 iconSz = ImGui::CalcTextSize(prefabIcon);
        dl->AddText(ImGui::GetFont(), 28.f,
            { iconX + (48.f - iconSz.x) * 0.5f,
              iconY + (48.f - 28.f) * 0.5f },
            IM_COL32(50, 220, 210, 255), prefabIcon);
        // Small "P" badge in bottom-right corner
        dl->AddRectFilled({ iconX + 32.f, iconY + 32.f }, { iconX + 47.f, iconY + 47.f },
            IM_COL32(30, 160, 150, 230), 3.f);
        dl->AddText(ImGui::GetFont(), 11.f,
            { iconX + 35.f, iconY + 35.f },
            IM_COL32(220, 255, 252, 255), "P");
    }
    else {
        GLuint thumbTex = (rec && rec->thumbnailTex) ? rec->thumbnailTex : g_checkerTex;

        if (rec && rec->thumbnailTex && rec->thumbnailTex != g_checkerTex) {
            // Calculate centered position for the 48x48 thumbnail
            float imgX = cellStart.x + (cellSize - 48.f) * 0.5f;
            float imgY = cellStart.y + 8.f;
            dl->AddImage((ImTextureID)(uintptr_t)rec->thumbnailTex,
                ImVec2(imgX, imgY),
                ImVec2(imgX + 48.f, imgY + 48.f));
        }
        else {
            const char* iconChar = AssetTypeIcon(atype);
            if (atype == AssetType::Scene) iconChar = "\xef\x86\xbb"; // fa-tree
            dl->AddText(ImGui::GetFont(), 32.f,
                { cellStart.x + (cellSize - 32.f) * 0.5f, cellStart.y + 18.f },
                IM_COL32(200, 210, 230, 200), iconChar);
        }
    }

    // Reimport badge
    if (rec && rec->needsReimport) {
        dl->AddCircleFilled({ cellStart.x + 8.f, cellStart.y + 8.f }, 5.f,
            IM_COL32(255, 100, 40, 220));
    }

    // Label (truncated)
    std::string label = name;
    const float maxLabelW = cellSize - 6.f;   // 3 px padding each side

    if (ImGui::CalcTextSize(label.c_str()).x > maxLabelW) {
        // Shrink until label + ellipsis fits within the card
        while (!label.empty() &&
            ImGui::CalcTextSize((label + "\xe2\x80\xa6").c_str()).x > maxLabelW)
            label.pop_back();
        label += "\xe2\x80\xa6";   // UTF-8 "…"
    }

    ImVec2 textSz = ImGui::CalcTextSize(label.c_str());
    dl->AddText({ cellStart.x + (cellSize - textSz.x) * 0.5f,
                  cellStart.y + cellSize - 2.f },
        hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 205, 215, 255),
        label.c_str());

    // Selection logic
    if (clicked && !ctxOpen) {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl) {
            ab.selectedGUIDs.clear();
            ab.selectedPaths.clear();
        }
        if (!gstr.empty()) {
            // GUID-tracked asset
            if (io.KeyShift && !ab.lastClickedGUID.empty()) {
                if (std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr)
                    == ab.selectedGUIDs.end())
                    ab.selectedGUIDs.push_back(gstr);
                ab.focusedGUID = gstr;
            }
            else {
                auto it = std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr);
                if (it == ab.selectedGUIDs.end())
                    ab.selectedGUIDs.push_back(gstr);
                else if (io.KeyCtrl)
                    ab.selectedGUIDs.erase(it);
                ab.focusedGUID = gstr;
                ab.lastClickedGUID = gstr;
            }
        }
        else {
            // Folder or unregistered file: use path-based selection
            auto it = std::find(ab.selectedPaths.begin(), ab.selectedPaths.end(), path);
            if (it == ab.selectedPaths.end())
                ab.selectedPaths.push_back(path);
            else if (io.KeyCtrl)
                ab.selectedPaths.erase(it);
        }
    }

    // Drag source (only for non-folders)
    // SourceAllowNullID is required because the last item is an InvisibleButton,
    // which has a null item-ID in ImGui's drag context.
    if (!isDir && !gstr.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        AssetDragPayload payload;
        strncpy_s(payload.guidStr, sizeof(payload.guidStr), gstr.c_str(), sizeof(payload.guidStr) - 1);
        strncpy_s(payload.path, sizeof(payload.path), path.c_str(), sizeof(payload.path) - 1);
        payload.type = atype;

        // If a .vert + .frag pair is multi-selected and we're dragging one of them,
        // upgrade the payload type to Shader so drop targets know it's a complete pair.
        // We scan selectedGUIDs AND the dragged file itself to cover the case where
        // the drag started before the second file's selection was registered.
        if (atype == AssetType::ShaderSource) {
            bool hasVert = false, hasFrag = false;
            // Check the dragged file first
            {
                std::string ext = fs::path(path).extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".vert") hasVert = true;
                else if (ext == ".frag") hasFrag = true;
            }
            // Then scan the rest of the selection
            for (auto& g : ab.selectedGUIDs) {
                auto* r = db.FindByGUID(g);
                if (!r) continue;
                std::string ext = fs::path(r->path).extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".vert") hasVert = true;
                else if (ext == ".frag") hasFrag = true;
                if (hasVert && hasFrag) break;
            }
            if (hasVert && hasFrag)
                payload.type = AssetType::Shader;
        }

        ImGui::SetDragDropPayload(kAssetDragPayload, &payload, sizeof(payload));
        // If dragging a multi-selection, show count; otherwise show the file name
        int selNonDir = 0;
        for (auto& g : ab.selectedGUIDs) {
            auto* r = db.FindByGUID(g);
            if (r) ++selNonDir;
        }
        if (selNonDir > 1) {
            ImGui::TextUnformatted(AssetTypeIcon(atype));
            ImGui::SameLine();
            if (payload.type == AssetType::Shader)
                ImGui::TextUnformatted("\xef\x81\x9b  Shader pair");
            else
                ImGui::Text("%d assets", selNonDir);
        }
        else {
            ImGui::TextUnformatted(AssetTypeIcon(atype));
            ImGui::SameLine();
            ImGui::TextUnformatted(name.c_str());
        }
        ImGui::EndDragDropSource();
    }

    // Tooltip
    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(name.c_str());
        if (rec) {
            ImGui::TextDisabled("%s  •  %s", AssetTypeName(rec->type),
                FormatBytes(rec->fileSize).c_str());
            ImGui::TextDisabled("GUID: %s", gstr.substr(0, 18).c_str());
            if (rec->needsReimport)
                ImGui::TextColored({ 1.f,0.6f,0.2f,1.f }, "  Needs reimport");
        }
        ImGui::EndTooltip();
    }

    ImGui::PopID();
    return dblClicked;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAssetBrowserContent  — Main content area (toolbar + files + settings)
//  Extracted to be reused in both "All Assets" and "Favorites" tabs
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawAssetBrowserContent(
    AssetBrowserState& ab,
    AssetDatabase& db,
    float dt,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset,
    const std::function<void(const std::string& scenePath)>& onOpenScene = nullptr)
{
    // Clamp currentDir to root at start (safety)
    if (ab.rootPath.empty()) {
        IM_ASSERT(false && "AssetBrowserState::rootPath not set! Call SetRootPath() before using asset browser.");
        return;
    }

    // Normalize currentDir to ensure it has a trailing slash for consistency
    if (!ab.currentDir.empty() && ab.currentDir.back() != '/' && ab.currentDir.back() != '\\')
        ab.currentDir += '/';

    ab.currentDir = ClampToRoot(ab.currentDir, ab.rootPath);

    // ── Toolbar ─────────────────────────────────────────────────────────────
    // Navigation: back
    bool canGoBack = !ab.navStack.empty();
    if (!canGoBack) ImGui::BeginDisabled();
    if (ImGui::Button(ICON_FA_ARROW_LEFT, { 28, 24 })) { // fa-arrows (back)
        if (!ab.navStack.empty()) {
            std::string backDir = ab.navStack.back();
            backDir = ClampToRoot(backDir, ab.rootPath);
            ab.currentDir = backDir;
            ab.navStack.pop_back();
            ab.dirDirty = true;
            ab.selectedGUIDs.clear();
        }
    }
    if (!canGoBack) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go back");

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button(ICON_FA_REDO_ALT, { 28, 24 })) {  // fa-sync
        ab.dirDirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh");

    ImGui::SameLine();

    // Path display (read-only to show current location)
    ImGui::SetNextItemWidth(300.f);
    char pathBuf[512];
    strncpy_s(pathBuf, sizeof(pathBuf), ab.currentDir.c_str(), sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    ImGui::InputText("##abpath", pathBuf, sizeof(pathBuf), ImGuiInputTextFlags_ReadOnly);

    ImGui::SameLine();

    // Search
    ImGui::SetNextItemWidth(140.f);
    ImGui::InputTextWithHint("##absearch", "\xef\x80\x82 Search...",
        ab.searchBuf, sizeof(ab.searchBuf));

    ImGui::SameLine();

    // Type filter combo
    static const char* kTypeNames[] = {
        "All", "Texture", "Model", "Audio", "Script",
        "Scene", "Material", "Prefab", "Font", "Shader", "Other"
    };
    ImGui::SetNextItemWidth(80.f);
    int filt = ab.typeFilter + 1;
    if (ImGui::Combo("##abtype", &filt, kTypeNames, 11)) {
        ab.typeFilter = filt - 1;
    }

    ImGui::SameLine();

    // Favorites toggle — capture state BEFORE the button so Push/Pop always match
    {
        bool favWasOn = ab.showFavoritesOnly;
        if (favWasOn) ImGui::PushStyleColor(ImGuiCol_Button, { 0.6f, 0.5f, 0.1f, 1.f });
        if (ImGui::Button("\xef\x80\x85")) ab.showFavoritesOnly = !ab.showFavoritesOnly;  // fa-star
        if (favWasOn) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show favorites only");

    ImGui::SameLine();

    // View toggle
    // View toggle — capture mode before button to keep Push/Pop balanced
    {
        bool isGrid = (ab.viewMode == AssetBrowserState::Grid);
        if (isGrid) ImGui::PushStyleColor(ImGuiCol_Button, { 0.22f, 0.47f, 0.80f, 0.75f });
        if (ImGui::Button("\xef\x84\xa7")) ab.viewMode = AssetBrowserState::Grid;  // fa-th-large
        if (isGrid) ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    {
        bool isList = (ab.viewMode == AssetBrowserState::List);
        if (isList) ImGui::PushStyleColor(ImGuiCol_Button, { 0.22f, 0.47f, 0.80f, 0.75f });
        if (ImGui::Button("\xef\x80\xba")) ab.viewMode = AssetBrowserState::List;  // fa-list
        if (isList) ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    // Icon size slider (grid only)
    if (ab.viewMode == AssetBrowserState::Grid) {
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##iconSz", &ab.iconSize, 48.f, 128.f, "%.0f");
    }

    ImGui::Separator();

    // ── Breadcrumb navigation ──────────────────────────────────────────────
    {
        // Build breadcrumb from rootPath
        fs::path current(ab.currentDir);
        fs::path root(ab.rootPath);

        // Get relative path from root to current
        std::string relPathStr;
        try {
            fs::path rel = fs::relative(current, root);
            if (rel.empty() || rel.string() == ".") {
                // At root - just show "Assets"
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.8f, 0.8f, 0.5f, 1.f });
                ImGui::TextUnformatted("Assets");
                ImGui::PopStyleColor();
            }
            else {
                // Build clickable breadcrumb
                fs::path cumulative = root;
                std::vector<std::string> parts;
                for (const auto& part : rel) {
                    parts.push_back(part.string());
                }

                // Root button
                ImGui::PushID(0);
                ImGui::PushStyleColor(ImGuiCol_Button, { 0.25f, 0.25f, 0.3f, 0.8f });
                if (ImGui::SmallButton("Assets")) {
                    ab.navStack.push_back(ab.currentDir);
                    ab.currentDir = ab.rootPath;
                    ab.dirDirty = true;
                    ab.selectedGUIDs.clear();
                }
                ImGui::PopStyleColor();
                ImGui::PopID();

                cumulative = ab.rootPath;
                for (size_t i = 0; i < parts.size(); ++i) {
                    cumulative += parts[i];
                    if (!cumulative.empty() && cumulative.string().back() != '/') cumulative += '/';

                    ImGui::SameLine();
                    ImGui::TextDisabled("/");
                    ImGui::SameLine();

                    ImGui::PushID((int)(i + 1));
                    bool isLast = (i == parts.size() - 1);
                    if (isLast) {
                        ImGui::PushStyleColor(ImGuiCol_Text, { 0.8f, 0.8f, 0.5f, 1.f });
                        ImGui::TextUnformatted(parts[i].c_str());
                        ImGui::PopStyleColor();
                    }
                    else {
                        ImGui::PushStyleColor(ImGuiCol_Button, { 0.25f, 0.25f, 0.3f, 0.8f });
                        if (ImGui::SmallButton(parts[i].c_str())) {
                            ab.navStack.push_back(ab.currentDir);
                            ab.currentDir = cumulative.string();
                            ab.dirDirty = true;
                            ab.selectedGUIDs.clear();
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
            }
        }
        catch (...) {
            // Fallback - just show the path as text
            ImGui::TextDisabled("%s", ab.currentDir.c_str());
        }
    }

    ImGui::Separator();

    // ── Main content area (file listing) ───────────────────────────────────
    const ImGuiStyle& style = ImGui::GetStyle();
    float statusBarH = ImGui::GetTextLineHeightWithSpacing()
        + style.ItemSpacing.y + 1.f;
    float contentH = ImGui::GetContentRegionAvail().y - statusBarH;

    ImGui::BeginChild("##ab_files", { 0.f, contentH }, false,
        ImGuiWindowFlags_HorizontalScrollbar);

    // Drop target for entire file area
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            auto* p2 = (AssetDragPayload*)payload->Data;
            ab.pendingDrop = *p2;
            ab.hasPendingDrop = true;
        }
        ImGui::EndDragDropTarget();
    }

    // ── Context menu for empty area (right-click background) ──────────────
    if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_NoOpenOverItems)) {
        ImGui::TextDisabled("Create new...");
        ImGui::Separator();
        if (ImGui::MenuItem("Script (.cpp)")) {
            strncpy_s(ab.wizardScriptName, sizeof(ab.wizardScriptName), "NewScript", sizeof(ab.wizardScriptName) - 1);
            ab.wizardScriptTemplate = 0;
            ab.showScriptWizard = true;
        }
        if (ImGui::MenuItem("Scene (.honscene)")) {
            static int sceneCounter = 1;
            std::string name = "NewScene" + std::to_string(sceneCounter++);
            if (CreateNewScene(ab.currentDir, name)) {
                ab.dirDirty = true;
            }
        }
        if (ImGui::MenuItem("Material (.honmat)")) {
            static int matCounter = 1;
            std::string name = "NewMaterial" + std::to_string(matCounter++);
            if (CreateNewMaterial(ab.currentDir, name)) {
                ab.dirDirty = true;
            }
        }
        // Prefab entry — with cyan text to match the grid icon color
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.3f, 1.f, 0.9f, 1.f });
        if (ImGui::MenuItem("\xef\x86\xb2  Empty Prefab (.honprefab)")) {
            static int prefabCounter = 1;
            std::string name = "NewPrefab" + std::to_string(prefabCounter++);
            if (CreateNewPrefab(ab.currentDir, name)) {
                ab.dirDirty = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f, 0.5f, 0.5f, 1.f });
        if (ImGui::MenuItem("\xef\x81\x9b  Shader (.honshader)")) {
            ab.showShaderWizard = true;
            ab.shaderWizardVert[0] = '\0';
            ab.shaderWizardFrag[0] = '\0';
            strncpy_s(ab.shaderWizardName, sizeof(ab.shaderWizardName), "NewShader", sizeof(ab.shaderWizardName) - 1);
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
        if (ImGui::MenuItem("Folder")) {
            static int folderCounter = 1;
            std::string name = "NewFolder" + std::to_string(folderCounter++);
            if (CreateNewFolder(ab.currentDir, name)) {
                ab.dirDirty = true;
            }
        }
        ImGui::EndPopup();
    }

    // ── Apply filters to dirEntries ──────────────────────────────────────────
    std::string searchLow(ab.searchBuf);
    for (auto& c : searchLow) c = (char)std::tolower((unsigned char)c);

    auto matchesFilter = [&](const fs::directory_entry& e) -> bool {
        if (e.is_directory()) return !ab.showFavoritesOnly;  // dirs shown unless fav-only
        std::string name = e.path().filename().string();
        std::string nameLow = name;
        for (auto& c : nameLow) c = (char)std::tolower((unsigned char)c);
        if (!searchLow.empty() && nameLow.find(searchLow) == std::string::npos) return false;
        AssetType atype = ExtToAssetType(e.path().extension().string());
        if (ab.typeFilter >= 0 && (int)atype != ab.typeFilter) return false;
        if (ab.showFavoritesOnly) {
            auto* rec = db.FindByPath(e.path().string());
            if (!rec || !rec->isFavorite) return false;
        }
        return true;
        };

    // Check if we have entries to display
    if (ab.dirEntries.empty()) {
        ImGui::TextDisabled("  This folder is empty.");
        ImGui::TextDisabled("  Right-click to create new assets.");
    }

    // ── Keyboard shortcuts inside the file area ───────────────────────────────
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        ImGuiIO& io = ImGui::GetIO();
        // Ctrl+A: select all visible non-folder assets
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            ab.selectedGUIDs.clear();
            for (auto& e : ab.dirEntries) {
                if (!matchesFilter(e) || e.is_directory()) continue;
                auto* r = db.FindByPath(e.path().string());
                if (r) ab.selectedGUIDs.push_back(r->guid.ToString());
            }
        }
        // Escape: clear selection
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ab.selectedGUIDs.clear();
    }

    // ── GRID VIEW ────────────────────────────────────────────────────────────
    if (ab.viewMode == AssetBrowserState::Grid) {
        float cellW = ab.iconSize + 8.f;
        float availX = ImGui::GetContentRegionAvail().x;
        int cols = (std::max)(1, (int)(availX / cellW));

        // Build a flat ordered list of visible GUIDs for Shift range-select
        std::vector<std::string> visibleGUIDs;
        for (auto& e : ab.dirEntries) {
            if (!matchesFilter(e)) continue;
            auto* r = e.is_directory() ? nullptr : db.FindByPath(e.path().string());
            visibleGUIDs.push_back(r ? r->guid.ToString() : "");
        }

        int col = 0;
        int entryIdx = -1;
        for (auto& entry : ab.dirEntries) {
            if (!matchesFilter(entry)) continue;
            ++entryIdx;

            bool ctxOpen = false;
            bool dblClick = DrawGridCell(ab, db, entry, ab.iconSize, ctxOpen, onImportAsset);

            EnsureAssetRegistered(db, entry);
            std::string path = entry.path().string();
            AssetRecord* rec = entry.is_directory() ? nullptr : db.FindByPath(path);
            std::string gstr = rec ? rec->guid.ToString() : "";

            // Shift range-select: when DrawGridCell added gstr and lastClickedGUID differs,
            // fill in all GUIDs between the anchor and this item.
            if (!gstr.empty() && !ab.lastClickedGUID.empty() && gstr != ab.lastClickedGUID) {
                ImGuiIO& io = ImGui::GetIO();
                if (io.KeyShift) {
                    // Find anchor and current positions in visible list
                    int anchorIdx = -1, curIdx = -1;
                    for (int i = 0; i < (int)visibleGUIDs.size(); ++i) {
                        if (visibleGUIDs[i] == ab.lastClickedGUID) anchorIdx = i;
                        if (visibleGUIDs[i] == gstr)               curIdx = i;
                    }
                    if (anchorIdx >= 0 && curIdx >= 0) {
                        int lo = (std::min)(anchorIdx, curIdx);
                        int hi = (std::max)(anchorIdx, curIdx);
                        for (int i = lo; i <= hi; ++i) {
                            if (!visibleGUIDs[i].empty() &&
                                std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(),
                                    visibleGUIDs[i]) == ab.selectedGUIDs.end())
                                ab.selectedGUIDs.push_back(visibleGUIDs[i]);
                        }
                    }
                }
            }

            if (dblClick) {
                if (entry.is_directory()) {
                    // Navigate into folder
                    std::string newDir = entry.path().string();
                    // Ensure trailing slash
                    if (!newDir.empty() && newDir.back() != '/' && newDir.back() != '\\')
                        newDir += '/';
                    ab.navStack.push_back(ab.currentDir);
                    ab.currentDir = newDir;
                    ab.dirDirty = true;
                    ab.selectedGUIDs.clear();
                    break;
                }
                else {
                    AssetType t = ExtToAssetType(entry.path().extension().string());
                    if (t == AssetType::Scene && onOpenScene) {
                        onOpenScene(path);
                    }
                    else if (onImportAsset) {
                        onImportAsset(path, t);
                    }
                }
            }

            ++col;
            if (col < cols) ImGui::SameLine(0.f, 4.f);
            else col = 0;
        }
    }
    // ── LIST VIEW ────────────────────────────────────────────────────────────
    else {
        if (ImGui::BeginTable("##ab_list", 4,
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.f);
            ImGui::TableSetupColumn("Fav", ImGuiTableColumnFlags_WidthFixed, 30.f);
            ImGui::TableHeadersRow();

            for (auto& entry : ab.dirEntries) {
                if (!matchesFilter(entry)) continue;
                std::string name = entry.path().filename().string();
                bool isDir = entry.is_directory();
                AssetType atype = isDir ? AssetType::Unknown
                    : ExtToAssetType(entry.path().extension().string());

                EnsureAssetRegistered(db, entry);
                std::string path = entry.path().string();
                AssetRecord* rec = isDir ? nullptr : db.FindByPath(path);
                std::string gstr = rec ? rec->guid.ToString() : "";

                bool selected = !gstr.empty() &&
                    std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr)
                    != ab.selectedGUIDs.end();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                ImGui::PushID(name.c_str());
                // Choose icon: folder, scene, prefab, or default type icon
                const char* iconChar = " ";
                if (isDir) iconChar = ICON_FA_FOLDER;
                else if (atype == AssetType::Scene)  iconChar = ICON_FA_TREE;
                else if (atype == AssetType::Prefab) iconChar = ICON_FA_CUBE;  // fa-cubes
                else iconChar = AssetTypeIcon(atype);
                std::string label = std::string(iconChar) + " " + name;
                if (ImGui::Selectable(label.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    ImGuiIO& io = ImGui::GetIO();
                    if (!io.KeyCtrl) ab.selectedGUIDs.clear();
                    if (!gstr.empty()) {
                        auto it = std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr);
                        if (it == ab.selectedGUIDs.end()) ab.selectedGUIDs.push_back(gstr);
                        else if (io.KeyCtrl) ab.selectedGUIDs.erase(it);
                        ab.focusedGUID = gstr;
                    }
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        if (isDir) {
                            // Navigate into folder
                            std::string newDir = path;
                            if (!newDir.empty() && newDir.back() != '/' && newDir.back() != '\\')
                                newDir += '/';
                            ab.navStack.push_back(ab.currentDir);
                            ab.currentDir = newDir;
                            ab.dirDirty = true;
                            ab.selectedGUIDs.clear();
                        }
                        else if (atype == AssetType::Scene && onOpenScene) {
                            onOpenScene(path);
                        }
                        else if (onImportAsset) {
                            onImportAsset(path, atype);
                        }
                    }
                }

                // Drag source — must come immediately after the Selectable so ImGui
                // still has the correct last-item ID.  Moving it below the context-menu
                // block caused the source to never fire.
                if (!isDir && !gstr.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    AssetDragPayload payload;
                    strncpy_s(payload.guidStr, sizeof(payload.guidStr), gstr.c_str(), sizeof(payload.guidStr) - 1);
                    strncpy_s(payload.path, sizeof(payload.path), path.c_str(), sizeof(payload.path) - 1);
                    payload.type = atype;
                    ImGui::SetDragDropPayload(kAssetDragPayload, &payload, sizeof(payload));
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
                }

                bool ctxOpen2 = ImGui::BeginPopupContextItem("##lctx");
                if (ctxOpen2) {
                    AssetRecord* recCtx = gstr.empty() ? nullptr : db.FindByGUID(gstr);
                    bool isDirCtx = entry.is_directory();

                    ImGui::TextDisabled("%s", entry.path().filename().string().c_str());
                    ImGui::Separator();

                    // Prefab-first action
                    bool isPrefabCtx = !isDirCtx &&
                        ExtToAssetType(entry.path().extension().string()) == AssetType::Prefab;
                    if (isPrefabCtx) {
                        ImGui::PushStyleColor(ImGuiCol_Text, { 0.3f, 1.f, 0.9f, 1.f });
                        if (ImGui::MenuItem("\xef\x86\xb2  Edit Prefab")) {
                            if (onImportAsset)
                                onImportAsset(path, AssetType::Prefab);  // triggers EnterPrefabEditMode
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::MenuItem("Instantiate in Scene")) {
                            // Instantiate (shift+dbl-click alternative) — uses the import callback
                            // with a sentinel to skip edit mode; handled by main.cpp's lambda
                            // We can't easily distinguish here, so just call onImportAsset and
                            // let the user drag-drop to instantiate without entering edit mode.
                            if (onImportAsset)
                                onImportAsset(path + "?instantiate", AssetType::Prefab);
                        }
                        ImGui::Separator();
                    }

                    if (!isDirCtx) {
                        if (ImGui::MenuItem("Open")) {
                            OpenWithDefaultProgram(path);
                        }

                        if (recCtx && ImGui::MenuItem("Reimport")) recCtx->needsReimport = true;
                        ImGui::Separator();

                        if (ImGui::MenuItem("Copy Path")) {
                            ImGui::SetClipboardText(path.c_str());

                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Show in Explorer")) {
                            std::string cmd = "explorer.exe /select,\"" + path + "\"";
                            system(cmd.c_str());
                        }

                    }
                    if (recCtx) {
                        if (ImGui::MenuItem(recCtx->isFavorite ? "Remove from Favorites" : "Add to Favorites"))
                            recCtx->isFavorite = !recCtx->isFavorite;
                    }
                    if (!isDirCtx && recCtx) {
                        if (ImGui::MenuItem("Rename...")) {
                            ab.showRenameModal = true;
                            strncpy_s(ab.renameTargetGUID, sizeof(ab.renameTargetGUID), gstr.c_str(), sizeof(ab.renameTargetGUID) - 1);
                            strncpy_s(ab.renameTargetPath, sizeof(ab.renameTargetPath), path.c_str(), sizeof(ab.renameTargetPath) - 1);
                            // Pre-fill with full filename so user can change extension too
                            strncpy_s(ab.renameNewName, sizeof(ab.renameNewName), entry.path().filename().string().c_str(), sizeof(ab.renameNewName) - 1);
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Duplicate")) {
                            std::string newGuid = db.Duplicate(gstr);
                            if (!newGuid.empty()) ab.dirDirty = true;
                        }
                    }
                    // Folder rename
                    if (isDirCtx) {
                        if (ImGui::MenuItem("Rename Folder...")) {
                            ab.showFolderRenameModal = true;
                            strncpy_s(ab.folderRenameOldPath, sizeof(ab.folderRenameOldPath),
                                entry.path().string().c_str(), sizeof(ab.folderRenameOldPath) - 1);
                            strncpy_s(ab.folderRenameNewName, sizeof(ab.folderRenameNewName),
                                entry.path().filename().string().c_str(), sizeof(ab.folderRenameNewName) - 1);
                        }
                    }
                    // Shader creation from .vert / .frag
                    if (!isDirCtx) {
                        AssetType et = ExtToAssetType(entry.path().extension().string());
                        if (et == AssetType::ShaderSource) {
                            ImGui::Separator();
                            ImGui::PushStyleColor(ImGuiCol_Text, { 0.9f, 0.5f, 0.5f, 1.f });
                            if (ImGui::MenuItem("\xef\x81\x9b  Create Shader from Sources...")) {
                                ab.showShaderWizard = true;
                                FillShaderWizardFromSelection(ab, db, path);
                            }
                            ImGui::PopStyleColor();
                        }
                    }
                    ImGui::Separator();
                    if (!isDirCtx && recCtx) {
                        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.4f, 0.4f, 1.f });
                        if (ImGui::MenuItem("Delete")) {
                            db.BulkDelete({ gstr });
                            ab.dirDirty = true;
                            ab.selectedGUIDs.erase(
                                std::remove(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr),
                                ab.selectedGUIDs.end());
                        }
                        ImGui::PopStyleColor();
                    }
                    // Folder delete
                    if (isDirCtx) {
                        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.4f, 0.4f, 1.f });
                        if (ImGui::MenuItem("Delete Folder")) {
                            try { fs::remove_all(entry.path()); }
                            catch (...) {}
                            ab.dirDirty = true;
                        }
                        ImGui::PopStyleColor();
                    }
                    if (ab.selectedGUIDs.size() > 1) {
                        ImGui::Separator();
                        ImGui::TextDisabled("%zu selected", ab.selectedGUIDs.size());
                        if (ImGui::MenuItem("Delete all selected")) {
                            db.BulkDelete(ab.selectedGUIDs);
                            ab.selectedGUIDs.clear();
                            ab.dirDirty = true;
                        }
                        if (ImGui::MenuItem("Duplicate all selected")) {
                            for (auto& g : ab.selectedGUIDs) db.Duplicate(g);
                            ab.dirDirty = true;
                        }
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                if (isDir) ImGui::TextDisabled("Folder");
                else if (atype == AssetType::Scene) ImGui::TextDisabled("Scene");
                else ImGui::TextDisabled("%s", AssetTypeName(atype));
                ImGui::TableSetColumnIndex(2);
                if (rec) ImGui::TextDisabled("%s", FormatBytes(rec->fileSize).c_str());
                ImGui::TableSetColumnIndex(3);
                if (rec && rec->isFavorite)
                    ImGui::TextColored({ 1.f,0.85f,0.2f,1.f }, "\xef\x80\x85");

                // Sub-objects
                if (rec && !rec->subObjects.empty()) {
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::TreeNode(("  ##sub_" + gstr).c_str())) {
                        for (auto& so : rec->subObjects) {
                            ImGui::PushID(&so);
                            ImGui::TreeNodeEx(so.name.c_str(),
                                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%s)", AssetTypeName(so.type));
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();  // ab_files

    // ── Status bar ───────────────────────────────────────────────────────────
    ImGui::Separator();
    {
        int64_t selSize = 0;
        for (auto& g : ab.selectedGUIDs) {
            auto* r = db.FindByGUID(g);
            if (r) selSize += r->fileSize;
        }
        if (!ab.selectedGUIDs.empty())
            ImGui::TextDisabled("  %zu selected  (%s)  |  %zu items in directory",
                ab.selectedGUIDs.size(), FormatBytes(selSize).c_str(),
                ab.dirEntries.size());
        else
            ImGui::TextDisabled("  %zu items  |  %zu assets in database",
                ab.dirEntries.size(), db.records.size());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawAssetBrowserPanel  — Main panel to be called from IDE
//  Parameters:
//    ab   — browser UI state (persistent across frames, rootPath must be set)
//    db   — asset database
//    dt   — frame delta time (for rescan timer)
//    onImportAsset — callback(path, type) when user drops / double-clicks an asset
//    onOpenScene   — callback(scenePath) when user double-clicks a .honscene
// ─────────────────────────────────────────────────────────────────────────────

inline void DrawAssetBrowserPanel(
    AssetBrowserState& ab,
    AssetDatabase& db,
    float dt,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset,
    const std::function<void(const std::string& scenePath)>& onOpenScene = nullptr)
{
    EnsureCheckerTex();

    // CRITICAL: rootPath must be set by the IDE (via SetRootPath) before first draw.
    if (ab.rootPath.empty()) {
        IM_ASSERT(false && "AssetBrowserState::rootPath not set! Call SetRootPath() with project asset folder.");
        return;
    }

    // Periodic rescan: update file listing from currentDir, and refresh the asset
    // database from the entire rootPath (so new/modified assets are discovered).
    ab.rescanTimer += dt;
    if (ab.dirDirty || ab.rescanTimer > 3.f) {
        // Clamp currentDir to root before scanning
        ab.currentDir = ClampToRoot(ab.currentDir, ab.rootPath);

        // Ensure trailing slash for consistent path handling
        if (!ab.currentDir.empty() && ab.currentDir.back() != '/' && ab.currentDir.back() != '\\')
            ab.currentDir += '/';

        // Refresh directory listing (files/folders in currentDir)
        ab.dirEntries.clear();
        try {
            if (fs::exists(ab.currentDir) && fs::is_directory(ab.currentDir)) {
                for (auto& e : fs::directory_iterator(ab.currentDir)) {
                    std::string fname = e.path().filename().string();
                    // Skip hidden files/folders (those starting with .)
                    if (!fname.empty() && fname[0] == '.') continue;
                    ab.dirEntries.push_back(e);
                }
                // Sort: directories first, then files alphabetically
                std::sort(ab.dirEntries.begin(), ab.dirEntries.end(),
                    [](const fs::directory_entry& a, const fs::directory_entry& b) {
                        if (a.is_directory() != b.is_directory())
                            return a.is_directory() > b.is_directory();
                        return a.path().filename().string() < b.path().filename().string();
                    });
            }
            else {
                // Current directory doesn't exist - reset to root
                ab.currentDir = ab.rootPath;
                if (!ab.currentDir.empty() && ab.currentDir.back() != '/' && ab.currentDir.back() != '\\')
                    ab.currentDir += '/';
                ab.navStack.clear();
                // Rescan after reset
                for (auto& e : fs::directory_iterator(ab.currentDir)) {
                    std::string fname = e.path().filename().string();
                    if (!fname.empty() && fname[0] == '.') continue;
                    ab.dirEntries.push_back(e);
                }
                std::sort(ab.dirEntries.begin(), ab.dirEntries.end(),
                    [](const fs::directory_entry& a, const fs::directory_entry& b) {
                        if (a.is_directory() != b.is_directory())
                            return a.is_directory() > b.is_directory();
                        return a.path().filename().string() < b.path().filename().string();
                    });
            }
        }
        catch (const std::exception& e) {
            // Log error but continue
            (void)e;
            ab.dirEntries.clear();
        }

        // Scan the ENTIRE asset root into the database – not just the current folder.
        // This ensures all assets in the project are known and can be referenced.
        if (fs::exists(ab.rootPath)) {
            db.ScanDirectory(ab.rootPath);
        }

        ab.dirDirty = false;
        ab.rescanTimer = 0.f;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.07f, 0.075f, 0.085f, 1.f });
    ImGui::BeginChild("##ab_outer", { 0, 0 }, false);

    // ── Tabs ────────────────────────────────────────────────────────────────
    ImGui::BeginTabBar("AssetBrowserTabs", ImGuiTabBarFlags_None);

    // --- Tab: All Assets ---
    if (ImGui::BeginTabItem("All Assets")) {
        bool prevFav = ab.showFavoritesOnly;
        ab.showFavoritesOnly = false;
        DrawAssetBrowserContent(ab, db, dt, onImportAsset, onOpenScene);
        ab.showFavoritesOnly = prevFav;
        ImGui::EndTabItem();
    }

    // --- Tab: Favorites ---
    if (ImGui::BeginTabItem("Favorites")) {
        bool prevFav = ab.showFavoritesOnly;
        ab.showFavoritesOnly = true;
        DrawAssetBrowserContent(ab, db, dt, onImportAsset, onOpenScene);
        ab.showFavoritesOnly = prevFav;
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    ImGui::EndChild();  // ab_outer
    ImGui::PopStyleColor();

    // ── Rename modal ─────────────────────────────────────────────────────────
    if (ab.showRenameModal) { ImGui::OpenPopup("Rename Asset"); ab.showRenameModal = false; }
    if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("New filename (with extension):");
        ImGui::InputText("##renameinput", ab.renameNewName, sizeof(ab.renameNewName),
            ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("Rename", { 120, 0 })) {
            if (ab.renameNewName[0] != '\0') {
                auto* rec = db.FindByGUID(std::string(ab.renameTargetGUID));

                // Determine the old path: prefer DB record, fall back to stored path
                fs::path oldPath;
                if (rec)
                    oldPath = fs::path(rec->path);
                else if (ab.renameTargetPath[0] != '\0')
                    oldPath = fs::path(ab.renameTargetPath);

                if (!oldPath.empty() && fs::exists(oldPath)) {
                    fs::path newPath = oldPath.parent_path() / ab.renameNewName;
                    bool renamed = false;
                    try {
                        fs::rename(oldPath, newPath);
                        renamed = true;
                    }
                    catch (...) {}
                    if (renamed) {
                        if (rec) {
                            db.pathToGUID.erase(rec->path);
                            rec->path = newPath.string();
                            rec->displayName = newPath.stem().string();
                            rec->type = ExtToAssetType(newPath.extension().string());
                            db.pathToGUID[rec->path] = std::string(ab.renameTargetGUID);
                        }
                        ab.dirDirty = true;
                    }
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 90, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // ── Script Wizard modal ──────────────────────────────────────────────────────
    if (ab.showScriptWizard) { ImGui::OpenPopup("Create Script##abwizard"); ab.showScriptWizard = false; }
    if (ImGui::BeginPopupModal("Create Script##abwizard", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static const char* kTemplates[] = { "Empty", "Start/Update", "Full Example" };
        ImGui::InputText("Script Name", ab.wizardScriptName, sizeof(ab.wizardScriptName),
            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Combo("Template", &ab.wizardScriptTemplate, kTemplates, 3);
        ImGui::TextDisabled("Language: C++ (.cpp)");
        ImGui::Separator();

        if (ImGui::Button("Create", { 120, 0 }))
        {
            if (CreateNewScript(ab.currentDir, ab.wizardScriptName, ab.wizardScriptTemplate)) {
                db.Register((fs::path(ab.currentDir) / (std::string(ab.wizardScriptName) + ".cpp")).string());
                ab.dirDirty = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Shader Wizard modal ──────────────────────────────────────────────────────
    if (ab.showShaderWizard) { ImGui::OpenPopup("Create Shader##abshader"); ab.showShaderWizard = false; }
    if (ImGui::BeginPopupModal("Create Shader##abshader", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("Creates a .honshader asset linking a vertex and fragment source.");
        ImGui::Separator();

        ImGui::SetNextItemWidth(280.f);
        ImGui::InputText("Shader Name##sh", ab.shaderWizardName, sizeof(ab.shaderWizardName),
            ImGuiInputTextFlags_AutoSelectAll);

        ImGui::Spacing();

        // Vertex row — icon + path + clear button
        bool hasVert = ab.shaderWizardVert[0] != '\0';
        ImGui::PushStyleColor(ImGuiCol_Text, hasVert
            ? ImVec4{ 0.4f, 0.9f, 0.4f, 1.f }   // green tick
        : ImVec4{ 0.9f, 0.5f, 0.2f, 1.f });  // orange warning
        ImGui::TextUnformatted(hasVert ? "\xef\x84\x9e" : "\xef\x81\xb1");  // check / warning
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("Vertex (.vert)##shv", ab.shaderWizardVert, sizeof(ab.shaderWizardVert));
        if (hasVert) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##clv")) ab.shaderWizardVert[0] = '\0';
        }

        // Fragment row
        bool hasFrag = ab.shaderWizardFrag[0] != '\0';
        ImGui::PushStyleColor(ImGuiCol_Text, hasFrag
            ? ImVec4{ 0.4f, 0.9f, 0.4f, 1.f }
        : ImVec4{ 0.9f, 0.5f, 0.2f, 1.f });
        ImGui::TextUnformatted(hasFrag ? "\xef\x84\x9e" : "\xef\x81\xb1");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(360.f);
        ImGui::InputText("Fragment (.frag)##shf", ab.shaderWizardFrag, sizeof(ab.shaderWizardFrag));
        if (hasFrag) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x##clf")) ab.shaderWizardFrag[0] = '\0';
        }

        // Re-fill hint when slots are missing
        if (!hasVert || !hasFrag) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.6f, 0.65f, 0.7f, 1.f });
            ImGui::TextUnformatted("  Tip: Ctrl+click both .vert and .frag in the browser,");
            ImGui::TextUnformatted("  then right-click \xe2\x86\x92 Create Shader from Sources to auto-fill.");
            ImGui::PopStyleColor();
        }

        ImGui::Separator();

        bool canCreate = ab.shaderWizardName[0] != '\0' && hasVert && hasFrag;
        if (!canCreate) ImGui::BeginDisabled();
        if (ImGui::Button("Create Shader", { 140, 0 })) {
            if (CreateShaderAsset(ab.currentDir, ab.shaderWizardName,
                ab.shaderWizardVert, ab.shaderWizardFrag)) {
                fs::path p = fs::path(ab.currentDir) / (std::string(ab.shaderWizardName) + ".honshader");
                db.Register(p.string());
                ab.dirDirty = true;
            }
            ImGui::CloseCurrentPopup();
        }
        if (!canCreate) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel##sh", { 80, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Folder Rename modal ──────────────────────────────────────────────────────
    if (ab.showFolderRenameModal) { ImGui::OpenPopup("Rename Folder##abfr"); ab.showFolderRenameModal = false; }
    if (ImGui::BeginPopupModal("Rename Folder##abfr", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextDisabled("New folder name:");
        ImGui::InputText("##frname", ab.folderRenameNewName, sizeof(ab.folderRenameNewName),
            ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("Rename##fr", { 120, 0 })) {
            if (ab.folderRenameNewName[0] != '\0') {
                fs::path oldP(ab.folderRenameOldPath);
                fs::path newP = oldP.parent_path() / ab.folderRenameNewName;
                try { fs::rename(oldP, newP); }
                catch (...) {}
                // If we navigated into that folder, update currentDir
                if (std::string(ab.folderRenameOldPath) == ab.currentDir ||
                    ab.currentDir.find(ab.folderRenameOldPath) == 0) {
                    ab.currentDir = newP.string();
                    if (!ab.currentDir.empty() && ab.currentDir.back() != '/')
                        ab.currentDir += '/';
                }
                ab.dirDirty = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##fr", { 80, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Viewport drag-drop target helper
//  Call this inside the Viewport window after rendering the scene image.
//  Returns true (and fills payload) if an asset was dropped.
// ─────────────────────────────────────────────────────────────────────────────
inline bool HandleViewportAssetDrop(AssetDragPayload& outPayload) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p =
            ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            outPayload = *(AssetDragPayload*)p->Data;
            ImGui::EndDragDropTarget();
            return true;
        }
        ImGui::EndDragDropTarget();
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inspector drag-drop target helper
//  Returns true if an asset was dropped onto an inspector slot.
// ─────────────────────────────────────────────────────────────────────────────
inline bool DrawInspectorAssetSlot(const char* label, AssetType acceptType,
    std::string& currentPath, AssetDatabase& db)
{
    ImGui::PushID(label);
    AssetRecord* cur = currentPath.empty() ? nullptr : db.FindByPath(currentPath);
    std::string display = cur ? cur->displayName : "(none)";

    ImGui::TextUnformatted(label);
    ImGui::SameLine(100.f);
    ImGui::Button(display.c_str(), { -1, 0 });

    bool changed = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            auto* payload = (AssetDragPayload*)p->Data;
            if (acceptType == AssetType::Unknown || payload->type == acceptType) {
                currentPath = payload->path;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return changed;
}