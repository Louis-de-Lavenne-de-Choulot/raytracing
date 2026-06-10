// ide_ai_chat.h — AI Assistant using OpenRouter API
// Integrated as a dockable inspector tab with content generation and rollback.
#pragma once

#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <imgui.h>
#include <glm/glm.hpp>

// Forward declarations
class CommandBus;

struct AIChatMessage {
    enum Role { User, Assistant, System };
    Role role;
    std::string content;
};

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

    static const char* GetWindowName() { return "AI Assistant"; }
    void ClearHistory();
    bool IsVisible() const { return visible; }
    void SetVisible(bool v) { visible = v; }
    void ToggleVisible() { visible = !visible; }

private:
    // Background worker
    void WorkerLoop();

    // Build system prompt (includes content‑generation commands)
    std::string BuildSystemPrompt() const;

    // HTTP request (WinHTTP on Windows, libcurl stubbed on other platforms)
    bool SendRequest(const std::vector<AIChatMessage>& messages, std::string& response);

    // Extract normal commands (lines starting with '>')
    std::vector<std::string> ExtractCommands(const std::string& aiText);

    // Parse advanced blocks (<vert>, <frag>, <vertexdata>, <indexdata>, <content>)
    std::string ExtractBlock(const std::string& text, const std::string& tag);
    std::vector<float>  ExtractFloatArray(const std::string& text, const std::string& tag);
    std::vector<uint32_t> ExtractUIntArray(const std::string& text, const std::string& tag);

    // Write a temporary file; returns empty string on failure
    std::string WriteTempFile(const std::string& content, const std::string& extension);

    // Content generation with rollback
    bool CreateShaderFromSource(const std::string& name,
        const std::string& vertSrc,
        const std::string& fragSrc,
        CommandBus* cmdBus);
    bool CreateCustomMeshFromData(const std::string& name,
        const std::vector<float>& vertices,
        const std::vector<uint32_t>& indices,
        const std::string& shaderName,
        const glm::vec3& position,
        CommandBus* cmdBus);
    bool WriteFileContent(const std::string& path, const std::string& content);

    // Add message to history (thread-safe)
    void AddMessage(AIChatMessage::Role role, const std::string& content);

    // JSON escaping
    static std::string JsonEscape(const std::string& s);
    static std::string GetEnvSafe(const char* name);

    // Update scene context (currently unused, kept for future)
    void UpdateSceneContext(const std::string& context) { sceneContext = context; }

private:
    std::string apiKey;
    std::string model = "openrouter/free";
    std::string lastError;
    std::string sceneContext;
    bool visible = true;

    // Conversation history
    std::vector<AIChatMessage> history;
    mutable std::mutex historyMutex;

    // Queue of user prompts
    std::deque<std::string> pendingUserMessages;
    std::mutex pendingMutex;
    std::condition_variable pendingCV;

    // AI results
    struct AIResult {
        std::string responseText;
        bool success;
        std::string error;
    };
    std::deque<AIResult> pendingResults;
    std::mutex resultMutex;

    // Commands queued for the main thread (for rollback‑sensitive operations)
    std::vector<std::string> pendingEngineCommands;
    std::mutex pendingCommandsMutex;

    // Background thread control
    std::thread workerThread;
    std::atomic<bool> stopWorker;

    // UI state
    char inputBuffer[4096] = "";
    bool autoScroll = true;
    bool waitingForAI = false;
    float lastResponseTime = 0.0f;
    bool showSettings = true;
};