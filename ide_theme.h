#pragma once
// ide_theme.h  —  HonHon Engine IDE  —  Theme / Appearance System
// =============================================================================
// Covers:
//   - Built-in themes: Dark (default), Light, Midnight Blue, Warm Dark
//   - Per-theme color storage (full ImGuiCol_COUNT array)
//   - JSON-style save / load  (plain text, no external lib)
//   - ApplyTheme()  — writes into ImGui::GetStyle().Colors
//   - DrawThemeSelector()  — combo + custom color editor popup
// =============================================================================

#include <string>
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Theme definition
// ─────────────────────────────────────────────────────────────────────────────
struct Theme {
    std::string name;
    ImVec4      colors[ImGuiCol_COUNT];
    float       windowRounding = 6.f;
    float       frameRounding = 4.f;
    float       tabRounding = 4.f;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in: Dark  (matches ApplyIDEStyle in main.cpp)
// ─────────────────────────────────────────────────────────────────────────────
inline Theme MakeThemeDark() {
    Theme t; t.name = "Dark";
    ImVec4* c = t.colors;

    const ImVec4 bg0 = { 0.078f, 0.082f, 0.094f, 1.f };
    const ImVec4 bg1 = { 0.110f, 0.114f, 0.129f, 1.f };
    const ImVec4 bg2 = { 0.145f, 0.150f, 0.168f, 1.f };
    const ImVec4 bg3 = { 0.190f, 0.196f, 0.216f, 1.f };
    const ImVec4 bg4 = { 0.240f, 0.248f, 0.270f, 1.f };
    const ImVec4 acc0 = { 0.22f,  0.47f,  0.80f,  1.f };
    const ImVec4 acc1 = { 0.30f,  0.55f,  0.88f,  1.f };
    const ImVec4 acc2 = { 0.18f,  0.40f,  0.70f,  1.f };
    const ImVec4 bdr = { 0.24f,  0.25f,  0.28f,  1.f };
    const ImVec4 txt = { 0.90f,  0.91f,  0.93f,  1.f };
    const ImVec4 txtD = { 0.48f,  0.50f,  0.54f,  1.f };

    // Fill everything with safe defaults first
    for (int i = 0; i < ImGuiCol_COUNT; ++i) c[i] = bg1;

    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg0;
    c[ImGuiCol_PopupBg] = { 0.13f,0.135f,0.15f,0.98f };
    c[ImGuiCol_MenuBarBg] = bg0;
    c[ImGuiCol_Border] = bdr;
    c[ImGuiCol_BorderShadow] = { 0,0,0,0 };
    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = bg4;
    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = { 0.15f,0.16f,0.19f,1.f };
    c[ImGuiCol_TitleBgCollapsed] = bg0;
    c[ImGuiCol_ScrollbarBg] = bg0;
    c[ImGuiCol_ScrollbarGrab] = bg4;
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.30f,0.31f,0.34f,1.f };
    c[ImGuiCol_ScrollbarGrabActive] = acc0;
    c[ImGuiCol_CheckMark] = acc1;
    c[ImGuiCol_SliderGrab] = acc0;
    c[ImGuiCol_SliderGrabActive] = acc1;
    c[ImGuiCol_Button] = { acc0.x,acc0.y,acc0.z,0.75f };
    c[ImGuiCol_ButtonHovered] = acc1;
    c[ImGuiCol_ButtonActive] = acc2;
    c[ImGuiCol_Header] = { acc0.x,acc0.y,acc0.z,0.45f };
    c[ImGuiCol_HeaderHovered] = { acc0.x,acc0.y,acc0.z,0.65f };
    c[ImGuiCol_HeaderActive] = acc0;
    c[ImGuiCol_Separator] = bdr;
    c[ImGuiCol_SeparatorHovered] = acc0;
    c[ImGuiCol_SeparatorActive] = acc1;
    c[ImGuiCol_ResizeGrip] = { acc0.x,acc0.y,acc0.z,0.20f };
    c[ImGuiCol_ResizeGripHovered] = acc0;
    c[ImGuiCol_ResizeGripActive] = acc1;
    c[ImGuiCol_Tab] = { 0.13f,0.135f,0.15f,1.f };
    c[ImGuiCol_TabHovered] = bg3;
    c[ImGuiCol_TabActive] = { 0.18f,0.19f,0.22f,1.f };
    c[ImGuiCol_TabUnfocused] = bg0;
    c[ImGuiCol_TabUnfocusedActive] = bg1;
    c[ImGuiCol_DockingPreview] = { acc0.x,acc0.y,acc0.z,0.60f };
    c[ImGuiCol_DockingEmptyBg] = bg0;
    c[ImGuiCol_PlotLines] = acc0;
    c[ImGuiCol_PlotLinesHovered] = acc1;
    c[ImGuiCol_PlotHistogram] = acc0;
    c[ImGuiCol_PlotHistogramHovered] = acc1;
    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = bdr;
    c[ImGuiCol_TableBorderLight] = { 0.18f,0.19f,0.21f,1.f };
    c[ImGuiCol_TextSelectedBg] = { acc0.x,acc0.y,acc0.z,0.35f };
    c[ImGuiCol_NavHighlight] = acc1;
    c[ImGuiCol_NavWindowingHighlight] = acc1;
    c[ImGuiCol_NavWindowingDimBg] = { 0,0,0,0.4f };
    c[ImGuiCol_ModalWindowDimBg] = { 0,0,0,0.5f };
    c[ImGuiCol_Text] = txt;
    c[ImGuiCol_TextDisabled] = txtD;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in: Light
// ─────────────────────────────────────────────────────────────────────────────
inline Theme MakeThemeLight() {
    Theme t; t.name = "Light";
    // Start from ImGui light defaults
    ImGui::StyleColorsLight();
    ImVec4* src = ImGui::GetStyle().Colors;
    for (int i = 0; i < ImGuiCol_COUNT; ++i) t.colors[i] = src[i];
    // Accent overrides
    ImVec4 acc = { 0.18f, 0.40f, 0.80f, 1.f };
    t.colors[ImGuiCol_Button] = { acc.x,acc.y,acc.z,0.75f };
    t.colors[ImGuiCol_ButtonHovered] = { acc.x + 0.08f,acc.y + 0.08f,acc.z + 0.08f,1.f };
    t.colors[ImGuiCol_CheckMark] = acc;
    t.colors[ImGuiCol_SliderGrab] = acc;
    t.colors[ImGuiCol_Header] = { acc.x,acc.y,acc.z,0.35f };
    t.colors[ImGuiCol_DockingPreview] = { acc.x,acc.y,acc.z,0.55f };
    t.windowRounding = 5.f;
    t.frameRounding = 3.f;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in: Midnight Blue
// ─────────────────────────────────────────────────────────────────────────────
inline Theme MakeThemeMidnight() {
    Theme t = MakeThemeDark(); t.name = "Midnight Blue";
    const ImVec4 bg0 = { 0.02f, 0.04f, 0.12f, 1.f };
    const ImVec4 bg1 = { 0.04f, 0.07f, 0.18f, 1.f };
    const ImVec4 bg2 = { 0.07f, 0.12f, 0.24f, 1.f };
    const ImVec4 acc = { 0.10f, 0.55f, 1.00f, 1.f };
    t.colors[ImGuiCol_WindowBg] = bg1;
    t.colors[ImGuiCol_ChildBg] = bg0;
    t.colors[ImGuiCol_MenuBarBg] = bg0;
    t.colors[ImGuiCol_FrameBg] = bg2;
    t.colors[ImGuiCol_TitleBg] = bg0;
    t.colors[ImGuiCol_TitleBgActive] = bg1;
    t.colors[ImGuiCol_Button] = { acc.x,acc.y,acc.z,0.60f };
    t.colors[ImGuiCol_ButtonHovered] = { acc.x + 0.05f,acc.y + 0.05f,acc.z + 0.05f,1.f };
    t.colors[ImGuiCol_CheckMark] = acc;
    t.colors[ImGuiCol_SliderGrab] = acc;
    t.colors[ImGuiCol_DockingPreview] = { acc.x,acc.y,acc.z,0.5f };
    t.colors[ImGuiCol_Tab] = bg0;
    t.colors[ImGuiCol_TabActive] = bg2;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in: Warm Dark
// ─────────────────────────────────────────────────────────────────────────────
inline Theme MakeThemeWarmDark() {
    Theme t = MakeThemeDark(); t.name = "Warm Dark";
    const ImVec4 bg0 = { 0.10f, 0.07f, 0.05f, 1.f };
    const ImVec4 bg1 = { 0.14f, 0.10f, 0.07f, 1.f };
    const ImVec4 bg2 = { 0.18f, 0.14f, 0.10f, 1.f };
    const ImVec4 acc = { 0.85f, 0.55f, 0.20f, 1.f };
    t.colors[ImGuiCol_WindowBg] = bg1;
    t.colors[ImGuiCol_ChildBg] = bg0;
    t.colors[ImGuiCol_MenuBarBg] = bg0;
    t.colors[ImGuiCol_FrameBg] = bg2;
    t.colors[ImGuiCol_TitleBg] = bg0;
    t.colors[ImGuiCol_TitleBgActive] = bg1;
    t.colors[ImGuiCol_Button] = { acc.x,acc.y,acc.z,0.70f };
    t.colors[ImGuiCol_ButtonHovered] = { acc.x,acc.y + 0.05f,acc.z,1.f };
    t.colors[ImGuiCol_CheckMark] = acc;
    t.colors[ImGuiCol_SliderGrab] = acc;
    t.colors[ImGuiCol_DockingPreview] = { acc.x,acc.y,acc.z,0.5f };
    t.colors[ImGuiCol_Tab] = bg0;
    t.colors[ImGuiCol_TabActive] = bg2;
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Apply a theme to the current ImGui style
// ─────────────────────────────────────────────────────────────────────────────
inline void ApplyTheme(const Theme& theme) {
    ImGuiStyle& s = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        s.Colors[i] = theme.colors[i];
    s.WindowRounding = theme.windowRounding;
    s.FrameRounding = theme.frameRounding;
    s.TabRounding = theme.tabRounding;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ThemeManager — holds all themes, current index, save/load
// ─────────────────────────────────────────────────────────────────────────────
struct ThemeManager {
    std::vector<Theme> themes;
    int   currentIdx = 0;   // index into themes[]
    bool  customEditorOpen = false;

    ThemeManager() {
        themes.push_back(MakeThemeDark());
        themes.push_back(MakeThemeLight());
        themes.push_back(MakeThemeMidnight());
        themes.push_back(MakeThemeWarmDark());
    }

    void Apply() {
        if (currentIdx >= 0 && currentIdx < (int)themes.size())
            ApplyTheme(themes[currentIdx]);
    }

    // Save current index (and any custom) to file
    void Save(const std::string& path) const {
        std::ofstream f(path);
        if (!f) return;
        f << "current=" << currentIdx << "\n";
        // Persist custom themes (beyond the 4 built-ins)
        for (int ti = 4; ti < (int)themes.size(); ++ti) {
            const Theme& t = themes[ti];
            f << "theme_name=" << t.name << "\n";
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
                f << "col_" << i << "="
                << t.colors[i].x << ","
                << t.colors[i].y << ","
                << t.colors[i].z << ","
                << t.colors[i].w << "\n";
            f << "end_theme\n";
        }
    }

    void Load(const std::string& path) {
        std::ifstream f(path); if (!f) return;
        std::string line;
        Theme* cur = nullptr;
        while (std::getline(f, line)) {
            if (line.rfind("current=", 0) == 0) {
                try { currentIdx = std::stoi(line.substr(8)); }
                catch (...) {}
                currentIdx = (std::max)(0, (std::min)(currentIdx, (int)themes.size() - 1));
            }
            else if (line.rfind("theme_name=", 0) == 0) {
                themes.push_back(MakeThemeDark());
                cur = &themes.back();
                cur->name = line.substr(11);
            }
            else if (line == "end_theme") {
                cur = nullptr;
            }
            else if (cur && line.rfind("col_", 0) == 0) {
                auto eq = line.find('='); if (eq == std::string::npos) continue;
                int idx = 0;
                try { idx = std::stoi(line.substr(4, eq - 4)); }
                catch (...) { continue; }
                if (idx < 0 || idx >= ImGuiCol_COUNT) continue;
                std::string val = line.substr(eq + 1);
                float r = 0, g = 0, b = 0, a = 1;
                sscanf_s(val.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
                cur->colors[idx] = { r, g, b, a };
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  DrawThemeSelector — call from the Preferences window / menu
// ─────────────────────────────────────────────────────────────────────────────
inline void DrawThemeSelector(ThemeManager& mgr) {
    ImGui::SeparatorText("Theme");

    // Build name list
    std::vector<const char*> names;
    for (auto& t : mgr.themes) names.push_back(t.name.c_str());

    int prev = mgr.currentIdx;
    if (ImGui::Combo("##theme_combo", &mgr.currentIdx, names.data(), (int)names.size())) {
        if (mgr.currentIdx != prev) mgr.Apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("Customize...")) mgr.customEditorOpen = true;
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
        // Duplicate current theme as a new custom
        Theme copy = mgr.themes[mgr.currentIdx];
        copy.name += " (Custom)";
        mgr.themes.push_back(copy);
        mgr.currentIdx = (int)mgr.themes.size() - 1;
        mgr.Apply();
    }

    // ── Custom color editor popup ──────────────────────────────────────────
    if (mgr.customEditorOpen) {
        ImGui::SetNextWindowSize({ 440, 520 }, ImGuiCond_Once);
        if (ImGui::Begin("Theme Color Editor", &mgr.customEditorOpen)) {
            Theme& t = mgr.themes[mgr.currentIdx];
            ImGui::InputText("Name##thname", &t.name[0], 64);
            ImGui::Separator();
            ImGui::BeginChild("##colscroll", { 0, -36 }, true);
            for (int i = 0; i < ImGuiCol_COUNT; ++i) {
                ImGui::PushID(i);
                bool changed = ImGui::ColorEdit4(ImGui::GetStyleColorName(i),
                    (float*)&t.colors[i],
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                if (changed) mgr.Apply();
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (ImGui::Button("Reset to Defaults")) {
                mgr.themes[mgr.currentIdx] = MakeThemeDark();
                mgr.Apply();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close")) mgr.customEditorOpen = false;
        }
        ImGui::End();
    }
}