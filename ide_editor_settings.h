#pragma once
// ide_editor_settings.h  —  HonHon Engine IDE  —  Editor Settings (non-project)
// =============================================================================
// Covers:
//   - Font sizes (UI / code / console)
//   - Toolbar customization (which buttons are visible)
//   - Global editor preferences
// =============================================================================

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Toolbar configuration
// ─────────────────────────────────────────────────────────────────────────────
struct ToolbarItem {
    enum Type {
        Play, PauseStep, ToolsView, ToolsMove, ToolsRotate, ToolsScale,
        Snap, Favorites, Search, Save, Undo, Redo
    };
    Type type;
    bool visible = true;
    const char* icon;
    const char* tooltip;
};

struct ToolbarSettings {
    bool showPlayPause = true;
    bool showTools = true;
    bool showSnap = true;
    bool showFavorites = true;
    bool showSave = true;
    bool showUndoRedo = true;
    bool showSearch = true;

    // Custom order (store indices)
    std::vector<int> order;

    static ToolbarSettings Default() {
        ToolbarSettings s;
        s.order = { 0,1,2,3,4,5,6,7,8 }; // play, tools, snap, fav, save, undo, redo, search
        return s;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Editor settings (global, not per-project)
// ─────────────────────────────────────────────────────────────────────────────
struct EditorSettings {
    // Appearance
    float uiFontSize = 16.0f;
    float codeFontSize = 16.0f;
    float consoleFontSize = 14.0f;
    bool  useCustomFont = true;
    char  customFontPath[256] = "assets/JetBrainsMono-Medium.ttf";

    // Theme (reference to ThemeManager index)
    int   activeTheme = 0;

    // Toolbar
    ToolbarSettings toolbar;

    // Editor behaviour
    bool autoRefreshAssets = true;
    bool autoRecompileScripts = true;
    float assetRefreshInterval = 3.0f;
    bool  showWelcomeOnStartup = true;

    // Window state (layout persistence)
    std::string dockLayoutFile = "ide_layout.ini";

    // Clipboard component
    struct ComponentClipboard {
        std::string componentType;  // "transform", "material", "light"
        float pos[3] = { 0,0,0 };
        float scale[3] = { 1,1,1 };
        float rot[3] = { 0,0,0 };
        float color[4] = { 1,1,1,1 };
        float intensity = 1.0f;
        std::string lightType;
        std::string shader;
    } clipboard;

    // Save/Load
    void Save(const std::string& path) {
        std::ofstream f(path);
        if (!f) return;
        f << "uiFontSize=" << uiFontSize << "\n";
        f << "codeFontSize=" << codeFontSize << "\n";
        f << "consoleFontSize=" << consoleFontSize << "\n";
        f << "useCustomFont=" << useCustomFont << "\n";
        f << "customFontPath=" << customFontPath << "\n";
        f << "activeTheme=" << activeTheme << "\n";
        f << "autoRefreshAssets=" << autoRefreshAssets << "\n";
        f << "autoRecompileScripts=" << autoRecompileScripts << "\n";
        f << "assetRefreshInterval=" << assetRefreshInterval << "\n";
        f << "showWelcomeOnStartup=" << showWelcomeOnStartup << "\n";
        f << "toolbar.showPlayPause=" << toolbar.showPlayPause << "\n";
        f << "toolbar.showTools=" << toolbar.showTools << "\n";
        f << "toolbar.showSnap=" << toolbar.showSnap << "\n";
        f << "toolbar.showFavorites=" << toolbar.showFavorites << "\n";
        f << "toolbar.showSave=" << toolbar.showSave << "\n";
        f << "toolbar.showUndoRedo=" << toolbar.showUndoRedo << "\n";
        f << "toolbar.showSearch=" << toolbar.showSearch << "\n";
        // toolbar order list
        f << "toolbar.order=";
        for (size_t i = 0; i < toolbar.order.size(); ++i) {
            if (i) f << ",";
            f << toolbar.order[i];
        }
        f << "\n";
    }

    void Load(const std::string& path) {
        std::ifstream f(path);
        if (!f) return;
        std::string line;
        while (std::getline(f, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            try {
                if (key == "uiFontSize") uiFontSize = std::stof(val);
                else if (key == "codeFontSize") codeFontSize = std::stof(val);
                else if (key == "consoleFontSize") consoleFontSize = std::stof(val);
                else if (key == "useCustomFont") useCustomFont = (val == "1");
                else if (key == "activeTheme") activeTheme = std::stoi(val);
                else if (key == "autoRefreshAssets") autoRefreshAssets = (val == "1");
                else if (key == "autoRecompileScripts") autoRecompileScripts = (val == "1");
                else if (key == "assetRefreshInterval") assetRefreshInterval = std::stof(val);
                else if (key == "showWelcomeOnStartup") showWelcomeOnStartup = (val == "1");
                else if (key == "toolbar.showPlayPause") toolbar.showPlayPause = (val == "1");
                else if (key == "toolbar.showTools") toolbar.showTools = (val == "1");
                else if (key == "toolbar.showSnap") toolbar.showSnap = (val == "1");
                else if (key == "toolbar.showFavorites") toolbar.showFavorites = (val == "1");
                else if (key == "toolbar.showSave") toolbar.showSave = (val == "1");
                else if (key == "toolbar.showUndoRedo") toolbar.showUndoRedo = (val == "1");
                else if (key == "toolbar.showSearch") toolbar.showSearch = (val == "1");
                else if (key == "toolbar.order") {
                    toolbar.order.clear();
                    size_t start = 0;
                    while (start < val.size()) {
                        size_t end = val.find(',', start);
                        if (end == std::string::npos) end = val.size();
                        toolbar.order.push_back(std::stoi(val.substr(start, end - start)));
                        start = end + 1;
                    }
                }
            }
            catch (...) {}
        }
        if (toolbar.order.empty()) toolbar.order = ToolbarSettings::Default().order;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  DrawPreferencesWindow  —  modal for editor preferences
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawPreferencesWindow(EditorSettings& settings, bool& open) {
    if (!open) return;
    ImGui::SetNextWindowSize({ 560, 480 }, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Preferences", &open)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("PrefsTabs")) {
        // ── Appearance tab ─────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Appearance")) {
            ImGui::SeparatorText("Fonts");
            ImGui::Checkbox("Use custom font", &settings.useCustomFont);
            if (settings.useCustomFont) {
                ImGui::InputText("Font path", settings.customFontPath, sizeof(settings.customFontPath));
                ImGui::DragFloat("UI font size", &settings.uiFontSize, 0.5f, 8.f, 32.f, "%.1f pt");
                ImGui::DragFloat("Code font size", &settings.codeFontSize, 0.5f, 8.f, 32.f, "%.1f pt");
                ImGui::DragFloat("Console font size", &settings.consoleFontSize, 0.5f, 8.f, 24.f, "%.1f pt");
            }
            ImGui::SeparatorText("Theme");
            ImGui::TextDisabled("Theme selection available in Theme Editor window");
            if (ImGui::Button("Open Theme Editor")) {
                // Placeholder — will delegate to ThemeManager
                ImGui::OpenPopup("###ThemeEditorPlaceholder");
            }
            ImGui::EndTabItem();
        }

        // ── Toolbar tab ───────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Toolbar")) {
            ImGui::Checkbox("Show Play/Pause", &settings.toolbar.showPlayPause);
            ImGui::Checkbox("Show Tools (Q/W/E/R)", &settings.toolbar.showTools);
            ImGui::Checkbox("Show Snap toggle", &settings.toolbar.showSnap);
            ImGui::Checkbox("Show Favorites filter", &settings.toolbar.showFavorites);
            ImGui::Checkbox("Show Save button", &settings.toolbar.showSave);
            ImGui::Checkbox("Show Undo/Redo", &settings.toolbar.showUndoRedo);
            ImGui::Checkbox("Show Search", &settings.toolbar.showSearch);
            ImGui::Separator();
            ImGui::TextDisabled("(Order customization coming soon)");
            ImGui::EndTabItem();
        }

        // ── Editor tab ────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Editor")) {
            ImGui::Checkbox("Auto-refresh assets", &settings.autoRefreshAssets);
            if (settings.autoRefreshAssets)
                ImGui::DragFloat("Refresh interval (s)", &settings.assetRefreshInterval, 0.5f, 1.f, 10.f);
            ImGui::Checkbox("Auto-recompile scripts on change", &settings.autoRecompileScripts);
            ImGui::Checkbox("Show welcome dialog on startup", &settings.showWelcomeOnStartup);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save Settings", { 120, 0 })) {
        // Caller will save to file
        open = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", { 100, 0 })) open = false;
    ImGui::End();
}