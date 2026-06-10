// ide_ai_chat.cpp — AI Assistant implementation with content generation & rollback
#include "ide_ai_chat.h"
#include "ide_global_values.h"
#include "commandBus.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <curl/curl.h>
#endif

// -----------------------------------------------------------------------------
//  Safe environment variable reader
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
//  JSON escaping (same as main.cpp's JStr)
// -----------------------------------------------------------------------------
std::string AIAssistant::JsonEscape(const std::string& s)
{
    std::string o;
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
    return o;
}

// -----------------------------------------------------------------------------
//  Constructor / Destructor
// -----------------------------------------------------------------------------
AIAssistant::AIAssistant() : stopWorker(false)
{
    model = "openrouter/free";
    apiKey = GetEnvSafe("OPENROUTER_API_KEY");
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

        try {
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
            // Safeguard – ignore
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
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingUserMessages.push_back(text);
    }
    pendingCV.notify_one();
    return true;
}

// -----------------------------------------------------------------------------
//  Update (main thread) – processes AI responses and executes commands with rollback
// -----------------------------------------------------------------------------
void AIAssistant::Update(CommandBus* cmdBus)
{
    // Process AI results
    std::deque<AIResult> results;
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        results.swap(pendingResults);
    }

    for (auto& res : results) {
        waitingForAI = false;
        lastResponseTime = static_cast<float>(ImGui::GetTime());

        if (res.success) {
            // 1. Simple line‑based commands
            auto simpleCmds = ExtractCommands(res.responseText);
            for (const auto& cmd : simpleCmds) {
                if (cmdBus) cmdBus->send(cmd);
            }

            // 2. Advanced content generation blocks (shaders, meshes, files)
            //    These are processed now, and any error will prevent registration
            //    (rollback is inherent because we only commit after successful compilation).

            // ---- SHADER CREATION ----
            std::string shaderName;
            size_t pos = 0;
            std::string responseCopy = res.responseText;
            while ((pos = responseCopy.find("> createshader", pos)) != std::string::npos) {
                size_t eol = responseCopy.find('\n', pos);
                std::string line = responseCopy.substr(pos, eol - pos);
                size_t q1 = line.find('"');
                if (q1 != std::string::npos) {
                    size_t q2 = line.find('"', q1 + 1);
                    if (q2 != std::string::npos) shaderName = line.substr(q1 + 1, q2 - q1 - 1);
                }
                std::string vertSrc = ExtractBlock(responseCopy, "vert");
                std::string fragSrc = ExtractBlock(responseCopy, "frag");
                if (!shaderName.empty() && !vertSrc.empty() && !fragSrc.empty()) {
                    CreateShaderFromSource(shaderName, vertSrc, fragSrc, cmdBus);
                }
                pos = eol;
            }

            // ---- CUSTOM MESH CREATION ----
            pos = 0;
            while ((pos = responseCopy.find("> createmesh", pos)) != std::string::npos) {
                size_t eol = responseCopy.find('\n', pos);
                std::string line = responseCopy.substr(pos, eol - pos);
                std::string meshName, shaderName;
                glm::vec3 position(0.f);
                // parse --shader "..." and --pos x y z
                std::regex shaderRegex(R"(--shader\s+\"([^\"]+)\")");
                std::regex posRegex(R"(--pos\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+))");
                std::smatch match;
                if (std::regex_search(line, match, shaderRegex)) shaderName = match[1];
                if (std::regex_search(line, match, posRegex)) {
                    position.x = std::stof(match[1]);
                    position.y = std::stof(match[2]);
                    position.z = std::stof(match[3]);
                }
                // name is the first token after "createmesh"
                std::istringstream iss(line);
                std::string token;
                iss >> token; // "createmesh"
                iss >> meshName;
                if (meshName.empty()) meshName = "GeneratedMesh";

                auto vertices = ExtractFloatArray(responseCopy, "vertexdata");
                auto indices = ExtractUIntArray(responseCopy, "indexdata");
                if (!vertices.empty() && !indices.empty()) {
                    CreateCustomMeshFromData(meshName, vertices, indices,
                        shaderName.empty() ? "default" : shaderName,
                        position, cmdBus);
                }
                pos = eol;
            }

            // ---- FILE WRITING (scripts, configs, etc.) ----
            pos = 0;
            while ((pos = responseCopy.find("> writefile", pos)) != std::string::npos) {
                size_t eol = responseCopy.find('\n', pos);
                std::string line = responseCopy.substr(pos, eol - pos);
                std::string filePath;
                size_t q1 = line.find('"');
                if (q1 != std::string::npos) {
                    size_t q2 = line.find('"', q1 + 1);
                    std::string userPath;
					if (q2 != std::string::npos) userPath = line.substr(q1 + 1, q2 - q1 - 1);
                    std::filesystem::path p(userPath);
                    if (p.is_absolute())
                        filePath = userPath;
                    else
                        filePath = (std::filesystem::path(g_projectRoot) / userPath).string();
                }
                std::string content = ExtractBlock(responseCopy, "content");
                if (!filePath.empty() && !content.empty()) {
                    WriteFileContent(filePath, content);
                }
                pos = eol;
            }
        }
        else {
            AddMessage(AIChatMessage::System, "Error: " + res.error);
        }
    }

    // Dispatch any pending engine commands (e.g., from CreateShaderFromSource)
    {
        std::lock_guard<std::mutex> lock(pendingCommandsMutex);
        for (const auto& cmd : pendingEngineCommands) {
            if (cmdBus) cmdBus->send(cmd);
        }
        pendingEngineCommands.clear();
    }
}

// -----------------------------------------------------------------------------
//  System prompt (includes content generation commands)
// -----------------------------------------------------------------------------
std::string AIAssistant::BuildSystemPrompt() const
{
    std::ostringstream prompt;

    // Chunk 1: Header and basic rules
    prompt << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    HONHON ENGINE AI ASSISTANT – SYSTEM PROMPT                ║
║                          (STRICT FORMAT – READ CAREFULLY)                     ║
╚═══════════════════════════════════════════════════════════════════════════════╝

You are an assistant that generates commands for the HonHon Engine editor.
Your output is parsed and executed immediately. Invalid commands or malformed
syntax will be silently ignored. You must follow the rules EXACTLY.

╔═══════════════════════════════════════════════════════════════════════════════╗
║                              RULE 1: COMMAND LINES                            ║
╚═══════════════════════════════════════════════════════════════════════════════╝

- Every command MUST start with the character '>' at the very beginning of a line.
- No spaces before '>'.
- The command text follows immediately after '>' and a single space.
- Do not put any other text on the same line as the '>'.
- You may include explanatory text before or after command blocks, but never on
  a line that starts with '>'.

CORRECT:
> plane Ground 0 -0.5 0 20 20 green
> addlight Sun 1.2 255 245 200 directional

INCORRECT:
plane Ground 0 -0.5 0 20 20 green   (missing '>')
> This creates a plane            (extra text after '>')
  > plane Ground ...              (space before '>')

╔═══════════════════════════════════════════════════════════════════════════════╗
║                    RULE 2: MULTI-LINE CONTENT (SHADERS, MESHES, FILES)       ║
╚═══════════════════════════════════════════════════════════════════════════════╝

For shaders, custom meshes, and file writes, use XML-style tags.
The command line must be followed immediately by the opening tag on a new line.

SHADER FORMAT:
> createshader "ShaderName"
<vert>
// GLSL vertex shader code (version 330 core)

</vert>
<frag>
// GLSL fragment shader code
</frag>

CUSTOM MESH FORMAT:
> createmesh "MeshName" --shader "ShaderName" --pos x y z
<vertexdata>
[ interleaved floats: px,py,pz, nx,ny,nz, u,v for each vertex ]
</vertexdata>
<indexdata>
[ triangle indices as flat array of uint32 ]
</indexdata>

FILE WRITE FORMAT:
> writefile "path/to/file.ext"
<content>
// file content (text or binary)
</content>

)";

    // Chunk 2: Vertex attributes and uniforms
    prompt << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    RULE 3: ENGINE VERTEX ATTRIBUTES (PRIMITIVES)             ║
╚═══════════════════════════════════════════════════════════════════════════════╝

The following vertex attributes are guaranteed for all primitive objects
(plane, sphere, rectangle, cube, etc.):

layout(location = 0) in vec3 aPos;      // position
layout(location = 1) in vec3 aNormal;   // normal
layout(location = 2) in vec4 aColor;    // per-vertex colour (rarely used)
layout(location = 3) in uvec4 aBoneIdx; // bone indices (skinning)
layout(location = 4) in vec4 aBoneWgt;  // bone weights (skinning)

IMPORTANT: Primitives have NO texture coordinates (UVs). Do not use aTexCoord.
If you need UVs, generate a custom mesh using "createmesh".

╔═══════════════════════════════════════════════════════════════════════════════╗
║                      RULE 4: ENGINE UNIFORMS (ALWAYS AVAILABLE)              ║
╚═══════════════════════════════════════════════════════════════════════════════╝

All shaders can rely on these uniforms being set automatically by the engine:

// Transform matrices
uniform mat4 uModel;               // model matrix (object to world)
uniform mat4 uView;                // view matrix (world to camera)
uniform mat4 uProj;                // projection matrix (camera to NDC)

// Lighting – directional light (sun)
uniform vec3  uSunDir;             // world-space direction towards sun
uniform vec3  uSunColor;           // RGB colour of the sun
uniform float uSunIntensity;       // intensity (0-20 typically)

// Ambient lighting
uniform vec3  uAmbientColor;       // from World Settings
uniform float uAmbientIntensity;   // 0-1

// Miscellaneous
uniform float uTime;               // seconds since engine start
uniform vec3  uCamPos;             // camera world position

// Shadow mapping
uniform mat4  uLightSpaceMatrix;   // shadow map projection * view
uniform sampler2DShadow uShadowMap;// depth comparison texture (slot 1)

Do NOT invent custom uniform names unless you also add them to the shader code.
The engine will not supply them.

)";

    // Chunk 3: Shadow helper and commands list (part 1)
    prompt << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    RULE 5: SHADOW SAMPLING HELPER (COPY THIS)                ║
╚═══════════════════════════════════════════════════════════════════════════════╝

Include this function in your fragment shader to get soft shadows:

float sampleShadow(vec4 lsPos) {
    vec3 proj = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = 0.003;
    vec2 texelSize = vec2(1.0 / 2048.0);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            vec3 coord = vec3(proj.xy + vec2(x,y) * texelSize, proj.z - bias);
            shadow += texture(uShadowMap, coord);
        }
    return shadow / 9.0;
}

╔═══════════════════════════════════════════════════════════════════════════════╗
║                         RULE 6: AVAILABLE COMMANDS (FULL LIST)               ║
╚═══════════════════════════════════════════════════════════════════════════════╝

Use these exact command formats:

── PRIMITIVE CREATION ─────────────────────────────────────────────────────────
plane <name> <x> <y> <z> [width] [height] [segmentsX] [segmentsZ] [color]
sphere <name> <x> <y> <z> [radius] [rings] [segments] [color]
rect <name> <x> <y> <z> [w] [h] [segmentsX] [segmentsY] [segmentsZ] [color]
obj <name> <filepath> [x] [y] [z]
gltf <name> <filepath> [x] [y] [z]

── LIGHTS ─────────────────────────────────────────────────────────────────────
addlight <name> <intensity> <r> <g> <b> [type]   (type: point / directional / ambient)
removelight <name>

── TRANSFORM ─────────────────────────────────────────────────────────────────
move <name> <x> <y> <z>
rotate <name> <pitch> <yaw> <roll>
scale <name> <sx> <sy> <sz>

── MATERIAL & RENDER ─────────────────────────────────────────────────────────
color <name> <r> <g> <b> [a]          (a=255 default)
visible <name> <true/false>
setshader <objectName> <shaderName>

)";

    // Chunk 4: Commands (continued)
    prompt << R"(
── SCENE MANAGEMENT ─────────────────────────────────────────────────────────
list
inspect <name>
delete <name>
clone <src> <dst>
rename <old> <new>
clearscene
savescene <path>
loadscene <path>
loadhdrskybox <filepath>

── UTILITY ───────────────────────────────────────────────────────────────────
help
echo <text...>

── CONTENT GENERATION ───────────────────────────────────────────────────────
createshader "ShaderName"   (must be followed by <vert> and <frag> blocks)
createmesh "MeshName" --shader "ShaderName" --pos x y z  (with <vertexdata> & <indexdata>)
writefile "path"            (with <content> block)

╔═══════════════════════════════════════════════════════════════════════════════╗
║                         RULE 7: COLOR NAMES (CASE-INSENSITIVE)               ║
╚═══════════════════════════════════════════════════════════════════════════════╝

white, black, red, green, blue, yellow, gray, orange, purple, cyan

You can also use RGB values: color MyObj 255 128 64

)";
    // Chunk 5: Complete examples (large block)
    prompt << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    RULE 8: COMPLETE WORKING EXAMPLES                         ║
╚═══════════════════════════════════════════════════════════════════════════════╝

EXAMPLE 1 – Create a waving water surface with a dense custom mesh:
> createshader "WaterShader"
<vert>
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vLightSpacePos;
uniform mat4 uLightSpaceMatrix;
void main() {
    vec3 pos = aPos;
    // Simple wave displacement based on X and Z
    pos.y += sin(pos.x * 1.5 + uTime) * 0.1;
    pos.y += cos(pos.z * 2.0 + uTime * 0.8) * 0.08;
    pos.y += sin((pos.x * 0.8 + pos.z * 0.6) * 2.0 + uTime * 1.2) * 0.05;
    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    vLightSpacePos = uLightSpaceMatrix * worldPos;
    gl_Position = uProj * uView * worldPos;
}
</vert>
<frag>
#version 330 core
out vec4 FragColor;
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vLightSpacePos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uAmbientColor;
uniform float uAmbientIntensity;
uniform float uTime;
uniform sampler2DShadow uShadowMap;
float sampleShadow(vec4 lsPos) {
    vec3 proj = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float bias = 0.003;
    vec2 texelSize = vec2(1.0 / 2048.0);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            vec3 coord = vec3(proj.xy + vec2(x,y) * texelSize, proj.z - bias);
            shadow += texture(uShadowMap, coord);
        }
    return shadow / 9.0;
}
void main() {
    vec3 N = normalize(vNormal);
    vec3 baseColor = vec3(0.05, 0.25, 0.55);
    vec3 highlight = vec3(0.35, 0.75, 0.95);
    float wave = sin(vWorldPos.x * 2.5 + uTime) * 0.5 + 0.5;
    wave += cos(vWorldPos.z * 3.2 + uTime * 0.9) * 0.4;
    wave = clamp(wave * 0.7, 0.2, 0.9);
    vec3 albedo = mix(baseColor, highlight, wave);
    float diff = max(dot(N, uSunDir), 0.0);
    vec3 diffuse = uSunColor * uSunIntensity * diff;
    vec3 ambient = uAmbientColor * uAmbientIntensity;
    float shadow = sampleShadow(vLightSpacePos);
    vec3 finalColor = (ambient + shadow * diffuse) * albedo;
    FragColor = vec4(finalColor, 1.0);
}
</frag>
> createmesh "WaterMesh" --shader "WaterShader" --pos 0 -0.5 0
<vertexdata>
[ -5.0,0,5.0, 0,1,0, 0,0, -4.2857,0,5.0, 0,1,0, 0.0714,0, -3.5714,0,5.0, 0,1,0, 0.1429,0, -2.8571,0,5.0, 0,1,0, 0.2143,0, -2.1429,0,5.0, 0,1,0, 0.2857,0, -1.4286,0,5.0, 0,1,0, 0.3571,0, -0.7143,0,5.0, 0,1,0, 0.4286,0, 0.0,0,5.0, 0,1,0, 0.5,0, 0.7143,0,5.0, 0,1,0, 0.5714,0, 1.4286,0,5.0, 0,1,0, 0.6429,0, 2.1429,0,5.0, 0,1,0, 0.7143,0, 2.8571,0,5.0, 0,1,0, 0.7857,0, 3.5714,0,5.0, 0,1,0, 0.8571,0, 4.2857,0,5.0, 0,1,0, 0.9286,0, 5.0,0,5.0, 0,1,0, 1,0, -5.0,0,3.5714, 0,1,0, 0,0.0714, -4.2857,0,3.5714, 0,1,0, 0.0714,0.0714, -3.5714,0,3.5714, 0,1,0, 0.1429,0.0714, -2.8571,0,3.5714, 0,1,0, 0.2143,0.0714, -2.1429,0,3.5714, 0,1,0, 0.2857,0.0714, -1.4286,0,3.5714, 0,1,0, 0.3571,0.0714, -0.7143,0,3.5714, 0,1,0, 0.4286,0.0714, 0.0,0,3.5714, 0,1,0, 0.5,0.0714, 0.7143,0,3.5714, 0,1,0, 0.5714,0.0714, 1.4286,0,3.5714, 0,1,0, 0.6429,0.0714, 2.1429,0,3.5714, 0,1,0, 0.7143,0.0714, 2.8571,0,3.5714, 0,1,0, 0.7857,0.0714, 3.5714,0,3.5714, 0,1,0, 0.8571,0.0714, 4.2857,0,3.5714, 0,1,0, 0.9286,0.0714, 5.0,0,3.5714, 0,1,0, 1,0.0714, -5.0,0,2.1429, 0,1,0, 0,0.1429, -4.2857,0,2.1429, 0,1,0, 0.0714,0.1429, -3.5714,0,2.1429, 0,1,0, 0.1429,0.1429, -2.8571,0,2.1429, 0,1,0, 0.2143,0.1429, -2.1429,0,2.1429, 0,1,0, 0.2857,0.1429, -1.4286,0,2.1429, 0,1,0, 0.3571,0.1429, -0.7143,0,2.1429, 0,1,0, 0.4286,0.1429, 0.0,0,2.1429, 0,1,0, 0.5,0.1429, 0.7143,0,2.1429, 0,1,0, 0.5714,0.1429, 1.4286,0,2.1429, 0,1,0, 0.6429,0.1429, 2.1429,0,2.1429, 0,1,0, 0.7143,0.1429, 2.8571,0,2.1429, 0,1,0, 0.7857,0.1429, 3.5714,0,2.1429, 0,1,0, 0.8571,0.1429, 4.2857,0,2.1429, 0,1,0, 0.9286,0.1429, 5.0,0,2.1429, 0,1,0, 1,0.1429, -5.0,0,0.7143, 0,1,0, 0,0.2143, -4.2857,0,0.7143, 0,1,0, 0.0714,0.2143, -3.5714,0,0.7143, 0,1,0, 0.1429,0.2143, -2.8571,0,0.7143, 0,1,0, 0.2143,0.2143, -2.1429,0,0.7143, 0,1,0, 0.2857,0.2143, -1.4286,0,0.7143, 0,1,0, 0.3571,0.2143, -0.7143,0,0.7143, 0,1,0, 0.4286,0.2143, 0.0,0,0.7143, 0,1,0, 0.5,0.2143, 0.7143,0,0.7143, 0,1,0, 0.5714,0.2143, 1.4286,0,0.7143, 0,1,0, 0.6429,0.2143, 2.1429,0,0.7143, 0,1,0, 0.7143,0.2143, 2.8571,0,0.7143, 0,1,0, 0.7857,0.2143, 3.5714,0,0.7143, 0,1,0, 0.8571,0.2143, 4.2857,0,0.7143, 0,1,0, 0.9286,0.2143, 5.0,0,0.7143, 0,1,0, 1,0.2143, -5.0,0,-0.7143, 0,1,0, 0,0.2857, -4.2857,0,-0.7143, 0,1,0, 0.0714,0.2857, -3.5714,0,-0.7143, 0,1,0, 0.1429,0.2857, -2.8571,0,-0.7143, 0,1,0, 0.2143,0.2857, -2.1429,0,-0.7143, 0,1,0, 0.2857,0.2857, -1.4286,0,-0.7143, 0,1,0, 0.3571,0.2857, -0.7143,0,-0.7143, 0,1,0, 0.4286,0.2857, 0.0,0,-0.7143, 0,1,0, 0.5,0.2857, 0.7143,0,-0.7143, 0,1,0, 0.5714,0.2857, 1.4286,0,-0.7143, 0,1,0, 0.6429,0.2857, 2.1429,0,-0.7143, 0,1,0, 0.7143,0.2857, 2.8571,0,-0.7143, 0,1,0, 0.7857,0.2857, 3.5714,0,-0.7143, 0,1,0, 0.8571,0.2857, 4.2857,0,-0.7143, 0,1,0, 0.9286,0.2857, 5.0,0,-0.7143, 0,1,0, 1,0.2857, -5.0,0,-2.1429, 0,1,0, 0,0.3571, -4.2857,0,-2.1429, 0,1,0, 0.0714,0.3571, -3.5714,0,-2.1429, 0,1,0, 0.1429,0.3571, -2.8571,0,-2.1429, 0,1,0, 0.2143,0.3571, -2.1429,0,-2.1429, 0,1,0, 0.2857,0.3571, -1.4286,0,-2.1429, 0,1,0, 0.3571,0.3571, -0.7143,0,-2.1429, 0,1,0, 0.4286,0.3571, 0.0,0,-2.1429, 0,1,0, 0.5,0.3571, 0.7143,0,-2.1429, 0,1,0, 0.5714,0.3571, 1.4286,0,-2.1429, 0,1,0, 0.6429,0.3571, 2.1429,0,-2.1429, 0,1,0, 0.7143,0.3571, 2.8571,0,-2.1429, 0,1,0, 0.7857,0.3571, 3.5714,0,-2.1429, 0,1,0, 0.8571,0.3571, 4.2857,0,-2.1429, 0,1,0, 0.9286,0.3571, 5.0,0,-2.1429, 0,1,0, 1,0.3571, -5.0,0,-3.5714, 0,1,0, 0,0.4286, -4.2857,0,-3.5714, 0,1,0, 0.0714,0.4286, -3.5714,0,-3.5714, 0,1,0, 0.1429,0.4286, -2.8571,0,-3.5714, 0,1,0, 0.2143,0.4286, -2.1429,0,-3.5714, 0,1,0, 0.2857,0.4286, -1.4286,0,-3.5714, 0,1,0, 0.3571,0.4286, -0.7143,0,-3.5714, 0,1,0, 0.4286,0.4286, 0.0,0,-3.5714, 0,1,0, 0.5,0.4286, 0.7143,0,-3.5714, 0,1,0, 0.5714,0.4286, 1.4286,0,-3.5714, 0,1,0, 0.6429,0.4286, 2.1429,0,-3.5714, 0,1,0, 0.7143,0.4286, 2.8571,0,-3.5714, 0,1,0, 0.7857,0.4286, 3.5714,0,-3.5714, 0,1,0, 0.8571,0.4286, 4.2857,0,-3.5714, 0,1,0, 0.9286,0.4286, 5.0,0,-3.5714, 0,1,0, 1,0.4286, -5.0,0,-5.0, 0,1,0, 0,0.5, -4.2857,0,-5.0, 0,1,0, 0.0714,0.5, -3.5714,0,-5.0, 0,1,0, 0.1429,0.5, -2.8571,0,-5.0, 0,1,0, 0.2143,0.5, -2.1429,0,-5.0, 0,1,0, 0.2857,0.5, -1.4286,0,-5.0, 0,1,0, 0.3571,0.5, -0.7143,0,-5.0, 0,1,0, 0.4286,0.5, 0.0,0,-5.0, 0,1,0, 0.5,0.5, 0.7143,0,-5.0, 0,1,0, 0.5714,0.5, 1.4286,0,-5.0, 0,1,0, 0.6429,0.5, 2.1429,0,-5.0, 0,1,0, 0.7143,0.5, 2.8571,0,-5.0, 0,1,0, 0.7857,0.5, 3.5714,0,-5.0, 0,1,0, 0.8571,0.5, 4.2857,0,-5.0, 0,1,0, 0.9286,0.5, 5.0,0,-5.0, 0,1,0, 1,0.5 ]
</vertexdata>
<indexdata>
[ 0,1,15, 1,2,16, 2,3,17, 3,4,18, 4,5,19, 5,6,20, 6,7,21, 7,8,22, 8,9,23, 9,10,24, 10,11,25, 11,12,26, 12,13,27, 13,14,28, 15,16,30, 16,17,31, 17,18,32, 18,19,33, 19,20,34, 20,21,35, 21,22,36, 22,23,37, 23,24,38, 24,25,39, 25,26,40, 26,27,41, 27,28,42, 28,29,43, 30,31,45, 31,32,46, 32,33,47, 33,34,48, 34,35,49, 35,36,50, 36,37,51, 37,38,52, 38,39,53, 39,40,54, 40,41,55, 41,42,56, 42,43,57, 43,44,58, 45,46,60, 46,47,61, 47,48,62, 48,49,63, 49,50,64, 50,51,65, 51,52,66, 52,53,67, 53,54,68, 54,55,69, 55,56,70, 56,57,71, 57,58,72, 58,59,73, 60,61,75, 61,62,76, 62,63,77, 63,64,78, 64,65,79, 65,66,80, 66,67,81, 67,68,82, 68,69,83, 69,70,84, 70,71,85, 71,72,86, 72,73,87, 73,74,88, 75,76,90, 76,77,91, 77,78,92, 78,79,93, 79,80,94, 80,81,95, 81,82,96, 82,83,97, 83,84,98, 84,85,99, 85,86,100, 86,87,101, 87,88,102, 88,89,103, 90,91,105, 91,92,106, 92,93,107, 93,94,108, 94,95,109, 95,96,110, 96,97,111, 97,98,112, 98,99,113, 99,100,114, 100,101,115, 101,102,116, 102,103,117, 103,104,118, 105,106,120, 106,107,121, 107,108,122, 108,109,123, 109,110,124, 110,111,125, 111,112,126, 112,113,127, 113,114,128, 114,115,129, 115,116,130, 116,117,131, 117,118,132, 118,119,133, 120,121,135, 121,122,136, 122,123,137, 123,124,138, 124,125,139, 125,126,140, 126,127,141, 127,128,142, 128,129,143, 129,130,144, 130,131,145, 131,132,146, 132,133,147, 133,134,148, 135,136,150, 136,137,151, 137,138,152, 138,139,153, 139,140,154, 140,141,155, 141,142,156, 142,143,157, 143,144,158, 144,145,159, 145,146,160, 146,147,161, 147,148,162, 148,149,163, 150,151,165, 151,152,166, 152,153,167, 153,154,168, 154,155,169, 155,156,170, 156,157,171, 157,158,172, 158,159,173, 159,160,174, 160,161,175, 161,162,176, 162,163,177, 163,164,178 ]
</indexdata>
> setshader WaterMesh WaterShader
> addlight Sun 1.2 255 245 200 directional

)";

    // Chunk 6: More examples (continue)
    prompt << R"(
EXAMPLE 2 – Create a glowing orb with a custom shader:
> createshader "GlowShader"
<vert>
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModelViewProj;
out vec3 vNormal;
void main() {
    vNormal = aNormal;
    gl_Position = uModelViewProj * vec4(aPos, 1.0);
}
</vert>
<frag>
#version 330 core
out vec4 FragColor;
in vec3 vNormal;
uniform float uTime;
void main() {
    vec3 color = vec3(0.8, 0.3, 0.6) + sin(uTime) * 0.2;
    float intensity = abs(vNormal.y) * 0.8 + 0.2;
    FragColor = vec4(color * intensity, 1.0);
}
</frag>
> sphere GlowBall 0 2 0 0.8 32 32
> setshader GlowBall GlowShader

EXAMPLE 3 – Create a simple scene with a ground plane and a point light:
> plane Ground 0 -0.5 0 15 15 green
> sphere Rock 2 0.3 3 0.5 16 16 gray
> addlight Torch 0.8 255 180 80 point
> move Torch 2 1 3

EXAMPLE 4 – Import an OBJ model:
> obj MyModel "assets/models/teapot.obj" 0 0 0
> scale MyModel 0.5 0.5 0.5
> color MyModel 200 150 100

EXAMPLE 5 – Load a glTF model with animation:
> gltf AnimatedCharacter "assets/characters/robot.glb" 0 0 0
> scale AnimatedCharacter 0.02 0.02 0.02

EXAMPLE 6 – Create a custom mesh procedurally (a simple pyramid):
> createmesh "Pyramid" --shader "default" --pos 0 0 0
<vertexdata>
[ -0.5,0,0.5, 0,0.7,0, 0,0,  0.5,0,0.5, 0,0.7,0, 0,0,  0,0.8,0, 0,0.7,0, 0,0, -0.5,0,-0.5, 0,0.7,0, 0,0,  0.5,0,-0.5, 0,0.7,0, 0,0 ]
</vertexdata>
<indexdata>
[ 0,1,2, 1,3,2, 3,4,2, 4,0,2, 0,4,1, 1,4,3 ]
</indexdata>

EXAMPLE 7 – Write a custom script:
> writefile "assets/scripts/Rotator.cs"
<content>
using UnityEngine;
public class Rotator : MonoBehaviour {
    public float speed = 30f;
    void Update() {
        transform.Rotate(Vector3.up, speed * Time.deltaTime);
    }
}
</content>

)";

    // Chunk 7: Negative examples and final rules
    prompt << R"(
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    RULE 9: WHAT NOT TO DO (NEGATIVE EXAMPLES)                ║
╚═══════════════════════════════════════════════════════════════════════════════╝

The following are FORBIDDEN and will cause your output to be ignored:

✘ Writing commands without '>' at the start.
✘ Using 'vertex' or 'frag' as keywords (use GLSL in <vert>/<frag> blocks only).
✘ Inventing custom uniforms without declaring them (e.g., uFlow, uDisplaced).
✘ Using aTexCoord on primitives (plane/sphere/rect have no UVs).
✘ Writing shader code outside <vert>/<frag> tags.
✘ Putting explanatory text on the same line as '>'.
✘ Using C++ or any non-GLSL syntax inside <vert>/<frag>.
✘ Creating a shader without the matching '> createshader' line.

╔═══════════════════════════════════════════════════════════════════════════════╗
║                              RULE 10: ROLLBACK BEHAVIOUR                      ║
╚═══════════════════════════════════════════════════════════════════════════════╝

If a shader compilation fails or a mesh import fails, the engine will NOT register
the asset. No harm is done to the scene. You can retry with corrected syntax.

╔═══════════════════════════════════════════════════════════════════════════════╗
║                                   FINAL NOTE                                  ║
╚═══════════════════════════════════════════════════════════════════════════════╝

Your response will be parsed line by line. Lines starting with '>' are commands.
XML blocks (<vert>, <frag>, <vertexdata>, <indexdata>, <content>) are collected
and processed after the command line. Any other text is ignored.

Now generate your response using ONLY the formats shown above.
)";

    if (!sceneContext.empty()) {
        prompt << "\n--- CURRENT SCENE CONTEXT (for reference) ---\n" << sceneContext << "\n";
    }
    return prompt.str();
}
// -----------------------------------------------------------------------------
//  Helper: Extract a block between tags (including newlines)
// -----------------------------------------------------------------------------
std::string AIAssistant::ExtractBlock(const std::string& text, const std::string& tag)
{
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    size_t start = text.find(open);
    if (start == std::string::npos) return "";
    start += open.length();
    size_t end = text.find(close, start);
    if (end == std::string::npos) return "";
    std::string block = text.substr(start, end - start);
    // Trim leading/trailing whitespace and newlines
    size_t l = block.find_first_not_of(" \t\n\r");
    if (l == std::string::npos) return "";
    size_t r = block.find_last_not_of(" \t\n\r");
    return block.substr(l, r - l + 1);
}

// -----------------------------------------------------------------------------
//  Extract a float array from a JSON‑like list
// -----------------------------------------------------------------------------
std::vector<float> AIAssistant::ExtractFloatArray(const std::string& text, const std::string& tag)
{
    std::string block = ExtractBlock(text, tag);
    if (block.empty()) return {};
    std::vector<float> result;
    std::regex numRegex(R"([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)");
    std::sregex_iterator it(block.begin(), block.end(), numRegex);
    std::sregex_iterator end;
    for (; it != end; ++it) {
        result.push_back(std::stof(it->str()));
    }
    return result;
}

// -----------------------------------------------------------------------------
//  Extract a uint32 array from a JSON‑like list
// -----------------------------------------------------------------------------
std::vector<uint32_t> AIAssistant::ExtractUIntArray(const std::string& text, const std::string& tag)
{
    std::string block = ExtractBlock(text, tag);
    if (block.empty()) return {};
    std::vector<uint32_t> result;
    std::regex numRegex(R"(\d+)");
    std::sregex_iterator it(block.begin(), block.end(), numRegex);
    std::sregex_iterator end;
    for (; it != end; ++it) {
        result.push_back(static_cast<uint32_t>(std::stoul(it->str())));
    }
    return result;
}

// -----------------------------------------------------------------------------
//  Write a temporary file (used for shader sources, OBJ files)
// -----------------------------------------------------------------------------
std::string AIAssistant::WriteTempFile(const std::string& content, const std::string& extension)
{
    std::string outDir;
    if (g_projectRoot.empty())
        outDir = "assets/shaders/";   // fallback
    else
        outDir = g_projectRoot + "/assets/shaders/";
    std::filesystem::create_directories(outDir);
    static int counter = 0;
    std::string filename = outDir + "ai_gen_" + std::to_string(++counter) + extension;
    std::ofstream f(filename, std::ios::binary);
    if (!f) return "";
    f.write(content.data(), content.size());
    f.close();
    return filename;
}

// -----------------------------------------------------------------------------
//  Create a shader from source: compile & register only if successful
// -----------------------------------------------------------------------------
bool AIAssistant::CreateShaderFromSource(const std::string& name,
    const std::string& vertSrc,
    const std::string& fragSrc,
    CommandBus* cmdBus)
{
    // Write temporary files
    std::string vertPath = WriteTempFile(vertSrc, ".vert");
    std::string fragPath = WriteTempFile(fragSrc, ".frag");
    if (vertPath.empty() || fragPath.empty()) {
        AddMessage(AIChatMessage::System, "Failed to write temporary shader files for: " + name);
        return false;
    }

    // Build the registration command
    std::string cmd = "registershader " + name + " \"" + vertPath + "\" \"" + fragPath + "\"";

    // We cannot directly compile here because the command bus processes asynchronously.
    // The command bus will call glCompileShader etc. If compilation fails, the engine
    // will not register the shader. That's our rollback: the shader never becomes usable.
    // We queue the command.
    {
        std::lock_guard<std::mutex> lock(pendingCommandsMutex);
        pendingEngineCommands.push_back(cmd);
    }
    AddMessage(AIChatMessage::System, "Queued shader registration: " + name);
    return true;
}

// -----------------------------------------------------------------------------
//  Create a custom mesh from vertex/index data (via OBJ + import)
// -----------------------------------------------------------------------------
bool AIAssistant::CreateCustomMeshFromData(const std::string& name,
    const std::vector<float>& vertices,
    const std::vector<uint32_t>& indices,
    const std::string& shaderName,
    const glm::vec3& position,
    CommandBus* cmdBus)
{
    // Basic validation: stride must be 8 floats (pos3, norm3, uv2)
    size_t stride = 8;
    if (vertices.size() % stride != 0) {
        AddMessage(AIChatMessage::System, "Invalid vertex data for mesh '" + name + "' – not multiple of 8 floats.");
        return false;
    }
    if (indices.size() % 3 != 0) {
        AddMessage(AIChatMessage::System, "Invalid index data for mesh '" + name + "' – not multiple of 3.");
        return false;
    }

    // Build OBJ file content
    std::ostringstream obj;
    size_t numVerts = vertices.size() / stride;
    for (size_t i = 0; i < numVerts; ++i) {
        const float* v = vertices.data() + i * stride;
        obj << "v " << v[0] << " " << v[1] << " " << v[2] << "\n";
        obj << "vn " << v[3] << " " << v[4] << " " << v[5] << "\n";
        obj << "vt " << v[6] << " " << v[7] << "\n";
    }
    for (size_t i = 0; i < indices.size(); i += 3) {
        obj << "f " << (indices[i] + 1) << "/" << (indices[i] + 1) << "/" << (indices[i] + 1)
            << " " << (indices[i + 1] + 1) << "/" << (indices[i + 1] + 1) << "/" << (indices[i + 1] + 1)
            << " " << (indices[i + 2] + 1) << "/" << (indices[i + 2] + 1) << "/" << (indices[i + 2] + 1) << "\n";
    }
    std::string objContent = obj.str();

    // Write temporary OBJ file
    std::string objPath = WriteTempFile(objContent, ".obj");
    if (objPath.empty()) {
        AddMessage(AIChatMessage::System, "Failed to write OBJ file for mesh: " + name);
        return false;
    }

    // Create object using "obj" command
    std::string cmd = "obj " + name + " \"" + objPath + "\" " +
        std::to_string(position.x) + " " +
        std::to_string(position.y) + " " +
        std::to_string(position.z);
    {
        std::lock_guard<std::mutex> lock(pendingCommandsMutex);
        pendingEngineCommands.push_back(cmd);
    }
    if (!shaderName.empty() && shaderName != "default") {
        std::string setShaderCmd = "setshader " + name + " " + shaderName;
        {
            std::lock_guard<std::mutex> lock(pendingCommandsMutex);
            pendingEngineCommands.push_back(setShaderCmd);
        }
    }
    AddMessage(AIChatMessage::System, "Queued mesh creation: " + name);
    return true;
}

// -----------------------------------------------------------------------------
//  Write any file directly to disk (no rollback – immediate)
// -----------------------------------------------------------------------------
bool AIAssistant::WriteFileContent(const std::string& path, const std::string& content)
{
    // Ensure parent directories exist
    std::filesystem::path filePath(path);
    std::filesystem::create_directories(filePath.parent_path());
    std::ofstream f(filePath, std::ios::binary);
    if (!f) {
        AddMessage(AIChatMessage::System, "Failed to write file: " + path);
        return false;
    }
    f.write(content.data(), content.size());
    f.close();
    AddMessage(AIChatMessage::System, "File written: " + path);
    return true;
}

// -----------------------------------------------------------------------------
//  Extract simple commands (lines starting with '>')
// -----------------------------------------------------------------------------
std::vector<std::string> AIAssistant::ExtractCommands(const std::string& aiText)
{
    std::vector<std::string> commands;
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
                if (!cmd.empty()) commands.push_back(cmd);
            }
        }
    }
    return commands;
}

// -----------------------------------------------------------------------------
//  Add a message to conversation history (thread-safe)
// -----------------------------------------------------------------------------
void AIAssistant::AddMessage(AIChatMessage::Role role, const std::string& content)
{
    std::lock_guard<std::mutex> lock(historyMutex);
    history.push_back({ role, content });
    while (history.size() > 80) history.erase(history.begin());
}

void AIAssistant::ClearHistory()
{
    std::lock_guard<std::mutex> lock(historyMutex);
    history.clear();
}

// -----------------------------------------------------------------------------
//  UI: Settings
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
        if (masked.length() > 8) masked = masked.substr(0, 4) + "..." + masked.substr(masked.length() - 4);
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
    if (ImGui::Button("Clear Key")) SetApiKey("");

    ImGui::SeparatorText("Model Selection");
    static const char* models[] = {
        "openrouter/free",
        "deepseek/deepseek-r1:free",
        "deepseek/deepseek-chat:free",
        "meta-llama/llama-3.3-70b-instruct:free",
        "google/gemini-2.5-flash:free",
        "qwen/qwen-2.5-72b-instruct:free"
    };
    int modelIdx = 0;
    for (int i = 0; i < 6; ++i) if (model == models[i]) { modelIdx = i; break; }
    if (ImGui::Combo("Model", &modelIdx, models, 6)) model = models[modelIdx];
    ImGui::TextDisabled("Note: 'openrouter/free' automatically rotates through free tier models.");
}

// -----------------------------------------------------------------------------
//  UI: Inspector tab (docked window)
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
    if (ImGui::SmallButton("Clear History")) ClearHistory();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::SameLine();
    if (waitingForAI) ImGui::TextDisabled("(thinking...)");
    ImGui::EndGroup();

    ImGui::Separator();

    // Chat display area
    float inputHeight = 100.0f;
    ImGui::BeginChild("ChatScroll", ImVec2(0, -inputHeight), true,
        ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_HorizontalScrollbar);

    // Keep track of message count to trigger auto-scroll
    static size_t lastMsgCount = 0;
    size_t currentMsgCount = history.size();
    bool shouldScrollToBottom = autoScroll && (currentMsgCount != lastMsgCount);
    lastMsgCount = currentMsgCount;

    {
        std::lock_guard<std::mutex> lock(historyMutex);
        for (const auto& msg : history) {
            ImGui::PushID(&msg);   // unique ID per message

            // Determine style
            ImVec4 textColor;
            const char* prefix = "";
            if (msg.role == AIChatMessage::User) {
                textColor = ImVec4(0.7f, 0.8f, 1.0f, 1.0f);
                prefix = "> ";
            }
            else if (msg.role == AIChatMessage::Assistant) {
                textColor = ImVec4(0.8f, 1.0f, 0.8f, 1.0f);
                prefix = "";
            }
            else { // System
                textColor = ImVec4(0.5f, 0.7f, 0.9f, 1.0f);
                prefix = "🔧 ";
            }

            // Prepare the full displayed text
            std::string displayText = prefix + msg.content;

            // Use an invisible button to capture right‑click and focus for Ctrl+C
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::TextWrapped("%s", displayText.c_str());
            ImGui::PopStyleColor();

            // Context menu on right‑click (anywhere in the message area)
            if (ImGui::BeginPopupContextItem(("ctx" + std::to_string((uintptr_t)&msg)).c_str())) {
                if (ImGui::MenuItem("Copy")) {
                    ImGui::SetClipboardText(msg.content.c_str());
                }
                ImGui::EndPopup();
            }

            // Also allow Ctrl+C when the item is hovered or focused
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_C) && ImGui::GetIO().KeyCtrl) {
                ImGui::SetClipboardText(msg.content.c_str());
            }

            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    // Auto‑scroll to bottom if enabled and new messages arrived
    if (shouldScrollToBottom) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild(); // ChatScroll

    ImGui::Separator();

    // Input area (unchanged)
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
            // Force auto‑scroll on next frame when the AI replies
            lastMsgCount = history.size(); // will cause scroll on next message addition
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Model: %s | Shaders/Meshes/File creation supported", model.c_str());
}

// -----------------------------------------------------------------------------
//  HTTP request (Windows: WinHTTP, other: stub – implement libcurl if needed)
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
        payload += R"(],"temperature":0.7,"max_tokens":4000})";
    }
    catch (...) {
        lastError = "Failed to construct JSON payload";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"HonHon IDE/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { lastError = "WinHttpOpen failed"; return false; }

    HINTERNET connect = WinHttpConnect(session, L"openrouter.ai", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); lastError = "WinHttpConnect failed"; return false; }

    HINTERNET request = WinHttpOpenRequest(connect, L"POST", L"/api/v1/chat/completions",
        NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        lastError = "WinHttpOpenRequest failed";
        return false;
    }

    std::string authHeader = "Authorization: Bearer " + apiKey;
    std::wstring headers = L"Content-Type: application/json\r\n" +
        std::wstring(authHeader.begin(), authHeader.end()) + L"\r\n";

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

    response.clear();
    DWORD bytesRead = 0;
    char buffer[4096];
    while (WinHttpReadData(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    // Extract "content" field
    auto findContent = [](const std::string& json) -> std::string {
        try {
            std::string search = "\"content\":\"";
            size_t pos = json.find(search);
            if (pos == std::string::npos) return "";
            pos += search.length();
            size_t end = pos;
            while (end < json.length()) {
                if (json[end] == '\\' && end + 1 < json.length()) end += 2;
                else if (json[end] == '"') break;
                else end++;
            }
            if (end >= json.length()) return "";
            std::string out = json.substr(pos, end - pos);
            // Unescape
            size_t p = 0;
            while ((p = out.find("\\n", p)) != std::string::npos) out.replace(p, 2, "\n");
            p = 0;
            while ((p = out.find("\\\"", p)) != std::string::npos) out.replace(p, 2, "\"");
            p = 0;
            while ((p = out.find("\\\\", p)) != std::string::npos) out.replace(p, 2, "\\");
            return out;
        }
        catch (...) { return ""; }
        };
    response = findContent(response);
    if (response.empty()) { lastError = "No content in API response"; return false; }
    return true;
}
#else
// Linux/macOS stub – implement with libcurl if needed
bool AIAssistant::SendRequest(const std::vector<AIChatMessage>& messages, std::string& response)
{
    response = "I'll help you create a sphere.\n> sphere MySphere 0 2 0 1.0 32 32 red";
    return true;
}
#endif