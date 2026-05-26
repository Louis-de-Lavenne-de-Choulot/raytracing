#pragma once
// ide_asset_browser.h  —  HonHon Engine IDE  —  Asset Browser Panel
// =============================================================================
// Covers:
//   - List / Grid view toggle
//   - Search (name, type filter), favorites filter
//   - Breadcrumb navigation + back button
//   - Thumbnail display (checkerboard fallback, type-icon overlay)
//   - Drag & drop payload from browser → scene (auto-imports as command)
//   - Context menu: open, rename, duplicate, delete, show in explorer
//   - Sub-objects expandable under compound assets
//   - Import settings inline in browser inspector strip
//   - Status bar: selected count, total size
// =============================================================================

#include "ide_asset_database.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <glad/glad.h>
#include <filesystem>

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
//  AssetBrowserState  — per-panel UI state
// ─────────────────────────────────────────────────────────────────────────────
struct AssetBrowserState {
    // Navigation
    std::string currentDir = "./assets/";
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
};

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: get background color tint per type
// ─────────────────────────────────────────────────────────────────────────────
static ImVec4 AssetTypeTint(AssetType t) {
    switch (t) {
    case AssetType::Texture:      return { 0.3f, 0.7f, 1.0f, 0.25f };
    case AssetType::Model:        return { 0.6f, 0.9f, 0.5f, 0.25f };
    case AssetType::Audio:        return { 0.9f, 0.6f, 0.3f, 0.25f };
    case AssetType::Script:       return { 0.8f, 0.5f, 0.9f, 0.25f };
    case AssetType::Scene:        return { 1.0f, 0.85f, 0.2f, 0.25f };
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
//  Rescan the current directory
// ─────────────────────────────────────────────────────────────────────────────
inline void AssetBrowserRescan(AssetBrowserState& ab, AssetDatabase& db) {
    ab.dirEntries.clear();
    try {
        for (auto& e : fs::directory_iterator(ab.currentDir))
            ab.dirEntries.push_back(e);
        std::sort(ab.dirEntries.begin(), ab.dirEntries.end(),
            [](const fs::directory_entry& a, const fs::directory_entry& b) {
                if (a.is_directory() != b.is_directory())
                    return a.is_directory() > b.is_directory();
                return a.path().filename() < b.path().filename();
            });
    }
    catch (...) {}
    // Register any new files in the database
    db.ScanDirectory(ab.currentDir);
    ab.dirDirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Context menu drawn per-item or globally on empty space
// ─────────────────────────────────────────────────────────────────────────────
static void DrawAssetContextMenu(AssetBrowserState& ab, AssetDatabase& db,
    const std::string& guidStr,
    const fs::directory_entry* entry)
{
    // NOTE: caller must have already opened the popup via BeginPopupContextItem.
    // This function only draws the menu body and calls EndPopup.

    AssetRecord* rec = guidStr.empty() ? nullptr : db.FindByGUID(guidStr);
    if (entry) {
        ImGui::TextDisabled("%s", entry->path().filename().string().c_str());
        ImGui::Separator();
    }

    bool isDir = entry && entry->is_directory();

    if (!isDir) {
        if (ImGui::MenuItem("Open")) { /* placeholder: system open */ }
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
// ─────────────────────────────────────────────────────────────────────────────
static bool DrawGridCell(AssetBrowserState& ab, AssetDatabase& db,
    const fs::directory_entry& entry,
    float cellSize, bool& ctxOpen)
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
        : (isDir ? ImVec4{ 0.3f,0.3f,0.1f,0.15f }
    : AssetTypeTint(atype));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cellStart,
        { cellStart.x + cellSize, cellStart.y + cellSize + 20.f },
        ImGui::ColorConvertFloat4ToU32(bg), 6.f);
    if (selected)
        dl->AddRect(cellStart,
            { cellStart.x + cellSize, cellStart.y + cellSize + 20.f },
            IM_COL32(80, 140, 220, 200), 6.f, 0, 2.f);

    // Invisible button for interaction
    ImGui::PushID(name.c_str());
    ImGui::InvisibleButton("##cell", { cellSize, cellSize + 20.f });
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    dblClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (ImGui::BeginPopupContextItem("##assetctx")) {
        ctxOpen = true;
        DrawAssetContextMenu(ab, db, gstr, &entry);   // calls EndPopup() internally
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
    GLuint thumbTex = (rec && rec->thumbnailTex) ? rec->thumbnailTex : g_checkerTex;
    if (rec && !rec->thumbnailTex) {
        // Draw type icon text centred
        dl->AddText(ImGui::GetFont(), 32.f,
            { cellStart.x + (cellSize - 20.f) * 0.5f, cellStart.y + 18.f },
            IM_COL32(200, 210, 230, 200),
            isDir ? "\xef\x81\xbb" : AssetTypeIcon(atype));
    }
    else {
        dl->AddImage((ImTextureID)(uintptr_t)thumbTex,
            { iconX, iconY }, { iconX + 48.f, iconY + 48.f });
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

    // Drag source
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
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset)
{
    // ── Toolbar ─────────────────────────────────────────────────────────────
    // Navigation: back
    bool canGoBack = !ab.navStack.empty();
    if (!canGoBack) ImGui::BeginDisabled();
    if (ImGui::Button("\xef\x81\x87")) {  // fa-arrows (back)
        ab.currentDir = ab.navStack.back();
        ab.navStack.pop_back();
        ab.dirDirty = true;
        ab.selectedGUIDs.clear();
    }
    if (!canGoBack) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go back");

    ImGui::SameLine();

    // Path input
    ImGui::SetNextItemWidth(240.f);
    char pathBuf[512]; strncpy_s(pathBuf, sizeof(pathBuf), ab.currentDir.c_str(), sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';
    if (ImGui::InputText("##abpath", pathBuf, sizeof(pathBuf),
        ImGuiInputTextFlags_EnterReturnsTrue)) {
        ab.navStack.push_back(ab.currentDir);
        ab.currentDir = pathBuf;
        ab.dirDirty = true;
        ab.selectedGUIDs.clear();
    }

    ImGui::SameLine();

    // Search
    ImGui::SetNextItemWidth(140.f);
    ImGui::InputTextWithHint("##absearch", "\xef\x80\x82 Search...",
        ab.searchBuf, sizeof(ab.searchBuf));
    bool hasSearch = (ab.searchBuf[0] != '\0');

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

    // Favorites toggle
    if (ab.showFavoritesOnly)
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.6f, 0.5f, 0.1f, 1.f });
    if (ImGui::Button("\xef\x80\x85")) ab.showFavoritesOnly = !ab.showFavoritesOnly;  // fa-star
    if (ab.showFavoritesOnly) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show favorites only");

    ImGui::SameLine();

    // View toggle
    if (ab.viewMode == AssetBrowserState::Grid)
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.22f, 0.47f, 0.80f, 0.75f });
    if (ImGui::Button("\xef\x84\xa7")) ab.viewMode = AssetBrowserState::Grid;  // fa-th-large
    if (ab.viewMode == AssetBrowserState::Grid) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (ab.viewMode == AssetBrowserState::List)
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.22f, 0.47f, 0.80f, 0.75f });
    if (ImGui::Button("\xef\x80\xba")) ab.viewMode = AssetBrowserState::List;  // fa-list
    if (ab.viewMode == AssetBrowserState::List) ImGui::PopStyleColor();

    ImGui::SameLine();

    // Icon size slider (grid only)
    if (ab.viewMode == AssetBrowserState::Grid) {
        ImGui::SetNextItemWidth(80.f);
        ImGui::SliderFloat("##iconSz", &ab.iconSize, 48.f, 128.f, "%.0f");
    }

    ImGui::Separator();

    // ── Breadcrumb with Home button and sibling dropdowns ──────────────────
    {
        // Home button
        ImVec2 bcPos = ImGui::GetCursorPos();
        if (ImGui::Button("\xef\x80\x95", { 24, 24 })) {  // fa-home
            ab.currentDir = "./assets/";
            ab.dirDirty = true;
            ab.selectedGUIDs.clear();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Go to root assets folder");

        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();

        fs::path p(ab.currentDir);
        std::string cumulative;
        int idx = 0;

        // Build path parts
        std::vector<std::string> parts;
        for (auto& part : p) {
            std::string partStr = part.string();
            if (!partStr.empty() && partStr != ".") {
                parts.push_back(partStr);
            }
        }

        for (size_t i = 0; i < parts.size(); ++i) {
            cumulative += parts[i] + "/";

            if (i > 0) {
                ImGui::SameLine(0, 2);
                ImGui::TextDisabled("/");
                ImGui::SameLine(0, 2);
            }

            ImGui::PushID(idx++);

            // Highlight current directory
            bool isLast = (i == parts.size() - 1);
            if (isLast) {
                ImGui::PushStyleColor(ImGuiCol_Button, { 0.3f, 0.5f, 0.8f, 0.5f });
            }

            if (ImGui::SmallButton(parts[i].c_str())) {
                ab.navStack.push_back(ab.currentDir);
                ab.currentDir = cumulative;
                ab.dirDirty = true;
                ab.selectedGUIDs.clear();
            }

            if (isLast) {
                ImGui::PopStyleColor();
            }

            // Hover tooltip
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", cumulative.c_str());
            }

            // Right-click dropdown for siblings
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup(("breadcrumb_" + cumulative).c_str());
            }

            if (ImGui::BeginPopup(("breadcrumb_" + cumulative).c_str())) {
                ImGui::TextDisabled("Navigate to:");
                ImGui::Separator();
                try {
                    fs::path parent = fs::path(cumulative).parent_path();
                    if (parent.empty()) parent = ".";
                    if (fs::exists(parent)) {
                        std::vector<fs::directory_entry> dirs;
                        for (auto& entry : fs::directory_iterator(parent)) {
                            if (entry.is_directory()) {
                                dirs.push_back(entry);
                            }
                        }
                        std::sort(dirs.begin(), dirs.end(),
                            [](const fs::directory_entry& a, const fs::directory_entry& b) {
                                return a.path().filename() < b.path().filename();
                            });

                        for (auto& entry : dirs) {
                            bool isCurrent = (entry.path().string() + "/" == cumulative);
                            if (ImGui::MenuItem(entry.path().filename().string().c_str(), nullptr, isCurrent)) {
                                ab.navStack.push_back(ab.currentDir);
                                ab.currentDir = entry.path().string() + "/";
                                ab.dirDirty = true;
                                ab.selectedGUIDs.clear();
                            }
                        }
                    }
                }
                catch (...) {}
                ImGui::EndPopup();
            }

            ImGui::PopID();
        }
    }
    ImGui::Separator();

    // ── Main content area (file listing only – import settings moved to Inspector) ──
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

    // ── GRID VIEW ────────────────────────────────────────────────────────────
    if (ab.viewMode == AssetBrowserState::Grid) {
        float cellW = ab.iconSize + 8.f;
        float availX = ImGui::GetContentRegionAvail().x;
        int   cols = (((std::max)))(1, (int)(availX / cellW));

        int col = 0;
        for (auto& entry : ab.dirEntries) {
            if (!matchesFilter(entry)) continue;

            bool ctxOpen = false;
            bool dblClick = DrawGridCell(ab, db, entry, ab.iconSize, ctxOpen);

            std::string path = entry.path().string();
            AssetRecord* rec = entry.is_directory() ? nullptr : db.FindByPath(path);
            std::string gstr = rec ? rec->guid.ToString() : "";

            if (dblClick) {
                if (entry.is_directory()) {
                    ab.navStack.push_back(ab.currentDir);
                    ab.currentDir = entry.path().string() + "/";
                    ab.dirDirty = true;
                    ab.selectedGUIDs.clear();
                    break;
                }
                else if (onImportAsset) {
                    AssetType t = ExtToAssetType(entry.path().extension().string());
                    onImportAsset(path, t);
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
                std::string label = std::string(isDir ? "\xef\x81\xbb " : (std::string(AssetTypeIcon(atype)) + " ")) + name;
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
                            ab.navStack.push_back(ab.currentDir);
                            ab.currentDir = path + "/";
                            ab.dirDirty = true;
                            ab.selectedGUIDs.clear();
                        }
                        else if (onImportAsset) {
                            onImportAsset(path, atype);
                        }
                    }
                }

                bool ctxOpen2 = ImGui::BeginPopupContextItem("##lctx");
                if (ctxOpen2) {
                    // Inline the context menu body (popup already open via BeginPopupContextItem)
                    AssetRecord* recCtx = gstr.empty() ? nullptr : db.FindByGUID(gstr);
                    bool isDirCtx = entry.is_directory();

                    ImGui::TextDisabled("%s", entry.path().filename().string().c_str());
                    ImGui::Separator();

                    if (!isDirCtx) {
                        if (ImGui::MenuItem("Open")) {}
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

                // Drag source
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
                ImGui::TextDisabled("%s", isDir ? "Folder" : AssetTypeName(atype));
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
//  DrawAssetBrowserPanel  — replaces the minimal one in main.cpp
//  Parameters:
//    ab   — browser UI state (persistent across frames)
//    db   — asset database
//    dt   — frame delta time (for rescan timer)
//    onImportModel — callback(path) when user drops / double-clicks a model
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawAssetBrowserPanel(
    AssetBrowserState& ab,
    AssetDatabase& db,
    float dt,
    const std::function<void(const std::string& path, AssetType t)>& onImportAsset)
{
    EnsureCheckerTex();

    // Periodic rescan
    ab.rescanTimer += dt;
    if (ab.dirDirty || ab.rescanTimer > 3.f) {
        AssetBrowserRescan(ab, db);
        ab.rescanTimer = 0.f;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.07f, 0.075f, 0.085f, 1.f });
    ImGui::BeginChild("##ab_outer", { 0, 0 }, false);

    // ── Tabs ────────────────────────────────────────────────────────────────
    ImGui::BeginTabBar("AssetBrowserTabs", ImGuiTabBarFlags_None);

    // --- Tab: All Assets ---
    if (ImGui::BeginTabItem("All Assets")) {
        // Save and restore favorites-only state for this tab
        bool prevFav = ab.showFavoritesOnly;
        ab.showFavoritesOnly = false;

        DrawAssetBrowserContent(ab, db, dt, onImportAsset);

        ab.showFavoritesOnly = prevFav;
        ImGui::EndTabItem();
    }

    // --- Tab: Favorites ---
    if (ImGui::BeginTabItem("Favorites")) {
        // Save and restore favorites-only state for this tab
        bool prevFav = ab.showFavoritesOnly;
        ab.showFavoritesOnly = true;

        DrawAssetBrowserContent(ab, db, dt, onImportAsset);

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