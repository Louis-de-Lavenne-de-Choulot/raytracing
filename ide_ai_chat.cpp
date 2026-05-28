#include "ide_ai_chat.h"
#include "commandBus.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cctype>
#include <ctime>
#include <exception> // Added for robust try-catch safeguards

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

// -----------------------------------------------------------------------------
//  Safe environment variable reader (inspired by ide_project_launcher.h)
// -----------------------------------------------------------------------------
std::string AIAssistant::GetEnvSafe(const char* name)
{
#if defined(_WIN32)
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name) == 0 && buf) {
        std::string s(buf);
        free(buf);
        return s;
    }
    return {};
#else
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string();
#endif
}

// -----------------------------------------------------------------------------
//  JSON string escaping (reuses pattern from main.cpp's JStr)
// -----------------------------------------------------------------------------
std::string AIAssistant::JsonEscape(const std::string& s)
{
    std::string o;
    try {
        o.reserve(s.size() + 2);
        o += '"';
        for (char c : s) {
            switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    o += buf;
                }
                else {
                    o += c;
                }
                break;
            }
        }
        o += '"';
    }
    catch (...) {
        // Safeguard: Return empty valid JSON string representation on failure
        return "\"\"";
    }
    return o;
}

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------
AIAssistant::AIAssistant() : stopWorker(false)
{
    // Updated: Default model globally configured to openrouter/free
    model = "openrouter/free";

    // Read API key from environment variable (safe method)
    apiKey = GetEnvSafe("OPENROUTER_API_KEY");

    // Start background worker thread
    workerThread = std::thread(&AIAssistant::WorkerLoop, this);
}

AIAssistant::~AIAssistant()
{
    stopWorker = true;
    pendingCV.notify_all();
    if (workerThread.joinable())
        workerThread.join();
}

void AIAssistant::SetApiKey(const std::string& key)
{
    apiKey = key;
}

// -----------------------------------------------------------------------------
//  Background worker thread
// -----------------------------------------------------------------------------
void AIAssistant::WorkerLoop()
{
    while (!stopWorker) {
        std::string userText;
        {
            std::unique_lock<std::mutex> lock(pendingMutex);
            pendingCV.wait(lock, [this] {
                return !pendingUserMessages.empty() || stopWorker;
                });
            if (stopWorker) break;
            userText = std::move(pendingUserMessages.front());
            pendingUserMessages.pop_front();
        }

        // Safeguard: Wrap the entire loop cycle execution to shield against runtime thread termination
        try {
            // Build full conversation: system prompt + history + user message
            std::vector<AIChatMessage> fullConversation;

            AIChatMessage sysMsg;
            sysMsg.role = AIChatMessage::System;
            sysMsg.content = BuildSystemPrompt();
            fullConversation.push_back(sysMsg);

            {
                std::lock_guard<std::mutex> lock(historyMutex);
                fullConversation.insert(fullConversation.end(), history.begin(), history.end());
            }

            AIChatMessage userMsg;
            userMsg.role = AIChatMessage::User;
            userMsg.content = userText;
            fullConversation.push_back(userMsg);

            std::string aiResponse;
            bool success = SendRequest(fullConversation, aiResponse);

            AIResult result;
            result.success = success;
            if (success) {
                result.responseText = aiResponse;
                AddMessage(AIChatMessage::User, userText);
                AddMessage(AIChatMessage::Assistant, aiResponse);
            }
            else {
                result.error = lastError;
            }

            {
                std::lock_guard<std::mutex> lock(resultMutex);
                pendingResults.push_back(result);
            }
        }
        catch (...) {
            // Safeguard: Silently ignore worker loop mutation errors to ensure engine doesn't crash
        }
    }
}

bool AIAssistant::SendUserMessage(const std::string& text)
{
    if (text.empty()) return false;
    if (apiKey.empty()) {
        lastError = "No API key. Set OPENROUTER_API_KEY environment variable or enter in Settings.";
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingUserMessages.push_back(text);
    }
    catch (...) {
        return false; // Safeguard: Ignore mutex allocation errors gracefully
    }
    pendingCV.notify_one();
    return true;
}

void AIAssistant::Update(CommandBus* cmdBus)
{
    // Process pending AI responses on main thread
    while (true) {
        AIResult res;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (pendingResults.empty()) break;
            res = std::move(pendingResults.front());
            pendingResults.pop_front();
        }

        waitingForAI = false;
        lastResponseTime = static_cast<float>(ImGui::GetTime());

        try {
            if (res.success) {
                auto commands = ExtractCommands(res.responseText);
                for (const auto& cmd : commands) {
                    if (cmdBus) cmdBus->send(cmd);
                }
            }
            else if (!res.error.empty()) {
                // Add error as system message
                AddMessage(AIChatMessage::System, "Error: " + res.error);
            }
        }
        catch (...) {
            // Safeguard: Command processing failure must never stall the application thread loop
        }
    }
}

// -----------------------------------------------------------------------------
//  HTTP request using WinHTTP (Windows, no extra dependencies)
// -----------------------------------------------------------------------------
#ifdef _WIN32
bool AIAssistant::SendRequest(const std::vector<AIChatMessage>& messages, std::string& response)
{
    if (apiKey.empty()) {
        lastError = "No API key set";
        return false;
    }

    std::string payload;
    try {
        // Build JSON payload safely
        payload = R"({"model":")" + model + R"(","messages":[)";
        for (const auto& msg : messages) {
            std::string roleStr;
            switch (msg.role) {
            case AIChatMessage::User:      roleStr = "user"; break;
            case AIChatMessage::Assistant: roleStr = "assistant"; break;
            case AIChatMessage::System:    roleStr = "system"; break;
            }
            payload += R"({"role":")" + roleStr + R"(","content":)" + JsonEscape(msg.content) + "},";
        }
        if (payload.back() == ',') payload.pop_back();
        payload += R"(],"temperature":0.7,"max_tokens":2000})";
    }
    catch (...) {
        lastError = "Failed to construct json payload safely";
        return false;
    }

    // WinHTTP setup
    HINTERNET session = WinHttpOpen(L"HonHon IDE/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        lastError = "WinHttpOpen failed";
        return false;
    }

    HINTERNET connect = WinHttpConnect(session, L"openrouter.ai", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        lastError = "WinHttpConnect failed";
        return false;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/api/v1/chat/completions",
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        lastError = "WinHttpOpenRequest failed";
        return false;
    }

    // Headers
    std::string authHeader = "Authorization: Bearer " + apiKey;
    std::wstring headers = L"Content-Type: application/json\r\n" +
        std::wstring(authHeader.begin(), authHeader.end()) + L"\r\n";

    // Send request
    BOOL sent = WinHttpSendRequest(request, headers.c_str(), (DWORD)headers.length(),
        (LPVOID)payload.c_str(), (DWORD)payload.length(),
        (DWORD)payload.length(), 0);
    if (!sent) {
        lastError = "WinHttpSendRequest failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, NULL)) {
        lastError = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    // Read response
    try {
        response.clear();
        DWORD bytesRead = 0;
        char buffer[4096];
        while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.append(buffer, bytesRead);
        }
    }
    catch (...) {
        lastError = "WinHttpReadData raised exception";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    // Parse JSON response for "content" field safely
    auto findContent = [&](const std::string& json) -> std::string {
        // Encase manual string index parsers to completely bypass out_of_range crashes
        try {
            std::string search = "\"content\":\"";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";
            pos += search.length();
            size_t end = pos;
            while (end < json.length()) {
                if (json[end] == '\\' && end + 1 < json.length()) {
                    end += 2;
                    continue;
                }
                if (json[end] == '"') break;
                end++;
            }
            if (end >= json.length()) return "";

            std::string out = json.substr(pos, end - pos);
            // Unescape JSON
            size_t p = 0;
            while ((p = out.find("\\n", p)) != std::string::npos) {
                out.replace(p, 2, "\n");
                p++;
            }
            p = 0;
            while ((p = out.find("\\\"", p)) != std::string::npos) {
                out.replace(p, 2, "\"");
                p++;
            }
            p = 0;
            while ((p = out.find("\\\\", p)) != std::string::npos) {
                out.replace(p, 2, "\\");
                p++;
            }
            return out;
        }
        catch (...) {
            return ""; // Catch parsing exceptions due to unexpected/truncated payloads
        }
        };

    response = findContent(response);
    if (response.empty()) {
        lastError = "No content in API response";
        return false;
    }

    return true;
}
#else
// Linux/macOS version with libcurl (stub - implement similarly)
bool AIAssistant::SendRequest(const std::vector<AIChatMessage>& messages, std::string& response)
{
    response = "I'll help you create a sphere.\n> sphere MySphere 0 2 0 1.0 32 32 red";
    return true;
}
#endif

// -----------------------------------------------------------------------------
//  Extract commands from AI response
// -----------------------------------------------------------------------------
std::vector<std::string> AIAssistant::ExtractCommands(const std::string& aiText)
{
    std::vector<std::string> commands;
    try {
        std::istringstream iss(aiText);
        std::string line;

        while (std::getline(iss, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;

            if (line[start] == '>') {
                std::string cmd = line.substr(start + 1);
                start = cmd.find_first_not_of(" \t");
                if (start != std::string::npos) {
                    cmd = cmd.substr(start);
                    if (!cmd.empty()) {
                        commands.push_back(cmd);
                    }
                }
            }
        }
    }
    catch (...) {
        // Safeguard: Silently handle processing/extraction string allocations
    }
    return commands;
}

// -----------------------------------------------------------------------------
//  System prompt with command documentation
// -----------------------------------------------------------------------------
std::string AIAssistant::BuildSystemPrompt() const
{
    std::ostringstream prompt;
    prompt << "You are a command generator for the HonHon Engine editor. "
        "Your only job: output HonHon console commands.\n\n"

        "--- STRICT RULES (FOLLOW EXACTLY) ---\n"
        "1. Each command MUST be on a separate line starting with '>'.\n"
        "2. Every command line MUST contain ONLY the command (no extra text).\n"
        "3. You may include natural language explanation before/after command blocks.\n"
        "4. NEVER add extra words after '>'. The line after '>' must be a valid command.\n"
        "5. Commands run immediately. The engine already knows what's in the scene.\n\n"

        "--- COMPLETE COMMAND REFERENCE (use these exact formats) ---\n"
        "object <name> <x> <y> <z> [width] [height] [color] - creates a rectangle\n"
        "  REQUIRED: name, x, y, z. DEFAULTS: width=5.0, height=5.0, color=white\n"
        "  EXAMPLE: object MyRect 0 1 0 3 4 red\n"

        "plane <name> <x> <y> <z> [width] [height] [color]\n"
        "  DEFAULTS: width=5.0, height=5.0, color=white\n"
        "  EXAMPLE: plane Ground 0 -0.5 0 20 20 green\n"

        "sphere <name> <x> <y> <z> [radius] [rings] [segments] [color]\n"
        "  DEFAULTS: radius=0.5, rings=20, segments=20, color=white\n"
        "  EXAMPLE: sphere Ball 2 1 3 0.8 32 32 red\n"

        "addlight <name> <intensity> <r> <g> <b> [type]\n"
        "  r,g,b: 0-255. type: 'point', 'directional', 'ambient' (default: point)\n"
        "  EXAMPLE: addlight Sun 1.5 255 200 100 directional\n"

        "removelight <name>\n"
        "delete <name>\n"
        "clearscene\n"
        "list\n"
        "inspect <name>\n"
        "move <name> <x> <y> <z>\n"
        "rotate <name> <pitch> <yaw> <roll>\n"
        "scale <name> <sx> <sy> <sz>\n"
        "color <name> <r> <g> <b> [a]        (a=255 default)\n"
        "visible <name> <true/false>\n"
        "clone <src> <dst>\n"
        "rename <old> <new>\n"
        "savescene <path>\n"
        "loadscene <path>\n"
        "obj <name> <filepath> [x] [y] [z]\n"
        "gltf <name> <filepath> [x] [y] [z]\n"
        "loadhdrskybox <filepath>\n\n"

        "--- COLOR NAMES (case-insensitive) ---\n"
        "white, black, red, green, blue, yellow, gray, orange, purple, cyan\n\n"

        "--- FEW-SHOT EXAMPLES (learn from these) ---\n\n"

        "User: Create a red sphere at the origin\n"
        "Assistant: "
        "> sphere RedSphere 0 0 0 1 20 20 red\n\n"

        "User: Add a directional light and a blue plane as the ground\n"
        "Assistant: "
        "> addlight Sun 1.2 255 245 200 directional\n"
        "> plane Ground 0 -0.5 0 20 20 blue\n\n"

        "User: Make a green rectangle 2 meters wide by 3 meters tall at X=5, Y=0, Z=2\n"
        "Assistant: "
        "> rect GreenPlate 5 0 2 2 3 green\n\n"

        "User: List everything in the scene\n"
        "Assistant: "
        "> list\n\n"

        "User: Change the cube's color to orange\n"
        "Assistant: "
        "> color Cube 255 100 0 255\n\n"

        "--- CRITICAL REMINDER ---\n"
        "Always put commands on their own lines starting with '>'. "
        "The line after '>' must be a valid command with no extra characters.\n\n";

    if (!sceneContext.empty()) {
        prompt << "--- CURRENT SCENE (use 'list' to refresh) ---\n" << sceneContext << "\n\n";
    }

    return prompt.str();
}

void AIAssistant::AddMessage(AIChatMessage::Role role, const std::string& content)
{
    try {
        std::lock_guard<std::mutex> lock(historyMutex);
        history.push_back({ role, content });
        while (history.size() > 80) {
            history.erase(history.begin());
        }
    }
    catch (...) {
        // Safeguard against lock acquisition failures or out-of-memory states
    }
}

void AIAssistant::ClearHistory()
{
    try {
        std::lock_guard<std::mutex> lock(historyMutex);
        history.clear();
    }
    catch (...) {}
}

// -----------------------------------------------------------------------------
//  Settings UI
// -----------------------------------------------------------------------------
void AIAssistant::DrawSettingsUI()
{
    ImGui::SeparatorText("OpenRouter API Settings");

    if (apiKey.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
        ImGui::TextWrapped("⚠ No API key configured");
        ImGui::PopStyleColor();
        ImGui::TextDisabled("Get a free key from: openrouter.ai/keys");
        ImGui::TextDisabled("Or set OPENROUTER_API_KEY environment variable.");
    }
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::TextWrapped("✓ API key configured");
        ImGui::PopStyleColor();
        std::string masked = apiKey;
        if (masked.length() > 8) {
            masked = masked.substr(0, 4) + "..." + masked.substr(masked.length() - 4);
        }
        ImGui::TextDisabled("Key: %s", masked.c_str());
    }

    static char keyBuffer[256] = "";
    ImGui::InputTextWithHint("API Key", "Enter your OpenRouter API key...",
        keyBuffer, sizeof(keyBuffer), ImGuiInputTextFlags_Password);

    if (ImGui::Button("Set Key") && keyBuffer[0] != '\0') {
        SetApiKey(keyBuffer);
        keyBuffer[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Key")) {
        SetApiKey("");
    }

    ImGui::SeparatorText("Model Selection");

    // Updated: Replaced legacy items with OpenRouter's primary free models
    static const char* models[] = {
        "openrouter/free",
        "deepseek/deepseek-r1:free",
        "deepseek/deepseek-chat:free",
        "meta-llama/llama-3.3-70b-instruct:free",
        "google/gemini-2.5-flash:free",
        "qwen/qwen-2.5-72b-instruct:free"
    };

    int modelIdx = 0;
    for (int i = 0; i < 6; i++) {
        if (model == models[i]) { modelIdx = i; break; }
    }
    if (ImGui::Combo("Model", &modelIdx, models, 6)) {
        model = models[modelIdx];
    }

    ImGui::TextDisabled("Note: 'openrouter/free' automatically rotates through available free tier models.");
}

// -----------------------------------------------------------------------------
//  Inspector Tab UI (embeddable, dockable)
// -----------------------------------------------------------------------------
void AIAssistant::DrawInspectorTab()
{
    if (ImGui::CollapsingHeader("AI Assistant Settings", &showSettings, ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawSettingsUI();
        ImGui::Separator();
    }

    if (apiKey.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
        ImGui::TextWrapped("⚠ No OpenRouter API key configured.");
        ImGui::TextWrapped("   Enter your key in the Settings section above.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::BeginGroup();
    if (ImGui::SmallButton("Clear History")) {
        ClearHistory();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    if (waitingForAI) {
        ImGui::TextDisabled("(thinking...)");
    }
    ImGui::EndGroup();

    ImGui::Separator();

    float inputHeight = 100.0f;
    ImGui::BeginChild("ChatScroll", ImVec2(0, -inputHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
    {
        std::lock_guard<std::mutex> lock(historyMutex);
        for (const auto& msg : history) {
            if (msg.role == AIChatMessage::User) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));
                ImGui::TextUnformatted(("> " + msg.content).c_str());
                ImGui::PopStyleColor();
            }
            else if (msg.role == AIChatMessage::Assistant) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 1.0f, 0.8f, 1.0f));
                std::string display = msg.content;
                if (display.length() > 3000) {
                    display = display.substr(0, 3000) + "...";
                }

                // --- Make AI answer selectable and copyable ---
                // Use a unique ID for each message (pointer or index)
                ImGui::PushID(&msg);   // good enough unique ID
                bool selected = false;
                ImGui::Selectable(display.c_str(), &selected,
                    ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    ImGui::SetClipboardText(msg.content.c_str());
                    // Optional: show a small toast/notification
                }
                // Also allow Ctrl+C when the item is focused/selected
                if (selected && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl) {
                    ImGui::SetClipboardText(msg.content.c_str());
                }
                ImGui::PopID();

                ImGui::PopStyleColor();
            }
            else if (msg.role == AIChatMessage::System) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
                ImGui::TextWrapped("🔧 %s", msg.content.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }

        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 50.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    ImGui::PushItemWidth(-1);
    bool enterPressed = ImGui::InputTextMultiline("##aiinput", inputBuffer, sizeof(inputBuffer),
        ImVec2(-1, 70),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    ImGui::PopItemWidth();

    ImGui::BeginGroup();
    bool sendClicked = ImGui::Button("Send Message", ImVec2(100, 32));
    ImGui::SameLine();
    ImGui::TextDisabled("  Tip: Press Enter to send, Ctrl+Enter for new line");
    ImGui::EndGroup();

    if ((enterPressed && !ImGui::GetIO().KeyCtrl) || sendClicked) {
        if (inputBuffer[0] != '\0') {
            std::string userMsg = inputBuffer;
            inputBuffer[0] = '\0';
            SendUserMessage(userMsg);
            waitingForAI = true;
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Model: %s | Commands start with '>'", model.c_str());
}