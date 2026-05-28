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

    // Rename modal
    bool  showRenameModal = false;
    char  renameTargetGUID[40] = {};
    char  renameNewName[128] = {};

    // Script wizard modal
    bool  showScriptWizard = false;
    char  wizardScriptName[128] = "NewScript";
    int   wizardScriptTemplate = 0;   // 0=Empty, 1=Start/Update, 2=Full Example

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

// Creates an empty .honprefab stub that can be filled in later
static bool CreateNewPrefab(const std::string& dir, const std::string& name) {
    fs::path filepath = fs::path(dir) / (name + ".honprefab");
    if (fs::exists(filepath)) return false;
    std::ofstream f(filepath);
    if (!f) return false;
    f << "{\n"
        << "  \"name\": \"" << name << "\",\n"
        << "  \"objects\": []\n"
        << "}\n";
    f.close();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Context menu drawn per-item or globally on empty space
// ─────────────────────────────────────────────────────────────────────────────
static void DrawAssetContextMenu(AssetBrowserState& ab, AssetDatabase& db,
    const std::string& guidStr,
    const fs::directory_entry* entry,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset = nullptr)
{
    // NOTE: caller must have already opened the popup via BeginPopupContextItem.
    // This function only draws the menu body and calls EndPopup.

    AssetRecord* rec = guidStr.empty() ? nullptr : db.FindByGUID(guidStr);
    if (entry) {
        // Show prefab-specific header
        bool isPrefab = !entry->is_directory() &&
            ExtToAssetType(entry->path().extension().string()) == AssetType::Prefab;
        if (isPrefab) {
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.2f, 0.9f, 0.85f, 1.f });
            ImGui::TextUnformatted("\xef\x86\xb2  ");  // cubes icon
            ImGui::SameLine();
            ImGui::TextUnformatted(entry->path().filename().string().c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("  Prefab");
        }
        else {
            ImGui::TextDisabled("%s", entry->path().filename().string().c_str());
        }
        ImGui::Separator();
    }

    bool isDir = entry && entry->is_directory();
    bool isPrefabEntry = entry && !isDir &&
        ExtToAssetType(entry->path().extension().string()) == AssetType::Prefab;

    if (!isDir) {
        // "Edit Prefab" + "Instantiate in Scene" for prefabs — surfaces at the top
        if (isPrefabEntry) {
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.3f, 1.f, 0.9f, 1.f });
            if (ImGui::MenuItem("\xef\x86\xb2  Edit Prefab")) {
                if (entry && onImportAsset)
                    onImportAsset(entry->path().string(), AssetType::Prefab);  // triggers EnterPrefabEditMode
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
        if (ImGui::MenuItem("Rename...")) {
            ab.showRenameModal = true;
            strncpy_s(ab.renameTargetGUID, sizeof(ab.renameTargetGUID), guidStr.c_str(), sizeof(ab.renameTargetGUID) - 1);
            strncpy_s(ab.renameNewName, sizeof(ab.renameNewName), rec->displayName.c_str(), sizeof(ab.renameNewName) - 1);
        }
        if (ImGui::MenuItem("Duplicate")) {
            std::string newGuid = db.Duplicate(guidStr);
            if (!newGuid.empty()) ab.dirDirty = true;
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, { 1.f, 0.4f, 0.4f, 1.f });
        if (ImGui::MenuItem("Delete")) {
            db.BulkDelete({ guidStr });
            ab.dirDirty = true;
            ab.selectedGUIDs.erase(
                std::remove(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), guidStr),
                ab.selectedGUIDs.end());
        }
        ImGui::PopStyleColor();
    }

    // Multi-selection bulk ops
    if (ab.selectedGUIDs.size() > 1) {
        ImGui::Separator();
        ImGui::TextDisabled("%zu selected", ab.selectedGUIDs.size());
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
    AssetRecord* rec = isDir ? nullptr : db.FindByPath(path);
    std::string gstr = (rec) ? rec->guid.ToString() : "";

    bool selected = !gstr.empty() &&
        std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr)
        != ab.selectedGUIDs.end();

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
        if (rec && !rec->thumbnailTex) {
            // Draw type icon text centred
            const char* iconChar = AssetTypeIcon(atype);
            // Override scene icon to a more distinctive one
            if (atype == AssetType::Scene) iconChar = "\xef\x86\xbb";  // fa-tree (scene)
            dl->AddText(ImGui::GetFont(), 32.f,
                { cellStart.x + (cellSize - 20.f) * 0.5f, cellStart.y + 18.f },
                IM_COL32(200, 210, 230, 200), iconChar);
        }
        else {
            dl->AddImage((ImTextureID)(uintptr_t)thumbTex,
                { iconX, iconY }, { iconX + 48.f, iconY + 48.f });
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
        if (!io.KeyCtrl && !io.KeyShift) ab.selectedGUIDs.clear();
        if (!gstr.empty()) {
            auto it = std::find(ab.selectedGUIDs.begin(), ab.selectedGUIDs.end(), gstr);
            if (it == ab.selectedGUIDs.end())
                ab.selectedGUIDs.push_back(gstr);
            else if (io.KeyCtrl)
                ab.selectedGUIDs.erase(it);
            ab.focusedGUID = gstr;
        }
    }

    // Drag source (only for non-folders)
    if (!isDir && !gstr.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        AssetDragPayload payload;
        strncpy_s(payload.guidStr, sizeof(payload.guidStr), gstr.c_str(), sizeof(payload.guidStr) - 1);
        strncpy_s(payload.path, sizeof(payload.path), path.c_str(), sizeof(payload.path) - 1);
        payload.type = atype;
        ImGui::SetDragDropPayload(kAssetDragPayload, &payload, sizeof(payload));
        ImGui::TextUnformatted(AssetTypeIcon(atype));
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
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
    if (ImGui::Button("\xef\x81\x87")) {  // fa-arrows (back)
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
    if (ImGui::Button("\xef\x81\x9e")) {  // fa-sync
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
        // Home button
        if (ImGui::Button("\xef\x80\x95", { 28, 24 })) {  // fa-home
            ab.navStack.push_back(ab.currentDir);
            ab.currentDir = ab.rootPath;
            ab.dirDirty = true;
            ab.selectedGUIDs.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to project asset folder");

        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();

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

    // ── GRID VIEW ────────────────────────────────────────────────────────────
    if (ab.viewMode == AssetBrowserState::Grid) {
        float cellW = ab.iconSize + 8.f;
        float availX = ImGui::GetContentRegionAvail().x;
        int cols = (std::max)(1, (int)(availX / cellW));

        int col = 0;
        for (auto& entry : ab.dirEntries) {
            if (!matchesFilter(entry)) continue;

            bool ctxOpen = false;
            bool dblClick = DrawGridCell(ab, db, entry, ab.iconSize, ctxOpen, onImportAsset);

            std::string path = entry.path().string();
            AssetRecord* rec = entry.is_directory() ? nullptr : db.FindByPath(path);
            std::string gstr = rec ? rec->guid.ToString() : "";

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
                if (isDir) iconChar = "\xef\x81\xbb ";
                else if (atype == AssetType::Scene)  iconChar = "\xef\x86\xbb ";
                else if (atype == AssetType::Prefab) iconChar = "\xef\x86\xb2 ";  // fa-cubes
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
                    }
                    if (recCtx) {
                        if (ImGui::MenuItem(recCtx->isFavorite ? "Remove from Favorites" : "Add to Favorites"))
                            recCtx->isFavorite = !recCtx->isFavorite;
                    }
                    if (!isDirCtx && recCtx) {
                        if (ImGui::MenuItem("Rename...")) {
                            ab.showRenameModal = true;
                            strncpy_s(ab.renameTargetGUID, sizeof(ab.renameTargetGUID), gstr.c_str(), sizeof(ab.renameTargetGUID) - 1);
                            strncpy_s(ab.renameNewName, sizeof(ab.renameNewName), recCtx->displayName.c_str(), sizeof(ab.renameNewName) - 1);
                        }
                        if (ImGui::MenuItem("Duplicate")) {
                            std::string newGuid = db.Duplicate(gstr);
                            if (!newGuid.empty()) ab.dirDirty = true;
                        }
                        ImGui::Separator();
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

                // Drag source (non-folder only)
                if (!isDir && !gstr.empty() && ImGui::BeginDragDropSource()) {
                    AssetDragPayload payload;
                    strncpy_s(payload.guidStr, sizeof(payload.guidStr), gstr.c_str(), sizeof(payload.guidStr) - 1);
                    strncpy_s(payload.path, sizeof(payload.path), path.c_str(), sizeof(payload.path) - 1);
                    payload.type = atype;
                    ImGui::SetDragDropPayload(kAssetDragPayload, &payload, sizeof(payload));
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::EndDragDropSource();
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
        ImGui::InputText("New display name", ab.renameNewName, sizeof(ab.renameNewName),
            ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("Rename", { 120, 0 })) {
            auto* rec = db.FindByGUID(std::string(ab.renameTargetGUID));
            if (rec) rec->displayName = ab.renameNewName;
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