#pragma once
// ide_ai_chat.h — AI Assistant using OpenRouter API
// Integrated as a dockable inspector tab

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <imgui.h>

// Forward declarations
class CommandBus;

struct AIChatMessage {
    enum Role { User, Assistant, System };
    Role role;
    std::string content;
};

// Stores conversation history and manages async requests
class AIAssistant {
public:
    AIAssistant();
    ~AIAssistant();

    // API key management
    void SetApiKey(const std::string& key);
    std::string GetApiKey() const { return apiKey; }
    bool IsConfigured() const { return !apiKey.empty(); }
    void ClearApiKey() { apiKey.clear(); }

    // User sends a message; returns true if request queued
    bool SendUserMessage(const std::string& text);

    // Called every frame to process incoming AI responses and execute commands
    void Update(CommandBus* cmdBus);

    // Draw the embeddable inspector tab (no popup)
    void DrawInspectorTab();

    // Draw settings UI (for preferences window)
    void DrawSettingsUI();

    // Get the window name for docking
    static const char* GetWindowName() { return "AI Assistant"; }

    // Clear conversation history
    void ClearHistory();

    // Toggle visibility
    bool IsVisible() const { return visible; }
    void SetVisible(bool v) { visible = v; }
    void ToggleVisible() { visible = !visible; }

private:
    // Background thread worker
    void WorkerLoop();

    // Build system prompt with command list
    std::string BuildSystemPrompt() const;

    // Perform HTTP POST to OpenRouter (Windows WinHTTP, no extra libs)
    bool SendRequest(const std::vector<AIChatMessage>& messages, std::string& response);

    // Extract commands from AI response (lines starting with '>')
    std::vector<std::string> ExtractCommands(const std::string& aiText);

    // Add a message to history (thread-safe)
    void AddMessage(AIChatMessage::Role role, const std::string& content);

    // JSON escape helper
    static std::string JsonEscape(const std::string& s);

    // Safe environment variable reading (Windows + POSIX)
    static std::string GetEnvSafe(const char* name);

    // Update the scene context string
    void UpdateSceneContext(const std::string& context) { sceneContext = context; }

private:
    std::string apiKey;
    std::string model = "openrouter/auto";
    std::string lastError;
    std::string sceneContext;  // Current scene info for context
    bool visible = true;

    // Conversation history (shared)
    std::vector<AIChatMessage> history;
    mutable std::mutex historyMutex;

    // Queue of user prompts waiting to be sent
    std::deque<std::string> pendingUserMessages;
    std::mutex pendingMutex;
    std::condition_variable pendingCV;

    // Results from AI (to be processed on main thread)
    struct AIResult {
        std::string responseText;
        bool success;
        std::string error;
    };
    std::deque<AIResult> pendingResults;
    std::mutex resultMutex;

    // Background thread
    std::thread workerThread;
    std::atomic<bool> stopWorker;

    // UI state
    char inputBuffer[4096] = "";
    bool autoScroll = true;
    bool waitingForAI = false;
    float lastResponseTime = 0.0f;
    bool showSettings = true;  // Settings section expanded by default
};