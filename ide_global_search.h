#pragma once
// ide_global_search.h  —  HonHon Engine IDE  —  Global Search (Ctrl+F)
// =============================================================================
// Features:
//   - Opens as a modal popup triggered by Ctrl+F
//   - Searches: scene objects, scene lights, asset file names, console log entries
//   - Real-time filtering as the user types
//   - Keyboard navigation: arrow keys + Enter to jump to result
//   - Result categories with icons
//   - Callback-based action dispatch (select object, navigate asset browser, etc.)
// =============================================================================

#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  A single search result entry
// ─────────────────────────────────────────────────────────────────────────────
struct SearchResult {
    enum Category { SceneObject, SceneLight, Asset, LogEntry, Command };

    Category    category;
    std::string label;       // display text
    std::string subLabel;    // secondary info (type, path, etc.)
    std::string actionKey;   // opaque key passed to onAction (name, guid, cmd, etc.)
    const char* icon;
};

// ─────────────────────────────────────────────────────────────────────────────
//  GlobalSearchState  — owned by IDEState
// ─────────────────────────────────────────────────────────────────────────────
struct GlobalSearchState {
    bool  open = false;
    char  buf[256] = {};
    int   selectedIdx = 0;     // keyboard-highlighted result
    bool  needsFocus = false; // focus input text on next frame
    std::vector<SearchResult> results;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: case-insensitive find
// ─────────────────────────────────────────────────────────────────────────────
static bool GS_Contains(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower((unsigned char)a)
        == std::tolower((unsigned char)b); });
    return it != haystack.end();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rebuild the result list from all data sources
// ─────────────────────────────────────────────────────────────────────────────
template<typename IDEStateT>
static void GS_Rebuild(GlobalSearchState& gs, const IDEStateT& ide)
{
    gs.results.clear();
    std::string query(gs.buf);

    // ── Scene objects ────────────────────────────────────────────────────────
    for (auto& obj : ide.objects) {
        if (GS_Contains(obj.name, query)) {
            SearchResult r;
            r.category = SearchResult::SceneObject;
            r.label = obj.name;
            r.subLabel = obj.shader.empty() ? "Object" : obj.shader;
            r.actionKey = obj.name;
            r.icon = "\xef\x86\xb2";   // fa-cube
            gs.results.push_back(r);
        }
    }

    // ── Scene lights ─────────────────────────────────────────────────────────
    for (auto& lt : ide.lights) {
        if (GS_Contains(lt.name, query)) {
            SearchResult r;
            r.category = SearchResult::SceneLight;
            r.label = lt.name;
            r.subLabel = lt.lightType + " light";
            r.actionKey = lt.name;
            r.icon = "\xef\x83\xab";   // fa-lightbulb
            gs.results.push_back(r);
        }
    }

    // ── Asset database ────────────────────────────────────────────────────────
    for (auto& [gstr, rec] : ide.assetDb.records) {
        if (GS_Contains(rec.displayName, query) ||
            GS_Contains(rec.path, query))
        {
            SearchResult r;
            r.category = SearchResult::Asset;
            r.label = rec.displayName;
            r.subLabel = rec.path;
            r.actionKey = gstr;
            r.icon = "\xef\x85\x9b";   // fa-file
            gs.results.push_back(r);
            if ((int)gs.results.size() >= 60) break;  // cap
        }
    }

    // Sort: exact matches first, then alphabetical within category
    std::stable_sort(gs.results.begin(), gs.results.end(),
        [&](const SearchResult& a, const SearchResult& b) {
            // Exact label match first
            auto aExact = GS_Contains(a.label, query) && a.label.size() == query.size();
            auto bExact = GS_Contains(b.label, query) && b.label.size() == query.size();
            if (aExact != bExact) return aExact > bExact;
            if (a.category != b.category) return a.category < b.category;
            return a.label < b.label;
        });

    gs.selectedIdx = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Category color helpers
// ─────────────────────────────────────────────────────────────────────────────
static ImVec4 GS_CategoryColor(SearchResult::Category cat)
{
    switch (cat) {
    case SearchResult::SceneObject: return { 0.55f, 0.85f, 1.0f, 1.f };
    case SearchResult::SceneLight:  return { 1.0f,  0.85f, 0.4f, 1.f };
    case SearchResult::Asset:       return { 0.65f, 0.90f, 0.65f, 1.f };
    case SearchResult::LogEntry:    return { 0.75f, 0.75f, 0.75f, 1.f };
    default:                        return { 0.90f, 0.70f, 0.90f, 1.f };
    }
}

static const char* GS_CategoryLabel(SearchResult::Category cat)
{
    switch (cat) {
    case SearchResult::SceneObject: return "Scene";
    case SearchResult::SceneLight:  return "Light";
    case SearchResult::Asset:       return "Asset";
    case SearchResult::LogEntry:    return "Log";
    default:                        return "Cmd";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  DrawGlobalSearch  — call once per frame inside the main ImGui loop
//    onSelectObject(name) — selects an object in the hierarchy
//    onSelectAsset(guid)  — navigates asset browser to that GUID's directory
// ─────────────────────────────────────────────────────────────────────────────
template<typename IDEStateT>
static void DrawGlobalSearch(
    GlobalSearchState& gs,
    IDEStateT& ide,
    std::function<void(const std::string&)> onSelectObject,
    std::function<void(const std::string&)> onSelectAsset)
{
    if (!gs.open) return;

    // Position: centred, near top
    ImGuiIO& io = ImGui::GetIO();
    float popW = (std::min)(560.f, io.DisplaySize.x * 0.5f);
    float popH = (std::min)(480.f, io.DisplaySize.y * 0.7f);
    ImVec2 popPos = { (io.DisplaySize.x - popW) * 0.5f,
                      io.DisplaySize.y * 0.12f };

    ImGui::SetNextWindowPos(popPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ popW, popH }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 12.f, 10.f });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
    if (!ImGui::Begin("##globalsearch", &gs.open, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    // ── Search input ─────────────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 10.f, 7.f });
    ImGui::SetNextItemWidth(-1.f);

    if (gs.needsFocus) {
        ImGui::SetKeyboardFocusHere();
        gs.needsFocus = false;
    }

    bool textChanged = ImGui::InputTextWithHint(
        "##gsinput", "\xef\x80\x82  Search objects, assets, commands...",
        gs.buf, sizeof(gs.buf));

    if (textChanged)
        GS_Rebuild(gs, ide);

    ImGui::PopStyleVar(2);

    // ── Keyboard navigation ───────────────────────────────────────────────────
    int n = (int)gs.results.size();

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && gs.selectedIdx < n - 1)
        ++gs.selectedIdx;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && gs.selectedIdx > 0)
        --gs.selectedIdx;

    bool activated = ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        gs.open = false;
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    // ── Result list ───────────────────────────────────────────────────────────
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (n == 0) {
        ImGui::TextDisabled(gs.buf[0] ? "  No results found." : "  Start typing to search...");
    }
    else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 4.f, 2.f });
        ImGui::BeginChild("##gsresults", { 0, -30.f }, false,
            ImGuiWindowFlags_HorizontalScrollbar);

        SearchResult::Category lastCat = (SearchResult::Category)-1;

        for (int i = 0; i < n; ++i)
        {
            const auto& r = gs.results[i];

            // Category header
            if (r.category != lastCat) {
                if (lastCat != (SearchResult::Category)-1)
                    ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.55f,1.f });
                ImGui::TextUnformatted(GS_CategoryLabel(r.category));
                ImGui::PopStyleColor();
                lastCat = r.category;
            }

            bool sel = (i == gs.selectedIdx);
            ImGui::PushID(i);

            if (sel) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    { p.x - 4.f, p.y - 2.f },
                    { p.x + ImGui::GetContentRegionAvail().x + 4.f,
                      p.y + ImGui::GetTextLineHeight() + 6.f },
                    IM_COL32(50, 100, 180, 100), 4.f);
            }

            ImGui::PushStyleColor(ImGuiCol_Text, GS_CategoryColor(r.category));
            ImGui::TextUnformatted(r.icon);
            ImGui::PopStyleColor();
            ImGui::SameLine(30.f);
            ImGui::TextUnformatted(r.label.c_str());

            if (!r.subLabel.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.5f, 0.55f, 1.f });
                ImGui::TextUnformatted(("  " + r.subLabel).c_str());
                ImGui::PopStyleColor();
            }

            if (ImGui::IsItemHovered()) gs.selectedIdx = i;
            bool rowClicked = ImGui::IsItemClicked();

            ImGui::PopID();

            if (rowClicked || (activated && sel)) {
                // Dispatch action
                switch (r.category) {
                case SearchResult::SceneObject:
                case SearchResult::SceneLight:
                    onSelectObject(r.actionKey);
                    break;
                case SearchResult::Asset:
                    onSelectAsset(r.actionKey);
                    break;
                default:
                    break;
                }
                gs.open = false;
                break;
            }
        }

        // Auto-scroll to selected
        if (activated || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
            ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            float lineH = ImGui::GetTextLineHeightWithSpacing();
            ImGui::SetScrollY(gs.selectedIdx * lineH - popH * 0.4f);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ── Footer hint ───────────────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f, 0.4f, 0.45f, 1.f });
    ImGui::TextUnformatted("  \xe2\x86\x91\xe2\x86\x93 navigate    Enter select    Esc close");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleVar(2);
}