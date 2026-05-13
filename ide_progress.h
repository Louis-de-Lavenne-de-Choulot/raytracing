#pragma once
// ide_progress.h  —  HonHon Engine IDE  —  Progress bar for long operations
// =============================================================================

#include <string>
#include <atomic>
#include <imgui.h>

struct ProgressState {
    std::atomic<bool> active{ false };
    std::atomic<float> progress{ 0.0f };
    std::string message;
    std::mutex mutex;

    void Start(const std::string& msg, float initialProgress = 0.0f) {
        active = true;
        progress = initialProgress;
        {
            std::lock_guard<std::mutex> lock(mutex);
            message = msg;
        }
    }

    void Update(float p, const std::string& msg = "") {
        progress = p;
        if (!msg.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            message = msg;
        }
    }

    void Finish() {
        active = false;
        progress = 0.0f;
    }

    bool IsActive() const { return active; }

    void Draw(float dt, bool modal = false) {
        if (!active) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImVec2 size = { 400, 80 };
        ImVec2 pos = { center.x - size.x * 0.5f, center.y - size.y * 0.5f };

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;
        if (modal) flags |= ImGuiWindowFlags_Modal;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 16.f, 16.f });
        ImGui::Begin("##ProgressOverlay", nullptr, flags);

        {
            std::lock_guard<std::mutex> lock(mutex);
            ImGui::TextUnformatted(message.c_str());
        }
        ImGui::Spacing();
        float p = progress.load();
        ImGui::ProgressBar(p, { -1, 0 }, std::to_string((int)(p * 100)).c_str());

        ImGui::End();
        ImGui::PopStyleVar(2);
    }
};