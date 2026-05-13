#pragma once
// ide_undo_history.h  —  HonHon Engine IDE  —  Selection history & component clipboard
// =============================================================================
// Covers:
//   - Navigation history (back/forward)
//   - Component clipboard (copy/paste transform, material, light)
// =============================================================================

#include <string>
#include <vector>
#include <functional>
#include <imgui.h>
#include "ide_icons.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Selection history manager
// ─────────────────────────────────────────────────────────────────────────────
struct SelectionHistory {
    std::vector<std::string> history;
    int currentPos = -1;
    int maxSize = 100;

    void Push(const std::string& selection) {
        if (selection.empty()) return;
        if (currentPos >= 0 && currentPos < (int)history.size() &&
            history[currentPos] == selection) return;

        // Remove forward history if we're not at the end
        if (currentPos + 1 < (int)history.size()) {
            history.erase(history.begin() + currentPos + 1, history.end());
        }
        history.push_back(selection);
        if ((int)history.size() > maxSize) {
            history.erase(history.begin());
        }
        else {
            ++currentPos;
        }
    }

    std::string Back() {
        if (currentPos > 0) {
            --currentPos;
            return history[currentPos];
        }
        return "";
    }

    std::string Forward() {
        if (currentPos + 1 < (int)history.size()) {
            ++currentPos;
            return history[currentPos];
        }
        return "";
    }

    bool CanGoBack() const { return currentPos > 0; }
    bool CanGoForward() const { return currentPos + 1 < (int)history.size(); }

    void Clear() {
        history.clear();
        currentPos = -1;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Component clipboard (stored in EditorSettings)
// ─────────────────────────────────────────────────────────────────────────────
// The actual struct is defined in ide_editor_settings.h as EditorSettings::ComponentClipboard
// This helper provides UI drawing functions.

inline void DrawCopyButton(const char* label, const char* componentType,
    const float* pos, const float* scale, const float* rot,
    const float* color, float intensity, const std::string& lightType,
    const std::string& shader,
    std::function<void(const EditorSettings::ComponentClipboard&)> onCopy)
{
    ImGui::SameLine();
    if (ImGui::SmallButton(("\xc2\xb7\xc2\xb7\xc2\xb7 " ICON_FA_COPY "##copy_" + std::string(label)).c_str())) {
        EditorSettings::ComponentClipboard clip;
        clip.componentType = componentType;
        if (pos) { clip.pos[0] = pos[0]; clip.pos[1] = pos[1]; clip.pos[2] = pos[2]; }
        if (scale) { clip.scale[0] = scale[0]; clip.scale[1] = scale[1]; clip.scale[2] = scale[2]; }
        if (rot) { clip.rot[0] = rot[0]; clip.rot[1] = rot[1]; clip.rot[2] = rot[2]; }
        if (color) { clip.color[0] = color[0]; clip.color[1] = color[1]; clip.color[2] = color[2]; clip.color[3] = color[3]; }
        clip.intensity = intensity;
        clip.lightType = lightType;
        clip.shader = shader;
        onCopy(clip);
    }
}

inline void DrawPasteButtonIfMatches(const char* label, const char* expectedType,
    const EditorSettings::ComponentClipboard& clip,
    std::function<void(const EditorSettings::ComponentClipboard&)> onPaste)
{
    if (clip.componentType == expectedType && !clip.componentType.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(("Paste##paste_" + std::string(label)).c_str())) {
            onPaste(clip);
        }
    }
}