#pragma once
// ide_toast.h  —  HonHon Engine IDE  —  Toast Notification System
// =============================================================================
// Features:
//   - Transient toast messages (bottom-right corner)
//   - 4 severity levels: Info, Success, Warning, Error
//   - Per-toast duration + fade-out
//   - Queue-based, stacks up to ~5 visible at once
//   - DrawToasts(dt) call once per frame after all panels
// =============================================================================

#include <string>
#include <vector>
#include <deque>
#include <imgui.h>

struct Toast {
    enum Level { Info = 0, Success, Warning, Error };

    std::string text;
    Level       level = Info;
    float       duration = 3.5f;   // seconds before fade starts
    float       timer = 0.f;    // elapsed seconds
    float       alpha = 1.f;    // for fade-out
};

struct ToastManager {
    std::deque<Toast> queue;
    static constexpr int kMaxVisible = 5;
    static constexpr float kFadeTime = 0.5f;  // fade duration in seconds

    void Push(const std::string& msg,
        Toast::Level level = Toast::Info,
        float        duration = 3.5f)
    {
        Toast t;
        t.text = msg;
        t.level = level;
        t.duration = duration;
        queue.push_back(t);
        // Keep queue from growing unbounded
        while ((int)queue.size() > kMaxVisible * 2)
            queue.pop_front();
    }

    // Call every frame after ImGui::Render() (or before — any time in the frame).
    void DrawToasts(float dt)
    {
        if (queue.empty()) return;

        ImGuiIO& io = ImGui::GetIO();

        // Advance timers, remove expired
        for (auto& t : queue) t.timer += dt;
        queue.erase(
            std::remove_if(queue.begin(), queue.end(),
                [](const Toast& t) { return t.timer >= t.duration + kFadeTime; }),
            queue.end());

        if (queue.empty()) return;

        // Only display the most recent kMaxVisible
        int start = (int)queue.size() > kMaxVisible
            ? (int)queue.size() - kMaxVisible : 0;

        const float W = 340.f;
        const float H = 36.f;
        const float PAD = 8.f;
        const float MARGIN = 16.f;
        float       baseX = io.DisplaySize.x - W - MARGIN;
        float       baseY = io.DisplaySize.y - MARGIN;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        for (int i = (int)queue.size() - 1; i >= start; --i)
        {
            Toast& t = queue[i];

            // Compute alpha
            float a = 1.f;
            if (t.timer > t.duration)
                a = 1.f - (t.timer - t.duration) / kFadeTime;
            a = (std::max)(0.f, (std::min)(1.f, a));

            // Slide-in effect: translate from +40px over 0.2 s
            float slideOffset = 0.f;
            if (t.timer < 0.2f) slideOffset = (1.f - t.timer / 0.2f) * 40.f;

            float y0 = baseY - H;
            float x0 = baseX + slideOffset;
            float x1 = x0 + W;
            float y1 = y0 + H;

            // Background color per level
            ImU32 bgCol, borderCol, iconCol;
            const char* icon;
            switch (t.level) {
            case Toast::Success:
                bgCol = IM_COL32(30, 60, 35, (int)(210 * a));
                borderCol = IM_COL32(60, 180, 80, (int)(220 * a));
                iconCol = IM_COL32(60, 200, 90, (int)(255 * a));
                icon = "\xef\x80\x8c";  // fa-check
                break;
            case Toast::Warning:
                bgCol = IM_COL32(60, 50, 20, (int)(210 * a));
                borderCol = IM_COL32(220, 170, 30, (int)(220 * a));
                iconCol = IM_COL32(240, 190, 40, (int)(255 * a));
                icon = "\xef\x81\xb1";  // fa-warning / exclamation-triangle
                break;
            case Toast::Error:
                bgCol = IM_COL32(60, 20, 20, (int)(210 * a));
                borderCol = IM_COL32(200, 60, 60, (int)(220 * a));
                iconCol = IM_COL32(240, 80, 80, (int)(255 * a));
                icon = "\xef\x81\xaa";  // fa-times-circle
                break;
            default: // Info
                bgCol = IM_COL32(20, 35, 60, (int)(210 * a));
                borderCol = IM_COL32(60, 120, 200, (int)(220 * a));
                iconCol = IM_COL32(100, 160, 240, (int)(255 * a));
                icon = "\xef\x81\x9a";  // fa-info-circle
                break;
            }

            dl->AddRectFilled({ x0, y0 }, { x1, y1 }, bgCol, 6.f);
            dl->AddRect({ x0, y0 }, { x1, y1 }, borderCol, 6.f, 0, 1.5f);

            // Icon
            dl->AddText({ x0 + PAD, y0 + (H - ImGui::GetFontSize()) * 0.5f },
                iconCol, icon);

            // Message text
            ImU32 textCol = IM_COL32(220, 225, 235, (int)(255 * a));
            // Truncate if needed
            std::string msg = t.text;
            const float maxTextW = W - PAD * 2 - 20.f;
            while (!msg.empty() &&
                ImGui::CalcTextSize(msg.c_str()).x > maxTextW)
                msg.pop_back();
            if (msg.size() < t.text.size()) msg += "\xe2\x80\xa6";  // ellipsis

            dl->AddText({ x0 + PAD + 20.f, y0 + (H - ImGui::GetFontSize()) * 0.5f },
                textCol, msg.c_str());

            // Progress bar underline (shows remaining time)
            float progress = 1.f - (std::min)(1.f, t.timer / t.duration);
            if (progress > 0.f) {
                dl->AddRectFilled({ x0 + 4.f, y1 - 3.f },
                    { x0 + 4.f + (W - 8.f) * progress, y1 - 1.f },
                    borderCol, 2.f);
            }

            baseY = y0 - PAD;
        }
    }
};