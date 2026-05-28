// ide_project_launcher.h — HonHon Engine project picker / creator.
// =============================================================================
// Shown once at startup (before the main editor loop begins) as a full-screen
// modal. The user either picks an existing project or creates a new one.
//
// Projects live in:   <Documents>/HonHengine/<ProjectName>/
// Default sub-tree:
//   assets/
//     scenes/          ← .honscene files land here by default
// =============================================================================
#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <chrono>

#include <imgui.h>
#include "imgui_impl_sdl2.h"
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <glad/glad.h>

#ifndef ImGuiChildFlags_Border
#  ifdef ImGuiChildFlags_FrameStyle
#    define ImGuiChildFlags_Border ImGuiChildFlags_FrameStyle
#  else
#    define HONHON_LEGACY_BEGINCHILD 1
#  endif
#endif

namespace HonHengine {

    namespace fs = std::filesystem;

    inline std::string GetDocumentsFolder()
    {
#if defined(_WIN32)
        auto getEnvSafe = [](const char* name) -> std::string {
            char* buf = nullptr; size_t sz = 0;
            if (_dupenv_s(&buf, &sz, name) == 0 && buf) {
                std::string s(buf); free(buf); return s;
            }
            return {};
            };
        std::string up = getEnvSafe("USERPROFILE");
        if (!up.empty()) return up + "\\Documents";
        std::string hd = getEnvSafe("HOMEDRIVE");
        std::string hp = getEnvSafe("HOMEPATH");
        if (!hd.empty() && !hp.empty()) return hd + hp + "\\Documents";
        return "C:\\Users\\Documents";
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/Documents";
        return "/tmp";
#else
        const char* xdg = std::getenv("XDG_DOCUMENTS_DIR");
        if (xdg) return std::string(xdg);
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + "/Documents";
        return "/tmp";
#endif
    }

    struct ProjectMeta {
        std::string name;
        std::string path;
        std::string lastModifiedStr;
        std::time_t lastModifiedTime = 0;
    };

    class ProjectLauncher {
    public:
        std::string chosenProjectName;
        std::string chosenProjectRoot;

        bool RunModal(SDL_Window* win, SDL_GLContext ctx)
        {
            using namespace std::chrono;
            auto startTime = steady_clock::now();

            std::string engineRoot = GetDocumentsFolder() + "/HonHengine";
            try { fs::create_directories(engineRoot); }
            catch (...) {}

            std::vector<ProjectMeta> projects;
            auto refreshProjects = [&]() {
                projects.clear();
                try {
                    if (fs::exists(engineRoot)) {
                        for (auto& p : fs::directory_iterator(engineRoot)) {
                            if (p.is_directory()) {
                                ProjectMeta meta;
                                meta.name = p.path().filename().string();
                                meta.path = p.path().string() + "/";

                                auto writeTime = fs::last_write_time(p.path());
                                auto sct = time_point_cast<system_clock::duration>(
                                    writeTime - decltype(writeTime)::clock::now() + system_clock::now()
                                );
                                meta.lastModifiedTime = system_clock::to_time_t(sct);

                                char tbuf[64];
                                struct tm lt;
#if defined(_WIN32)
                                localtime_s(&lt, &meta.lastModifiedTime);
#else
                                localtime_r(&meta.lastModifiedTime, &lt);
#endif
                                std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &lt);
                                meta.lastModifiedStr = tbuf;

                                projects.push_back(meta);
                            }
                        }
                        std::sort(projects.begin(), projects.end(), [](const ProjectMeta& a, const ProjectMeta& b) {
                            return a.lastModifiedTime > b.lastModifiedTime;
                            });
                    }
                }
                catch (...) {}
                };

            refreshProjects();

            int selectedIndex = -1;
            if (!projects.empty()) selectedIndex = 0;

            char newProjName[64] = "MyNewProject";
            char errorBuf[256] = "";

            bool done = false;
            bool accepted = false;
            bool focusedNewTab = false;

            while (!done) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    ImGui_ImplSDL2_ProcessEvent(&event);
                    if (event.type == SDL_QUIT) {
                        done = true; accepted = false;
                    }
                    if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(win)) {
                        done = true; accepted = false;
                    }
                }

                auto currTime = steady_clock::now();
                float elapsed = duration<float>(currTime - startTime).count();

                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();

                int w, h;
                SDL_GetWindowSize(win, &w, &h);
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));

                // Fullscreen background window with animated grid
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 1.f));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

                ImGui::Begin("LauncherBG", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);

                // Animated grid backdrop
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float gridSpacing = 40.0f;
                const ImU32 gridColor = IM_COL32(100, 110, 240, (int)(10.f + sinf(elapsed * 1.5f) * 4.f));
                float gridScroll = fmodf(elapsed * 12.f, gridSpacing);
                for (float x = gridScroll; x < (float)w; x += gridSpacing)
                    drawList->AddLine(ImVec2(x, 0), ImVec2(x, (float)h), gridColor, 1.f);
                for (float y = gridScroll; y < (float)h; y += gridSpacing)
                    drawList->AddLine(ImVec2(0, y), ImVec2((float)w, y), gridColor, 1.f);

                // Card dimensions and position
                const float cardW = 680.f;
                const float cardH = 500.f;
                ImVec2 cardPos((w - cardW) * 0.5f, (h - cardH) * 0.5f);

                // Draw drop shadow around the card
                for (int i = 1; i <= 8; i++) {
                    drawList->AddRectFilled(
                        ImVec2(cardPos.x - i, cardPos.y - i),
                        ImVec2(cardPos.x + cardW + i, cardPos.y + cardH + i),
                        IM_COL32(0, 0, 0, (12 - i) * 2), 12.f
                    );
                }

                // Now create the actual ImGui child window that acts as the card.
                // Everything inside will be clipped to this rectangle.
                ImGui::SetNextWindowPos(cardPos);
                ImGui::SetNextWindowSize(ImVec2(cardW, cardH));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(20, 21, 26, 245));

                if (ImGui::BeginChild("##Card", ImVec2(cardW, cardH), false,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar))
                {
                    // ---- Header ----
                    ImGui::SetCursorPos(ImVec2(30.f, 25.f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.57f, 0.65f, 1.f));
                    ImGui::TextUnformatted("HONHON ENGINE");
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(ImVec2(30.f, 45.f));
                    // TextProxyFontSignature helper (defined earlier)
                    TextProxyFontSignature("Project Hub Launcher", false, 1.3f);

                    // ---- Centered Tab Bar ----
                    const float contentWidth = cardW - 60.f;
                    float totalTabsWidth = 0.f;
                    const char* tabNames[] = { "  Open Project  ", "  Create New Project  " };
                    for (const char* name : tabNames) {
                        ImVec2 textSize = ImGui::CalcTextSize(name);
                        totalTabsWidth += textSize.x + ImGui::GetStyle().ItemInnerSpacing.x * 2.f;
                    }
                    totalTabsWidth += ImGui::GetStyle().TabRounding * 2.f;
                    float tabBarStartX = 30.f + (contentWidth - totalTabsWidth) * 0.5f;
                    if (tabBarStartX < 30.f) tabBarStartX = 30.f;
                    ImGui::SetCursorPos(ImVec2(tabBarStartX, 95.f));

                    // Tab styling
                    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.08f, 0.08f, 0.11f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18f, 0.18f, 0.26f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.13f, 0.14f, 0.22f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.06f, 0.06f, 0.08f, 1.f));
                    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.f);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 10.f));

                    if (ImGui::BeginTabBar("##LauncherTabs", ImGuiTabBarFlags_None))
                    {
                        // ---------- TAB 1: Open Project ----------
                        if (ImGui::BeginTabItem("  Open Project  "))
                        {
                            focusedNewTab = false;
                            ImGui::Spacing();

                            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.07f, 0.09f, 1.f));
							if (ImGui::BeginChild("##ProjList", ImVec2(contentWidth, 240.f), true, ImGuiWindowFlags_NoScrollbar))
                            {
                                if (projects.empty())
                                {
                                    ImGui::SetCursorPos(ImVec2(20.f, 100.f));
                                    ImGui::TextDisabled("No localized workspace project footprints discovered inside documents tree.");
                                }
                                else
                                {
                                    for (size_t i = 0; i < projects.size(); i++)
                                    {
                                        ImGui::PushID((int)i);
                                        bool isSelected = (selectedIndex == (int)i);

                                        if (isSelected) {
                                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.15f, 0.17f, 0.28f, 1.f));
                                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.15f, 0.17f, 0.28f, 1.f));
                                        }
                                        else {
                                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 0.f, 0.f, 0.f));
                                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.11f, 0.15f, 0.6f));
                                        }

                                        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.f, 0.5f));
                                        char itemLabel[256];
                                        std::snprintf(itemLabel, sizeof(itemLabel), "  %s", projects[i].name.c_str());

                                        if (ImGui::Selectable(itemLabel, isSelected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 42.f))) {
                                            selectedIndex = (int)i;
                                            if (ImGui::IsMouseDoubleClicked(0)) {
                                                chosenProjectName = projects[i].name;
                                                chosenProjectRoot = projects[i].path;
                                                done = true; accepted = true;
                                            }
                                        }
                                        ImGui::PopStyleVar();

                                        // Date label aligned to right inside the child
                                        float itemY = ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y;
                                        ImGui::SetCursorPos(ImVec2(contentWidth - 150.f, itemY + 13.f));
                                        ImGui::TextDisabled("%s", projects[i].lastModifiedStr.c_str());

                                        ImGui::PopStyleColor(2);
                                        ImGui::PopID();
                                    }
                                }
                            }
                            ImGui::EndChild();
                            ImGui::PopStyleColor();

                            // Launch button (right-aligned inside the card child)
                            ImGui::Spacing();
                            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 190.f);
                            bool hasSelection = (selectedIndex >= 0 && selectedIndex < (int)projects.size());
                            if (!hasSelection) ImGui::BeginDisabled();

                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14f, 0.18f, 0.36f, 1.f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.50f, 1.f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.32f, 0.65f, 1.f));

                            if (ImGui::Button("Launch Engine Project", ImVec2(160.f, 36.f))) {
                                chosenProjectName = projects[selectedIndex].name;
                                chosenProjectRoot = projects[selectedIndex].path;
                                done = true; accepted = true;
                            }
                            ImGui::PopStyleColor(3);
                            if (!hasSelection) ImGui::EndDisabled();

                            ImGui::EndTabItem();
                        }

                        // ---------- TAB 2: Create New Project ----------
                        if (ImGui::BeginTabItem("  Create New Project  "))
                        {
                            ImGui::Spacing();
                            ImGui::TextUnformatted("Project Name");
                            ImGui::SetNextItemWidth(contentWidth);
                            if (ImGui::InputText("##NewProjName", newProjName, sizeof(newProjName))) {
                                errorBuf[0] = '\0';
                            }

                            if (!focusedNewTab) {
                                ImGui::SetKeyboardFocusHere(-1);
                                focusedNewTab = true;
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("Root Destination Directory:");
                            std::string targetSimulatedPath = engineRoot + "/" + newProjName + "/";
                            ImGui::TextColored(ImVec4(0.45f, 0.70f, 0.50f, 1.f), "%s", targetSimulatedPath.c_str());

                            if (errorBuf[0] != '\0') {
                                ImGui::Spacing();
                                ImGui::TextColored(ImVec4(0.85f, 0.30f, 0.30f, 1.f), "Error: %s", errorBuf);
                            }

                            ImGui::Spacing();
                            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 190.f);
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.32f, 0.20f, 1.f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.48f, 0.30f, 1.f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.60f, 0.38f, 1.f));

                            if (ImGui::Button("Initialize Blueprint", ImVec2(160.f, 36.f))) {
                                std::string cleanedName = newProjName;
                                cleanedName.erase(std::remove_if(cleanedName.begin(), cleanedName.end(), [](char c) {
                                    return !(std::isalnum(c) || c == '_' || c == '-');
                                    }), cleanedName.end());

                                if (cleanedName.empty()) {
#if defined(_WIN32)
                                    strncpy_s(errorBuf, "Invalid string identifiers parsed", sizeof(errorBuf) - 1);
#else
                                    std::strncpy(errorBuf, "Invalid string identifiers parsed", sizeof(errorBuf) - 1);
#endif
                                }
                                else {
                                    std::string resolvedRoot = engineRoot + "/" + cleanedName + "/";
                                    if (fs::exists(resolvedRoot)) {
#if defined(_WIN32)
                                        strncpy_s(errorBuf, "Project node already allocated", sizeof(errorBuf) - 1);
#else
                                        std::strncpy(errorBuf, "Project node already allocated", sizeof(errorBuf) - 1);
#endif
                                    }
                                    else {
                                        try {
                                            fs::create_directories(resolvedRoot + "assets/scenes");
                                            fs::create_directories(resolvedRoot + "assets/textures");
                                            fs::create_directories(resolvedRoot + "assets/models");
                                            fs::create_directories(resolvedRoot + "assets/scripts");

                                            std::ofstream f(resolvedRoot + "assets/scenes/main.honscene");
                                            if (f.is_open()) {
                                                f << "{\"objects\":[],\"lights\":[]}";
                                                f.close();
                                            }

                                            chosenProjectName = cleanedName;
                                            chosenProjectRoot = resolvedRoot;
                                            done = true; accepted = true;
                                        }
                                        catch (const std::exception& e) {
#if defined(_WIN32)
                                            strncpy_s(errorBuf, e.what(), sizeof(errorBuf) - 1);
#else
                                            std::strncpy(errorBuf, e.what(), sizeof(errorBuf) - 1);
#endif
                                        }
                                    }
                                }
                            }
                            ImGui::PopStyleColor(3);
                            ImGui::EndTabItem();
                        }
                        else {
                            focusedNewTab = false;
                        }

                        ImGui::EndTabBar();
                    }

                    ImGui::PopStyleVar(2);  // TabRounding, ItemSpacing
                    ImGui::PopStyleColor(4); // Tab colors + FrameBg

                    // ---- Quit button (bottom left of the card) ----
                    ImGui::SetCursorPos(ImVec2(30.f, cardH - 57.f));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.08f, 0.08f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.12f, 0.12f, 1.f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.16f, 0.16f, 1.f));
                    if (ImGui::Button("Quit Engine", ImVec2(120.f, 32.f))) {
                        done = true; accepted = false;
                    }
                    ImGui::PopStyleColor(3);
                }
                ImGui::EndChild();  // End card child

                // Pop the child background color and window style vars
                ImGui::PopStyleColor(); // ChildBg
                ImGui::PopStyleVar(2);  // WindowRounding, WindowBorderSize

                ImGui::End(); // End LauncherBG fullscreen window
                ImGui::PopStyleVar(2); // WindowBorderSize, WindowPadding
                ImGui::PopStyleColor(); // WindowBg

                // ---- Rendering ----
                ImGui::Render();
                int dw, dh;
                SDL_GL_GetDrawableSize(win, &dw, &dh);
                glViewport(0, 0, dw, dh);
                glClearColor(0.05f, 0.05f, 0.06f, 1.f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                SDL_GL_SwapWindow(win);
            }

            return accepted;
        }
    private:
        static void TextProxyFontSignature(const char* label, bool bold, float scalingFactor)
        {
            float currentScale = ImGui::GetIO().FontGlobalScale;
            ImGui::GetIO().FontGlobalScale = currentScale * scalingFactor;
            if (bold) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", label);
            }
            else {
                ImGui::TextUnformatted(label);
            }
            ImGui::GetIO().FontGlobalScale = currentScale;
        }
    };
}