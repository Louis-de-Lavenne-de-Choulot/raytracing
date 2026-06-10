// main.cpp  —  HonHon Engine IDE (version corrigée, éclairage fiable)
// =============================================================================
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <queue>
#include <deque>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <cstring>
#include <cstdio>
#include <shared_mutex>
#include <cctype>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
//included from path since vcpkg cannot find it
#include "imgui_impl_sdl2.h"
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>    

#include "GPURenderer.h"
#include "sceneManager.h"
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include "directionallight.h"
#include "pointlight.h"
#include "plane.h"
#include "sphere.h"
#include "rectangle.h"
#include "importer.h"
#include "settings.h"
#include "basicmovements.h"
#include "textureManager.h"
#include "animation.h"
#include "material.h"
#include "skinnedShader.h"
#include "script_manager.h"
#include "gizmo.h"
#include "commandBus.h"
#include "rigidbody.h"
#include "collisiontrigger.h"

// ── Project & Asset Management ────────────────────────────────────────────────
#include "ide_global_values.h"
#include "ide_asset_database.h"
#include "ide_asset_browser.h"
#include "ide_package_manager.h"
#include "ide_theme.h"
#include "ide_global_search.h"
#include "ide_toast.h"
#include "ide_editor_settings.h"
#include "ide_undo_history.h"
#include "ide_layout.h"
#include "ide_progress.h"
#include "ide_project.h"
#include "ide_icons.h"
#include "ide_selection.h"
#include "ide_viewport_overlays.h"
#include "ide_camera_bookmarks.h"
#include "ide_ship.h"
#include "scene_io.h"
#include "ide_project_launcher.h"
#include "ide_ai_chat.h"

std::string g_projectRoot;   // e.g. "C:/Users/.../Documents/HonHengine/MyGame"
std::string g_projectName;   // e.g. "MyGame"

using namespace HonHengine;

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Hierarchy drag & drop — lets users drag objects/lights from the Hierarchy
//  panel and drop them onto the Viewport to reposition them.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* kHierarchyDragPayload = "HONHON_HIERARCHY_ITEM";

struct HierarchyDragPayload {
    char name[128] = {};  // scene object / light name
    bool isLight = false; // true = BaseLight, false = BaseObject
};


struct IDEObject {
    std::string name; float px = 0, py = 0, pz = 0; float sx = 1, sy = 1, sz = 1; float rw = 1, rx = 0, ry = 0, rz = 0; bool visible = true; bool locked = false; std::string shader; std::string tag; int cr = 255, cg = 255, cb = 255, ca = 255;
    std::string type;      // "sphere", "plane", "rect", "obj", "gltf"
    std::string assetPath; // For "obj"/"gltf" types — path to the mesh asset

    BaseObject toBaseObject() const {
        BaseObject obj;
        obj.transform.position = Vector3(px, py, pz);
        obj.transform.scale = Vector3(sx, sy, sz);
        obj.transform.rotation = Quaternion(rw, rx, ry, rz);
        obj.visible = visible;
        if (!shader.empty()) obj.render.shaderName = shader;
        if (!tag.empty()) obj.tag = tag;
        obj.material = new Material(0, 0, Color((double)cr, (double)cg, (double)cb, (double)ca));
        return obj;
    }
};
struct IDELight {
    std::string name; std::string lightType = "point"; float intensity = 1.f; int r = 255, g = 255, b = 255; float px = 0, py = 0, pz = 0; float rotx = 0, roty = 0, rotz = 0;

    BaseLight* toBaseLight() const {
        BaseLight* lt = nullptr;
        if (lightType == "directional") {
            auto* dl = new DirectionalLight();
            dl->transform.rotation = Quaternion::FromEuler(rotx, roty, rotz);
            lt = dl;
        }
        else {
            auto* pl = new PointLight();
            pl->transform.position = Vector3(px, py, pz);
            lt = pl;
        }
        lt->intensity = intensity;
        lt->color = Color((double)r, (double)g, (double)b);
        return lt;
    }
};


// =============================================================================
//  Name Validation & Auto-Sanitization (creates valid names automatically)
// =============================================================================
namespace NameValidator {
    // Reserved names that cannot be used
    static const std::unordered_set<std::string> ReservedNames = {
        "scene", "camera", "light", "object", "root", "none", "null", "undefined",
        "main", "editor", "game", "world", "level", "player", "system"
    };

    // Check if a name is valid (for internal use)
    static bool IsValidInternal(const std::string& n) {
        if (n.empty()) return false;
        if (!std::isalpha(static_cast<unsigned char>(n[0])) && n[0] != '_') return false;
        for (char c : n) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                return false;
        }
        std::string lower = n;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (ReservedNames.count(lower)) return false;
        return true;
    }

    // Sanitize a name (always returns a valid name)
    std::string Sanitize(const std::string& n) {
        if (n.empty()) return "Object";
        std::string result;
        result.reserve(n.size());
        // First character: must be letter or underscore
        if (n.size() > 0) {
            if (std::isalpha(static_cast<unsigned char>(n[0])) || n[0] == '_')
                result += n[0];
            else
                result += '_';
        }
        // Remaining characters: only alnum, _, -
        for (size_t i = 1; i < n.size(); ++i) {
            char c = n[i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
                result += c;
            else if (c == ' ' || c == '\t')
                result += '_';
            // Skip other invalid chars (don't add anything)
        }
        if (result.empty()) result = "Object";

        // Check against reserved names
        std::string lowerResult = result;
        std::transform(lowerResult.begin(), lowerResult.end(), lowerResult.begin(), ::tolower);
        if (ReservedNames.count(lowerResult))
            result = "_" + result;

        return result;
    }

    // Generate a unique name (automatically handles duplicates)
    std::string MakeUnique(const std::string& base,
        const std::vector<IDEObject>& objects,
        const std::vector<IDELight>& lights) {
        std::string candidate = Sanitize(base);
        if (candidate.empty()) candidate = "Object";

        // Check if name exists in either vector
        auto nameExists = [&](const std::string& name) -> bool {
            for (const auto& obj : objects) {
                if (obj.name == name) return true;
            }
            for (const auto& lt : lights) {
                if (lt.name == name) return true;
            }
            return false;
            };

        if (!nameExists(candidate)) return candidate;

        int counter = 1;
        while (true) {
            std::string attempt = candidate + "_" + std::to_string(counter);
            if (!nameExists(attempt)) return attempt;
            ++counter;
        }
    }

    std::string MakeUnique(const std::string& base,
        const std::unordered_map<std::string, BaseObject*>& objects,
        const std::unordered_map<std::string, BaseLight*>& lights) {
        std::string candidate = Sanitize(base);
        if (candidate.empty()) candidate = "Object";

        // Check if name exists in either map
        auto nameExists = [&](const std::string& name) -> bool {
            return objects.find(name) != objects.end() ||
                lights.find(name) != lights.end();
            };

        if (!nameExists(candidate)) return candidate;

        int counter = 1;
        while (true) {
            std::string attempt = candidate + "_" + std::to_string(counter);
            if (!nameExists(attempt)) return attempt;
            ++counter;
        }
    }

    // Get final name (sanitized + unique) - one call does it all
    std::string GetFinalName(const std::string& requested,
        const std::unordered_map<std::string, BaseObject*>& objects,
        const std::unordered_map<std::string, BaseLight*>& lights) {
        return MakeUnique(Sanitize(requested), objects, lights);
    }

    // Get final name (sanitized + unique) - one call does it all
    std::string GetFinalName(const std::string& requested,
        const std::vector<IDEObject>& objects,
        const std::vector<IDELight>& lights) {
        return MakeUnique(Sanitize(requested), objects, lights);
    }
}

// =============================================================================
//  Mutex global protégeant les listes d'objets et de lumières
// =============================================================================
std::shared_mutex g_sceneMutex;

struct DeferredTask {
    std::string name;        // for the response
    std::string path;
    double x = 0, y = 0, z = 0;
};

std::vector<DeferredTask> g_deferredGLTFTasks;
struct ConsoleLog;
std::mutex g_deferredTasksMutex;
static ConsoleLog* g_deferredLog = nullptr;

struct DeferredHDRTask {
    std::string path;
};

static std::vector<DeferredHDRTask> g_deferredHDRTasks;
static std::mutex g_deferredHDRMutex;

struct DeferredShaderTask {
    std::string name;
    std::string vertSrc;
    std::string fragSrc;
};

static std::vector<DeferredShaderTask> g_deferredShaderTasks;
static std::mutex g_deferredShaderMutex;


// =============================================================================
//  Structures pour l'IDE 
// =============================================================================
struct ConsoleLog {
    enum Kind { CMD, REPLY_OK, REPLY_ERR, INFO };
    struct Entry { Kind kind; std::string text; };
    std::deque<Entry> entries;
    std::mutex mtx;
    bool autoScroll = true;
    std::string selectedText;

    void push(Kind k, const std::string& t) {
        std::lock_guard<std::mutex> lk(mtx);
        entries.push_back({ k,t });
        if (entries.size() > 5000) entries.pop_front();
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mtx);
        entries.clear();
        selectedText.clear();
    }
};

struct SceneFBO {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0;
    void init(int width, int height) {
        w = width; h = height;
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) std::cerr << "FBO incomplete!\n";
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    void resize(int nw, int nh) {
        if (nw == w && nh == h) return;
        w = nw; h = nh;
        glBindTexture(GL_TEXTURE_2D, color); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, depth); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    }
    void bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h); }
    void unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    void destroy() { if (fbo) glDeleteFramebuffers(1, &fbo); if (color) glDeleteTextures(1, &color); if (depth) glDeleteRenderbuffers(1, &depth); }
};


static const char* kColorNames[] = {
    "white","black","red","green","blue","yellow","gray","orange","purple","cyan"
};
static const int kNumColors = 10;

struct HierarchyNode {
    enum Kind { OBJECT, LIGHT, CAMERA, FOLDER };
    Kind kind = OBJECT; std::string name; bool folderOpen = true; std::vector<HierarchyNode> children;
};

struct IDECamera {
    std::string name;
    float px = 0, py = 5, pz = 15;
    float fov = 60.f;
    Camera* runtimeCamera = nullptr; // non-owning, owned by sm->cameras
};


struct IDEState {
    std::vector<IDEObject> objects;
    std::vector<IDELight> lights;
    std::vector<IDECamera> cameras;          // scene cameras added by the user
    Camera* savedEditorCamera = nullptr;     // editor cam pointer, saved on Play
    // Non-owning pointer to the ScriptManager that lives in sm->scriptManager.
    // Ownership was moved there so GPURenderer::UpdateGameLogic can drive updates.
    ScriptManager* scriptManager = nullptr;
    float editorFov = 60.f; // editor viewport camera FOV
    bool consoleFocused = false;
    AIAssistant aiAssistant;
    bool showAIChat = false;
    bool quitRequested = false;
    bool showQuitModal = false;
    MultiSelection selection;

    std::vector<HierarchyNode> hierRoots;
    std::string clipboardName;
    bool clipboardIsLight = false;
    SceneFBO sceneFBO;
    SceneManager* sm = nullptr;
    GPURenderer* renderer = nullptr;
    BasicMovements* player = nullptr;
    CommandBus bus;
    ConsoleLog log;
    SDL_Window* window = nullptr;
    static constexpr int kUndoMaxDepth = 64;
    char cmdInput[512] = {};

    ImGuiID mainDockspaceId = 0;
    bool showHierarchy = true;
    bool showInspector = true;
    bool showConsole = true;
    bool showAssetBrowser = true;
    bool viewportHovered = false;
    bool showShipDialog = false;
    char shipSrcDir[512] = "./";
    char shipDstDir[512] = "./dist/";

    // ── Scene management for shipping ────────────────────────────────────────
    // All .honscene files found in the project, refreshed when the Ship dialog opens.
    struct SceneEntry {
        std::string path;       // relative or absolute path to the .honscene
        std::string name;       // display name (filename stem)
        bool selected = true;   // whether to include in the build
    };
    std::vector<SceneEntry> projectScenes;  // populated on Ship dialog open
    int shipFirstScene = 0;                 // index into projectScenes of the startup scene
    bool showAddObject = false;
    char newObjName[64] = "obj1";
    ObjectType newObjType = PLANE;
    char newObjFile[256] = "";
    float newObjPos[3] = {};
    float newObjScale = 1.f;
    int newObjColor = 0;
    int newObjSubdivX = 1;   // Plane: segmentsX  /  Rectangle: segmentsX
    int newObjSubdivY = 1;   // Rectangle only:   segmentsY
    int newObjSubdivZ = 1;   // Plane: segmentsZ  /  Rectangle: segmentsZ
    bool showAddLight = false;
    float newLightPos[3] = {};
    char newLightName[64] = "light1";
    float newLightIntensity = .75f;
    float newLightColor[3] = { 1.f,1.f,1.f };
    ObjectType newLightType = POINT_LIGHT;
    bool showAddCamera = false;
    char newCameraName[64] = "camera1";
    float newCameraPos[3] = { 0.f, 5.f, 15.f };
    float inspPos[3] = {};
    float inspScale[3] = { 1,1,1 };
    float inspRot[3] = {};
    float inspColor[4] = { 1,1,1,1 };
    char inspShader[64] = {};
    char inspTag[64] = {};
    bool playing = false;

    // Asset management
    AssetDatabase    assetDb;
    AssetBrowserState assetBrowser;

    // Project management
    ProjectSettings  project;
    ProjectUIState   projectUI;
    AutoSaveManager  autoSave;

    // Package manager
    PackageManager   packageMgr;
    PackageManagerUIState packageUI;

    // Global search
    GlobalSearchState globalSearch;

    // Toast notifications
    ToastManager toastMgr;

    // Editor settings
    EditorSettings editorSettings;

    // Selection history
    SelectionHistory selectionHistory;

    // Progress bar
    ProgressState progressState;

    // Inspector lock
    bool inspectorLocked = false;
    std::string lockedInspectorObject;

    char hierarchyFilter[128] = {};
    bool showStats = false;
    float statsTimer = 0.f;
    int statsFps = 0;
    int statsFrameCount = 0;
    bool showWorldSettings = false;
    float ambientIntensity = 0.15f;
    float fogDensity = 0.f;
    int fogColor[3] = { 200,210,230 };
    bool wireframe = false;
    bool showRenameModal = false;
    char renameOldName[64] = {};
    char renameNewName[64] = {};
    bool snapEnabled = false;
    float snapPosition = 0.25f;
    float snapRotation = 15.f;
    float snapScale = 0.1f;
    std::vector<std::string> cmdHistory;
    int cmdHistoryIdx = -1;
    std::vector<std::string> undoStack;
    bool sceneDirty = false;
    char sceneFilePath[512] = "assets/scene.honscene";
    bool showSaveModal = false;

    // ── Project-root–relative path helpers ───────────────────────────────────
    // Use these everywhere instead of hard-coded ".honassets" / "ide_settings.ini"
    // so that all per-project data lives inside the chosen project folder.
    std::string ProjectRoot() const {
        return std::string(project.rootFolder);
    }
    std::string HonAssetsPath() const {
        std::string r = ProjectRoot();
        return r.empty() ? ".honassets"
            : (fs::path(r) / ".honassets").string();
    }
    std::string EditorSettingsPath() const {
        std::string r = ProjectRoot();
        return r.empty() ? "ide_settings.ini"
            : (fs::path(r) / "ide_settings.ini").string();
    }
    std::string AssetRootDir() const {
        std::string r = ProjectRoot();
        return r.empty() ? "./assets/"
            : (fs::path(r) / "assets" / "").string();
    }
    bool showLoadModal = false;

    // ── Scene open from Asset Browser ─────────────────────────────────────────
    // When the user double-clicks a .honscene in the browser, we check if the
    // scene is dirty and show a confirmation prompt before loading.
    bool showSceneOpenPrompt = false;    // "Save before opening?" modal
    std::string pendingSceneOpenPath;   // the .honscene path to load once confirmed
    std::string pendingSelection;
    size_t logReadIdx = 0;
    bool showCreateFolder = false;
    char newFolderName[64] = "Group";
    bool showMoveToFolder = false;
    char moveTargetItem[64] = {};
    bool moveTargetIsLight = false;

    // Outils à la Unity
    int toolMode = 1;            // 0=View, 1=Translate, 2=Rotate, 3=Scale
    bool editorCamOrbit = false;
    ImVec2 editorCamLastMouse;

    // Gizmo state
    TransformGizmo gizmo;
    bool gizmoLocalMode = false;
    bool gizmoPivotCenter = false;

    // Camera bookmarks
    CameraBookmarks bookmarks;

    // Focus transition
    FocusTransition focus;

    // Ortho/persp
    bool viewportOrtho = false;
    float orthoSize = 10.f;

    // Overlays
    bool showGrid = true;
    bool showAxes = true;
    bool showIcons3D = true;
    bool showFrustum = true;
    int gridPlane = 0;   // 0=XZ, 1=XY, 2=YZ
    int debugMode = 0;   // 0=shaded, 1=wireframe, 2=overdraw, 3=depth, 4=normals

    // FPS fly mode
    bool fpsFlyMode = false;

    // Box selection
    BoxSelectionState boxSelect;

    // ── Skybox / Environment ─────────────────────────────────────────────────
    bool  showEnvironmentWindow = false;
    // 0 = Procedural (default), 1 = Cubemap from files
    int   skyboxMode = 0;
    // Per-face paths for cubemap import (right, left, top, bottom, front, back)
    char  skyboxFaces[6][512] = {};
    // Sun glow intensity override (0 = driven by directional light)
    float skyboxSunGlowOverride = -1.f; // -1 means "use light intensity"

    // ── Import Settings Overlay ───────────────────────────────────────────────
    // Floating temporary panel shown above the Inspector when an asset is
    // dropped onto the viewport (or focused in the browser).
    bool  showImportOverlay = false;
    std::string importOverlayGUID;          // which asset's settings to show
    bool  importOverlayHasImportBtn = false;// whether to show the "Import" button
    std::function<void()> importOverlayOnImport; // callback for Import button
    ImVec2 importOverlayAnchorPos = {};    // Inspector window top-left (updated each frame)
    ImVec2 importOverlayAnchorSize = {};    // Inspector window size     (updated each frame)

    // ── Physics / RigidBody ────────────────────────────────────────────────────
    PhysicsWorld physicsWorld;
    std::unordered_map<BaseObject*, RigidBody>         rigidBodies;
    std::unordered_map<BaseObject*, CollisionTrigger>  collisionTriggers;

    // ── Prefab ────────────────────────────────────────────────────────────────
    std::unordered_map<std::string, std::string> prefabSourceGUID;

    // Multi-object prefab creation (from folder right-click or multi-selection)
    bool showCreatePrefabFromFolder = false;
    std::string createPrefabFromFolderName;      // which folder to serialize
    char createPrefabMultiName[64] = {};

    // ── Prefab Edit Mode (Unity-style) ────────────────────────────────────────
    // When the user double-clicks a .honprefab in the asset browser, the editor
    // enters a dedicated "Prefab Edit Mode" — isolated from the main scene.
    // The prefab objects are loaded into a temporary list; on Save the file is
    // re-serialized and the mode is exited.
    bool  prefabEditMode = false;           // true while editing a prefab
    std::string prefabEditPath;             // path to the .honprefab being edited
    std::string prefabEditName;             // display name (stem of file)
    // Snapshot of the main scene objects/lights to restore on exit
    std::vector<IDEObject> prefabEditSceneSnapshot;
    std::vector<IDELight>  prefabEditLightSnapshot;
    // Objects loaded from the prefab for inline editing
    std::vector<IDEObject> prefabEditObjects;
    bool prefabEditDirty = false;           // unsaved changes in prefab editor

    // "Add Parent" (Create Empty Parent) state
    bool showAddParentModal = false;
    char addParentName[64] = "GameObject";

    // Hierarchy drag-into-folder payload tag
    static constexpr const char* kHierFolderDropPayload = "HONHON_HIER_INTO_FOLDER";

    // ── Profiler / Memory debug windows ──────────────────────────────────────
    bool showProfiler = false;
    bool showMemoryWindow = false;
    static constexpr int kProfilerSamples = 256;
    float frameTimeSamples[kProfilerSamples] = {};
    int   frameTimeSampleIdx = 0;

    // ── Toolchain settings window ─────────────────────────────────────────────
    bool showToolchainWindow = false;

    // ── Script creation wizard ────────────────────────────────────────────────
    bool showScriptWizard = false;
    char newScriptName[64] = "MyScript";
    int  newScriptTemplate = 0; // 0=Empty, 1=StartUpdate, 2=Full
};


// =============================================================================
//  Fonctions JSON utilitaires
// =============================================================================
std::string JStr(const std::string& s) {
    std::string o; o.reserve(s.size() + 2); o += '"';
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    o += '"'; return o;
}

static std::string MakeResponse(bool ok, const std::string& cmd, const std::string& msg, const std::string& extra = "") {
    std::ostringstream o;
    o << "{\"ok\":" << (ok ? "true" : "false") << ",\"cmd\":" << JStr(cmd) << ",\"msg\":" << JStr(msg);
    if (!extra.empty()) o << "," << extra;
    o << "}"; return o.str();
}
static std::string JVec3(const char* key, double x, double y, double z) {
    std::ostringstream o; o << std::fixed << std::setprecision(4);
    o << "\"" << key << "\":{\"x\":" << x << ",\"y\":" << y << ",\"z\":" << z << "}";
    return o.str();
}
static std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0, n = line.size();
    while (i < n) {
        // skip whitespace
        while (i < n && std::isspace((unsigned char)line[i])) ++i;
        if (i >= n) break;

        std::string tok;
        if (line[i] == '"') {
            // quoted token — consume until closing quote (no escape handling needed for paths)
            ++i;
            while (i < n && line[i] != '"') tok += line[i++];
            if (i < n) ++i; // skip closing quote
        }
        else {
            // unquoted token — consume until whitespace
            while (i < n && !std::isspace((unsigned char)line[i])) tok += line[i++];
        }
        t.push_back(tok);
    }
    return t;
}
static bool ParseDouble(const std::string& s, double& out) { try { out = std::stod(s); return true; } catch (...) { return false; } }
static bool ParseInt(const std::string& s, int& out) { try { out = std::stoi(s); return true; } catch (...) { return false; } }
std::string JsonGet(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\":");
    if (pos == std::string::npos) return "";
    pos += key.size() + 3;
    if (pos >= json.size()) return "";
    // Skip optional whitespace after the colon (e.g. "type": "rect" vs "type":"rect")
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n'))
        ++pos;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        ++pos; std::string v;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
            v += json[pos++];
        }
        return v;
    }
    std::string v;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') v += json[pos++];
    // Trim trailing whitespace from numeric/bool values
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n'))
        v.pop_back();
    return v;
}

static bool JsonOk(const std::string& json) { return JsonGet(json, "ok") == "true"; }


static void WriteTransform(std::ostream& out, const Transform& t, int indent = 6) {
    std::string sp(indent, ' ');
    out << sp << "\"transform\": {\n";
    out << sp << "  \"px\":" << t.position.x << ",\"py\":" << t.position.y << ",\"pz\":" << t.position.z << ",\n";
    out << sp << "  \"sx\":" << t.scale.x << ",\"sy\":" << t.scale.y << ",\"sz\":" << t.scale.z << ",\n";
    out << sp << "  \"rw\":" << t.rotation.w << ",\"rx\":" << t.rotation.x << ",\"ry\":" << t.rotation.y << ",\"rz\":" << t.rotation.z << "\n";
    out << sp << "}";
}

static void WriteTextureLayer(std::ostream& out, const TextureLayer& layer, int indent = 8) {
    std::string sp(indent, ' ');
    out << sp << "{\n";
    out << sp << "  \"textureName\": " << JStr(layer.texture.name) << ",\n";
    out << sp << "  \"texturePath\": " << JStr(layer.texture.path) << ",\n";
    out << sp << "  \"tilingU\": " << layer.texture.tilingU << ",\n";
    out << sp << "  \"tilingV\": " << layer.texture.tilingV << ",\n";
    out << sp << "  \"offsetU\": " << layer.texture.offsetU << ",\n";
    out << sp << "  \"offsetV\": " << layer.texture.offsetV << ",\n";
    out << sp << "  \"blendWeight\": " << layer.blendWeight << ",\n";
    out << sp << "  \"blendMode\": " << (int)layer.blendMode << ",\n";
    out << sp << "  \"maskMin\": " << layer.maskMin << ",\n";
    out << sp << "  \"maskMax\": " << layer.maskMax << ",\n";
    out << sp << "  \"maskChannel\": " << layer.maskChannel << ",\n";
    out << sp << "  \"maskInvert\": " << (layer.maskInvert ? "true" : "false") << ",\n";
    out << sp << "  \"maskType\": " << (int)layer.maskType << "\n";
    out << sp << "}";
}

static void WriteMaterial(std::ostream& out, Material* mat, int indent = 6) {
    if (!mat) return;
    std::string sp(indent, ' ');
    out << sp << "\"material\": {\n";
    out << sp << "  \"color\": [" << (int)mat->color.r << "," << (int)mat->color.g << "," << (int)mat->color.b << "," << (int)mat->color.a << "],\n";
    out << sp << "  \"specularity\": " << mat->specularity << ",\n";
    out << sp << "  \"reflectivity\": " << mat->reflectivity << ",\n";
    out << sp << "  \"textureLayers\": [\n";
    for (size_t i = 0; i < mat->textureLayers.size(); ++i) {
        WriteTextureLayer(out, mat->textureLayers[i], indent + 4);
        if (i + 1 < mat->textureLayers.size()) out << ",";
        out << "\n";
    }
    out << sp << "  ]\n";
    out << sp << "}";
}

static void WriteScript(std::ostream& out, const ScriptComponent& sc, int indent = 6) {
    std::string sp(indent, ' ');
    out << sp << "{\n";
    out << sp << "  \"guid\": " << JStr(sc.scriptGUID) << ",\n";
    out << sp << "  \"exposedVars\": [\n";
    for (size_t i = 0; i < sc.exposedVars.size(); ++i) {
        const auto& var = sc.exposedVars[i];
        out << sp << "    { \"name\": " << JStr(var.name) << ", \"type\": ";
        switch (var.type) {
        case ScriptVarType::Int:    out << "\"int\"";
            out << ", \"value\": " << var.value.i; break;
        case ScriptVarType::Float:  out << "\"float\"";
            out << ", \"value\": " << var.value.f; break;
        case ScriptVarType::Bool:   out << "\"bool\"";
            out << ", \"value\": " << (var.value.b ? "true" : "false"); break;
        case ScriptVarType::String: out << "\"string\"";
            out << ", \"value\": " << JStr(var.value.s ? var.value.s : ""); break;
        }
        out << " }";
        if (i + 1 < sc.exposedVars.size()) out << ",";
        out << "\n";
    }
    out << sp << "  ]\n";
    out << sp << "}";
}

static void WriteRigidBody(std::ostream& out, const RigidBody& rb, int indent = 6) {
    std::string sp(indent, ' ');
    out << sp << "\"rigidbody\": {\n";
    out << sp << "  \"mass\": " << rb.mass << ",\n";
    out << sp << "  \"drag\": " << rb.drag << ",\n";
    out << sp << "  \"angularDrag\": " << rb.angularDrag << ",\n";
    out << sp << "  \"restitution\": " << rb.restitution << ",\n";
    out << sp << "  \"friction\": " << rb.friction << ",\n";
    out << sp << "  \"useGravity\": " << (rb.useGravity ? "true" : "false") << ",\n";
    out << sp << "  \"isKinematic\": " << (rb.isKinematic ? "true" : "false") << ",\n";
    out << sp << "  \"velocity\": [" << rb.velocity.x << "," << rb.velocity.y << "," << rb.velocity.z << "],\n";
    out << sp << "  \"angularVelocity\": [" << rb.angularVelocity.x << "," << rb.angularVelocity.y << "," << rb.angularVelocity.z << "]\n";
    out << sp << "}";
}

static void WriteCollisionTrigger(std::ostream& out, const CollisionTrigger& ct, int indent = 6) {
    std::string sp(indent, ' ');
    out << sp << "\"collisionTrigger\": {\n";
    out << sp << "  \"enabled\": " << (ct.enabled ? "true" : "false") << ",\n";
    out << sp << "  \"isSolid\": " << (ct.isSolid ? "true" : "false") << ",\n";
    out << sp << "  \"filterTag\": " << JStr(ct.filterTag) << "\n";
    out << sp << "}";
}

static void WriteAnimator(std::ostream& out, const AnimatorComponent& anim, int indent = 6) {
    if (!anim.active()) return;
    std::string sp(indent, ' ');
    out << sp << "\"animator\": {\n";
    out << sp << "  \"speed\": " << anim.speed << ",\n";
    out << sp << "  \"currentClipName\": " << JStr(anim.currentClipName()) << "\n";
    out << sp << "}";
}

static void WriteRenderComponent(std::ostream& out, const RenderComponent& rc, TextureManager& texMgr, int indent = 6) {
    std::string sp(indent, ' ');
    out << sp << "\"render\": {\n";
    out << sp << "  \"useMaterialColor\": " << (rc.useMaterialColor ? "true" : "false") << ",\n";
    out << sp << "  \"depthWrite\": " << (rc.depthWrite ? "true" : "false") << ",\n";
    out << sp << "  \"blend\": " << (rc.blend ? "true" : "false") << ",\n";
    out << sp << "  \"cullFace\": " << (rc.cullFace ? "true" : "false") << ",\n";
    out << sp << "  \"textures\": [\n";
    for (size_t i = 0; i < rc.textures.size(); ++i) {
        const auto& bind = rc.textures[i];
        std::string texPath = texMgr.getPath(bind.textureName);  // now public
        out << sp << "    { \"uniformName\": " << JStr(bind.uniformName)
            << ", \"textureName\": " << JStr(bind.textureName)
            << ", \"texturePath\": " << JStr(texPath)
            << ", \"slot\": " << bind.slot << " }";
        if (i + 1 < rc.textures.size()) out << ",";
        out << "\n";
    }
    out << sp << "  ]\n";
    out << sp << "}";
}

static void WriteLightToJson(std::ostream& out, const IDELight& lt, int indent = 2) {
    std::string sp(indent, ' ');
    out << sp << "{\n";
    out << sp << "  \"name\": " << JStr(lt.name) << ",\n";
    out << sp << "  \"type\": " << JStr(lt.lightType) << ",\n";
    out << sp << "  \"intensity\": " << lt.intensity << ",\n";
    out << sp << "  \"color\": [" << lt.r << "," << lt.g << "," << lt.b << "],\n";
    out << sp << "  \"transform\": {\n";
    out << sp << "    \"px\":" << lt.px << ",\"py\":" << lt.py << ",\"pz\":" << lt.pz << ",\n";
    out << sp << "    \"rw\":1,\"rx\":0,\"ry\":0,\"rz\":0\n"; // rotation not used for lights except directional? We'll store rotation for directional later if needed
    out << sp << "  }\n";
    out << sp << "}";
}

// Deserialization helpers (used in EnterPrefabEditMode & InstantiatePrefab)
static void ApplyMaterialFromJson(BaseObject* obj, const std::string& jsonChunk, TextureManager& texMgr) {
    // Extract material object from jsonChunk (contains the whole object JSON)
    // For simplicity, we parse using our simple JsonGet functions and manual recursion.
    // Since JSON is small, we'll do it step by step.
    std::string colorStr = JsonGet(jsonChunk, "color");
    if (!colorStr.empty()) {
        int r = 255, g = 255, b = 255, a = 255;
        sscanf_s(colorStr.c_str(), "[%d,%d,%d,%d]", &r, &g, &b, &a);
        if (obj->material) delete obj->material;
        obj->material = new Material(0, 0, Color((double)r, (double)g, (double)b, (double)a), Color(0, 0, 0, 255));
    }
    std::string specStr = JsonGet(jsonChunk, "specularity");
    if (!specStr.empty() && obj->material) obj->material->specularity = std::stof(specStr);
    std::string reflStr = JsonGet(jsonChunk, "reflectivity");
    if (!reflStr.empty() && obj->material) obj->material->reflectivity = std::stof(reflStr);

    // Texture layers: find array
    size_t layersStart = jsonChunk.find("\"textureLayers\":[");
    if (layersStart != std::string::npos) {
        size_t arrStart = jsonChunk.find('[', layersStart);
        size_t depth = 0, pos = arrStart;
        std::vector<std::string> layerTokens;
        while (pos < jsonChunk.size()) {
            if (jsonChunk[pos] == '[') ++depth;
            else if (jsonChunk[pos] == ']') { if (--depth == 0) break; }
            if (jsonChunk[pos] == '{' && depth == 2) {
                size_t blockStart = pos, blockDepth = 0, bp = pos;
                while (bp < jsonChunk.size()) {
                    if (jsonChunk[bp] == '{') ++blockDepth;
                    else if (jsonChunk[bp] == '}') { if (--blockDepth == 0) break; }
                    ++bp;
                }
                layerTokens.push_back(jsonChunk.substr(blockStart, bp - blockStart + 1));
                pos = bp;
            }
            ++pos;
        }
        for (auto& tok : layerTokens) {
            TextureLayer layer;
            layer.texture.name = JsonGet(tok, "textureName");
            layer.texture.path = JsonGet(tok, "texturePath");
            if (!layer.texture.path.empty()) texMgr.load(layer.texture);
            layer.texture.tilingU = std::stof(JsonGet(tok, "tilingU"));
            layer.texture.tilingV = std::stof(JsonGet(tok, "tilingV"));
            layer.texture.offsetU = std::stof(JsonGet(tok, "offsetU"));
            layer.texture.offsetV = std::stof(JsonGet(tok, "offsetV"));
            layer.blendWeight = std::stof(JsonGet(tok, "blendWeight"));
            layer.blendMode = (LayerBlendMode)std::stoi(JsonGet(tok, "blendMode"));
            layer.maskMin = std::stof(JsonGet(tok, "maskMin"));
            layer.maskMax = std::stof(JsonGet(tok, "maskMax"));
            layer.maskChannel = std::stoi(JsonGet(tok, "maskChannel"));
            layer.maskInvert = JsonGet(tok, "maskInvert") == "true";
            layer.maskType = (LayerMaskType)std::stoi(JsonGet(tok, "maskType"));
            if (obj->material) obj->material->textureLayers.push_back(layer);
        }
        if (obj->material) obj->material->dirty = true;
    }
}

static void ApplyScriptsFromJson(BaseObject* obj, const std::string& jsonChunk, IDEState& ide) {
    size_t scriptsStart = jsonChunk.find("\"scripts\":[");
    if (scriptsStart == std::string::npos) return;
    size_t arrStart = jsonChunk.find('[', scriptsStart);
    size_t depth = 0, pos = arrStart;
    std::vector<std::string> scriptTokens;
    while (pos < jsonChunk.size()) {
        if (jsonChunk[pos] == '[') ++depth;
        else if (jsonChunk[pos] == ']') { if (--depth == 0) break; }
        if (jsonChunk[pos] == '{' && depth == 2) {
            size_t blockStart = pos, blockDepth = 0, bp = pos;
            while (bp < jsonChunk.size()) {
                if (jsonChunk[bp] == '{') ++blockDepth;
                else if (jsonChunk[bp] == '}') { if (--blockDepth == 0) break; }
                ++bp;
            }
            scriptTokens.push_back(jsonChunk.substr(blockStart, bp - blockStart + 1));
            pos = bp;
        }
        ++pos;
    }
    for (auto& tok : scriptTokens) {
        std::string guid = JsonGet(tok, "guid");
        AssetRecord* rec = ide.assetDb.FindByGUID(guid);
        if (!rec) continue;
        ScriptComponent sc;
        sc.scriptGUID = guid;
        if (ide.scriptManager && ide.scriptManager->LoadScript(guid, rec->path, sc)) {
            // Exposed variables
            size_t varsStart = tok.find("\"exposedVars\":[");
            if (varsStart != std::string::npos) {
                size_t arrStart2 = tok.find('[', varsStart);
                size_t depth2 = 0, pos2 = arrStart2;
                std::vector<std::string> varTokens;
                while (pos2 < tok.size()) {
                    if (tok[pos2] == '[') ++depth2;
                    else if (tok[pos2] == ']') { if (--depth2 == 0) break; }
                    if (tok[pos2] == '{' && depth2 == 2) {
                        size_t blockStart = pos2, blockDepth = 0, bp = pos2;
                        while (bp < tok.size()) {
                            if (tok[bp] == '{') ++blockDepth;
                            else if (tok[bp] == '}') { if (--blockDepth == 0) break; }
                            ++bp;
                        }
                        varTokens.push_back(tok.substr(blockStart, bp - blockStart + 1));
                        pos2 = bp;
                    }
                    ++pos2;
                }
                for (auto& vt : varTokens) {
                    std::string vname = JsonGet(vt, "name");
                    std::string vtype = JsonGet(vt, "type");
                    std::string vval = JsonGet(vt, "value");
                    ScriptVariable var;
                    var.name = vname;

                    if (vtype == "int") {
                        var.type = ScriptVarType::Int;
                        var.value.i = std::stoi(vval);
                    }
                    else if (vtype == "float") {
                        var.type = ScriptVarType::Float;
                        var.value.f = std::stof(vval);
                    }
                    else if (vtype == "bool") {
                        var.type = ScriptVarType::Bool;
                        var.value.b = (vval == "true");
                    }
                    else if (vtype == "string") {
                        var.type = ScriptVarType::String;
                        // Allocate new string on heap
                        var.value.s = new char[vval.size() + 1];
                        strcpy_s(var.value.s, vval.size() + 1, vval.c_str());
                    }

                    // Overwrite default value if this variable already exists in the component
                    bool found = false;
                    for (auto& ev : sc.exposedVars) {
                        if (ev.name == var.name) {
                            // Free old string if we're replacing a string variable
                            if (ev.type == ScriptVarType::String && ev.value.s) {
                                delete[] ev.value.s;
                            }
                            ev.value = var.value;
                            ev.type = var.type;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        sc.exposedVars.push_back(var);
                    }
                }
            }
            obj->scripts.push_back(sc);
        }
        else {
            // Fallback: store component without loading (so it appears in inspector)
            obj->scripts.push_back(sc);
        }
    }
}

static void ApplyRenderComponentFromJson(BaseObject* obj, const std::string& jsonChunk, IDEState& ide) {
    std::string shader = JsonGet(jsonChunk, "shaderName");
    if (!shader.empty()) obj->render.shaderName = shader;
    std::string useMatCol = JsonGet(jsonChunk, "useMaterialColor");
    if (!useMatCol.empty()) obj->render.useMaterialColor = (useMatCol == "true");
    std::string depthWrite = JsonGet(jsonChunk, "depthWrite");
    if (!depthWrite.empty()) obj->render.depthWrite = (depthWrite == "true");
    std::string blend = JsonGet(jsonChunk, "blend");
    if (!blend.empty()) obj->render.blend = (blend == "true");
    std::string cull = JsonGet(jsonChunk, "cullFace");
    if (!cull.empty()) obj->render.cullFace = (cull == "true");

    size_t texStart = jsonChunk.find("\"textures\":[");
    if (texStart != std::string::npos) {
        size_t arrStart = jsonChunk.find('[', texStart);
        size_t depth = 0, pos = arrStart;
        std::vector<std::string> texTokens;
        while (pos < jsonChunk.size()) {
            if (jsonChunk[pos] == '[') ++depth;
            else if (jsonChunk[pos] == ']') { if (--depth == 0) break; }
            if (jsonChunk[pos] == '{' && depth == 2) {
                size_t blockStart = pos, blockDepth = 0, bp = pos;
                while (bp < jsonChunk.size()) {
                    if (jsonChunk[bp] == '{') ++blockDepth;
                    else if (jsonChunk[bp] == '}') { if (--blockDepth == 0) break; }
                    ++bp;
                }
                texTokens.push_back(jsonChunk.substr(blockStart, bp - blockStart + 1));
                pos = bp;
            }
            ++pos;
        }
        for (auto& tok : texTokens) {
            std::string uniform = JsonGet(tok, "uniformName");
            std::string texName = JsonGet(tok, "textureName");
            std::string texPath = JsonGet(tok, "texturePath");
            int slot = std::stoi(JsonGet(tok, "slot"));
            if (!texPath.empty()) {
                Texture tex(texName, texPath);
                ide.renderer->texManager.load(tex);
            }
            obj->render.textures.push_back({ uniform, texName, slot });
        }
    }
}
// =============================================================================
//  Couleurs nommées
// =============================================================================
static const Color colWhite{ 255,255,255,255 };
static const Color colBlack{ 0,0,0,255 };
static const Color colRed{ 220,60,60,255 };
static const Color colGreen{ 60,180,60,255 };
static const Color colBlue{ 60,100,220,255 };
static const Color colYellow{ 230,200,40,255 };
static const Color colGray{ 140,140,140,255 };
static const Color colOrange{ 230,130,40,255 };
static const Color colPurple{ 150,60,200,255 };
static const Color colCyan{ 60,200,210,255 };
static Color NameToColor(const std::string& n) {
    if (n == "white") return colWhite; if (n == "black") return colBlack;
    if (n == "red") return colRed; if (n == "green") return colGreen;
    if (n == "blue") return colBlue; if (n == "yellow") return colYellow;
    if (n == "gray") return colGray; if (n == "orange") return colOrange;
    if (n == "purple") return colPurple; if (n == "cyan") return colCyan;
    return colWhite;
}
static Quaternion EulerToQuat(double pitch, double yaw, double roll) {
    double p = pitch * M_PI / 180.0 * 0.5, y = yaw * M_PI / 180.0 * 0.5, r = roll * M_PI / 180.0 * 0.5;
    double cp = cos(p), sp = sin(p), cy = cos(y), sy = sin(y), cr = cos(r), sr = sin(r);
    return Quaternion(cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy);
}

// =============================================================================
//  Registres globaux (protégés par g_sceneMutex)
// =============================================================================
std::unordered_map<std::string, BaseObject*> g_namedObjects;
std::unordered_map<std::string, BaseLight*>  g_namedLights;

// =============================================================================
//  CommandRegistry et command handlers (tous avec verrouillage)
// =============================================================================

struct CmdContext {
    SceneManager* sm = nullptr;
    GPURenderer* renderer = nullptr;
    std::shared_mutex* sceneMutex = &g_sceneMutex;
};

struct CommandRegistry {
    struct Entry {
        std::string syntax, description;
        std::function<void(CmdContext&, const std::vector<std::string>&, std::ostream&)> handler;
    };
    std::unordered_map<std::string, Entry> cmds;
    void Register(const std::string& name, const std::string& syntax, const std::string& desc,
        std::function<void(CmdContext&, const std::vector<std::string>&, std::ostream&)> fn) {
        cmds[name] = { syntax, desc, std::move(fn) };
    }
    void Dispatch(const std::string& line, CmdContext& ctx, std::ostream& out);
    void PrintHelp(std::ostream& out) const;
};


void CommandRegistry::Dispatch(const std::string& line, CmdContext& ctx, std::ostream& out) {
    auto tokens = Tokenize(line);
    if (tokens.empty()) return;
    std::string cmdName = tokens[0];
    tokens.erase(tokens.begin());
    auto it = cmds.find(cmdName);
    if (it == cmds.end()) {
        out << MakeResponse(false, cmdName, "Unknown command. Type 'help'.") << "\n";
        return;
    }
    it->second.handler(ctx, tokens, out);
}

void CommandRegistry::PrintHelp(std::ostream& out) const {
    out << "{\"ok\":true,\"cmd\":\"help\",\"msg\":\"";
    for (auto& [n, e] : cmds) out << e.syntax << "  —  " << e.description << "\\n";
    out << "\"}\n";
}

static void BuildCommands(CommandRegistry& reg, GPURenderer* renderer,
    AssetDatabase* assetDb, SceneManager* sceneMgr) {
    // help
    reg.Register("help", "help", "List all commands.",
        [&reg](CmdContext&, const std::vector<std::string>&, std::ostream& out) { reg.PrintHelp(out); });
    // echo
    reg.Register("echo", "echo <text...>", "Echo arguments back.",
        [](CmdContext&, const std::vector<std::string>& args, std::ostream& out) {
            std::string m; for (size_t i = 0; i < args.size(); ++i) m += (i ? " " : "") + args[i];
            out << MakeResponse(true, "echo", m, "\"echo\":" + JStr(m)) << "\n";
        });
    // list (lecture seule mais verrou quand même)
    reg.Register("list", "list", "List all named objects and lights.",
        [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out) {
            std::shared_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << "\"objects\":[";
            bool first = true;
            for (auto& [name, obj] : g_namedObjects) {
                // Determine type string from obj->type enum
                std::string typeStr;
                switch (obj->type) {
                case PLANE:      typeStr = "plane"; break;
                case RECTANGLE:  typeStr = "rect"; break;
                case SPHERE:     typeStr = "sphere"; break;
                case OBJ_MESH:   typeStr = "obj"; break;
                case GLTF_MESH:  typeStr = "gltf"; break;
                default:         typeStr = "unknown"; break;
                }

                // Asset path (stored in tag for OBJ/GLTF)
                std::string assetPath;
                if (obj->type == OBJ_MESH && obj->tag.rfind("OBJ:", 0) == 0)
                    assetPath = obj->tag.substr(4);
                else if (obj->type == GLTF_MESH && obj->tag.rfind("GLTF:", 0) == 0)
                    assetPath = obj->tag.substr(5);

                d << "{"
                    << "\"name\":" << JStr(name)
                    << ",\"visible\":" << (obj->visible ? "true" : "false")
                    << ",\"pos\":{\"x\":" << obj->transform.position.x
                    << ",\"y\":" << obj->transform.position.y
                    << ",\"z\":" << obj->transform.position.z << "}"
                    << ",\"scale\":{\"x\":" << obj->transform.scale.x
                    << ",\"y\":" << obj->transform.scale.y
                    << ",\"z\":" << obj->transform.scale.z << "}"
                    << ",\"shader\":" << JStr(obj->render.shaderName)
                    << ",\"type\":" << JStr(typeStr)
                    << ",\"assetPath\":" << JStr(assetPath)
                    << ",\"tag\":" << JStr(obj->tag)
                    << ",\"color\":{\"r\":" << (int)(obj->material ? obj->material->color.r : 255)
                    << ",\"g\":" << (int)(obj->material ? obj->material->color.g : 255)
                    << ",\"b\":" << (int)(obj->material ? obj->material->color.b : 255)
                    << ",\"a\":" << (int)(obj->material ? obj->material->color.a : 255) << "}"
                    << "}";
            }
            d << "],\"lights\":[";
            first = true;
            for (auto& [name, lt] : g_namedLights) {
                std::string lightType = "point";
                Vector3 lpos;
                if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                    lightType = "directional";
                    lpos = dl->transform.position;
                }
                else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                    lightType = "point";
                    lpos = pl->transform.position;
                }
                else {
                    lightType = "ambient"; // plain BaseLight — no transform
                }
                if (!first) d << ","; first = false;
                d << "{\"name\":" << JStr(name)
                    << ",\"intensity\":" << lt->intensity
                    << ",\"color\":{\"r\":" << (int)lt->color.r
                    << ",\"g\":" << (int)lt->color.g
                    << ",\"b\":" << (int)lt->color.b << "}"
                    << ",\"pos\":{\"x\":" << lpos.x << ",\"y\":" << lpos.y << ",\"z\":" << lpos.z << "}"
                    << ",\"type\":" << JStr(lightType) << "}";
            }
            d << "]";
            out << MakeResponse(true, "list",
                std::to_string(g_namedObjects.size()) + " objects, " + std::to_string(g_namedLights.size()) + " lights", d.str()) << "\n";
        });
    // inspect
    reg.Register("inspect", "inspect <name>", "Return transform and material.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "inspect", "Usage: inspect <name>") << "\n"; return; }
            std::shared_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                BaseObject* obj = it->second;
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << "\"name\":" << JStr(args[0]) << ",\"kind\":\"object\""
                    << ",\"visible\":" << (obj->visible ? "true" : "false")
                    << ",\"pos\":{\"x\":" << obj->transform.position.x
                    << ",\"y\":" << obj->transform.position.y
                    << ",\"z\":" << obj->transform.position.z << "}"
                    << ",\"scale\":{\"x\":" << obj->transform.scale.x
                    << ",\"y\":" << obj->transform.scale.y
                    << ",\"z\":" << obj->transform.scale.z << "}"
                    << ",\"rotation\":{\"w\":" << obj->transform.rotation.w
                    << ",\"x\":" << obj->transform.rotation.x
                    << ",\"y\":" << obj->transform.rotation.y
                    << ",\"z\":" << obj->transform.rotation.z << "}"
                    << ",\"shader\":" << JStr(obj->render.shaderName);
                if (obj->material)
                    d << ",\"color\":{\"r\":" << (int)obj->material->color.r
                    << ",\"g\":" << (int)obj->material->color.g
                    << ",\"b\":" << (int)obj->material->color.b
                    << ",\"a\":" << (int)obj->material->color.a << "}";
                out << MakeResponse(true, "inspect", "ok", d.str()) << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                BaseLight* lt = ltIt->second;
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                std::string lightType = "point";
                Vector3 lpos;
                if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                    lightType = "directional";
                    lpos = dl->transform.position;
                }
                else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                    lightType = "point";
                    lpos = pl->transform.position;
                }
                else {
                    lightType = "ambient"; // plain BaseLight — no transform
                }
                d << "\"name\":" << JStr(args[0]) << ",\"kind\":\"light\""
                    << ",\"type\":" << JStr(lightType)
                    << ",\"intensity\":" << lt->intensity
                    << ",\"color\":{\"r\":" << (int)lt->color.r
                    << ",\"g\":" << (int)lt->color.g
                    << ",\"b\":" << (int)lt->color.b << "}"
                    << ",\"pos\":{\"x\":" << lpos.x << ",\"y\":" << lpos.y << ",\"z\":" << lpos.z << "}";
                out << MakeResponse(true, "inspect", "ok", d.str()) << "\n";
                return;
            }
            out << MakeResponse(false, "inspect", "Unknown '" + args[0] + "'") << "\n";
        });
    reg.Register("plane", "plane <name> <x> <y> <z> [width] [height] [segmentsX] [segmentsZ] [color]", "Spawn a plane primitive.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "plane", "Usage: plane <name> <x> <y> <z> [width] [height] [segmentsX] [segmentsZ] [color]") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            if (finalName != args[0]) out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";

            double x, y, z, width = 5.0, height = 5.0;
            int segX = 1, segZ = 1;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "plane", "Bad coords") << "\n"; return;
            }
            if (args.size() >= 5) ParseDouble(args[4], width);
            if (args.size() >= 6) ParseDouble(args[5], height);
            if (args.size() >= 7) ParseInt(args[6], segX);
            if (args.size() >= 8) ParseInt(args[7], segZ);
            Color col = args.size() >= 9 ? NameToColor(args[8]) : colWhite;

            segX = (std::max)(1, segX);
            segZ = (std::max)(1, segZ);

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            BaseObject* obj = new HonHengine::Plane(Vector3(width, 1.0, height), Vector3(x, y, z), Quaternion(),
                new Material(0, 0, col, colBlack), segX, segZ);
            ctx.sm->objects->push_back(obj);
            g_namedObjects[finalName] = obj;

            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"width\":" << width << ",\"height\":" << height
                << ",\"segmentsX\":" << segX << ",\"segmentsZ\":" << segZ;
            out << MakeResponse(true, "plane", "'" + finalName + "' spawned", d.str()) << "\n";
        });

    // addlight (avec verrou)
    reg.Register("addlight", "addlight <name> <intensity> <r> <g> <b> [type]",
        "Add a light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 5) { out << MakeResponse(false, "addlight", "Usage: ...") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double intensity; int r, g, b;
            if (!ParseDouble(args[1], intensity) || !ParseInt(args[2], r) || !ParseInt(args[3], g) || !ParseInt(args[4], b)) {
                out << MakeResponse(false, "addlight", "Bad values") << "\n"; return;
            }
            Color col{ (double)std::clamp(r,0,255),(double)std::clamp(g,0,255),(double)std::clamp(b,0,255),255.0 };
            std::string lightType = (args.size() >= 6) ? args[5] : "point";
            // Normalise aliases
            if (lightType == "directionallight") lightType = "directional";
            if (lightType == "pointlight")       lightType = "point";
            if (lightType == "ambientlight")     lightType = "ambient";
            BaseLight* light = nullptr;
            if (lightType == "directional") {
                DirectionalLight* dl = new DirectionalLight(intensity, col, Vector3(0, 0, 0), Quaternion());
                dl->transform.rotation = Quaternion::LookRotation(Vector3(-0.5, -0.8, -0.3));
                light = dl;
            }
            else if (lightType == "ambient") {
                light = new BaseLight(intensity, col); // type = AMBIENT_LIGHT by default
            }
            else {
                lightType = "point"; // enforce canonical name
                light = new PointLight(intensity, col, Vector3(0, 5, 0));
            }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            ctx.sm->lights->push_back(light);
            g_namedLights[finalName] = light;

            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << "\"intensity\":" << intensity << ",\"color\":{\"r\":" << r << ",\"g\":" << g << ",\"b\":" << b << "},\"lightType\":" << JStr(lightType);
            out << MakeResponse(true, "addlight", "'" + finalName + "' added", d.str()) << "\n";
        });
    // removelight
    reg.Register("removelight", "removelight <name>", "Remove a light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "removelight", "Usage: removelight <name>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedLights.find(args[0]);
            if (it == g_namedLights.end()) { out << MakeResponse(false, "removelight", "Unknown light") << "\n"; return; }
            auto& lights = *ctx.sm->lights;
            lights.erase(std::remove(lights.begin(), lights.end(), it->second), lights.end());
            delete it->second;
            g_namedLights.erase(it);
            out << MakeResponse(true, "removelight", "'" + args[0] + "' removed") << "\n";
        });
    // delete
    reg.Register("delete", "delete <name>", "Delete an object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) { out << MakeResponse(false, "delete", "Usage: delete <name>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "delete", "Unknown object") << "\n"; return; }
            auto& objs = *ctx.sm->objects;
            objs.erase(std::remove(objs.begin(), objs.end(), it->second), objs.end());
            delete it->second;
            g_namedObjects.erase(it);
            out << MakeResponse(true, "delete", "'" + args[0] + "' deleted") << "\n";
        });
    // move
    reg.Register("move", "move <name> <x> <y> <z>", "Move object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "move", "Usage: move <name> <x> <y> <z>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            double x, y, z; if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "move", "Bad coords") << "\n"; return;
            }
            // Try object first, then light
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                it->second->transform.position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "move", "'" + args[0] + "' moved", d.str()) << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                if (auto* dl = dynamic_cast<DirectionalLight*>(ltIt->second)) dl->transform.position = Vector3(x, y, z);
                else if (auto* pl = dynamic_cast<PointLight*>(ltIt->second))  pl->transform.position = Vector3(x, y, z);
                std::ostringstream d; d << std::fixed << std::setprecision(4);
                d << JVec3("pos", x, y, z);
                out << MakeResponse(true, "move", "'" + args[0] + "' moved", d.str()) << "\n";
                return;
            }
            out << MakeResponse(false, "move", "Unknown '" + args[0] + "'") << "\n";
        });
    // scale
    reg.Register("scale", "scale <name> <sx> <sy> <sz>", "Scale object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "scale", "Usage: scale <name> <sx> <sy> <sz>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "scale", "Unknown object") << "\n"; return; }
            double sx, sy, sz; if (!ParseDouble(args[1], sx) || !ParseDouble(args[2], sy) || !ParseDouble(args[3], sz)) {
                out << MakeResponse(false, "scale", "Bad values") << "\n"; return;
            }
            it->second->transform.scale = Vector3(sx, sy, sz);
            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("scale", sx, sy, sz);
            out << MakeResponse(true, "scale", "'" + args[0] + "' scaled", d.str()) << "\n";
        });
    // rotate
    reg.Register("rotate", "rotate <name> <pitch> <yaw> <roll>", "Rotate object or light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "rotate", "Usage: rotate <name> <pitch> <yaw> <roll>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            double p, y, r; if (!ParseDouble(args[1], p) || !ParseDouble(args[2], y) || !ParseDouble(args[3], r)) {
                out << MakeResponse(false, "rotate", "Bad angles") << "\n"; return;
            }
            Quaternion q = EulerToQuat(p, y, r);
            auto it = g_namedObjects.find(args[0]);
            if (it != g_namedObjects.end()) {
                it->second->transform.rotation = q;
                out << MakeResponse(true, "rotate", "'" + args[0] + "' rotated") << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(args[0]);
            if (ltIt != g_namedLights.end()) {
                if (auto* dl = dynamic_cast<DirectionalLight*>(ltIt->second)) dl->transform.rotation = q;
                else if (auto* pl = dynamic_cast<PointLight*>(ltIt->second))  pl->transform.rotation = q;
                out << MakeResponse(true, "rotate", "'" + args[0] + "' rotated") << "\n";
                return;
            }
            out << MakeResponse(false, "rotate", "Unknown '" + args[0] + "'") << "\n";
        });
    // color
    reg.Register("color", "color <name> <r> <g> <b> [a]", "Set object color.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) {
                out << MakeResponse(false, "color", "Usage: color <name> <r> <g> <b> [a]") << "\n";
                return;
            }

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "color", "Unknown object") << "\n";
                return;
            }
            int r, g, b, a = 255;
            if (!ParseInt(args[1], r) || !ParseInt(args[2], g) || !ParseInt(args[3], b)) {
                out << MakeResponse(false, "color", "Bad values") << "\n";
                return;
            }
            if (args.size() >= 5) ParseInt(args[4], a);

            BaseObject* obj = it->second;
            if (!obj->material) {
                obj->material = new Material(0, 0, colWhite, colBlack);
            }

            // Use setter that marks dirty
            obj->material->setColor(Color{
                (double)std::clamp(r, 0, 255),
                (double)std::clamp(g, 0, 255),
                (double)std::clamp(b, 0, 255),
                (double)std::clamp(a, 0, 255)
                });

            obj->refreshVertexColors();

            out << MakeResponse(true, "color", "Color set on '" + args[0] + "'") << "\n";
        });
    reg.Register("visible", "visible <name> <true/false>", "Set object visibility.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "visible", "Usage: visible <name> <true/false>") << "\n";
                return;
            }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "visible", "Unknown object") << "\n";
                return;
            }
            bool visible = (args[1] == "true" || args[1] == "1");
            it->second->visible = visible;
            out << MakeResponse(true, "visible", std::string(args[0]) + " visibility set to " + (visible ? "true" : "false")) << "\n";
        });
    // setshader
    reg.Register("setshader", "setshader <name> <shaderName>", "Assign shader.",
        [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "setshader", "Usage: setshader <name> <shaderName>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "setshader", "Unknown object") << "\n"; return; }

            BaseObject* obj = it->second;
            const std::string& shaderName = args[1];

            obj->render.shaderName = shaderName;

            if (obj->material) {
                GLuint prog = renderer->GetShader(shaderName);
                obj->material->customShaderProgram = prog; // 0 if not yet registered — will stay 0 until re-assigned after registershader
            }

            out << MakeResponse(true, "setshader", "Shader '" + shaderName + "' assigned") << "\n";
        });
    // registershader
    reg.Register("registershader", "registershader <name> <vertFile> <fragFile>", "Compile and register shader.",
        [renderer](CmdContext&, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 3) { out << MakeResponse(false, "registershader", "Usage: registershader <name> <vert> <frag>") << "\n"; return; }

            auto resolvePath = [](const std::string& p) -> std::string {
                fs::path fp(p);
                if (fp.is_absolute()) return p;
                return (fs::path(g_projectRoot) / fp).string();
                };

            auto readFile = [](const std::string& path) -> std::string {
                std::ifstream f(path); if (!f) return "";
                return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
                };

            std::string vertPath = resolvePath(args[1]);
            std::string fragPath = resolvePath(args[2]);
            std::string vert = readFile(vertPath), frag = readFile(fragPath);
            if (vert.empty()) { out << MakeResponse(false, "registershader", "Cannot read vert: " + vertPath) << "\n"; return; }
            if (frag.empty()) { out << MakeResponse(false, "registershader", "Cannot read frag: " + fragPath) << "\n"; return; }
            {
                std::lock_guard<std::mutex> lock(g_deferredShaderMutex);
                g_deferredShaderTasks.push_back({ args[0], std::move(vert), std::move(frag) });
            }
            out << MakeResponse(true, "registershader", "Shader '" + args[0] + "' queued for registration") << "\n";
        });

    reg.Register("loadhdrskybox", "loadhdrskybox <path>", "Load HDR equirectangular image as skybox.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) {
                out << MakeResponse(false, "loadhdrskybox", "Usage: loadhdrskybox <path>") << "\n";
                return;
            }
            std::string path = args[0];
            if (path.front() == '"') path = path.substr(1);
            if (path.back() == '"') path.pop_back();

            {
                std::lock_guard<std::mutex> lock(g_deferredHDRMutex);
                g_deferredHDRTasks.push_back({ path });
            }
            out << MakeResponse(true, "loadhdrskybox", "HDR skybox queued for loading: " + path) << "\n";
        });
    // clone
    reg.Register("clone", "clone <srcName> <newName>", "Duplicate an object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "clone", "Usage: clone <src> <new>") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[1], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[1]) {
                out << MakeResponse(true, "plane", "Name '" + args[1] + "' changed to '" + finalName + "'") << "\n";
            }

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(args[0]);
            if (it == g_namedObjects.end()) { out << MakeResponse(false, "clone", "Unknown source") << "\n"; return; }
            if (g_namedObjects.count(args[1])) { out << MakeResponse(false, "clone", "Name already exists") << "\n"; return; }
            BaseObject* src = it->second;
            BaseObject* copy = new BaseObject(*src);
            copy->transform.position.x += 0.5;
            if (src->material) copy->material = new Material(*src->material);
            ctx.sm->objects->push_back(copy);

            g_namedObjects[finalName] = copy;

            out << MakeResponse(true, "clone", "'" + finalName + "' cloned as '" + args[1] + "'") << "\n";
        });
    // rename
    reg.Register("rename", "rename <oldName> <newName>", "Rename object/light.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) { out << MakeResponse(false, "rename", "Usage: rename <old> <new>") << "\n"; return; }
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            const std::string& oldN = args[0], rawName = args[1];

            std::string finalName = NameValidator::GetFinalName(rawName, g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != rawName) {
                out << MakeResponse(true, "plane", "Name '" + rawName + "' changed to '" + finalName + "'") << "\n";
            }

            auto objIt = g_namedObjects.find(oldN);
            if (objIt != g_namedObjects.end()) {
                if (g_namedObjects.count(finalName)) { out << MakeResponse(false, "rename", "Name already in use") << "\n"; return; }
                BaseObject* obj = objIt->second;
                g_namedObjects.erase(objIt);
                g_namedObjects[finalName] = obj;
                out << MakeResponse(true, "rename", "'" + oldN + "' -> '" + finalName + "'") << "\n";
                return;
            }
            auto ltIt = g_namedLights.find(oldN);
            if (ltIt != g_namedLights.end()) {
                if (g_namedLights.count(finalName)) { out << MakeResponse(false, "rename", "Name already in use") << "\n"; return; }
                BaseLight* lt = ltIt->second;
                g_namedLights.erase(ltIt);
                g_namedLights[finalName] = lt;
                out << MakeResponse(true, "rename", "'" + oldN + "' -> '" + finalName + "'") << "\n";
                return;
            }
            out << MakeResponse(false, "rename", "Unknown '" + oldN + "'") << "\n";
        });
    // clearscene
    reg.Register("clearscene", "clearscene", "Remove all objects and lights.",
        [](CmdContext& ctx, const std::vector<std::string>&, std::ostream& out) {
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            for (auto& [name, obj] : g_namedObjects) {
                auto& objs = *ctx.sm->objects;
                objs.erase(std::remove(objs.begin(), objs.end(), obj), objs.end());
                delete obj;
            }
            g_namedObjects.clear();
            for (auto& [name, lt] : g_namedLights) {
                auto& lights = *ctx.sm->lights;
                lights.erase(std::remove(lights.begin(), lights.end(), lt), lights.end());
                delete lt;
            }
            g_namedLights.clear();
            out << MakeResponse(true, "clearscene", "Scene cleared") << "\n";
        });
    // savescene <path> — delegates to the shared SceneSave() in scene_io.cpp
    reg.Register("savescene", "savescene <path>", "Save the current scene to a .honscene file.",
        [](CmdContext&, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) {
                out << MakeResponse(false, "savescene", "Usage: savescene <path>") << "\n";
                return;
            }
            std::string path = args[0];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            SceneSaveResult r = SceneSave(path);
            if (r.ok)
                out << MakeResponse(true, "savescene", "Scene saved: " + path) << "\n";
            else
                out << MakeResponse(false, "savescene", r.errorMsg) << "\n";
        });

    // loadscene <path> — delegates to the shared SceneLoad() in scene_io.cpp.
    // The caller is responsible for sending "clearscene" first if a clean load is desired.
    reg.Register("loadscene", "loadscene <path>", "Load a .honscene file into the current scene.",
        [assetDb, sceneMgr](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.empty()) {
                out << MakeResponse(false, "loadscene", "Usage: loadscene <path>") << "\n";
                return;
            }
            std::string path = args[0];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            SceneLoadResult r = SceneLoad(path, ctx.sm, assetDb, sceneMgr);
            if (!r.ok) {
                out << MakeResponse(false, "loadscene", r.errorMsg) << "\n";
                return;
            }
            std::string msg = "Loaded " + std::to_string(r.objCount) + " objects, " +
                std::to_string(r.lightCount) + " lights from " + path;
            if (!r.warnings.empty()) msg += " (warnings: " + r.warnings + ")";
            out << MakeResponse(true, "loadscene", msg) << "\n";
        });
    // quit
    reg.Register("quit", "quit", "Exit the application.",
        [](CmdContext&, const std::vector<std::string>&, std::ostream& out) {
            out << MakeResponse(true, "quit", "Shutting down...") << "\n";
            SDL_Event qev{}; qev.type = SDL_QUIT; SDL_PushEvent(&qev);
        });

    // obj — import a Wavefront OBJ file into the scene
    reg.Register("obj", "obj <name> <path> [x] [y] [z]",
        "Import an OBJ mesh.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "obj", "Usage: obj <name> <path> [x] [y] [z]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x = 0, y = 0, z = 0;
            if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }
            // Strip surrounding quotes if present
            std::string path = args[1];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            BaseObject* obj = nullptr;
            try {
                obj = Importer::ImportFromOBJ(path);
            }
            catch (const std::exception& e) {
                out << MakeResponse(false, "obj", std::string("Import failed: ") + e.what()) << "\n";
                return;
            }
            if (!obj) {
                out << MakeResponse(false, "obj", "ImportFromOBJ returned null for: " + path) << "\n";
                return;
            }
            obj->transform.position = Vector3(x, y, z);
            obj->tag = "OBJ:" + path;
            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            ctx.sm->objects->push_back(obj);

            g_namedObjects[finalName] = obj;

            out << MakeResponse(true, "obj", "'" + finalName + "' imported from " + path) << "\n";
        });

    // gltf — FIX 1 : on diffère l'import sur le thread principal
    reg.Register("gltf", "gltf <name> <path> [x] [y] [z]",
        "Import a glTF/GLB mesh.",
        [renderer](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "gltf", "Usage: gltf <name> <path> [x] [y] [z]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x = 0, y = 0, z = 0;
            if (args.size() >= 5) { ParseDouble(args[2], x); ParseDouble(args[3], y); ParseDouble(args[4], z); }
            std::string path = args[1];
            if (!path.empty() && path.front() == '"') path = path.substr(1);
            if (!path.empty() && path.back() == '"') path.pop_back();

            {
                std::lock_guard<std::mutex> lk(g_deferredTasksMutex);
                g_deferredGLTFTasks.push_back({ finalName, path, x, y, z });
            }
            out << MakeResponse(true, "gltf", "'" + finalName + "' queued for import") << "\n";
        });

    // sphere — spawn a UV sphere approximated with a scaled cube (replace with real sphere mesh if available)
    // sphere — spawn a sphere primitive
    reg.Register("sphere", "sphere <name> <x> <y> <z> [radius=0.5] [rings=20] [segments=20] [color]",
        "Spawn a sphere primitive.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) {
                out << MakeResponse(false, "sphere", "Usage: sphere <name> <x> <y> <z> [radius=0.5] [rings=20] [segments=20] [color]") << "\n";
                return;
            }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            // If name was changed, log it
            if (finalName != args[0]) {
                out << MakeResponse(true, "plane", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";
            }

            double x, y, z, radius = 0.5;
            int rings = 20, segments = 20;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "sphere", "Bad coords") << "\n";
                return;
            }

            if (args.size() >= 5) ParseDouble(args[4], radius);
            if (args.size() >= 6) ParseInt(args[5], rings);
            if (args.size() >= 7) ParseInt(args[6], segments);
            Color col = args.size() >= 8 ? NameToColor(args[7]) : colWhite;

            // Clamp to reasonable values
            rings = std::clamp(rings, 3, 100);
            segments = std::clamp(segments, 3, 100);

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            // Use the proper Sphere class
            BaseObject* obj = new HonHengine::Sphere(Vector3(radius, radius, radius),
                Vector3(x, y, z),
                Quaternion(),
                new Material(0, 0, col, colBlack),
                segments, rings);
            ctx.sm->objects->push_back(obj);

            g_namedObjects[finalName] = obj;

            std::ostringstream d;
            d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"radius\":" << radius
                << ",\"rings\":" << rings << ",\"segments\":" << segments
                << ",\"color\":" << JStr(args.size() >= 8 ? args[7] : "white");
            out << MakeResponse(true, "sphere", "'" + finalName + "' spawned", d.str()) << "\n";
        });

    // rect — spawn a flat rectangle (thin slab)
    reg.Register("rect", "rect <name> <x> <y> <z> [w] [h] [segmentsX] [segmentsY] [segmentsZ] [color]",
        "Spawn a flat rectangle.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 4) { out << MakeResponse(false, "rect", "Usage: rect <name> <x> <y> <z> [w] [h] [segmentsX] [segmentsY] [segmentsZ] [color]") << "\n"; return; }

            std::string finalName = NameValidator::GetFinalName(args[0], g_namedObjects, g_namedLights);
            if (finalName != args[0]) out << MakeResponse(true, "rect", "Name '" + args[0] + "' changed to '" + finalName + "'") << "\n";

            double x, y, z, w = 5.0, h = 5.0;
            int segX = 1, segY = 1, segZ = 1;
            if (!ParseDouble(args[1], x) || !ParseDouble(args[2], y) || !ParseDouble(args[3], z)) {
                out << MakeResponse(false, "rect", "Bad coords") << "\n"; return;
            }
            if (args.size() >= 5) ParseDouble(args[4], w);
            if (args.size() >= 6) ParseDouble(args[5], h);
            if (args.size() >= 7) ParseInt(args[6], segX);
            if (args.size() >= 8) ParseInt(args[7], segY);
            if (args.size() >= 9) ParseInt(args[8], segZ);
            Color col = args.size() >= 10 ? NameToColor(args[9]) : colWhite;

            segX = (std::max)(1, segX); segY = (std::max)(1, segY); segZ = (std::max)(1, segZ);

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            BaseObject* obj = new HonHengine::Rectangle(Vector3(w * 0.5, w * 0.5, h * 0.5), Vector3(x, y, z), Quaternion(),
                new Material(0, 0, col, colBlack), segX, segY, segZ);
            ctx.sm->objects->push_back(obj);
            g_namedObjects[finalName] = obj;

            std::ostringstream d; d << std::fixed << std::setprecision(4);
            d << JVec3("pos", x, y, z) << ",\"w\":" << w << ",\"h\":" << h
                << ",\"segmentsX\":" << segX << ",\"segmentsY\":" << segY << ",\"segmentsZ\":" << segZ;
            out << MakeResponse(true, "rect", "'" + finalName + "' spawned", d.str()) << "\n";
        });

    // ── attachscript <objectName> <scriptGUID> ────────────────────────────────
    // Resolves the GUID to a source path via the captured AssetDatabase,
    // creates a ScriptComponent, and pushes it onto the target object's scripts
    // vector.  Thread-safe via g_sceneMutex.
    reg.Register("attachscript",
        "attachscript <objectName> <scriptGUID>",
        "Attach a script asset (by GUID) to a named scene object.",
        [assetDb, sceneMgr](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "attachscript",
                    "Usage: attachscript <objectName> <scriptGUID>") << "\n";
                return;
            }
            const std::string& objName = args[0];
            const std::string& guid = args[1];

            // Resolve GUID → source path via the captured AssetDatabase
            std::string srcPath;
            if (assetDb) {
                AssetRecord* rec = assetDb->FindByGUID(guid);
                if (rec) srcPath = rec->path;
            }
            if (srcPath.empty()) srcPath = guid;  // fallback: use the GUID string as path

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(objName);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "attachscript",
                    "Unknown object '" + objName + "'") << "\n";
                return;
            }
            BaseObject* obj = it->second;

            ScriptComponent newComp;
            newComp.scriptGUID = guid;
            newComp.compiledPath = "";

            ScriptManager* sm = sceneMgr ? sceneMgr->scriptManager.get() : nullptr;
            if (sm && sm->LoadScript(guid, srcPath, newComp)) {
                obj->scripts.push_back(newComp);
                out << MakeResponse(true, "attachscript",
                    "Script '" + guid + "' attached to '" + objName + "'") << "\n";
            }
            else {
                // ScriptManager unavailable or load failed — record the component
                // so the Inspector can still display and manage it.
                obj->scripts.push_back(newComp);
                out << MakeResponse(true, "attachscript",
                    "Script component recorded for '" + objName +
                    "' (runtime load skipped)") << "\n";
            }
        });

    // ── setmaterial <objectName> <materialPath> ───────────────────────────────
    // Replaces the object's material.  For .honmat files a placeholder white
    // material is created (a full material asset pipeline can extend this).
    // For any other path the same default is used.  Thread-safe.
    reg.Register("setmaterial",
        "setmaterial <objectName> <materialPath>",
        "Assign a material asset to a named scene object.",
        [](CmdContext& ctx, const std::vector<std::string>& args, std::ostream& out) {
            if (args.size() < 2) {
                out << MakeResponse(false, "setmaterial",
                    "Usage: setmaterial <objectName> <materialPath>") << "\n";
                return;
            }
            const std::string& objName = args[0];
            const std::string& matPath = args[1];

            std::unique_lock<std::shared_mutex> lock(*ctx.sceneMutex);
            auto it = g_namedObjects.find(objName);
            if (it == g_namedObjects.end()) {
                out << MakeResponse(false, "setmaterial",
                    "Unknown object '" + objName + "'") << "\n";
                return;
            }
            BaseObject* obj = it->second;

            // Delete the old material to avoid a leak
            delete obj->material;
            // Create a fresh default white material.  If a full .honmat loader
            // is added later, insert it here based on the file extension.
            obj->material = new Material(0, 0,
                Color(255.0, 255.0, 255.0, 255.0),
                Color(0.0, 0.0, 0.0, 255.0));

            out << MakeResponse(true, "setmaterial",
                "Material '" + matPath + "' assigned to '" + objName + "'") << "\n";
        });
}

// Thin bridge: IDEState -> ShipConfig, runs ShipBuilder on a background thread.
static void RunShipBuild(const HonHengine::ShipConfig& cfg, ConsoleLog& log)
{
    using namespace HonHengine;
    ShipBuilder builder;
    std::atomic<bool> cancel{ false };

    ShipResult result = builder.Build(cfg,
        [&log](float /*progress*/, const std::string& msg, bool isError) {
            log.push(isError ? ConsoleLog::REPLY_ERR : ConsoleLog::INFO, msg);
        },
        cancel);

    if (result.success) {
        log.push(ConsoleLog::REPLY_OK,
            "[Ship] Build succeeded -> " + result.executablePath);
    }
    else {
        log.push(ConsoleLog::REPLY_ERR,
            "[Ship] Build FAILED: " + result.errorLog);
    }
}
// =============================================================================
//  Fonctions de rafraîchissement de la hiérarchie (inchangées)
// =============================================================================
static void DrawWelcomeModal(IDEState& ide, bool& firstRun) {
    if (!firstRun) return;
    ImGui::OpenPopup("Welcome to HonHon Engine");
    firstRun = false;

    if (ImGui::BeginPopupModal("Welcome to HonHon Engine", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Welcome to the HonHon Engine IDE!");
        ImGui::Separator();
        ImGui::BulletText("Press Ctrl+F to search across objects, assets, and commands");
        ImGui::BulletText("Use Q/W/E/R to switch tools (View/Move/Rotate/Scale)");
        ImGui::BulletText("Right-click in Hierarchy for object options");
        ImGui::BulletText("Drag assets from Asset Browser into the Viewport to instantiate");
        ImGui::Spacing();
        if (ImGui::Button("Get Started", { 200, 0 })) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void PushSelectionHistory(IDEState& ide, const std::string& selection) {
    if (ide.inspectorLocked) return;
    ide.selectionHistory.Push(selection);
}

// Rebuild the flat+folder hierarchy.
static void RebuildHierarchy(IDEState& ide)
{
    std::unordered_map<std::string, bool> inTree; // name → isLight
    std::function<void(std::vector<HierarchyNode>&)> collect = [&](std::vector<HierarchyNode>& nodes) {
        for (auto& n : nodes) {
            if (n.kind == HierarchyNode::FOLDER) collect(n.children);
            else inTree[n.name] = (n.kind == HierarchyNode::LIGHT);
        }
        };
    collect(ide.hierRoots);

    // Ajouter les objets non présents dans l'arborescence
    for (auto& obj : ide.objects)
        if (!inTree.count(obj.name)) {
            HierarchyNode n;
            n.kind = HierarchyNode::OBJECT;
            n.name = obj.name;
            // Restaurer l'état d'ouverture du dossier si nécessaire
            ide.hierRoots.push_back(n);
        }
    for (auto& lt : ide.lights)
        if (!inTree.count(lt.name)) {
            HierarchyNode n;
            n.kind = HierarchyNode::LIGHT;
            n.name = lt.name;
            ide.hierRoots.push_back(n);
        }
    // Add cameras that are not yet in the tree
    for (auto& cam : ide.cameras)
        if (!inTree.count(cam.name)) {
            HierarchyNode n;
            n.kind = HierarchyNode::CAMERA;
            n.name = cam.name;
            ide.hierRoots.push_back(n);
        }

    // Nettoyer les références invalides
    std::unordered_set<std::string> objNames, ltNames;
    for (auto& o : ide.objects) objNames.insert(o.name);
    for (auto& l : ide.lights)  ltNames.insert(l.name);
    std::unordered_set<std::string> camNames;
    for (auto& c : ide.cameras) camNames.insert(c.name);

    std::function<void(std::vector<HierarchyNode>&)> prune = [&](std::vector<HierarchyNode>& nodes) {
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](HierarchyNode& n) -> bool {
            if (n.kind == HierarchyNode::FOLDER) {
                prune(n.children);
                // Supprimer les dossiers vides
                return n.children.empty() && n.name != "Root";
            }
            if (n.kind == HierarchyNode::OBJECT) return !objNames.count(n.name);
            if (n.kind == HierarchyNode::LIGHT)  return !ltNames.count(n.name);
            if (n.kind == HierarchyNode::CAMERA) return !camNames.count(n.name);
            return false;
            }), nodes.end());
        };
    prune(ide.hierRoots);
}

static void ParseListReply(const std::string& json, IDEState& ide)
{
    // Save current selection and old object metadata before parsing
    std::set<std::string> previousSelection = ide.selection.items;
    std::string oldPrimary = ide.selection.Primary();

    static std::vector<IDEObject> lastObjects;
    static std::vector<IDELight> lastLights;

    std::vector<IDEObject> newObjects;
    std::vector<IDELight> newLights;

    // ─────────────────────────────────────────────────────────────────────────
    //  PARSE OBJECTS
    // ─────────────────────────────────────────────────────────────────────────
    size_t objStart = json.find("\"objects\":[");
    if (objStart != std::string::npos) {
        size_t objEnd = json.find("],\"lights\"", objStart);
        if (objEnd == std::string::npos) objEnd = json.find("]}", objStart);

        std::string objectsStr = json.substr(objStart + 10, objEnd - objStart - 10);

        size_t pos = 0;
        while (true) {
            // Find object start
            size_t objPos = objectsStr.find("{", pos);
            if (objPos == std::string::npos) break;

            // Find matching closing brace
            int braceDepth = 0;
            size_t objEndPos = objPos;
            for (size_t i = objPos; i < objectsStr.size(); ++i) {
                if (objectsStr[i] == '{') braceDepth++;
                else if (objectsStr[i] == '}') {
                    braceDepth--;
                    if (braceDepth == 0) {
                        objEndPos = i;
                        break;
                    }
                }
            }
            std::string objJson = objectsStr.substr(objPos, objEndPos - objPos + 1);
            pos = objEndPos + 1;

            // --- Parse object fields ---
            IDEObject obj;

            // name
            obj.name = JsonGet(objJson, "name");

            // visible
            std::string visStr = JsonGet(objJson, "visible");
            obj.visible = (visStr != "false");

            // position
            std::string posJson = JsonGet(objJson, "pos");
            if (!posJson.empty()) {
                sscanf_s(posJson.c_str(), "{\"x\":%f,\"y\":%f,\"z\":%f}",
                    &obj.px, &obj.py, &obj.pz);
            }

            // scale
            std::string scaleJson = JsonGet(objJson, "scale");
            if (!scaleJson.empty()) {
                sscanf_s(scaleJson.c_str(), "{\"x\":%f,\"y\":%f,\"z\":%f}",
                    &obj.sx, &obj.sy, &obj.sz);
            }

            // shader
            obj.shader = JsonGet(objJson, "shader");

            // type (direct from engine – no inference!)
            obj.type = JsonGet(objJson, "type");

            // assetPath (for OBJ/GLTF models)
            obj.assetPath = JsonGet(objJson, "assetPath");

            // tag
            obj.tag = JsonGet(objJson, "tag");

            // color
            std::string colorJson = JsonGet(objJson, "color");
            if (!colorJson.empty()) {
                sscanf_s(colorJson.c_str(), "{\"r\":%d,\"g\":%d,\"b\":%d,\"a\":%d}",
                    &obj.cr, &obj.cg, &obj.cb, &obj.ca);
            }

            // default values for fields not in JSON
            obj.locked = false;
            obj.rw = 1.0f;
            obj.rx = obj.ry = obj.rz = 0.0f;

            newObjects.push_back(obj);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  PARSE LIGHTS
    // ─────────────────────────────────────────────────────────────────────────
    size_t lightStart = json.find("\"lights\":[");
    if (lightStart != std::string::npos) {
        size_t contentStart = lightStart + 10;
        size_t lightEnd = json.find("]", contentStart);
        if (lightEnd == std::string::npos) lightEnd = json.size();
        std::string lightsStr = json.substr(contentStart, lightEnd - contentStart);

        size_t pos = 0;
        while (true) {
            size_t namePos = lightsStr.find("\"name\":\"", pos);
            if (namePos == std::string::npos) break;

            size_t nameEnd = lightsStr.find("\"", namePos + 8);
            std::string name = lightsStr.substr(namePos + 8, nameEnd - namePos - 8);

            IDELight light;
            light.name = name;
            light.lightType = "point";
            light.intensity = 1.0f;
            light.r = light.g = light.b = 255;
            light.px = light.py = light.pz = 0.0f;
            light.rotx = light.roty = light.rotz = 0.0f;

            // Extract intensity
            size_t intPos = lightsStr.find("\"intensity\":", nameEnd);
            if (intPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + intPos + 12, "%f", &light.intensity);
            }

            // Extract light type
            size_t typePos = lightsStr.find("\"type\":\"", nameEnd);
            if (typePos != std::string::npos) {
                size_t typeEnd = lightsStr.find("\"", typePos + 8);
                if (typeEnd != std::string::npos) {
                    light.lightType = lightsStr.substr(typePos + 8, typeEnd - typePos - 8);
                }
            }

            // Extract position (if present)
            size_t posXPos = lightsStr.find("\"x\":", nameEnd);
            if (posXPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posXPos + 4, "%f", &light.px);
            }
            size_t posYPos = lightsStr.find("\"y\":", posXPos + 1);
            if (posYPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posYPos + 4, "%f", &light.py);
            }
            size_t posZPos = lightsStr.find("\"z\":", posYPos + 1);
            if (posZPos != std::string::npos) {
                sscanf_s(lightsStr.c_str() + posZPos + 4, "%f", &light.pz);
            }

            newLights.push_back(light);
            pos = nameEnd + 1;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  DETECT SCENE CHANGES
    // ─────────────────────────────────────────────────────────────────────────
    bool changed = (newObjects.size() != lastObjects.size() || newLights.size() != lastLights.size());
    if (!changed) {
        for (size_t i = 0; i < newObjects.size(); i++) {
            if (newObjects[i].name != lastObjects[i].name) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        for (size_t i = 0; i < newLights.size(); i++) {
            if (newLights[i].name != lastLights[i].name ||
                newLights[i].lightType != lastLights[i].lightType) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        ide.objects = std::move(newObjects);
        ide.lights = std::move(newLights);
        lastObjects = ide.objects;
        lastLights = ide.lights;
        RebuildHierarchy(ide);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  RESTORE SELECTION
    // ─────────────────────────────────────────────────────────────────────────
    std::set<std::string> restoredSelection;
    for (const auto& name : previousSelection) {
        bool found = false;
        for (const auto& obj : ide.objects) {
            if (obj.name == name) { found = true; break; }
        }
        if (!found) {
            for (const auto& lt : ide.lights) {
                if (lt.name == name) { found = true; break; }
            }
        }
        // Also keep cameras in the selection
        if (!found) {
            for (const auto& cam : ide.cameras) {
                if (cam.name == name) { found = true; break; }
            }
        }
        if (found) {
            restoredSelection.insert(name);
        }
    }

    // Update selection
    ide.selection.items = restoredSelection;

    // If selection has changed, update inspector
    if (restoredSelection.empty()) {
        if (!oldPrimary.empty()) {
            ide.log.push(ConsoleLog::INFO, "[Selection] Selection cleared");
        }
    }
    else if (restoredSelection.size() == 1) {
        std::string newPrimary = ide.selection.Primary();
        if (newPrimary != oldPrimary) {
            ide.bus.send("inspect " + newPrimary);
        }
    }
    else if (restoredSelection.size() > 1 && oldPrimary != ide.selection.Primary()) {
        // For multi-selection, inspect the primary
        ide.bus.send("inspect " + ide.selection.Primary());
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  HANDLE PENDING SELECTION (after object creation)
    // ─────────────────────────────────────────────────────────────────────────
    if (!ide.pendingSelection.empty()) {
        if (ide.selection.Contains(ide.pendingSelection)) {
            ide.pendingSelection.clear();
        }
        else {
            // Wait for the object to appear in the list
            bool pendingFound = false;
            for (const auto& obj : ide.objects) {
                if (obj.name == ide.pendingSelection) {
                    pendingFound = true;
                    break;
                }
            }
            if (!pendingFound) {
                for (const auto& lt : ide.lights) {
                    if (lt.name == ide.pendingSelection) {
                        pendingFound = true;
                        break;
                    }
                }
            }
            if (pendingFound) {
                ide.selection.SetSingle(ide.pendingSelection);
                ide.bus.send("inspect " + ide.pendingSelection);
                ide.pendingSelection.clear();
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  UPDATE LOCKED INSPECTOR
    // ─────────────────────────────────────────────────────────────────────────
    if (ide.inspectorLocked && !ide.lockedInspectorObject.empty()) {
        bool lockedExists = false;
        for (const auto& obj : ide.objects)
            if (obj.name == ide.lockedInspectorObject) { lockedExists = true; break; }
        if (!lockedExists)
            for (const auto& lt : ide.lights)
                if (lt.name == ide.lockedInspectorObject) { lockedExists = true; break; }
        if (!lockedExists)
            for (const auto& cam : ide.cameras)
                if (cam.name == ide.lockedInspectorObject) { lockedExists = true; break; }
        if (!lockedExists) {
            ide.inspectorLocked = false;
            ide.lockedInspectorObject.clear();
        }
    }
}

static void ParseInspectReply(const std::string& json, IDEState& ide)
{
    // Camera nodes are handled entirely by the camera inspector — skip parse.
    if (json.find("camera:") != std::string::npos) return;
    // Also skip failed inspects whose target is in ide.cameras
    {
        std::string msg = JsonGet(json, "msg");
        if (!msg.empty() && msg.rfind("camera:", 0) == 0) return;
        for (auto& c : ide.cameras)
            if (c.name == JsonGet(json, "name")) return;
    }

    ide.inspPos[0] = ide.inspPos[1] = ide.inspPos[2] = 0.f;
    ide.inspScale[0] = ide.inspScale[1] = ide.inspScale[2] = 1.f;
    ide.inspRot[0] = ide.inspRot[1] = ide.inspRot[2] = 0.f;
    ide.inspColor[0] = ide.inspColor[1] = ide.inspColor[2] = 1.f;
    ide.inspColor[3] = 1.f;
    ide.inspShader[0] = '\0';
    ide.inspTag[0] = '\0';

    auto posB = json.find("\"pos\":{");
    if (posB != std::string::npos) {
        std::string posS = json.substr(posB + 7);
        auto pe = posS.find('}');
        if (pe != std::string::npos) posS = posS.substr(0, pe);
        try { ide.inspPos[0] = std::stof(JsonGet(posS, "x")); }
        catch (...) {}
        try { ide.inspPos[1] = std::stof(JsonGet(posS, "y")); }
        catch (...) {}
        try { ide.inspPos[2] = std::stof(JsonGet(posS, "z")); }
        catch (...) {}
    }
    auto scB = json.find("\"scale\":{");
    if (scB != std::string::npos) {
        std::string scS = json.substr(scB + 9);
        auto se = scS.find('}');
        if (se != std::string::npos) scS = scS.substr(0, se);
        try { ide.inspScale[0] = std::stof(JsonGet(scS, "x")); }
        catch (...) {}
        try { ide.inspScale[1] = std::stof(JsonGet(scS, "y")); }
        catch (...) {}
        try { ide.inspScale[2] = std::stof(JsonGet(scS, "z")); }
        catch (...) {}
    }
    auto cB = json.find("\"color\":{");
    if (cB != std::string::npos) {
        std::string cS = json.substr(cB + 9);
        auto ce = cS.find('}');
        if (ce != std::string::npos) cS = cS.substr(0, ce);
        try { ide.inspColor[0] = std::stof(JsonGet(cS, "r")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[1] = std::stof(JsonGet(cS, "g")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[2] = std::stof(JsonGet(cS, "b")) / 255.f; }
        catch (...) {}
        try { ide.inspColor[3] = std::stof(JsonGet(cS, "a")) / 255.f; }
        catch (...) {}
    }

    std::string inspectedName = JsonGet(json, "name");

    auto visB = json.find("\"visible\":");
    if (visB != std::string::npos) {
        std::string visStr = json.substr(visB + 10);
        bool visible = (visStr.find("true") != std::string::npos);
        // Mettre à jour l'objet dans ide.objects
        for (auto& obj : ide.objects) {
            if (obj.name == inspectedName) {
                obj.visible = visible;
                break;
            }
        }
    }
    std::string sh = JsonGet(json, "shader");
    strncpy_s(ide.inspShader, sizeof(ide.inspShader), sh.c_str(), sizeof(ide.inspShader) - 1);

    if (inspectedName.empty()) inspectedName = ide.selection.Primary();
    for (auto& lt : ide.lights) {
        if (lt.name == inspectedName) {
            try {
                std::string intensityStr = JsonGet(json, "intensity");
                if (!intensityStr.empty()) {
                    lt.intensity = std::stof(intensityStr);
                }
            }
            catch (...) {
                ide.log.push(ConsoleLog::REPLY_ERR,
                    "[ParseInspect] Failed to parse intensity for " + inspectedName);
            }
            auto cB2 = json.find("\"color\":{");
            if (cB2 != std::string::npos) {
                std::string cS2 = json.substr(cB2 + 9);
                auto ce2 = cS2.find('}');
                if (ce2 != std::string::npos) cS2 = cS2.substr(0, ce2);
                try { lt.r = (int)std::stof(JsonGet(cS2, "r")); }
                catch (...) {}
                try { lt.g = (int)std::stof(JsonGet(cS2, "g")); }
                catch (...) {}
                try { lt.b = (int)std::stof(JsonGet(cS2, "b")); }
                catch (...) {}
            }
            std::string lt_type = JsonGet(json, "type");
            if (!lt_type.empty()) lt.lightType = lt_type;
            lt.px = ide.inspPos[0];
            lt.py = ide.inspPos[1];
            lt.pz = ide.inspPos[2];
            break;
        }
    }
}

static void HandleReply(const std::string& json, IDEState& ide)
{
    std::string cmd = JsonGet(json, "cmd");
    bool ok = JsonOk(json);
    std::string msg = JsonGet(json, "msg");

    if (cmd == "list") { ParseListReply(json, ide); }
    if (cmd == "inspect") { ParseInspectReply(json, ide); }

    ConsoleLog::Kind k = ok ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR;
    ide.log.push(k, "[" + cmd + "] " + msg);
}

static void RefreshSceneList(IDEState& ide) { ide.bus.send("list"); }

static void ShellThread(CommandBus* bus, CommandRegistry* reg, CmdContext* ctx, std::atomic<bool>* running, ConsoleLog* log) {
    log->push(ConsoleLog::INFO, "[Shell] Engine shell ready.");
    while (*running) {
        std::string cmd;
        if (!bus->pop(cmd)) continue;
        if (cmd.empty()) continue;
        std::ostringstream reply;
        reg->Dispatch(cmd, *ctx, reply);
        std::string line = reply.str();
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (!line.empty()) {
            bool ok = JsonOk(line);
            log->push(ok ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR, line);
        }
    }
}


static void ApplyIDEStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 6.f;
    s.ChildRounding = 4.f;
    s.FrameRounding = 4.f;
    s.PopupRounding = 5.f;
    s.ScrollbarRounding = 6.f;
    s.GrabRounding = 4.f;
    s.TabRounding = 4.f;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.PopupBorderSize = 1.f;

    s.FramePadding = { 8, 5 };
    s.ItemSpacing = { 8, 6 };
    s.ItemInnerSpacing = { 6, 4 };
    s.IndentSpacing = 18.f;
    s.ScrollbarSize = 11.f;
    s.GrabMinSize = 10.f;
    s.WindowTitleAlign = { 0.0f, 0.5f };
    s.SeparatorTextBorderSize = 1.f;

    const ImVec4 bg0 = { 0.078f, 0.082f, 0.094f, 1.f };
    const ImVec4 bg1 = { 0.110f, 0.114f, 0.129f, 1.f };
    const ImVec4 bg2 = { 0.145f, 0.150f, 0.168f, 1.f };
    const ImVec4 bg3 = { 0.190f, 0.196f, 0.216f, 1.f };
    const ImVec4 bg4 = { 0.240f, 0.248f, 0.270f, 1.f };

    const ImVec4 acc0 = { 0.22f, 0.47f, 0.80f, 1.f };
    const ImVec4 acc1 = { 0.30f, 0.55f, 0.88f, 1.f };
    const ImVec4 acc2 = { 0.18f, 0.40f, 0.70f, 1.f };

    const ImVec4 bdr = { 0.24f, 0.25f, 0.28f, 1.f };

    const ImVec4 txt = { 0.90f, 0.91f, 0.93f, 1.f };
    const ImVec4 txtD = { 0.48f, 0.50f, 0.54f, 1.f };

    auto* c = s.Colors;

    c[ImGuiCol_WindowBg] = bg1;
    c[ImGuiCol_ChildBg] = bg0;
    c[ImGuiCol_PopupBg] = { 0.13f, 0.135f, 0.15f, 0.98f };
    c[ImGuiCol_MenuBarBg] = bg0;

    c[ImGuiCol_Border] = bdr;
    c[ImGuiCol_BorderShadow] = { 0,0,0,0 };

    c[ImGuiCol_FrameBg] = bg2;
    c[ImGuiCol_FrameBgHovered] = bg3;
    c[ImGuiCol_FrameBgActive] = bg4;

    c[ImGuiCol_TitleBg] = bg0;
    c[ImGuiCol_TitleBgActive] = { 0.15f, 0.16f, 0.19f, 1.f };
    c[ImGuiCol_TitleBgCollapsed] = bg0;

    c[ImGuiCol_ScrollbarBg] = bg0;
    c[ImGuiCol_ScrollbarGrab] = bg4;
    c[ImGuiCol_ScrollbarGrabHovered] = { 0.30f, 0.31f, 0.34f, 1.f };
    c[ImGuiCol_ScrollbarGrabActive] = acc0;

    c[ImGuiCol_CheckMark] = acc1;
    c[ImGuiCol_SliderGrab] = acc0;
    c[ImGuiCol_SliderGrabActive] = acc1;

    c[ImGuiCol_Button] = { acc0.x, acc0.y, acc0.z, 0.75f };
    c[ImGuiCol_ButtonHovered] = acc1;
    c[ImGuiCol_ButtonActive] = acc2;

    c[ImGuiCol_Header] = { acc0.x, acc0.y, acc0.z, 0.45f };
    c[ImGuiCol_HeaderHovered] = { acc0.x, acc0.y, acc0.z, 0.65f };
    c[ImGuiCol_HeaderActive] = acc0;

    c[ImGuiCol_Separator] = bdr;
    c[ImGuiCol_SeparatorHovered] = acc0;
    c[ImGuiCol_SeparatorActive] = acc1;

    c[ImGuiCol_ResizeGrip] = { acc0.x, acc0.y, acc0.z, 0.20f };
    c[ImGuiCol_ResizeGripHovered] = acc0;
    c[ImGuiCol_ResizeGripActive] = acc1;

    c[ImGuiCol_Tab] = { 0.13f, 0.135f, 0.15f, 1.f };
    c[ImGuiCol_TabHovered] = bg3;
    c[ImGuiCol_TabActive] = { 0.18f, 0.19f, 0.22f, 1.f };
    c[ImGuiCol_TabUnfocused] = bg0;
    c[ImGuiCol_TabUnfocusedActive] = bg1;

    c[ImGuiCol_PlotLines] = acc0;
    c[ImGuiCol_PlotLinesHovered] = acc1;
    c[ImGuiCol_PlotHistogram] = acc0;
    c[ImGuiCol_PlotHistogramHovered] = acc1;

    c[ImGuiCol_TableHeaderBg] = bg2;
    c[ImGuiCol_TableBorderStrong] = bdr;
    c[ImGuiCol_TableBorderLight] = { 0.18f, 0.19f, 0.21f, 1.f };

    c[ImGuiCol_TextSelectedBg] = { acc0.x, acc0.y, acc0.z, 0.35f };
    c[ImGuiCol_NavHighlight] = acc1;
    c[ImGuiCol_NavWindowingHighlight] = acc1;
    c[ImGuiCol_NavWindowingDimBg] = { 0,0,0,0.4f };
    c[ImGuiCol_ModalWindowDimBg] = { 0,0,0,0.5f };

    c[ImGuiCol_Text] = txt;
    c[ImGuiCol_TextDisabled] = txtD;
}


static void DrawHierarchyNodes(std::vector<HierarchyNode>& nodes, IDEState& ide,
    const std::string& filterLow)
{
    auto matchesFilter = [&](const std::string& name) {
        if (filterLow.empty()) return true;
        std::string low = name;
        for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        return low.find(filterLow) != std::string::npos;
        };

    for (auto& node : nodes)
    {
        if (node.kind == HierarchyNode::FOLDER)
        {
            bool hasMatch = filterLow.empty();
            if (!hasMatch) {
                std::function<bool(const std::vector<HierarchyNode>&)> anyMatch =
                    [&](const std::vector<HierarchyNode>& ch) -> bool {
                    for (auto& c : ch) {
                        if (c.kind == HierarchyNode::FOLDER && anyMatch(c.children)) return true;
                        if (matchesFilter(c.name)) return true;
                    }
                    return false;
                    };
                hasMatch = anyMatch(node.children);
            }
            if (!hasMatch) continue;

            ImGuiTreeNodeFlags folderFlags = ImGuiTreeNodeFlags_SpanFullWidth
                | ImGuiTreeNodeFlags_OpenOnArrow;
            if (node.folderOpen) folderFlags |= ImGuiTreeNodeFlags_DefaultOpen;

            ImGui::PushStyleColor(ImGuiCol_Text, { 0.85f, 0.75f, 0.5f, 1.f });
            bool open = ImGui::TreeNodeEx(("##folder_" + node.name).c_str(), folderFlags);
            node.folderOpen = open;
            ImGui::SameLine();
            ImGui::TextUnformatted((std::string(ICON_FA_FOLDER " ") + node.name).c_str());
            ImGui::PopStyleColor();

            // ── Folder drop target: accept hierarchy nodes dropped into this folder ──
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* folderPayload =
                    ImGui::AcceptDragDropPayload(IDEState::kHierFolderDropPayload)) {
                    // payload = null-terminated name string
                    std::string draggedName(static_cast<const char*>(folderPayload->Data));
                    // Find and move the dragged node from hierRoots into this folder
                    std::function<bool(std::vector<HierarchyNode>&, HierarchyNode&)> moveNode =
                        [&](std::vector<HierarchyNode>& src, HierarchyNode& target) -> bool {
                        for (auto it = src.begin(); it != src.end(); ++it) {
                            if (it->kind != HierarchyNode::FOLDER && it->name == draggedName) {
                                target.children.push_back(*it);
                                src.erase(it);
                                return true;
                            }
                            if (it->kind == HierarchyNode::FOLDER) {
                                if (moveNode(it->children, target)) return true;
                            }
                        }
                        return false;
                        };
                    moveNode(ide.hierRoots, node);
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::BeginPopupContextItem(("fctx_" + node.name).c_str()))
            {
                ImGui::TextDisabled(ICON_FA_FOLDER "  %s", node.name.c_str());
                ImGui::Separator();

                // ── Create sub-items ──────────────────────────────────────────
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem(ICON_FA_CUBE "  Empty Child")) {
                        HierarchyNode empty;
                        empty.kind = HierarchyNode::OBJECT;
                        // pick a unique name
                        std::string eName = "GameObject";
                        int ei = 1;
                        auto nameUsed = [&](const std::string& n) {
                            for (auto& o : ide.objects) if (o.name == n) return true;
                            for (auto& l : ide.lights)  if (l.name == n) return true;
                            return false;
                            };
                        while (nameUsed(eName)) eName = "GameObject_" + std::to_string(ei++);
                        ide.bus.send("sphere " + eName + " 0 0 0");   // placeholder
                        empty.name = eName;
                        node.children.push_back(empty);
                        ide.selection.SetSingle(eName);
                        ide.sceneDirty = true;
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();

                // ── Prefab from folder ────────────────────────────────────────
                if (ImGui::MenuItem(ICON_FA_BOXES_STACKED "  Create Prefab from Folder...")) {
                    ide.createPrefabFromFolderName = node.name;
                    ide.showCreatePrefabFromFolder = true;
                }

                ImGui::Separator();

                // ── Folder management ─────────────────────────────────────────
                if (ImGui::MenuItem(ICON_FA_PEN "  Rename Folder...")) {
                    strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), node.name.c_str(), sizeof(ide.renameOldName) - 1);
                    strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), node.name.c_str(), sizeof(ide.renameNewName) - 1);
                    ide.showRenameModal = true;
                }
                if (ImGui::MenuItem(ICON_FA_OBJECT_GROUP "  Select All Children")) {
                    ide.selection.Clear();
                    std::function<void(const std::vector<HierarchyNode>&)> selectAll =
                        [&](const std::vector<HierarchyNode>& ch) {
                        for (auto& c : ch) {
                            if (c.kind != HierarchyNode::FOLDER) ide.selection.Add(c.name);
                            else selectAll(c.children);
                        }
                        };
                    selectAll(node.children);
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.4f,0.4f,1.f });
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Dissolve Folder (keep children)")) {
                    for (auto& child : node.children)
                        ide.hierRoots.push_back(child);
                    node.children.clear();
                    node.name = "\x01__DELETE__";
                }
                if (ImGui::MenuItem(ICON_FA_TRASH "  Delete Folder and Contents")) {
                    for (auto& child : node.children) {
                        if (child.kind == HierarchyNode::LIGHT)
                            ide.bus.send("removelight " + child.name);
                        else if (child.kind == HierarchyNode::OBJECT)
                            ide.bus.send("delete " + child.name);
                    }
                    node.children.clear();
                    node.name = "\x01__DELETE__";
                    ide.sceneDirty = true;
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }

            if (open) {
                DrawHierarchyNodes(node.children, ide, filterLow);
                ImGui::TreePop();
            }
            continue;
        }

        if (!matchesFilter(node.name)) continue;

        bool isLight = (node.kind == HierarchyNode::LIGHT);
        bool isCamera = (node.kind == HierarchyNode::CAMERA);

        IDEObject* pObj = nullptr;
        IDELight* pLt = nullptr;
        IDECamera* pCam = nullptr;
        if (!isLight && !isCamera) for (auto& o : ide.objects) if (o.name == node.name) { pObj = &o; break; }
        else if (isLight)          for (auto& l : ide.lights)  if (l.name == node.name) { pLt = &l; break; }
        else                       for (auto& c : ide.cameras) if (c.name == node.name) { pCam = &c; break; }

        bool locked = pObj && pObj->locked;
        bool visible = pObj ? pObj->visible : true;

        // MODIFIED: multi-selection
        bool selected = ide.selection.Contains(node.name);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
            | ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_SpanFullWidth;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        if (locked)         ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.5f,1.f });
        else if (isCamera)  ImGui::PushStyleColor(ImGuiCol_Text, { 0.6f,0.9f,1.0f,1.f });
        else if (isLight)   ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.9f,0.4f,1.f });
        else                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

        std::string iconStr;
        if (isCamera)
            iconStr = "  " ICON_FA_VIDEO " ";
        else if (isLight)
            iconStr = (pLt && pLt->lightType == "directional")
            ? "  " ICON_FA_SUN " "
            : (pLt && pLt->lightType == "ambient")
            ? "  " ICON_FA_GLOBE " "
            : "  " ICON_FA_CIRCLE_DOT " ";
        else
            iconStr = locked ? "  " ICON_FA_LOCK " " : "  " ICON_FA_CUBE " ";

        if (!isCamera && !isLight && !visible) iconStr = "  " ICON_FA_EYE_SLASH " ";

        std::string label = iconStr + node.name;
        if (pObj && !pObj->tag.empty()) label += "  [" + pObj->tag + "]";

        ImGui::TreeNodeEx(("##hnode_" + node.name).c_str(), flags);
        bool rowClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !locked;

        // ── Drag source: drag this node to the Viewport to reposition it ────
        // Also accepts a drop on a folder node (see folder context above).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            HierarchyDragPayload hdp{};
            strncpy_s(hdp.name, sizeof(hdp.name),
                node.name.c_str(), sizeof(hdp.name) - 1);
            hdp.isLight = isLight;
            ImGui::SetDragDropPayload(kHierarchyDragPayload, &hdp, sizeof(hdp));
            // Also register the folder-into-folder payload (same data, different tag)
            ImGui::SetDragDropPayload(IDEState::kHierFolderDropPayload,
                node.name.c_str(), node.name.size() + 1);
            ImGui::TextUnformatted(isLight ? "Move Light" : "Move Object");
            ImGui::SameLine();
            ImGui::TextUnformatted(node.name.c_str());
            ImGui::EndDragDropSource();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
        bool textClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !locked;
        ImGui::PopStyleColor();

        if ((rowClicked || textClicked) && !locked) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                ide.selection.Toggle(node.name);
            }
            else {
                ide.selection.SetSingle(node.name);
            }
            ide.bus.send("inspect " + ide.selection.Primary());
        }

        // ── Drop target: accept asset payloads to attach scripts/materials ──
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* assetPayload =
                ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
                auto* ap = (AssetDragPayload*)assetPayload->Data;
                AssetRecord* rec = ide.assetDb.FindByGUID(std::string(ap->guidStr));
                if (rec) {
                    if (rec->type == AssetType::Script && !isLight) {
                        // Attach script to the BaseObject
                        BaseObject* baseObj = nullptr;
                        {
                            std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                            auto oit = g_namedObjects.find(node.name);
                            if (oit != g_namedObjects.end()) baseObj = oit->second;
                        }
                        if (baseObj) {
                            ScriptComponent newComp;
                            newComp.scriptGUID = ap->guidStr;
                            if (ide.scriptManager &&
                                ide.scriptManager->LoadScript(ap->guidStr, rec->path, newComp)) {
                                baseObj->scripts.push_back(newComp);
                                ide.log.push(ConsoleLog::REPLY_OK,
                                    "[Hierarchy] Script '" + rec->displayName
                                    + "' attached to '" + node.name + "'");
                            }
                            else {
                                ide.log.push(ConsoleLog::REPLY_ERR,
                                    "[Hierarchy] Failed to attach script " + rec->displayName);
                            }
                            ide.sceneDirty = true;
                        }
                    }
                    else if (rec->type == AssetType::Material) {
                        char cmd[512];
                        std::snprintf(cmd, sizeof(cmd), "setmaterial %s %s",
                            node.name.c_str(), rec->path.c_str());
                        ide.bus.send(cmd);
                        ide.sceneDirty = true;
                    }
                    else if ((ap->type == AssetType::Shader ||
                        rec->type == AssetType::Shader ||
                        rec->type == AssetType::ShaderSource) && !isLight) {
                        // Drop a .honshader onto an object — or drop a multi-selected
                        // .vert + .frag pair (payload.type upgraded to Shader).
                        std::string shaderName;
                        if (rec->type == AssetType::Shader) {
                            // Already a compiled .honshader — use its display name
                            shaderName = rec->displayName;
                        }
                        else if (ap->type == AssetType::Shader) {
                            // Multi-selected vert+frag pair drag: find both files from
                            // the browser's current selection, create .honshader if needed,
                            // then queue shader registration.
                            std::string vertPath, fragPath;
                            std::string stemName = fs::path(rec->path).stem().string();
                            for (auto& g : ide.assetBrowser.selectedGUIDs) {
                                auto* r = ide.assetDb.FindByGUID(g);
                                if (!r) continue;
                                std::string ext = fs::path(r->path).extension().string();
                                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                                if (ext == ".vert" && vertPath.empty()) vertPath = r->path;
                                else if (ext == ".frag" && fragPath.empty()) fragPath = r->path;
                            }
                            if (!vertPath.empty() && !fragPath.empty()) {
                                shaderName = stemName;
                                // Create .honshader sidecar if it doesn't exist yet
                                std::string dir = fs::path(vertPath).parent_path().string();
                                fs::path honPath = fs::path(dir) / (shaderName + ".honshader");
                                if (!fs::exists(honPath)) {
                                    CreateShaderAsset(dir, shaderName, vertPath, fragPath);
                                    ide.assetDb.Register(honPath.string());
                                    ide.assetBrowser.dirDirty = true;
                                }
                                // Queue GPU shader compilation
                                std::ifstream vf(vertPath), ff(fragPath);
                                std::string vsrc((std::istreambuf_iterator<char>(vf)), {});
                                std::string fsrc((std::istreambuf_iterator<char>(ff)), {});
                                if (!vsrc.empty() && !fsrc.empty()) {
                                    std::lock_guard<std::mutex> lk(g_deferredShaderMutex);
                                    g_deferredShaderTasks.push_back({ shaderName, vsrc, fsrc });
                                }
                            }
                        }
                        else {
                            // Single ShaderSource drag — just use the stem name
                            shaderName = fs::path(rec->path).stem().string();
                        }
                        if (!shaderName.empty()) {
                            char cmd[512];
                            std::snprintf(cmd, sizeof(cmd), "setshader %s %s",
                                node.name.c_str(), shaderName.c_str());
                            ide.log.push(ConsoleLog::REPLY_OK,
                                "[Hierarchy] Shader '" + shaderName
                                + "' assigned to '" + node.name + "'");
                            ide.bus.send(cmd);
                            ide.sceneDirty = true;
                        }
                    }
                    else if (rec->type == AssetType::Model ||
                        rec->type == AssetType::Prefab) {
                        // Open import overlay for 3D assets
                        ide.importOverlayGUID = ap->guidStr;
                        ide.showImportOverlay = true;
                        ide.importOverlayHasImportBtn = false;
                        ide.importOverlayOnImport = nullptr;
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Context menu — Unity-style
        if (ImGui::BeginPopupContextItem(("hctx_" + node.name).c_str()))
        {
            if (!ide.selection.Contains(node.name)) {
                ide.selection.SetSingle(node.name);
                ide.bus.send("inspect " + node.name);
            }

            size_t selCount = ide.selection.Size();
            ImGui::TextDisabled("  %s%s", node.name.c_str(),
                selCount > 1 ? (" (+" + std::to_string(selCount - 1) + " more)").c_str() : "");
            ImGui::Separator();

            // ── Create sub-menu (Unity style) ────────────────────────────────
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem(ICON_FA_CUBE "  Empty GameObject")) {
                    std::string eName = "GameObject";
                    int ei = 1;
                    auto nameUsed2 = [&](const std::string& n) {
                        for (auto& o : ide.objects) if (o.name == n) return true;
                        for (auto& l : ide.lights)  if (l.name == n) return true;
                        return false;
                        };
                    while (nameUsed2(eName)) eName = "GameObject_" + std::to_string(ei++);
                    ide.bus.send("sphere " + eName + " 0 0 0");
                    ide.sceneDirty = true;
                    ide.pendingSelection = eName;
                    RefreshSceneList(ide);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            // ── Rename ───────────────────────────────────────────────────────
            if (ImGui::MenuItem(ICON_FA_PEN "  Rename...", "F2")) {
                strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), node.name.c_str(), sizeof(ide.renameOldName) - 1);
                strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), node.name.c_str(), sizeof(ide.renameNewName) - 1);
                ide.showRenameModal = true;
            }

            // ── Duplicate ────────────────────────────────────────────────────
            if (ImGui::MenuItem(ICON_FA_COPY "  Duplicate", "Ctrl+D")) {
                std::string newName = node.name + "_copy";
                if (isCamera) {
                    if (pCam) {
                        Camera* newRtCam = new Camera(
                            Vector3(pCam->px, pCam->py, pCam->pz),
                            Quaternion::LookRotation(Vector3(0, 0, -1)));
                        ide.sm->cameras->push_back(newRtCam);
                        IDECamera dup = *pCam;
                        dup.name = newName;
                        dup.runtimeCamera = newRtCam;
                        ide.cameras.push_back(dup);
                        ide.sceneDirty = true;
                        RebuildHierarchy(ide);
                    }
                }
                else if (!isLight) {
                    ide.bus.send("clone " + node.name + " " + newName);
                    ide.sceneDirty = true;
                    ide.pendingSelection = newName;
                    RefreshSceneList(ide);
                }
                else {
                    if (pLt) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                            newName.c_str(), pLt->intensity, pLt->r, pLt->g, pLt->b,
                            pLt->lightType.c_str());
                        ide.bus.send(buf);
                    }
                    ide.sceneDirty = true;
                    ide.pendingSelection = newName;
                    RefreshSceneList(ide);
                }
            }

            if (ImGui::MenuItem(ICON_FA_SCISSORS "  Copy", "Ctrl+C")) {
                ide.clipboardName = node.name;
                ide.clipboardIsLight = isLight;
            }

            bool canPaste = !ide.clipboardName.empty();
            if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", "Ctrl+V", false, canPaste)) {
                std::string newName = ide.clipboardName + "_copy";
                if (!ide.clipboardIsLight) {
                    ide.bus.send("clone " + ide.clipboardName + " " + newName);
                }
                else {
                    IDELight* src = nullptr;
                    for (auto& l : ide.lights) if (l.name == ide.clipboardName) { src = &l; break; }
                    if (src) {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                            newName.c_str(), src->intensity, src->r, src->g, src->b,
                            src->lightType.c_str());
                        ide.bus.send(buf);
                    }
                }
                ide.sceneDirty = true;
                ide.pendingSelection = newName;
                RefreshSceneList(ide);
            }

            ImGui::Separator();

            // ── Add Parent (Create Empty Parent) — Unity's "Create Empty Parent" ──
            if (ImGui::MenuItem(ICON_FA_OBJECT_GROUP "  Add Parent (Create Empty Parent)")) {
                ide.showAddParentModal = true;
            }

            // ── Group into Folder ─────────────────────────────────────────────
            if (selCount >= 1) {
                std::string groupLabel = selCount > 1
                    ? (ICON_FA_FOLDER "  Group " + std::to_string(selCount) + " Objects into Folder...")
                    : (ICON_FA_FOLDER "  Move to New Folder...");
                if (ImGui::MenuItem(groupLabel.c_str())) {
                    ide.showCreateFolder = true;
                }
            }

            ImGui::Separator();

            // ── Focus ─────────────────────────────────────────────────────────
            if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS "  Focus", "F")) {
                if (pObj) {
                    glm::vec3 target(pObj->px, pObj->py, pObj->pz);
                    ide.focus.Start(ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize, target, 5.f);
                }
                else if (pCam) {
                    glm::vec3 target(pCam->px, pCam->py, pCam->pz);
                    ide.focus.Start(ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize, target, 5.f);
                }
            }

            ImGui::Separator();

            // ── Visibility / Lock ─────────────────────────────────────────────
            if (!isLight && pObj) {
                bool vis = pObj->visible;
                if (ImGui::MenuItem(vis
                    ? (ICON_FA_EYE_SLASH "  Hide")
                    : (ICON_FA_EYE      "  Show"))) {
                    ide.bus.send("visible " + node.name + " " + (vis ? "false" : "true"));
                    ide.sceneDirty = true;
                }
                if (ImGui::MenuItem(locked
                    ? (ICON_FA_LOCK_OPEN "  Unlock")
                    : (ICON_FA_LOCK      "  Lock"))) {
                    pObj->locked = !pObj->locked;
                    ide.sceneDirty = true;
                }
            }

            // ── Properties (select only) ──────────────────────────────────────
            if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  Properties")) {
                ide.selection.SetSingle(node.name);
                ide.bus.send("inspect " + node.name);
            }

            ImGui::Separator();

            // ── Create Prefab (single or multi) ──────────────────────────────
            if (!isLight && !isCamera) {
                if (selCount > 1) {
                    if (ImGui::MenuItem(ICON_FA_BOXES_STACKED "  Create Prefab from Selection...")) {
                        // Open multi-prefab modal (reuse folder path with empty folder name)
                        ide.createPrefabFromFolderName = "";
                        ide.showCreatePrefabFromFolder = true;
                    }
                }
                else {
                    if (ImGui::MenuItem(ICON_FA_BOX "  Create Prefab...")) {
                        // Trigger single-object prefab modal via Inspector section
                        ide.selection.SetSingle(node.name);
                        ide.bus.send("inspect " + node.name);
                    }
                }
            }

            ImGui::Separator();

            // ── Delete ────────────────────────────────────────────────────────
            ImGui::PushStyleColor(ImGuiCol_Text, { 1.f,0.4f,0.4f,1.f });
            const char* deleteLabel = isLight ? ICON_FA_TRASH "  Remove Light"
                : isCamera ? ICON_FA_TRASH "  Remove Camera"
                : ICON_FA_TRASH "  Delete";
            if (ImGui::MenuItem(deleteLabel, "Del")) {
                if (isCamera) {
                    ide.cameras.erase(
                        std::remove_if(ide.cameras.begin(), ide.cameras.end(),
                            [&](const IDECamera& c) { return c.name == node.name; }),
                        ide.cameras.end());
                    if (ide.sm && ide.sm->cameras) {
                        auto& sc = *ide.sm->cameras;
                        sc.erase(std::remove_if(sc.begin(), sc.end(),
                            [&](Camera* c) {
                                for (auto& ic : ide.cameras) if (ic.runtimeCamera == c) return false;
                                return true;
                            }), sc.end());
                    }
                    if (!ide.cameras.empty() && ide.savedEditorCamera == nullptr)
                        ide.sm->currentCamera = ide.cameras[0].runtimeCamera;
                }
                else if (isLight) {
                    ide.bus.send("removelight " + node.name);
                }
                else {
                    ide.undoStack.push_back("delete " + node.name);
                    if ((int)ide.undoStack.size() > IDEState::kUndoMaxDepth)
                        ide.undoStack.erase(ide.undoStack.begin());
                    ide.bus.send("delete " + node.name);
                }
                ide.sceneDirty = true;
                ide.selection.Remove(node.name);
                RebuildHierarchy(ide);
            }
            ImGui::PopStyleColor();

            ImGui::EndPopup();
        }
    }

    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const HierarchyNode& n) {
        return n.kind == HierarchyNode::FOLDER && n.name == "\x01__DELETE__";
        }), nodes.end());
}


static void DrawTransformInspector(IDEState& ide, const std::string& target, IDEObject* pObj, bool isLight)
{
    if (ImGui::CollapsingHeader("  Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // Copy/Paste buttons (à implémenter)

        if (isLight && pObj == nullptr) {
            IDELight* pLt = nullptr;
            for (auto& l : ide.lights) if (l.name == target) { pLt = &l; break; }
            if (pLt && pLt->lightType == "ambient") {
                ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.5f,1.f });
                ImGui::TextWrapped("  Ambient lights have no position or rotation.");
                ImGui::PopStyleColor();
            }
            else {
                float step = ide.snapEnabled ? ide.snapPosition : 0.05f;
                bool posChanged = ImGui::DragFloat3("Position##obj", ide.inspPos, step);
                if (posChanged) {
                    float* p = ide.inspPos;
                    if (ide.snapEnabled) {
                        for (int i = 0; i < 3; i++)
                            p[i] = std::round(p[i] / ide.snapPosition) * ide.snapPosition;
                    }
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f", target.c_str(), p[0], p[1], p[2]);
                    ide.bus.send(buf);
                    ide.sceneDirty = true;
                }

                float rotStep = ide.snapEnabled ? ide.snapRotation : 0.5f;
                bool rotChanged = ImGui::DragFloat3("Rotation##obj", ide.inspRot, rotStep);
                if (rotChanged) {
                    float* r = ide.inspRot;
                    if (ide.snapEnabled) {
                        for (int i = 0; i < 3; i++)
                            r[i] = std::round(r[i] / ide.snapRotation) * ide.snapRotation;
                    }
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f", target.c_str(), r[0], r[1], r[2]);
                    ide.bus.send(buf);
                    ide.sceneDirty = true;
                }
            }
        }
        else if (pObj) {
            float step = ide.snapEnabled ? ide.snapPosition : 0.05f;
            bool posChanged = ImGui::DragFloat3("Position##obj", ide.inspPos, step);
            if (posChanged) {
                float* p = ide.inspPos;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        p[i] = std::round(p[i] / ide.snapPosition) * ide.snapPosition;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f", target.c_str(), p[0], p[1], p[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
                if (ide.prefabEditMode) ide.prefabEditDirty = true;
            }

            float rotStep = ide.snapEnabled ? ide.snapRotation : 0.5f;
            bool rotChanged = ImGui::DragFloat3("Rotation##obj", ide.inspRot, rotStep);
            if (rotChanged) {
                float* r = ide.inspRot;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        r[i] = std::round(r[i] / ide.snapRotation) * ide.snapRotation;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f", target.c_str(), r[0], r[1], r[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
                if (ide.prefabEditMode) ide.prefabEditDirty = true;
            }

            float scStep = ide.snapEnabled ? ide.snapScale : 0.01f;
            bool scaleChanged = ImGui::DragFloat3("Scale##obj", ide.inspScale, scStep, 0.001f, 1000.f);
            if (scaleChanged) {
                float* s = ide.inspScale;
                if (ide.snapEnabled) {
                    for (int i = 0; i < 3; i++)
                        s[i] = std::round(s[i] / ide.snapScale) * ide.snapScale;
                }
                char buf[128];
                std::snprintf(buf, sizeof(buf), "scale %s %.4f %.4f %.4f", target.c_str(), s[0], s[1], s[2]);
                ide.bus.send(buf);
                ide.sceneDirty = true;
                if (ide.prefabEditMode) ide.prefabEditDirty = true;
            }
        }

        // Reset buttons
        if (ImGui::SmallButton("Reset Pos")) {
            ide.inspPos[0] = ide.inspPos[1] = ide.inspPos[2] = 0.f;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "move %s 0 0 0", target.c_str());
            ide.bus.send(buf); ide.sceneDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Rot")) {
            ide.inspRot[0] = ide.inspRot[1] = ide.inspRot[2] = 0.f;
            ide.bus.send(std::string("rotate ") + target + " 0 0 0");
            ide.sceneDirty = true;
        }
        if (!isLight && pObj) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset Scale")) {
                ide.inspScale[0] = ide.inspScale[1] = ide.inspScale[2] = 1.f;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "scale %s 1 1 1", target.c_str());
                ide.bus.send(buf); ide.sceneDirty = true;
            }
        }
    }
}

static void DrawLightInspector(IDEState& ide, IDELight* pLt)
{
    ImGui::PushID(pLt->name.c_str());

    if (ImGui::CollapsingHeader("  Light", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* kLightTypes[] = { "Point", "Directional", "Ambient" };
        int ltIdx = (pLt->lightType == "directional") ? 1
            : (pLt->lightType == "ambient") ? 2
            : 0;

        if (ImGui::Combo("Type##lt", &ltIdx, kLightTypes, 3)) {
            pLt->lightType = (ltIdx == 1) ? "directional"
                : (ltIdx == 2) ? "ambient"
                : "point";

            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        if (ImGui::DragFloat("Intensity##lt", &pLt->intensity, 0.05f, 0.f, 20.f)) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        float lcol[3] = { pLt->r / 255.f, pLt->g / 255.f, pLt->b / 255.f };
        if (ImGui::ColorEdit3("Color##lt", lcol)) {
            pLt->r = (int)(lcol[0] * 255);
            pLt->g = (int)(lcol[1] * 255);
            pLt->b = (int)(lcol[2] * 255);

            char buf[256];
            std::snprintf(buf, sizeof(buf), "removelight %s", pLt->name.c_str());
            ide.bus.send(buf);
            std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                pLt->name.c_str(), pLt->intensity,
                pLt->r, pLt->g, pLt->b, pLt->lightType.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f,0.2f,0.2f,1.f });
        if (ImGui::Button("Remove Light")) {
            ide.bus.send("removelight " + pLt->name);
            ide.selection.Remove(pLt->name);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
        ImGui::PopStyleColor();
    }

    ImGui::PopID();
}

static void DrawMaterialInspector(IDEState& ide, const std::string& target, IDEObject* pObj)
{
    if (ImGui::CollapsingHeader("  Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
        bool colorChanged = false;

        // Color Picker avec retour immédiat
        if (ImGui::ColorEdit4("Albedo##obj", ide.inspColor)) {
            colorChanged = true;
        }

        // Appliquer le changement quand l'utilisateur relâche la souris ou après modification
        if (ImGui::IsItemDeactivatedAfterEdit() || (colorChanged && !ImGui::IsItemActive())) {
            int r = (int)(ide.inspColor[0] * 255), g = (int)(ide.inspColor[1] * 255),
                b = (int)(ide.inspColor[2] * 255), a = (int)(ide.inspColor[3] * 255);
            char buf[128];
            std::snprintf(buf, sizeof(buf), "color %s %d %d %d %d", target.c_str(), r, g, b, a);
            ide.bus.send(buf);
            ide.sceneDirty = true;


            BaseObject* baseObj = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                auto it = g_namedObjects.find(target);
                if (it != g_namedObjects.end()) baseObj = it->second;
            }
            if (baseObj) {
                baseObj->refreshVertexColors();  // Immediate refresh
            }

            // Forcer un rafraîchissement pour que la couleur s'affiche immédiatement
            RefreshSceneList(ide);
        }

        ImGui::TextDisabled("  Quick:");
        static const struct { const char* label; int r, g, b; } kQuickColors[] = {
            {"White",255,255,255},{"Gray",140,140,140},{"Black",0,0,0},
            {"Red",220,60,60},{"Green",60,180,60},{"Blue",60,100,220},
            {"Yellow",230,200,40},{"Orange",230,130,40},{"Cyan",60,200,210},
        };

        int quickIndex = 0;
        for (auto& qc : kQuickColors) {
            if (quickIndex % 3 != 0) ImGui::SameLine();
            ImVec4 col{ qc.r / 255.f, qc.g / 255.f, qc.b / 255.f, 1.f };
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
            if (ImGui::SmallButton(qc.label)) {
                ide.inspColor[0] = qc.r / 255.f; ide.inspColor[1] = qc.g / 255.f;
                ide.inspColor[2] = qc.b / 255.f; ide.inspColor[3] = 1.f;
                char buf[128];
                std::snprintf(buf, sizeof(buf), "color %s %d %d %d 255", target.c_str(), qc.r, qc.g, qc.b);
                ide.bus.send(buf);
                ide.sceneDirty = true;
                // Forcer un rafraîchissement immédiat
                RefreshSceneList(ide);
            }
            ImGui::PopStyleColor(2);
            ++quickIndex;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Aperçu de la couleur actuelle
        ImVec4 previewCol(ide.inspColor[0], ide.inspColor[1], ide.inspColor[2], 1.0f);
        ImGui::TextUnformatted("Preview:");
        ImGui::SameLine();
        ImGui::ColorButton("##preview", previewCol, ImGuiColorEditFlags_NoTooltip, ImVec2(50, 20));

        // Bouton Reset
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset to White")) {
            ide.inspColor[0] = 1.0f; ide.inspColor[1] = 1.0f; ide.inspColor[2] = 1.0f; ide.inspColor[3] = 1.0f;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "color %s 255 255 255 255", target.c_str());
            ide.bus.send(buf);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }
}

static void DrawRendererInspector(IDEState& ide, const std::string& target, IDEObject* pObj)
{
    if (ImGui::CollapsingHeader("  Renderer"))
    {
        ImGui::InputText("Shader##obj", ide.inspShader, sizeof(ide.inspShader));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ide.bus.send(std::string("setshader ") + target + " " + ide.inspShader);
            ide.sceneDirty = true;
        }
        ImGui::InputText("Tag##obj", ide.inspTag, sizeof(ide.inspTag));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            ide.bus.send(std::string("settag ") + target + " " + ide.inspTag);
            ide.sceneDirty = true;
        }
    }

    if (pObj) {
        bool vis = pObj->visible;
        if (ImGui::Checkbox("Visible##obj", &vis)) {
            ide.bus.send("visible " + target + " " + (vis ? "true" : "false"));
            ide.sceneDirty = true;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Button("Duplicate")) {
        std::string newName = target + "_copy";
        ide.bus.send("clone " + target + " " + newName);
        ide.sceneDirty = true;
        RefreshSceneList(ide);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename...")) {
        strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), target.c_str(), sizeof(ide.renameOldName) - 1);
        strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), target.c_str(), sizeof(ide.renameNewName) - 1);
        ide.showRenameModal = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus")) {
        if (pObj) {
            glm::vec3 targetPos(pObj->px, pObj->py, pObj->pz);
            ide.focus.Start(ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize, targetPos, 5.f);
        }
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f,0.2f,0.2f,1.f });
    if (ImGui::Button("Delete")) {
        ide.bus.send("delete " + target);
        ide.sceneDirty = true;
        ide.selection.Remove(target);
        RefreshSceneList(ide);
    }
    ImGui::PopStyleColor();
}

static void DrawHierarchyPanel(IDEState& ide)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{ 0.08f, 0.085f, 0.09f, 1.0f });
    ImGui::BeginChild("##hier", ImVec2(0, 0), false);

    // --- Barre d'outils compacte ---
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

    // Groupe de boutons à gauche
    ImGui::BeginGroup();

    // Bouton Create (menu déroulant)
    if (ImGui::Button(ICON_FA_PLUS, ImVec2(28, 24))) {
        ImGui::OpenPopup("hier_create_menu");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create new object or light");

    if (ImGui::BeginPopup("hier_create_menu")) {
        ImGui::SeparatorText("3D Objects");
        if (ImGui::MenuItem(ICON_FA_SQUARE " Plane")) { ide.newObjType = PLANE; ide.showAddObject = true; }
        if (ImGui::MenuItem(ICON_FA_SQUARE " Rectangle")) { ide.newObjType = RECTANGLE; ide.showAddObject = true; }
        if (ImGui::MenuItem(ICON_FA_CIRCLE " Sphere")) { ide.newObjType = SPHERE; ide.showAddObject = true; }
        ImGui::SeparatorText("Lights");
        if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Ambient Light")) { ide.newLightType = AMBIENT_LIGHT; ide.showAddLight = true; }
        if (ImGui::MenuItem(ICON_FA_SUN " Directional Light")) { ide.newLightType = DIRECTIONAL_LIGHT; ide.showAddLight = true; }
        if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT " Point Light")) { ide.newLightType = POINT_LIGHT; ide.showAddLight = true; }
        ImGui::SeparatorText("Scene");
        if (ImGui::MenuItem(ICON_FA_VIDEO " Camera")) { ide.showAddCamera = true; }
        ImGui::SeparatorText("Organization");
        if (ImGui::MenuItem(ICON_FA_FOLDER " Folder")) { ide.showCreateFolder = true; }
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // Refresh button
    if (ImGui::Button(ICON_FA_REDO_ALT, ImVec2(28, 24))) {
        RefreshSceneList(ide);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refresh hierarchy");

    // Paste button (si clipboard non vide)
    if (!ide.clipboardName.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LAYER_GROUP, ImVec2(28, 24))) {
            std::string newName = ide.clipboardName + "_copy";
            if (!ide.clipboardIsLight) {
                ide.bus.send("clone " + ide.clipboardName + " " + newName);
            }
            else {
                IDELight* src = nullptr;
                for (auto& l : ide.lights) if (l.name == ide.clipboardName) { src = &l; break; }
                if (src) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "addlight %s %.2f %d %d %d %s",
                        newName.c_str(), src->intensity, src->r, src->g, src->b,
                        src->lightType.c_str());
                    ide.bus.send(buf);
                }
            }
            ide.sceneDirty = true;
            ide.pendingSelection = newName;
            RefreshSceneList(ide);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste " ICON_FA_LAYER_GROUP);
    }

    ImGui::EndGroup();

    // Champ de recherche (prend l'espace restant)
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##hierfilter", ICON_FA_SEARCH " Search...",
        ide.hierarchyFilter, sizeof(ide.hierarchyFilter));

    ImGui::Separator();

    // Compteur compact
    ImGui::TextDisabled(" %zu objects | %zu lights", ide.objects.size(), ide.lights.size());
    ImGui::Separator();

    ImGui::PopStyleVar(); // ItemSpacing

    // Zone de défilement pour la hiérarchie
    ImGui::BeginChild("##hier_scroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    std::string filterLow(ide.hierarchyFilter);
    for (auto& c : filterLow) c = (char)std::tolower((unsigned char)c);
    DrawHierarchyNodes(ide.hierRoots, ide, filterLow);

    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawScriptsInspector(IDEState& ide, const std::string& target, BaseObject* obj) {
    if (!obj) return;

    ImGui::SeparatorText("Scripts");

    // Drop a script asset directly onto this section
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
            auto* ap = (AssetDragPayload*)payload->Data;

            // Handle script drops
            if (ap->type == AssetType::Script) {
                AssetRecord* rec = ide.assetDb.FindByGUID(std::string(ap->guidStr));
                if (rec) {
                    ScriptComponent newComp;
                    newComp.scriptGUID = ap->guidStr;
                    if (ide.scriptManager && ide.scriptManager->LoadScript(ap->guidStr, rec->path, newComp))
                        obj->scripts.push_back(newComp);
                    else
                        ide.log.push(ConsoleLog::REPLY_ERR,
                            "[Inspector] Failed to attach script " + rec->displayName);
                    ide.sceneDirty = true;
                }
            }
            // Optionally handle HDR drops in the scripts section (unusual, but possible)
            else if (ap->type == AssetType::Texture) {
                std::string ext = std::filesystem::path(ap->path).extension().string();
                if (ext == ".hdr") {
                    ide.log.push(ConsoleLog::INFO,
                        "[Inspector] HDR dropped on Scripts section - use the Environment window instead");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Add Script button
    if (ImGui::Button("+ Add Script", { -1, 0 })) {
        ImGui::OpenPopup("AddScriptPopup");
    }
    if (ImGui::BeginPopup("AddScriptPopup")) {
        // Show all script assets from the database
        for (auto& [guid, rec] : ide.assetDb.records) {
            if (rec.type == AssetType::Script) {
                if (ImGui::MenuItem(rec.displayName.c_str())) {
                    ScriptComponent newComp;
                    newComp.scriptGUID = guid;
                    newComp.compiledPath = "";
                    if (ide.scriptManager->LoadScript(guid, rec.path, newComp)) {
                        obj->scripts.push_back(newComp);
                        ide.sceneDirty = true;
                    }
                    else {
                        ide.log.push(ConsoleLog::REPLY_ERR,
                            "Failed to load script " + rec.displayName);
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    // List attached scripts
    for (size_t i = 0; i < obj->scripts.size(); ++i) {
        auto& comp = obj->scripts[i];
        // Get asset record for display name
        AssetRecord* rec = ide.assetDb.FindByGUID(comp.scriptGUID);
        std::string name = rec ? rec->displayName : comp.scriptGUID;

        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.2f, 0.3f, 0.4f, 1.0f });
        if (ImGui::CollapsingHeader(name.c_str())) {
            ImGui::PopStyleColor();
            // Exposed variables editing
            for (auto& var : comp.exposedVars) {
                ImGui::PushID(&var);
                switch (var.type) {
                case ScriptVarType::Int:
                    if (ImGui::DragInt(var.name.c_str(), &var.value.i, 0.1f))
                        var.dirty = true;
                    break;
                case ScriptVarType::Float:
                    if (ImGui::DragFloat(var.name.c_str(), &var.value.f, 0.01f))
                        var.dirty = true;
                    break;
                case ScriptVarType::Bool:
                    if (ImGui::Checkbox(var.name.c_str(), &var.value.b))
                        var.dirty = true;
                    break;
                case ScriptVarType::String: {
                    char buf[256];
                    strncpy_s(buf, sizeof(buf), var.value.s ? var.value.s : "", _TRUNCATE);
                    if (ImGui::InputText(var.name.c_str(), buf, sizeof(buf))) {
                        delete[] var.value.s;
                        var.value.s = new char[strlen(buf) + 1];
                        strcpy_s(var.value.s, strlen(buf) + 1, buf);
                        var.dirty = true;
                    }
                    break;
                }
                }
                ImGui::PopID();
            }
            if (ImGui::Button("Remove Script")) {
                ide.scriptManager->UnloadScript(comp);
                obj->scripts.erase(obj->scripts.begin() + i);
                ide.sceneDirty = true;
                i--;
            }
        }
        else {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    // Note: For HDR skybox drops, they should be handled in the Viewport or Environment window,
    // not in the Scripts section. Add a hint if needed
    ImGui::TextDisabled("Drag scripts from Asset Browser to attach them");
}

// =============================================================================
//  RigidBody Inspector
// =============================================================================
static void DrawRigidBodyInspector(IDEState& ide, BaseObject* obj)
{
    if (!obj) return;

    auto it = ide.rigidBodies.find(obj);
    bool hasRB = (it != ide.rigidBodies.end());

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.15f, 0.25f, 0.45f, 1.0f });
    if (!hasRB)
    {
        ImGui::Separator();
        if (ImGui::Button("+ Add RigidBody", { -1, 0 }))
        {
            RigidBody rb;
            rb.object = obj;
            ide.rigidBodies[obj] = rb;
            ide.physicsWorld.Register(&ide.rigidBodies[obj]);
            ide.sceneDirty = true;
            ide.log.push(ConsoleLog::INFO, "[Physics] RigidBody added to object");
        }
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PopStyleColor();

    RigidBody& rb = it->second;
    if (ImGui::CollapsingHeader("  RigidBody", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("rigidbody");
        ImGui::DragFloat("Mass", (float*)&rb.mass, 0.1f, 0.001f, 10000.f);
        ImGui::DragFloat("Drag", (float*)&rb.drag, 0.001f, 0.f, 1.f);
        ImGui::DragFloat("Angular Drag", (float*)&rb.angularDrag, 0.001f, 0.f, 1.f);
        ImGui::DragFloat("Restitution", (float*)&rb.restitution, 0.01f, 0.f, 1.f);
        ImGui::DragFloat("Friction", (float*)&rb.friction, 0.01f, 0.f, 1.f);
        ImGui::Checkbox("Use Gravity", &rb.useGravity);
        ImGui::SameLine();
        ImGui::Checkbox("Is Kinematic", &rb.isKinematic);
        ImGui::Separator();
        ImGui::TextDisabled("Velocity: %.2f %.2f %.2f",
            rb.velocity.x, rb.velocity.y, rb.velocity.z);
        ImGui::TextDisabled("Grounded: %s", rb.isGrounded ? "yes" : "no");

        ImGui::Spacing();
        if (ImGui::Button("Add Force (0,10,0)")) {
            rb.AddImpulse(Vector3(0, 10, 0));
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f, 0.2f, 0.2f, 1.f });
        if (ImGui::Button("Remove RigidBody")) {
            ide.physicsWorld.Unregister(&rb);
            ide.rigidBodies.erase(it);
            ide.sceneDirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

// =============================================================================
//  CollisionTrigger Inspector
// =============================================================================
static void DrawCollisionTriggerInspector(IDEState& ide, BaseObject* obj)
{
    if (!obj) return;

    auto it = ide.collisionTriggers.find(obj);
    bool hasCT = (it != ide.collisionTriggers.end());

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.25f, 0.15f, 0.40f, 1.0f });
    if (!hasCT)
    {
        ImGui::Separator();
        if (ImGui::Button("+ Add CollisionTrigger", { -1, 0 }))
        {
            CollisionTrigger ct;
            ct.object = obj;
            ide.collisionTriggers[obj] = ct;
            ide.sceneDirty = true;
            ide.log.push(ConsoleLog::INFO, "[Physics] CollisionTrigger added to object");
        }
        ImGui::PopStyleColor();
        return;
    }
    ImGui::PopStyleColor();

    CollisionTrigger& ct = it->second;
    if (ImGui::CollapsingHeader("  CollisionTrigger", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("collisiontrigger");
        ImGui::Checkbox("Enabled", &ct.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Is Solid", &ct.isSolid);

        char filterBuf[64];
        strncpy_s(filterBuf, sizeof(filterBuf), ct.filterTag.c_str(), _TRUNCATE);
        if (ImGui::InputText("Filter Tag", filterBuf, sizeof(filterBuf)))
            ct.filterTag = filterBuf;

        ImGui::Separator();
        ImGui::TextDisabled("Callbacks: not yet editable in UI");
        ImGui::TextDisabled("  OnEnter / OnStay / OnExit handled via script.");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f, 0.2f, 0.2f, 1.f });
        if (ImGui::Button("Remove CollisionTrigger")) {
            ide.collisionTriggers.erase(it);
            ide.sceneDirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
}

// =============================================================================
//  AnimatorComponent Inspector
// =============================================================================
static void DrawAnimatorInspector(IDEState& ide, BaseObject* obj)
{
    if (!obj) return;
    // AnimatorComponent is stored directly on BaseObject
    AnimatorComponent* anim = &obj->animator;
    if (!anim) return;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.15f, 0.35f, 0.20f, 1.0f });
    if (ImGui::CollapsingHeader("  Animator", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PopStyleColor();
        ImGui::PushID("animator");

        // Current clip
        std::string curClip = anim->active() ? anim->currentClipName() : "(none)";
        ImGui::TextDisabled("Current clip: %s", curClip.c_str());
        ImGui::DragFloat("Speed", &anim->speed, 0.01f, 0.f, 10.f);

        // Skeleton / bone info
        if (anim->skeleton) {
            ImGui::TextDisabled("Bones: %zu  |  Palette: %zu",
                anim->skeleton->bones.size(), anim->bonePalette.size());
        }

        ImGui::Separator();

        // Clip selection from asset database
        if (ImGui::BeginCombo("Play Clip", curClip.c_str()))
        {
            for (auto& [guid, rec] : ide.assetDb.records)
            {
                if (rec.type != AssetType::Model) continue;
                for (auto& sub : rec.subObjects)
                {
                    // Animation clips are stored as sub-objects of model assets
                    bool sel = (curClip == sub.name);
                    if (ImGui::Selectable(sub.name.c_str(), sel))
                    {
                        // Build a minimal AnimationClip to play
                        auto clip = std::make_shared<AnimationClip>();
                        clip->name = sub.name;
                        clip->duration = 1.f;
                        clip->loops = true;
                        anim->play(clip, 0.f);
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        // Cross-fade popup
        if (ImGui::Button("Cross-fade to..."))
            ImGui::OpenPopup("CrossFadePopup");

        if (ImGui::BeginPopup("CrossFadePopup"))
        {
            static float fadeDur = 0.25f;
            ImGui::DragFloat("Fade Duration", &fadeDur, 0.01f, 0.01f, 5.f);
            ImGui::Separator();
            for (auto& [guid, rec] : ide.assetDb.records)
            {
                if (rec.type != AssetType::Model) continue;
                for (auto& sub : rec.subObjects)
                {
                    if (ImGui::MenuItem(sub.name.c_str()))
                    {
                        auto clip = std::make_shared<AnimationClip>();
                        clip->name = sub.name;
                        clip->duration = 1.f;
                        clip->loops = true;
                        anim->crossFadeTo(clip, fadeDur, 0.f);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
    else { ImGui::PopStyleColor(); }
}

// =============================================================================
//  Material Texture Layers Editor
// =============================================================================
static void DrawMaterialTextureLayersEditor(IDEState& ide, BaseObject* obj)
{
    if (!obj || !obj->material) return;
    Material* mat = obj->material;

    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{ 0.35f, 0.20f, 0.10f, 1.0f });
    if (ImGui::CollapsingHeader("  Texture Layers"))
    {
        ImGui::PopStyleColor();
        ImGui::PushID("texlayers");

        static const char* blendModeNames[] = { "Normal", "Multiply", "Add" };
        static const char* maskChanNames[] = { "R", "G", "B", "A" };

        int toRemove = -1;
        for (int i = 0; i < (int)mat->textureLayers.size(); ++i)
        {
            TextureLayer& layer = mat->textureLayers[i];
            ImGui::PushID(i);

            char header[64];
            std::snprintf(header, sizeof(header), "Layer %d: %s", i,
                layer.texture.name.empty() ? "(no texture)" : layer.texture.name.c_str());

            if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen))
            {
                // Texture drop target
                char texBuf[256];
                strncpy_s(texBuf, sizeof(texBuf),
                    layer.texture.name.empty() ? "(none)" : layer.texture.name.c_str(), _TRUNCATE);
                ImGui::InputText("Texture", texBuf, sizeof(texBuf),
                    ImGuiInputTextFlags_ReadOnly);

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetDragPayload)) {
                        auto* ap = (AssetDragPayload*)p->Data;
                        if (ap->type == AssetType::Texture) {
                            std::string texName = fs::path(ap->path).stem().string();
                            Texture texDesc(texName, ap->path);
                            ide.renderer->texManager.load(texDesc);
                            layer.texture = texDesc;
                            mat->dirty = true;
                            ide.sceneDirty = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                bool changed = false;
                changed |= ImGui::DragFloat("Tiling U", &layer.texture.tilingU, 0.01f, 0.01f, 100.f);
                changed |= ImGui::DragFloat("Tiling V", &layer.texture.tilingV, 0.01f, 0.01f, 100.f);
                changed |= ImGui::DragFloat("Offset U", &layer.texture.offsetU, 0.01f, -100.f, 100.f);
                changed |= ImGui::DragFloat("Offset V", &layer.texture.offsetV, 0.01f, -100.f, 100.f);
                changed |= ImGui::DragFloat("Weight", &layer.blendWeight, 0.01f, 0.f, 1.f);
                int blendModeInt = static_cast<int>(layer.blendMode);
                if (ImGui::Combo("Blend Mode", &blendModeInt, blendModeNames, 3)) {
                    layer.blendMode = static_cast<LayerBlendMode>(blendModeInt);
                }

                ImGui::Separator();
                ImGui::TextDisabled("Mask");
                changed |= ImGui::DragFloat("Mask Min", &layer.maskMin, 0.01f, 0.f, 1.f);
                changed |= ImGui::DragFloat("Mask Max", &layer.maskMax, 0.01f, 0.f, 1.f);
                changed |= ImGui::Combo("Mask Channel", &layer.maskChannel, maskChanNames, 4);
                changed |= ImGui::Checkbox("Invert Mask", &layer.maskInvert);
                int maskTypeInt = static_cast<int>(layer.maskType);
                if (ImGui::DragInt("Mask Type", &maskTypeInt, 1, 0, 4)) {
                    layer.maskType = static_cast<LayerMaskType>(maskTypeInt);
                }

                if (changed) {
                    mat->dirty = true;
                    ide.sceneDirty = true;
                }

                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, { 0.7f, 0.2f, 0.2f, 1.f });
                if (ImGui::SmallButton("Remove Layer")) toRemove = i;
                ImGui::PopStyleColor();

                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        if (toRemove >= 0) {
            mat->textureLayers.erase(mat->textureLayers.begin() + toRemove);
            mat->dirty = true;
            ide.sceneDirty = true;
        }

        if ((int)mat->textureLayers.size() < 8)
        {
            if (ImGui::Button("+ Add Texture Layer", { -1, 0 })) {
                mat->textureLayers.push_back(TextureLayer{});
                mat->dirty = true;
                ide.sceneDirty = true;
            }
        }
        else {
            ImGui::TextDisabled("Maximum 8 layers reached.");
        }

        ImGui::PopID();
    }
    else { ImGui::PopStyleColor(); }
}



static glm::vec3 GetCameraSpawnPos(IDEState& ide, float dist = 6.f)
{
    if (!ide.sm || !ide.sm->currentCamera) return glm::vec3(0.f);
    Camera* cam = ide.sm->currentCamera;
    glm::vec3 pos = cam->transform.position.ToGLM();
    glm::vec3 fwd = glm::normalize(cam->transform.forward().ToGLM());
    return pos + fwd * dist;
}

// =============================================================================
//  Prefab Edit Mode  — Unity-style isolated prefab editing
//  EnterPrefabEditMode: saves scene snapshot, loads prefab objects for editing.
//  SaveAndExitPrefabEditMode: serializes edits back to the .honprefab file and
//    restores the main scene snapshot.
//  ExitPrefabEditModeDiscard: exits without saving (restores scene snapshot).
// =============================================================================

// Forward-declare so EnterPrefabEditMode can call it
static std::string InstantiatePrefab(IDEState& ide, const std::string& path);

static void EnterPrefabEditMode(IDEState& ide, const std::string& path) {
    if (ide.prefabEditMode) return;

    // Snapshot current scene
    ide.prefabEditSceneSnapshot = ide.objects;
    ide.prefabEditLightSnapshot = ide.lights;

    ide.bus.send("clearscene");
    ide.selection.Clear();

    std::ifstream f(path);
    if (!f.is_open()) {
        ide.log.push(ConsoleLog::REPLY_ERR, "[PrefabEdit] Cannot open: " + path);
        // Restore snapshot
        ide.objects = ide.prefabEditSceneSnapshot;
        ide.lights = ide.prefabEditLightSnapshot;
        for (auto& o : ide.objects) {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "sphere %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
            ide.bus.send(buf);
        }
        return;
    }
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    ide.prefabEditName = JsonGet(json, "name");
    if (ide.prefabEditName.empty())
        ide.prefabEditName = fs::path(path).stem().string();

    // Helper to extract a JSON array from the top-level object
    auto extractArray = [&](const std::string& arrayKey) -> std::string {
        std::string marker = "\"" + arrayKey + "\"";
        auto pos = json.find(marker);
        if (pos == std::string::npos) return "";
        pos = json.find('[', pos);
        if (pos == std::string::npos) return "";
        int depth = 0;
        size_t start = pos;
        for (size_t i = pos; i < json.size(); ++i) {
            if (json[i] == '[') ++depth;
            else if (json[i] == ']') {
                --depth;
                if (depth == 0) return json.substr(start + 1, i - start - 1);
            }
        }
        return "";
        };

    // ---- Objects -------------------------------------------------
    std::string objArray = extractArray("objects");
    if (!objArray.empty()) {
        std::vector<std::string> objTokens;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < objArray.size(); ++i) {
            if (objArray[i] == '{') {
                if (depth == 0) start = i;
                ++depth;
            }
            else if (objArray[i] == '}') {
                --depth;
                if (depth == 0) objTokens.push_back(objArray.substr(start, i - start + 1));
            }
        }

        for (auto& tok : objTokens) {
            std::string objName = JsonGet(tok, "name");
            std::string objType = JsonGet(tok, "type");
            std::string assetPath = JsonGet(tok, "assetPath");
            std::string visibleStr = JsonGet(tok, "visible");
            std::string tag = JsonGet(tok, "tag");
            if (tag.empty()) tag = "";

            // Spawn a placeholder object (type determines the command)
            char cmd[512];
            if (objType == "plane") {
                std::snprintf(cmd, sizeof(cmd), "plane %s 0 0 0", objName.c_str());
            }
            else if (objType == "rect") {
                std::snprintf(cmd, sizeof(cmd), "rect %s 0 0 0", objName.c_str());
            }
            else if (objType == "obj" && !assetPath.empty()) {
                std::snprintf(cmd, sizeof(cmd), "obj %s \"%s\" 0 0 0", objName.c_str(), assetPath.c_str());
            }
            else if (objType == "gltf" && !assetPath.empty()) {
                std::snprintf(cmd, sizeof(cmd), "gltf %s \"%s\" 0 0 0", objName.c_str(), assetPath.c_str());
            }
            else { // default to sphere
                std::snprintf(cmd, sizeof(cmd), "sphere %s 0 0 0", objName.c_str());
            }
            ide.bus.send(cmd);

            // Retrieve the actual engine object
            BaseObject* obj = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                auto it = g_namedObjects.find(objName);
                if (it != g_namedObjects.end()) obj = it->second;
            }
            if (!obj) continue;

            // Transform
            auto tpos = tok.find("\"transform\":");
            if (tpos != std::string::npos) {
                size_t tstart = tok.find('{', tpos);
                size_t tend = tok.find('}', tstart);
                if (tstart != std::string::npos && tend != std::string::npos) {
                    std::string tsub = tok.substr(tstart, tend - tstart + 1);
                    obj->transform.position.x = std::stof(JsonGet(tsub, "px"));
                    obj->transform.position.y = std::stof(JsonGet(tsub, "py"));
                    obj->transform.position.z = std::stof(JsonGet(tsub, "pz"));
                    obj->transform.scale.x = std::stof(JsonGet(tsub, "sx"));
                    obj->transform.scale.y = std::stof(JsonGet(tsub, "sy"));
                    obj->transform.scale.z = std::stof(JsonGet(tsub, "sz"));
                    obj->transform.rotation.w = std::stof(JsonGet(tsub, "rw"));
                    obj->transform.rotation.x = std::stof(JsonGet(tsub, "rx"));
                    obj->transform.rotation.y = std::stof(JsonGet(tsub, "ry"));
                    obj->transform.rotation.z = std::stof(JsonGet(tsub, "rz"));
                }
            }
            obj->visible = (visibleStr != "false");
            obj->tag = tag;

            // Material
            if (tok.find("\"material\":") != std::string::npos) {
                size_t mstart = tok.find("\"material\":");
                if (mstart != std::string::npos) {
                    size_t mend = tok.find('}', mstart);
                    if (mend != std::string::npos) {
                        std::string msub = tok.substr(mstart, mend - mstart + 1);
                        ApplyMaterialFromJson(obj, msub, ide.renderer->texManager);
                    }
                }
            }

            // Shader
            std::string shader = JsonGet(tok, "shaderName");
            if (!shader.empty()) obj->render.shaderName = shader;

            // Scripts
            if (tok.find("\"scripts\":") != std::string::npos)
                ApplyScriptsFromJson(obj, tok, ide);

            // RigidBody
            if (tok.find("\"rigidbody\":") != std::string::npos) {
                size_t rstart = tok.find("\"rigidbody\":");
                if (rstart != std::string::npos) {
                    size_t rend = tok.find('}', rstart);
                    if (rend != std::string::npos) {
                        std::string rsub = tok.substr(rstart, rend - rstart + 1);
                        RigidBody rb;
                        rb.mass = std::stof(JsonGet(rsub, "mass"));
                        rb.drag = std::stof(JsonGet(rsub, "drag"));
                        rb.angularDrag = std::stof(JsonGet(rsub, "angularDrag"));
                        rb.restitution = std::stof(JsonGet(rsub, "restitution"));
                        rb.friction = std::stof(JsonGet(rsub, "friction"));
                        rb.useGravity = (JsonGet(rsub, "useGravity") == "true");
                        rb.isKinematic = (JsonGet(rsub, "isKinematic") == "true");
                        rb.object = obj;
                        ide.rigidBodies[obj] = rb;
                        ide.physicsWorld.Register(&ide.rigidBodies[obj]);
                    }
                }
            }

            // CollisionTrigger
            if (tok.find("\"collisionTrigger\":") != std::string::npos) {
                size_t cstart = tok.find("\"collisionTrigger\":");
                if (cstart != std::string::npos) {
                    size_t cend = tok.find('}', cstart);
                    if (cend != std::string::npos) {
                        std::string csub = tok.substr(cstart, cend - cstart + 1);
                        CollisionTrigger ct;
                        ct.enabled = (JsonGet(csub, "enabled") == "true");
                        ct.isSolid = (JsonGet(csub, "isSolid") == "true");
                        ct.filterTag = JsonGet(csub, "filterTag");
                        ct.object = obj;
                        ide.collisionTriggers[obj] = ct;
                    }
                }
            }

            // Animator
            if (tok.find("\"animator\":") != std::string::npos) {
                size_t astart = tok.find("\"animator\":");
                if (astart != std::string::npos) {
                    size_t aend = tok.find('}', astart);
                    if (aend != std::string::npos) {
                        std::string asub = tok.substr(astart, aend - astart + 1);
                        obj->animator.speed = std::stof(JsonGet(asub, "speed"));
                        std::string clipName = JsonGet(asub, "currentClipName");
                        if (!clipName.empty()) {
                            // Try to find a matching clip asset
                            for (auto& [guid, rec] : ide.assetDb.records) {
                                if (rec.type == AssetType::Model) {
                                    for (auto& sub : rec.subObjects) {
                                        if (sub.name == clipName) {
                                            auto clip = std::make_shared<AnimationClip>();
                                            clip->name = sub.name;
                                            clip->duration = 1.f;
                                            clip->loops = true;
                                            obj->animator.play(clip, 0.f);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Render component (texture bindings)
            if (tok.find("\"render\":") != std::string::npos) {
                size_t rstart = tok.find("\"render\":");
                if (rstart != std::string::npos) {
                    size_t rend = tok.find('}', rstart);
                    if (rend != std::string::npos) {
                        std::string rsub = tok.substr(rstart, rend - rstart + 1);
                        ApplyRenderComponentFromJson(obj, rsub, ide);
                    }
                }
            }

            // Store in IDE objects for UI synchronisation
            IDEObject ideObj;
            ideObj.name = objName;
            ideObj.type = objType;
            ideObj.assetPath = assetPath;
            ideObj.px = obj->transform.position.x;
            ideObj.py = obj->transform.position.y;
            ideObj.pz = obj->transform.position.z;
            ideObj.sx = obj->transform.scale.x;
            ideObj.sy = obj->transform.scale.y;
            ideObj.sz = obj->transform.scale.z;
            ideObj.rw = obj->transform.rotation.w;
            ideObj.rx = obj->transform.rotation.x;
            ideObj.ry = obj->transform.rotation.y;
            ideObj.rz = obj->transform.rotation.z;
            ideObj.visible = obj->visible;
            if (obj->material) {
                ideObj.cr = (int)obj->material->color.r;
                ideObj.cg = (int)obj->material->color.g;
                ideObj.cb = (int)obj->material->color.b;
                ideObj.ca = (int)obj->material->color.a;
            }
            ide.objects.push_back(ideObj);
        }
    }

    // ---- Lights -------------------------------------------------
    std::string lightArray = extractArray("lights");
    if (!lightArray.empty()) {
        std::vector<std::string> lightTokens;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < lightArray.size(); ++i) {
            if (lightArray[i] == '{') {
                if (depth == 0) start = i;
                ++depth;
            }
            else if (lightArray[i] == '}') {
                --depth;
                if (depth == 0) lightTokens.push_back(lightArray.substr(start, i - start + 1));
            }
        }
        for (auto& tok : lightTokens) {
            IDELight lt;
            lt.name = JsonGet(tok, "name");
            lt.lightType = JsonGet(tok, "type");
            lt.intensity = std::stof(JsonGet(tok, "intensity"));
            std::string colStr = JsonGet(tok, "color");
            sscanf_s(colStr.c_str(), "[%d,%d,%d]", &lt.r, &lt.g, &lt.b);
            auto tpos = tok.find("\"transform\":");
            if (tpos != std::string::npos) {
                size_t tstart = tok.find('{', tpos);
                size_t tend = tok.find('}', tstart);
                if (tstart != std::string::npos && tend != std::string::npos) {
                    std::string tsub = tok.substr(tstart, tend - tstart + 1);
                    lt.px = std::stof(JsonGet(tsub, "px"));
                    lt.py = std::stof(JsonGet(tsub, "py"));
                    lt.pz = std::stof(JsonGet(tsub, "pz"));
                }
            }
            ide.lights.push_back(lt);
            // Create the light in the engine
            char cmd[256];
            std::snprintf(cmd, sizeof(cmd), "addlight %s %.2f %d %d %d %s",
                lt.name.c_str(), lt.intensity, lt.r, lt.g, lt.b, lt.lightType.c_str());
            ide.bus.send(cmd);
            std::snprintf(cmd, sizeof(cmd), "move %s %.4f %.4f %.4f",
                lt.name.c_str(), lt.px, lt.py, lt.pz);
            ide.bus.send(cmd);
        }
    }

    ide.prefabEditMode = true;
    ide.prefabEditPath = path;
    ide.prefabEditDirty = false;
    ide.sceneDirty = false;
    RefreshSceneList(ide);
    if (!ide.objects.empty()) {
        ide.selection.SetSingle(ide.objects[0].name);
        ide.bus.send("inspect " + ide.objects[0].name);
    }
    ide.toastMgr.Push("Editing prefab: " + ide.prefabEditName, Toast::Success, 2.f);
    ide.log.push(ConsoleLog::INFO, "[PrefabEdit] Entered edit mode for: " + path);
}

// Serialize the current ide.objects back into the .honprefab file and exit.
static void SaveAndExitPrefabEditMode(IDEState& ide) {
    if (!ide.prefabEditMode) return;

    std::ofstream f(ide.prefabEditPath);
    if (!f.is_open()) {
        ide.log.push(ConsoleLog::REPLY_ERR, "[PrefabEdit] Could not write: " + ide.prefabEditPath);
        return;
    }

    f << "{\n";
    f << "  \"name\": \"" << ide.prefabEditName << "\",\n";
    f << "  \"objects\": [\n";

    // ---- Write objects -----------------------------------------
    for (size_t i = 0; i < ide.objects.size(); ++i) {
        const auto& o = ide.objects[i];
        BaseObject* obj = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
            auto it = g_namedObjects.find(o.name);
            if (it != g_namedObjects.end()) obj = it->second;
        }
        if (!obj) continue;

        f << "    {\n";
        f << "      \"name\": \"" << o.name << "\",\n";
        std::string objType = o.type.empty() ? "sphere" : o.type;
        f << "      \"type\": \"" << objType << "\",\n";
        if (!o.assetPath.empty())
            f << "      \"assetPath\": \"" << o.assetPath << "\",\n";
        f << "      \"visible\": " << (obj->visible ? "true" : "false") << ",\n";
        f << "      \"tag\": " << JStr(obj->tag) << ",\n";
        WriteTransform(f, obj->transform, 6);
        f << ",\n";
        if (obj->material) WriteMaterial(f, obj->material, 6);
        f << ",\n";
        f << "      \"shaderName\": " << JStr(obj->render.shaderName) << ",\n";

        // Scripts
        if (!obj->scripts.empty()) {
            f << "      \"scripts\": [\n";
            for (size_t si = 0; si < obj->scripts.size(); ++si) {
                WriteScript(f, obj->scripts[si], 8);
                if (si + 1 < obj->scripts.size()) f << ",";
                f << "\n";
            }
            f << "      ],\n";
        }

        // RigidBody
        auto rbIt = ide.rigidBodies.find(obj);
        if (rbIt != ide.rigidBodies.end()) {
            WriteRigidBody(f, rbIt->second, 6);
            f << ",\n";
        }

        // CollisionTrigger
        auto ctIt = ide.collisionTriggers.find(obj);
        if (ctIt != ide.collisionTriggers.end()) {
            WriteCollisionTrigger(f, ctIt->second, 6);
            f << ",\n";
        }

        // Animator
        if (obj->animator.active()) {
            WriteAnimator(f, obj->animator, 6);
            f << ",\n";
        }

        // Render component (texture bindings)
        WriteRenderComponent(f, obj->render, ide.renderer->texManager, 6);
        f << "\n    }";
        if (i + 1 < ide.objects.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // ---- Write lights ------------------------------------------
    f << "  \"lights\": [\n";
    for (size_t i = 0; i < ide.lights.size(); ++i) {
        WriteLightToJson(f, ide.lights[i], 4);
        if (i + 1 < ide.lights.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n}\n";
    f.close();

    ide.toastMgr.Push("Prefab saved: " + ide.prefabEditName, Toast::Success, 2.5f);
    ide.log.push(ConsoleLog::REPLY_OK, "[PrefabEdit] Saved: " + ide.prefabEditPath);
    ide.assetBrowser.dirDirty = true;
    ide.prefabEditDirty = false;

    // Restore main scene snapshot
    ide.bus.send("clearscene");
    ide.objects = ide.prefabEditSceneSnapshot;
    ide.lights = ide.prefabEditLightSnapshot;
    for (auto& o : ide.objects) {
        char buf[512];
        std::string t = o.type.empty() ? "sphere" : o.type;
        if (t == "plane") {
            std::snprintf(buf, sizeof(buf), "plane %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "rect") {
            std::snprintf(buf, sizeof(buf), "rect %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "obj" && !o.assetPath.empty()) {
            std::snprintf(buf, sizeof(buf), "obj %s \"%s\" %.4f %.4f %.4f", o.name.c_str(), o.assetPath.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "gltf" && !o.assetPath.empty()) {
            std::snprintf(buf, sizeof(buf), "gltf %s \"%s\" %.4f %.4f %.4f", o.name.c_str(), o.assetPath.c_str(), o.px, o.py, o.pz);
        }
        else {
            std::snprintf(buf, sizeof(buf), "sphere %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        ide.bus.send(buf);
    }
    ide.prefabEditMode = false;
    ide.prefabEditPath.clear();
    ide.prefabEditName.clear();
    ide.prefabEditObjects.clear();
    ide.prefabEditSceneSnapshot.clear();
    ide.prefabEditLightSnapshot.clear();
    ide.selection.Clear();
    ide.sceneDirty = false;
    RefreshSceneList(ide);
}

// Exit without saving — restores the main scene snapshot.
static void ExitPrefabEditModeDiscard(IDEState& ide)
{
    if (!ide.prefabEditMode) return;

    ide.bus.send("clearscene");
    ide.objects = ide.prefabEditSceneSnapshot;
    ide.lights = ide.prefabEditLightSnapshot;
    for (auto& o : ide.objects) {
        char buf[512];
        std::string t = o.type.empty() ? "sphere" : o.type;
        if (t == "plane") {
            std::snprintf(buf, sizeof(buf), "plane %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "rect") {
            std::snprintf(buf, sizeof(buf), "rect %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "obj" && !o.assetPath.empty()) {
            std::snprintf(buf, sizeof(buf), "obj %s \"%s\" %.4f %.4f %.4f", o.name.c_str(), o.assetPath.c_str(), o.px, o.py, o.pz);
        }
        else if (t == "gltf" && !o.assetPath.empty()) {
            std::snprintf(buf, sizeof(buf), "gltf %s \"%s\" %.4f %.4f %.4f", o.name.c_str(), o.assetPath.c_str(), o.px, o.py, o.pz);
        }
        else {
            std::snprintf(buf, sizeof(buf), "sphere %s %.4f %.4f %.4f", o.name.c_str(), o.px, o.py, o.pz);
        }
        ide.bus.send(buf);
    }

    ide.prefabEditMode = false;
    ide.prefabEditPath.clear();
    ide.prefabEditName.clear();
    ide.prefabEditObjects.clear();
    ide.prefabEditSceneSnapshot.clear();
    ide.prefabEditLightSnapshot.clear();
    ide.prefabEditDirty = false;
    ide.selection.Clear();
    ide.sceneDirty = false;
    ide.toastMgr.Push("Prefab edit discarded.", Toast::Info, 1.5f);
    ide.log.push(ConsoleLog::INFO, "[PrefabEdit] Exited without saving.");
}

// =============================================================================
//  Prefab instantiation helper
//  Reads a .honprefab JSON file and spawns all listed objects into the scene.
//  Returns the name of the first spawned object (for selection), or empty.
// =============================================================================
static std::string InstantiatePrefab(IDEState& ide, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        ide.log.push(ConsoleLog::REPLY_ERR, "[Prefab] Cannot open: " + path);
        return {};
    }
    std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    glm::vec3 spawnOrigin = GetCameraSpawnPos(ide);
    std::string firstSpawned;
    // Track every spawned name so we can group them in a hierarchy folder at the end.
    std::vector<std::string> spawnedObjects;
    std::vector<std::string> spawnedLights;

    auto extractArray = [&](const std::string& arrayKey) -> std::string {
        std::string marker = "\"" + arrayKey + "\"";
        auto pos = json.find(marker);
        if (pos == std::string::npos) return "";
        pos = json.find('[', pos);
        if (pos == std::string::npos) return "";
        int depth = 0;
        size_t start = pos;
        for (size_t i = pos; i < json.size(); ++i) {
            if (json[i] == '[') ++depth;
            else if (json[i] == ']') {
                --depth;
                if (depth == 0) return json.substr(start + 1, i - start - 1);
            }
        }
        return "";
        };

    // ---- Objects -------------------------------------------------
    std::string objArray = extractArray("objects");
    if (!objArray.empty()) {
        std::vector<std::string> objTokens;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < objArray.size(); ++i) {
            if (objArray[i] == '{') {
                if (depth == 0) start = i;
                ++depth;
            }
            else if (objArray[i] == '}') {
                --depth;
                if (depth == 0) objTokens.push_back(objArray.substr(start, i - start + 1));
            }
        }

        for (auto& tok : objTokens) {
            std::string objName = JsonGet(tok, "name");
            std::string objType = JsonGet(tok, "type");
            std::string assetPath = JsonGet(tok, "assetPath");

            // Make unique name (avoid collisions)
            std::string finalName = objName;
            int n = 1;
            while (g_namedObjects.count(finalName) || g_namedLights.count(finalName))
                finalName = objName + "_" + std::to_string(n++);

            // Parse transform (position, scale, rotation)
            glm::vec3 pos(0, 0, 0), scl(1, 1, 1);
            Quaternion rot(1, 0, 0, 0);

            auto tpos = tok.find("\"transform\":");
            if (tpos != std::string::npos) {
                size_t tstart = tok.find('{', tpos);
                size_t tend = tok.find('}', tstart);
                if (tstart != std::string::npos && tend != std::string::npos) {
                    std::string tsub = tok.substr(tstart, tend - tstart + 1);
                    pos.x = std::stof(JsonGet(tsub, "px"));
                    pos.y = std::stof(JsonGet(tsub, "py"));
                    pos.z = std::stof(JsonGet(tsub, "pz"));
                    scl.x = std::stof(JsonGet(tsub, "sx"));
                    scl.y = std::stof(JsonGet(tsub, "sy"));
                    scl.z = std::stof(JsonGet(tsub, "sz"));
                    rot.w = std::stof(JsonGet(tsub, "rw"));
                    rot.x = std::stof(JsonGet(tsub, "rx"));
                    rot.y = std::stof(JsonGet(tsub, "ry"));
                    rot.z = std::stof(JsonGet(tsub, "rz"));
                }
            }
            pos += spawnOrigin;

            // Spawn the object
            char cmd[512];
            if (objType == "plane") {
                std::snprintf(cmd, sizeof(cmd), "plane %s %.3f %.3f %.3f", finalName.c_str(), pos.x, pos.y, pos.z);
            }
            else if (objType == "rect") {
                std::snprintf(cmd, sizeof(cmd), "rect %s %.3f %.3f %.3f", finalName.c_str(), pos.x, pos.y, pos.z);
            }
            else if (objType == "obj" && !assetPath.empty()) {
                std::snprintf(cmd, sizeof(cmd), "obj %s \"%s\" %.3f %.3f %.3f", finalName.c_str(), assetPath.c_str(), pos.x, pos.y, pos.z);
            }
            else if (objType == "gltf" && !assetPath.empty()) {
                std::snprintf(cmd, sizeof(cmd), "gltf %s \"%s\" %.3f %.3f %.3f", finalName.c_str(), assetPath.c_str(), pos.x, pos.y, pos.z);
            }
            else {
                std::snprintf(cmd, sizeof(cmd), "sphere %s %.3f %.3f %.3f", finalName.c_str(), pos.x, pos.y, pos.z);
            }
            ide.bus.send(cmd);

            // Apply scale and rotation
            if (scl.x != 1 || scl.y != 1 || scl.z != 1) {
                std::snprintf(cmd, sizeof(cmd), "scale %s %.4f %.4f %.4f", finalName.c_str(), scl.x, scl.y, scl.z);
                ide.bus.send(cmd);
            }
            if (rot.w != 1 || rot.x != 0 || rot.y != 0 || rot.z != 0) {
                glm::vec3 euler = glm::eulerAngles(rot.ToGLM());
                std::snprintf(cmd, sizeof(cmd), "rotate %s %.3f %.3f %.3f", finalName.c_str(),
                    glm::degrees(euler.x), glm::degrees(euler.y), glm::degrees(euler.z));
                ide.bus.send(cmd);
            }

            // Apply the rest of the properties (material, scripts, physics, …)
            BaseObject* obj = nullptr;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                auto it = g_namedObjects.find(finalName);
                if (it != g_namedObjects.end()) obj = it->second;
            }
            if (obj) {
                std::string visibleStr = JsonGet(tok, "visible");
                obj->visible = (visibleStr != "false");
                obj->tag = JsonGet(tok, "tag");

                if (tok.find("\"material\":") != std::string::npos) {
                    size_t mstart = tok.find("\"material\":");
                    if (mstart != std::string::npos) {
                        size_t mend = tok.find('}', mstart);
                        if (mend != std::string::npos) {
                            std::string msub = tok.substr(mstart, mend - mstart + 1);
                            ApplyMaterialFromJson(obj, msub, ide.renderer->texManager);
                        }
                    }
                }

                std::string shader = JsonGet(tok, "shaderName");
                if (!shader.empty()) obj->render.shaderName = shader;

                if (tok.find("\"scripts\":") != std::string::npos)
                    ApplyScriptsFromJson(obj, tok, ide);

                if (tok.find("\"rigidbody\":") != std::string::npos) {
                    size_t rstart = tok.find("\"rigidbody\":");
                    if (rstart != std::string::npos) {
                        size_t rend = tok.find('}', rstart);
                        if (rend != std::string::npos) {
                            std::string rsub = tok.substr(rstart, rend - rstart + 1);
                            RigidBody rb;
                            rb.mass = std::stof(JsonGet(rsub, "mass"));
                            rb.drag = std::stof(JsonGet(rsub, "drag"));
                            rb.angularDrag = std::stof(JsonGet(rsub, "angularDrag"));
                            rb.restitution = std::stof(JsonGet(rsub, "restitution"));
                            rb.friction = std::stof(JsonGet(rsub, "friction"));
                            rb.useGravity = (JsonGet(rsub, "useGravity") == "true");
                            rb.isKinematic = (JsonGet(rsub, "isKinematic") == "true");
                            rb.object = obj;
                            ide.rigidBodies[obj] = rb;
                            ide.physicsWorld.Register(&ide.rigidBodies[obj]);
                        }
                    }
                }

                if (tok.find("\"collisionTrigger\":") != std::string::npos) {
                    size_t cstart = tok.find("\"collisionTrigger\":");
                    if (cstart != std::string::npos) {
                        size_t cend = tok.find('}', cstart);
                        if (cend != std::string::npos) {
                            std::string csub = tok.substr(cstart, cend - cstart + 1);
                            CollisionTrigger ct;
                            ct.enabled = (JsonGet(csub, "enabled") == "true");
                            ct.isSolid = (JsonGet(csub, "isSolid") == "true");
                            ct.filterTag = JsonGet(csub, "filterTag");
                            ct.object = obj;
                            ide.collisionTriggers[obj] = ct;
                        }
                    }
                }

                if (tok.find("\"animator\":") != std::string::npos) {
                    size_t astart = tok.find("\"animator\":");
                    if (astart != std::string::npos) {
                        size_t aend = tok.find('}', astart);
                        if (aend != std::string::npos) {
                            std::string asub = tok.substr(astart, aend - astart + 1);
                            obj->animator.speed = std::stof(JsonGet(asub, "speed"));
                            std::string clipName = JsonGet(asub, "currentClipName");
                            if (!clipName.empty()) {
                                for (auto& [guid, rec] : ide.assetDb.records) {
                                    if (rec.type == AssetType::Model) {
                                        for (auto& sub : rec.subObjects) {
                                            if (sub.name == clipName) {
                                                auto clip = std::make_shared<AnimationClip>();
                                                clip->name = sub.name;
                                                clip->duration = 1.f;
                                                clip->loops = true;
                                                obj->animator.play(clip, 0.f);
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (tok.find("\"render\":") != std::string::npos) {
                    size_t rstart = tok.find("\"render\":");
                    if (rstart != std::string::npos) {
                        size_t rend = tok.find('}', rstart);
                        if (rend != std::string::npos) {
                            std::string rsub = tok.substr(rstart, rend - rstart + 1);
                            ApplyRenderComponentFromJson(obj, rsub, ide);
                        }
                    }
                }
            }

            if (firstSpawned.empty()) firstSpawned = finalName;
            spawnedObjects.push_back(finalName);
        }
    }

    // ---- Lights -------------------------------------------------
    std::string lightArray = extractArray("lights");
    if (!lightArray.empty()) {
        std::vector<std::string> lightTokens;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < lightArray.size(); ++i) {
            if (lightArray[i] == '{') {
                if (depth == 0) start = i;
                ++depth;
            }
            else if (lightArray[i] == '}') {
                --depth;
                if (depth == 0) lightTokens.push_back(lightArray.substr(start, i - start + 1));
            }
        }
        for (auto& tok : lightTokens) {
            IDELight lt;
            lt.name = JsonGet(tok, "name");
            lt.lightType = JsonGet(tok, "type");
            lt.intensity = std::stof(JsonGet(tok, "intensity"));
            std::string colStr = JsonGet(tok, "color");
            sscanf_s(colStr.c_str(), "[%d,%d,%d]", &lt.r, &lt.g, &lt.b);
            glm::vec3 pos(0, 0, 0);
            auto tpos = tok.find("\"transform\":");
            if (tpos != std::string::npos) {
                size_t tstart = tok.find('{', tpos);
                size_t tend = tok.find('}', tstart);
                if (tstart != std::string::npos && tend != std::string::npos) {
                    std::string tsub = tok.substr(tstart, tend - tstart + 1);
                    pos.x = std::stof(JsonGet(tsub, "px"));
                    pos.y = std::stof(JsonGet(tsub, "py"));
                    pos.z = std::stof(JsonGet(tsub, "pz"));
                }
            }
            pos += spawnOrigin;
            char cmd[256];
            std::snprintf(cmd, sizeof(cmd), "addlight %s %.2f %d %d %d %s",
                lt.name.c_str(), lt.intensity, lt.r, lt.g, lt.b, lt.lightType.c_str());
            ide.bus.send(cmd);
            std::snprintf(cmd, sizeof(cmd), "move %s %.4f %.4f %.4f",
                lt.name.c_str(), pos.x, pos.y, pos.z);
            ide.bus.send(cmd);
            ide.lights.push_back(lt);
            spawnedLights.push_back(lt.name);
        }
    }

    if (!firstSpawned.empty()) {
        ide.sceneDirty = true;

        // ── Group all spawned objects/lights into a dedicated Prefab folder ───
        // Mirrors Unity behaviour: instantiated prefabs appear as a labelled
        // folder in the hierarchy, not loose at the scene root.
        std::string prefabStem = fs::path(path).stem().string();

        // Make the folder name unique if a same-named folder already exists.
        std::string folderName = prefabStem;
        {
            int folderIdx = 1;
            std::function<bool(const std::vector<HierarchyNode>&, const std::string&)> folderExists =
                [&](const std::vector<HierarchyNode>& nodes, const std::string& nm) -> bool {
                for (auto& n : nodes) {
                    if (n.kind == HierarchyNode::FOLDER && n.name == nm) return true;
                    if (n.kind == HierarchyNode::FOLDER && folderExists(n.children, nm)) return true;
                }
                return false;
                };
            while (folderExists(ide.hierRoots, folderName))
                folderName = prefabStem + "_" + std::to_string(folderIdx++);
        }

        // Build the folder and insert placeholder children for every spawned
        // object/light BEFORE calling RefreshSceneList so hierarchy is correct.
        HierarchyNode prefabFolder;
        prefabFolder.kind = HierarchyNode::FOLDER;
        prefabFolder.name = folderName;
        prefabFolder.folderOpen = true;

        for (auto& nm : spawnedObjects) {
            HierarchyNode n; n.kind = HierarchyNode::OBJECT; n.name = nm;
            prefabFolder.children.push_back(n);
        }
        for (auto& nm : spawnedLights) {
            HierarchyNode n; n.kind = HierarchyNode::LIGHT; n.name = nm;
            prefabFolder.children.push_back(n);
        }

        // Remove those nodes from hierRoots in case a synchronous list reply
        // already placed them at root level.
        std::set<std::string> spawnedSet(spawnedObjects.begin(), spawnedObjects.end());
        spawnedSet.insert(spawnedLights.begin(), spawnedLights.end());
        std::function<void(std::vector<HierarchyNode>&)> removeMoved =
            [&](std::vector<HierarchyNode>& nodes) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                [&](const HierarchyNode& n) {
                    return (n.kind == HierarchyNode::OBJECT || n.kind == HierarchyNode::LIGHT)
                        && spawnedSet.count(n.name);
                }), nodes.end());
            for (auto& n : nodes)
                if (n.kind == HierarchyNode::FOLDER) removeMoved(n.children);
            };
        removeMoved(ide.hierRoots);
        ide.hierRoots.push_back(std::move(prefabFolder));

        RefreshSceneList(ide);
        ide.toastMgr.Push("Prefab instantiated: " + folderName, Toast::Success, 2.f);
        ide.log.push(ConsoleLog::REPLY_OK, "[Prefab] Instantiated into folder '" + folderName + "': " + path);
    }
    return firstSpawned;
}

// =============================================================================
//  Prefab creation helper
// =============================================================================
static void DrawPrefabSection(IDEState& ide, const std::string& target, BaseObject* obj)
{
    if (!obj) return;

    ImGui::Separator();
    if (ImGui::Button("Create Prefab...", { -1, 0 }))
        ImGui::OpenPopup("CreatePrefabPopup");

    if (ImGui::BeginPopupModal("CreatePrefabPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char prefabName[64] = {};
        if (prefabName[0] == '\0')
            strncpy_s(prefabName, sizeof(prefabName), target.c_str(), _TRUNCATE);
        ImGui::InputText("Prefab Name", prefabName, sizeof(prefabName));
        if (ImGui::Button("Save", { 120, 0 })) {
            // Build JSON with full component data – no version field
            std::string outDir = std::string(ide.project.rootFolder).empty()
                ? "assets/prefabs/" : (fs::path(ide.project.rootFolder) / "assets" / "prefabs" / "").string();
            fs::create_directories(outDir);
            std::string outPath = outDir + prefabName + ".honprefab";
            std::ofstream f(outPath);
            if (f.is_open()) {
                f << "{\n";
                f << "  \"name\": \"" << prefabName << "\",\n";
                f << "  \"objects\": [\n";
                f << "    {\n";
                f << "      \"name\": \"" << target << "\",\n";

                // Determine object type
                std::string objType = "sphere";
                if (dynamic_cast<Plane*>(obj)) {
                    objType = "plane";
                    // Get subdivision values
                    Plane* p = dynamic_cast<Plane*>(obj);
                }
                else if (dynamic_cast<HonHengine::Rectangle*>(obj)) {
                    objType = "rect";
                    HonHengine::Rectangle* r = dynamic_cast<HonHengine::Rectangle*>(obj);
                }
                else if (!obj->tag.empty()) {
                    if (obj->tag.rfind("OBJ:", 0) == 0) objType = "obj";
                    else if (obj->tag.rfind("GLTF:", 0) == 0) objType = "gltf";
                }
                f << "      \"type\": \"" << objType << "\",\n";

                if (objType == "obj" || objType == "gltf") {
                    std::string ap = obj->tag.substr(objType == "obj" ? 4 : 5);
                    f << "      \"assetPath\": \"" << ap << "\",\n";
                }

                f << "      \"visible\": " << (obj->visible ? "true" : "false") << ",\n";
                f << "      \"tag\": " << JStr(obj->tag) << ",\n";

                WriteTransform(f, obj->transform, 6);
                f << ",\n";

                if (obj->material) WriteMaterial(f, obj->material, 6);
                f << ",\n";

                f << "      \"shaderName\": " << JStr(obj->render.shaderName) << ",\n";

                if (!obj->scripts.empty()) {
                    f << "      \"scripts\": [\n";
                    for (size_t si = 0; si < obj->scripts.size(); ++si) {
                        WriteScript(f, obj->scripts[si], 8);
                        if (si + 1 < obj->scripts.size()) f << ",";
                        f << "\n";
                    }
                    f << "      ],\n";
                }

                auto rbIt = ide.rigidBodies.find(obj);
                if (rbIt != ide.rigidBodies.end()) {
                    WriteRigidBody(f, rbIt->second, 6);
                    f << ",\n";
                }

                auto ctIt = ide.collisionTriggers.find(obj);
                if (ctIt != ide.collisionTriggers.end()) {
                    WriteCollisionTrigger(f, ctIt->second, 6);
                    f << ",\n";
                }

                if (obj->animator.active()) {
                    WriteAnimator(f, obj->animator, 6);
                    f << ",\n";
                }

                WriteRenderComponent(f, obj->render, ide.renderer->texManager, 6);
                f << "\n    }\n";
                f << "  ],\n";
                f << "  \"lights\": []\n";
                f << "}\n";
                f.close();

                auto& rec = ide.assetDb.Register(outPath);
                ide.assetDb.Save(ide.HonAssetsPath());
                ide.toastMgr.Push("Prefab saved: " + outPath, Toast::Success, 2.5f);
                ide.prefabSourceGUID[target] = rec.guid.ToString();
                ide.assetBrowser.dirDirty = true;
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Prefab] Could not write: " + outPath);
            }
            prefabName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) {
            prefabName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Show prefab source GUID if this object was instantiated from one
    auto pfIt = ide.prefabSourceGUID.find(target);
    if (pfIt != ide.prefabSourceGUID.end())
    {
        ImGui::TextDisabled("Prefab source: %s", pfIt->second.c_str());
        if (ImGui::SmallButton("Apply to Prefab")) {
            // Find the source prefab file and overwrite it with current transforms
            AssetRecord* rec = ide.assetDb.FindByGUID(pfIt->second);
            if (rec && !rec->path.empty() && obj) {
                std::ofstream pf(rec->path);
                if (pf.is_open()) {
                    // Determine object type again
                    std::string objType2 = "sphere";
                    if (dynamic_cast<Plane*>(obj)) objType2 = "plane";
                    else if (dynamic_cast<HonHengine::Rectangle*>(obj)) objType2 = "rect";
                    else if (!obj->tag.empty()) {
                        if (obj->tag.rfind("OBJ:", 0) == 0) objType2 = "obj";
                        else if (obj->tag.rfind("GLTF:", 0) == 0) objType2 = "gltf";
                    }

                    pf << "{\n";
                    pf << "  \"name\": \"" << fs::path(rec->path).stem().string() << "\",\n";
                    pf << "  \"objects\": [\n";
                    pf << "    {\n";
                    pf << "      \"name\": \"" << target << "\",\n";
                    pf << "      \"type\": \"" << objType2 << "\",\n";
                    if (objType2 == "obj" || objType2 == "gltf") {
                        std::string ap2 = obj->tag.substr(objType2 == "obj" ? 4 : 5);
                        pf << "      \"assetPath\": \"" << ap2 << "\",\n";
                    }
                    pf << "      \"visible\": " << (obj->visible ? "true" : "false") << ",\n";
                    pf << "      \"tag\": " << JStr(obj->tag) << ",\n";
                    WriteTransform(pf, obj->transform, 6);
                    pf << ",\n";
                    if (obj->material) WriteMaterial(pf, obj->material, 6);
                    pf << ",\n";
                    pf << "      \"shaderName\": " << JStr(obj->render.shaderName) << ",\n";
                    if (!obj->scripts.empty()) {
                        pf << "      \"scripts\": [\n";
                        for (size_t si = 0; si < obj->scripts.size(); ++si) {
                            WriteScript(pf, obj->scripts[si], 8);
                            if (si + 1 < obj->scripts.size()) pf << ",";
                            pf << "\n";
                        }
                        pf << "      ],\n";
                    }
                    auto rbIt2 = ide.rigidBodies.find(obj);
                    if (rbIt2 != ide.rigidBodies.end()) {
                        WriteRigidBody(pf, rbIt2->second, 6);
                        pf << ",\n";
                    }
                    auto ctIt2 = ide.collisionTriggers.find(obj);
                    if (ctIt2 != ide.collisionTriggers.end()) {
                        WriteCollisionTrigger(pf, ctIt2->second, 6);
                        pf << ",\n";
                    }
                    if (obj->animator.active()) {
                        WriteAnimator(pf, obj->animator, 6);
                        pf << ",\n";
                    }
                    WriteRenderComponent(pf, obj->render, ide.renderer->texManager, 6);
                    pf << "\n    }\n";
                    pf << "  ],\n";
                    pf << "  \"lights\": []\n";
                    pf << "}\n";
                    pf.close();
                    ide.toastMgr.Push("Applied to prefab: " + rec->displayName, Toast::Success, 2.f);
                    ide.assetBrowser.dirDirty = true;
                    if (ide.prefabEditMode && ide.prefabEditPath == rec->path)
                        ide.prefabEditDirty = false;
                }
                else {
                    ide.log.push(ConsoleLog::REPLY_ERR, "[Prefab] Could not write: " + rec->path);
                }
            }
            else {
                ide.log.push(ConsoleLog::INFO,
                    "[Prefab] Apply-to-prefab: source asset not found in database.");
            }
        }
    }
}

// =============================================================================
//  Multi-Object Transform Editing
// =============================================================================
static void DrawMultiSelectionInspector(IDEState& ide)
{
    size_t count = ide.selection.Size();
    ImGui::TextColored({ 0.8f, 0.9f, 1.0f, 1.f },
        "  %zu objects selected", count);
    ImGui::Separator();

    // Gather objects
    std::vector<BaseObject*> selectedObjects;
    {
        std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
        for (const auto& name : ide.selection.items) {
            auto it = g_namedObjects.find(name);
            if (it != g_namedObjects.end()) selectedObjects.push_back(it->second);
        }
    }

    ImGui::TextDisabled("  Transform (applies delta to all selected)");

    static float multiPos[3] = {};
    static float multiRot[3] = {};
    static float multiScale[3] = { 0.f, 0.f, 0.f };

    float step = ide.snapEnabled ? ide.snapPosition : 0.05f;

    if (ImGui::DragFloat3("Position Offset##multi", multiPos, step))
    {
        for (BaseObject* o : selectedObjects) {
            o->transform.position.x += multiPos[0];
            o->transform.position.y += multiPos[1];
            o->transform.position.z += multiPos[2];
        }
        multiPos[0] = multiPos[1] = multiPos[2] = 0.f;
        ide.sceneDirty = true;
    }

    float rotStep = ide.snapEnabled ? ide.snapRotation : 0.5f;
    if (ImGui::DragFloat3("Rotation Offset##multi", multiRot, rotStep))
    {
        for (BaseObject* o : selectedObjects) {
            Quaternion delta = EulerToQuat(multiRot[0], multiRot[1], multiRot[2]);
            o->transform.rotation = delta * o->transform.rotation;
        }
        multiRot[0] = multiRot[1] = multiRot[2] = 0.f;
        ide.sceneDirty = true;
    }

    float scaleStep = ide.snapEnabled ? ide.snapScale : 0.01f;
    if (ImGui::DragFloat3("Scale Offset##multi", multiScale, scaleStep))
    {
        for (BaseObject* o : selectedObjects) {
            o->transform.scale.x += multiScale[0];
            o->transform.scale.y += multiScale[1];
            o->transform.scale.z += multiScale[2];
        }
        multiScale[0] = multiScale[1] = multiScale[2] = 0.f;
        ide.sceneDirty = true;
    }

    ImGui::Spacing();
    if (ImGui::Button("Clear Selection")) ide.selection.Clear();
    ImGui::SameLine();
    if (ImGui::Button("Delete All")) {
        for (const auto& name : ide.selection.items)
            ide.bus.send("delete " + name);
        ide.selection.Clear();
        ide.sceneDirty = true;
        RefreshSceneList(ide);
    }
}

// =============================================================================
//  Profiler Window
// =============================================================================
static void DrawProfilerWindow(IDEState& ide, float dt)
{
    if (!ide.showProfiler) return;
    ImGui::SetNextWindowSize({ 500, 350 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Profiler", &ide.showProfiler))
    {
        // Record frame time
        ide.frameTimeSamples[ide.frameTimeSampleIdx] = dt * 1000.f;
        ide.frameTimeSampleIdx = (ide.frameTimeSampleIdx + 1) % IDEState::kProfilerSamples;

        float maxMs = 0.f;
        for (float v : ide.frameTimeSamples) maxMs = (std::max)(maxMs, v);

        char overlay[32];
        float cur = ide.frameTimeSamples[(ide.frameTimeSampleIdx - 1 + IDEState::kProfilerSamples)
            % IDEState::kProfilerSamples];
        std::snprintf(overlay, sizeof(overlay), "%.2f ms", cur);

        ImGui::PlotLines("Frame Time (ms)", ide.frameTimeSamples, IDEState::kProfilerSamples,
            ide.frameTimeSampleIdx, overlay, 0.f, (std::max)(maxMs * 1.2f, 33.f), { -1, 80 });

        ImGui::Separator();
        ImGui::TextDisabled("FPS: %d", ide.statsFps > 0 ? ide.statsFps : (int)(1.f / (dt + 1e-6f)));
        ImGui::TextDisabled("Objects: %zu | Lights: %zu", ide.objects.size(), ide.lights.size());
        ImGui::TextDisabled("RigidBodies: %zu | Triggers: %zu",
            ide.rigidBodies.size(), ide.collisionTriggers.size());
        ImGui::TextDisabled("Scripts total: (see Memory window)");

        ImGui::Separator();
        ImGui::TextDisabled("Draw calls: reported by GPURenderer (if exposed)");
    }
    ImGui::End();
}

// =============================================================================
//  Memory Window
// =============================================================================
static void DrawMemoryWindow(IDEState& ide)
{
    if (!ide.showMemoryWindow) return;
    ImGui::SetNextWindowSize({ 420, 300 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Memory", &ide.showMemoryWindow))
    {
        size_t texCount = 0;
        size_t scriptCount = 0;
        size_t totalVerts = 0;

        for (auto& [guid, rec] : ide.assetDb.records) {
            if (rec.type == AssetType::Texture) ++texCount;
            if (rec.type == AssetType::Script)  ++scriptCount;
        }

        {
            std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
            for (auto& [name, obj] : g_namedObjects) {
                if (obj) {
                    totalVerts += obj->bVertices.size();
                }
            }
        }

        ImGui::Text("Asset Database Records:  %zu", ide.assetDb.records.size());
        ImGui::Text("Textures registered:     %zu", texCount);
        ImGui::Text("Scripts registered:      %zu", scriptCount);
        ImGui::Text("Total mesh vertices:     %zu", totalVerts);
        ImGui::Text("Scene objects:           %zu", ide.objects.size());
        ImGui::Text("Scene lights:            %zu", ide.lights.size());
        ImGui::Text("RigidBodies:             %zu", ide.rigidBodies.size());
        ImGui::Text("Collision Triggers:      %zu", ide.collisionTriggers.size());
        ImGui::Text("Hierarchy folders:       %zu", [&]() {
            size_t cnt = 0;
            for (auto& n : ide.hierRoots) if (n.kind == HierarchyNode::FOLDER) ++cnt;
            return cnt;
            }());
    }
    ImGui::End();
}

// =============================================================================
//  Toolchain Settings Window
// =============================================================================
static void DrawToolchainWindow(IDEState& ide)
{
    if (!ide.showToolchainWindow) return;
    ImGui::SetNextWindowSize({ 520, 420 }, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scripting Toolchain", &ide.showToolchainWindow))
    {
        EditorSettings& es = ide.editorSettings;

        ImGui::SeparatorText("Compiler");
        ImGui::InputText("Compiler Path", es.compilerPath, sizeof(es.compilerPath));
        ImGui::InputText("Engine Lib Path", es.engineLibPath, sizeof(es.engineLibPath));

        ImGui::SeparatorText("Include Paths");
        static char newInclude[256] = {};
        ImGui::InputText("##newinc", newInclude, sizeof(newInclude));
        ImGui::SameLine();
        if (ImGui::Button("Add##inc")) {
            if (newInclude[0] != '\0') {
                es.includePaths.push_back(newInclude);
                newInclude[0] = '\0';
            }
        }
        for (int i = 0; i < (int)es.includePaths.size(); ++i) {
            ImGui::PushID(i);
            ImGui::TextUnformatted(es.includePaths[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { es.includePaths.erase(es.includePaths.begin() + i); --i; }
            ImGui::PopID();
        }

        ImGui::SeparatorText("Library Paths");
        static char newLib[256] = {};
        ImGui::InputText("##newlib", newLib, sizeof(newLib));
        ImGui::SameLine();
        if (ImGui::Button("Add##lib")) {
            if (newLib[0] != '\0') {
                es.libraryPaths.push_back(newLib);
                newLib[0] = '\0';
            }
        }
        for (int i = 0; i < (int)es.libraryPaths.size(); ++i) {
            ImGui::PushID(1000 + i);
            ImGui::TextUnformatted(es.libraryPaths[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { es.libraryPaths.erase(es.libraryPaths.begin() + i); --i; }
            ImGui::PopID();
        }

        ImGui::SeparatorText("Link Libraries");
        static char newLinkLib[128] = {};
        ImGui::InputText("##newlnk", newLinkLib, sizeof(newLinkLib));
        ImGui::SameLine();
        if (ImGui::Button("Add##lnk")) {
            if (newLinkLib[0] != '\0') {
                es.linkLibraries.push_back(newLinkLib);
                newLinkLib[0] = '\0';
            }
        }
        for (int i = 0; i < (int)es.linkLibraries.size(); ++i) {
            ImGui::PushID(2000 + i);
            ImGui::TextUnformatted(es.linkLibraries[i].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { es.linkLibraries.erase(es.linkLibraries.begin() + i); --i; }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Apply & Save", { 160, 0 }))
        {
            ScriptManager::SetToolchainPath(fs::path(es.compilerPath));
            for (auto& inc : es.includePaths)
                ScriptManager::AddIncludePath(fs::path(inc));
            for (auto& lib : es.libraryPaths)
                ScriptManager::AddLibraryPath(fs::path(lib));
            for (auto& lnk : es.linkLibraries)
                ScriptManager::AddLinkLibrary(lnk);
            es.Save(ide.EditorSettingsPath());
            ide.toastMgr.Push("Toolchain settings applied.", Toast::Success, 2.0f);
        }
    }
    ImGui::End();
}

// =============================================================================
//  Script Creation Wizard Modal
// =============================================================================
static void DrawScriptWizardModal(IDEState& ide)
{
    if (ide.showScriptWizard) {
        ImGui::OpenPopup("Create Script##wizard");
        ide.showScriptWizard = false;
    }

    if (ImGui::BeginPopupModal("Create Script##wizard", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static const char* kTemplates[] = { "Empty", "Start/Update", "Full Example" };
        ImGui::InputText("Script Name", ide.newScriptName, sizeof(ide.newScriptName));
        ImGui::Combo("Template", &ide.newScriptTemplate, kTemplates, 3);
        ImGui::TextDisabled("Language: C++ (.cpp)");

        if (ImGui::Button("Create", { 120, 0 }))
        {
            std::string scriptDir = std::string(ide.project.rootFolder).empty()
                ? "scripts/" : (fs::path(ide.project.rootFolder) / "scripts" / "").string();
            fs::create_directories(scriptDir);
            std::string outPath = scriptDir + ide.newScriptName + ".cpp";

            std::ofstream f(outPath);
            if (f.is_open())
            {
                f << "#include \"iscript.h\"\n";
                f << "#include \"baseobject.h\"\n\n";
                f << "class " << ide.newScriptName << " : public IScript {\n";
                f << "public:\n";
                if (ide.newScriptTemplate >= 1) {
                    f << "    void Start() override {}\n";
                    f << "    void Update(float dt) override {}\n";
                }
                if (ide.newScriptTemplate == 2) {
                    f << "    void OnDestroy() override {}\n";
                    f << "    void OnCollision(BaseObject* other) {}\n";
                }
                f << "};\n\n";
                f << "extern \"C\" IScript* CreateScript() { return new " << ide.newScriptName << "(); }\n";
                f << "extern \"C\" void DestroyScript(IScript* s) { delete s; }\n";
                f.close();

                // Register in asset DB
                auto& rec = ide.assetDb.Register(outPath);
                ide.assetDb.Save(ide.HonAssetsPath());
                ide.assetBrowser.dirDirty = true;

                // Attempt to compile
                if (ide.scriptManager) {
                    std::string dllOut;
                    std::string cmd = "g++ -std=c++20 -shared -fPIC -o "
                        + scriptDir + ide.newScriptName + ".so "
                        + outPath + " 2>&1";
                    ScriptManager::CompileScript(outPath, cmd, dllOut);
                }

                ide.toastMgr.Push("Script created: " + outPath, Toast::Success, 2.5f);
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Script] Could not write: " + outPath);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void DrawInspectorPanel(IDEState& ide)
{
    ImGui::BeginChild("##insp", { 0,0 }, false);

    ImGui::PushID("insp_lock");
    if (ImGui::Button(ide.inspectorLocked ? ICON_FA_LOCK : ICON_FA_UNLOCK, { 28, 24 })) {
        if (!ide.inspectorLocked && !ide.selection.Empty()) {
            ide.lockedInspectorObject = ide.selection.Primary();
            ide.inspectorLocked = true;
        }
        else {
            ide.inspectorLocked = false;
            ide.lockedInspectorObject.clear();
        }
    }
    ImGui::PopID();
    ImGui::SameLine();

    std::string inspectionTarget = ide.inspectorLocked ? ide.lockedInspectorObject : ide.selection.Primary();

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload))
        {
            IM_ASSERT(payload->DataSize == sizeof(AssetDragPayload));
            const AssetDragPayload& drop = *static_cast<const AssetDragPayload*>(payload->Data);
            std::string guidStr(drop.guidStr);
            std::string path(drop.path);

            if (drop.type == AssetType::Script && !inspectionTarget.empty())
            {
                // Drop a script directly onto a scene object in the Inspector header
                BaseObject* baseObj = nullptr;
                {
                    std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                    auto it = g_namedObjects.find(inspectionTarget);
                    if (it != g_namedObjects.end()) baseObj = it->second;
                }
                if (baseObj)
                {
                    AssetRecord* rec = ide.assetDb.FindByGUID(guidStr);
                    if (rec)
                    {
                        ScriptComponent newComp;
                        newComp.scriptGUID = guidStr;
                        if (ide.scriptManager && ide.scriptManager->LoadScript(guidStr, rec->path, newComp))
                            baseObj->scripts.push_back(newComp);
                        else
                            ide.log.push(ConsoleLog::REPLY_ERR,
                                "[Inspector] Failed to attach script " + rec->displayName);
                        ide.sceneDirty = true;
                    }
                }
            }
            else if ((drop.type == AssetType::Shader ||
                drop.type == AssetType::ShaderSource) && !inspectionTarget.empty())
            {
                std::string shaderName;
                AssetRecord* rec = ide.assetDb.FindByGUID(guidStr);
                if (rec && rec->type == AssetType::Shader) {
                    // Already a compiled .honshader
                    shaderName = rec->displayName;
                }
                else if (drop.type == AssetType::Shader && rec && rec->type == AssetType::ShaderSource) {
                    // Multi-selected vert+frag pair: find both, create .honshader if needed
                    std::string vertPath, fragPath;
                    std::string stemName = fs::path(rec->path).stem().string();
                    for (auto& g : ide.assetBrowser.selectedGUIDs) {
                        auto* r = ide.assetDb.FindByGUID(g);
                        if (!r) continue;
                        std::string ext = fs::path(r->path).extension().string();
                        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                        if (ext == ".vert" && vertPath.empty()) vertPath = r->path;
                        else if (ext == ".frag" && fragPath.empty()) fragPath = r->path;
                    }
                    if (!vertPath.empty() && !fragPath.empty()) {
                        shaderName = stemName;
                        std::string dir = fs::path(vertPath).parent_path().string();
                        fs::path honPath = fs::path(dir) / (shaderName + ".honshader");
                        if (!fs::exists(honPath)) {
                            CreateShaderAsset(dir, shaderName, vertPath, fragPath);
                            ide.assetDb.Register(honPath.string());
                            ide.assetBrowser.dirDirty = true;
                        }
                        std::ifstream vf(vertPath), ff(fragPath);
                        std::string vsrc((std::istreambuf_iterator<char>(vf)), {});
                        std::string fsrc((std::istreambuf_iterator<char>(ff)), {});
                        if (!vsrc.empty() && !fsrc.empty()) {
                            std::lock_guard<std::mutex> lk(g_deferredShaderMutex);
                            g_deferredShaderTasks.push_back({ shaderName, vsrc, fsrc });
                        }
                    }
                }
                else if (rec) {
                    shaderName = fs::path(rec->path).stem().string();
                }
                if (!shaderName.empty()) {
                    char cmd[512];
                    std::snprintf(cmd, sizeof(cmd), "setshader %s %s",
                        inspectionTarget.c_str(), shaderName.c_str());
                    ide.log.push(ConsoleLog::CMD, std::string("> ") + cmd);
                    ide.bus.send(cmd);
                    ide.toastMgr.Push("Shader '" + shaderName + "' assigned",
                        Toast::Success, 2.f);
                    ide.sceneDirty = true;
                }
            }
            else
            {
                // For any other asset type, open the import overlay
                ide.assetBrowser.focusedGUID = guidStr;
                ide.assetBrowser.selectedGUIDs = { guidStr };
                ide.importOverlayGUID = guidStr;
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = false;
                ide.importOverlayOnImport = nullptr;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ide.snapEnabled) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f,0.9f,0.4f,1.f });
        ImGui::TextUnformatted("  SNAP ON");
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }

    // Multi-selection: delegate to dedicated inspector
    if (ide.selection.Size() > 1) {
        DrawMultiSelectionInspector(ide);
        ImGui::EndChild();
        return;
    }

    if (inspectionTarget.empty())
    {
        ImGui::TextDisabled("  Select an object or light in the Hierarchy.");
        ImGui::EndChild();
        return;
    }

    IDEObject* pObj = nullptr;
    for (auto& o : ide.objects) if (o.name == inspectionTarget) { pObj = &o; break; }

    IDELight* pLt = nullptr;
    for (auto& l : ide.lights) if (l.name == inspectionTarget) { pLt = &l; break; }

    IDECamera* pCam = nullptr;
    for (auto& c : ide.cameras) if (c.name == inspectionTarget) { pCam = &c; break; }

    bool isLight = (pObj == nullptr && pLt != nullptr);
    bool isCamera = (pObj == nullptr && pLt == nullptr && pCam != nullptr);
    bool isUnknown = (pObj == nullptr && pLt == nullptr && pCam == nullptr);

    // ── Camera inspector ─────────────────────────────────────────────────────
    if (isCamera && pCam)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.6f,0.9f,1.0f,1.f });
        ImGui::Text("  " ICON_FA_VIDEO "  %s  [Camera]", inspectionTarget.c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();

        bool changed = false;
        changed |= ImGui::DragFloat3("Position##cam", &pCam->px, 0.05f);
        changed |= ImGui::DragFloat("FOV##cam", &pCam->fov, 0.5f, 10.f, 170.f);

        if (changed && pCam->runtimeCamera)
        {
            pCam->runtimeCamera->transform.position = Vector3(pCam->px, pCam->py, pCam->pz);
            ide.sceneDirty = true;
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Make this the active play camera
        bool isActive = (!ide.cameras.empty() && &ide.cameras[0] == pCam);
        if (isActive)
            ImGui::TextColored({ 0.4f,1.f,0.4f,1.f }, "  " ICON_FA_CIRCLE_CHECK "  Default play camera");
        else if (ImGui::Button("Set as default play camera"))
        {
            // Swap to front so Play picks it up as cameras[0]
            for (size_t i = 1; i < ide.cameras.size(); ++i)
                if (&ide.cameras[i] == pCam) {
                    std::swap(ide.cameras[0], ide.cameras[i]);
                    break;
                }
        }

        ImGui::EndChild();
        return;
    }

    if (isUnknown)
    {
        ImGui::TextDisabled("  '%s' not found in scene.", inspectionTarget.c_str());
        ImGui::Spacing();
        if (ImGui::SmallButton("Clear selection")) ide.selection.Clear();
        ImGui::EndChild();
        return;
    }

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, { 0.85f,0.95f,1.0f,1.f });
    if (isLight) {
        const char* lightIcon = (pLt->lightType == "directional") ? ICON_FA_SUN
            : (pLt->lightType == "ambient") ? ICON_FA_GLOBE
            : ICON_FA_CIRCLE_DOT;
        ImGui::Text("  %s  %s  [Light]", lightIcon, inspectionTarget.c_str());
    }
    else {
        const char* lockIcon = (pObj && pObj->locked) ? " " ICON_FA_LOCK : "";
        ImGui::Text("  %s%s", inspectionTarget.c_str(), lockIcon);
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (pObj && pObj->locked) {
        ImGui::PushStyleColor(ImGuiCol_Text, { 0.6f,0.6f,0.6f,1.f });
        ImGui::TextWrapped("  Object is locked. Right-click in Hierarchy to unlock.");
        ImGui::PopStyleColor();
        ImGui::EndChild();
        return;
    }

    // Transform component
    DrawTransformInspector(ide, inspectionTarget, pObj, isLight);

    // Light component
    if (isLight && pLt) {
        DrawLightInspector(ide, pLt);
    }

    // Material component (objects only)
    if (!isLight && pObj) {
        DrawMaterialInspector(ide, inspectionTarget, pObj);
    }

    // Renderer component
    if (!isLight && pObj) {
        DrawRendererInspector(ide, inspectionTarget, pObj);
    }

    // Scripts component
    if (!isLight && pObj) {
        BaseObject* baseObj = nullptr;
        std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
        auto it = g_namedObjects.find(inspectionTarget);
        if (it != g_namedObjects.end()) baseObj = it->second;
        lock.unlock();
        DrawScriptsInspector(ide, inspectionTarget, baseObj);
    }

    // Physics components (objects only)
    if (!isLight && pObj) {
        BaseObject* baseObj = nullptr;
        {
            std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
            auto it = g_namedObjects.find(inspectionTarget);
            if (it != g_namedObjects.end()) baseObj = it->second;
        }
        DrawRigidBodyInspector(ide, baseObj);
        DrawCollisionTriggerInspector(ide, baseObj);
        DrawAnimatorInspector(ide, baseObj);
        DrawMaterialTextureLayersEditor(ide, baseObj);
        DrawPrefabSection(ide, inspectionTarget, baseObj);
    }

    // ── Asset Import Settings ─────────────────────────────────────────────────
    // Import settings are now shown in the floating ImportSettingsOverlay panel
    // above the Inspector (triggered by asset focus / drag-drop).
    // The overlay is drawn in MainScene_Run after all panels.

    ImGui::EndChild();
}

// =============================================================================
//  FPS Fly Camera (editor, not play mode)
// =============================================================================
static void UpdateFPSCamera(IDEState& ide, float dt)
{
    if (!ide.fpsFlyMode || ide.playing) return;
    if (!ide.viewportHovered && !ide.fpsFlyMode) return;

    Camera* cam = ide.sm->currentCamera;
    if (!cam) return;

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    float speed = 5.0f * dt;

    // Allow speed boost with Shift
    if (keys[SDL_SCANCODE_LSHIFT]) speed *= 3.0f;

    // ZQSD / WASD forward/back
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_Z]) {
        Vector3 fwd = cam->transform.forward();
        cam->transform.position = cam->transform.position + fwd * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
        Vector3 fwd = cam->transform.forward();
        cam->transform.position = cam->transform.position - fwd * speed;
    }
    // Strafe
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_Q]) {
        Vector3 right = cam->transform.right();
        cam->transform.position = cam->transform.position - right * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
        Vector3 right = cam->transform.right();
        cam->transform.position = cam->transform.position + right * speed;
    }
    // Vertical
    if (keys[SDL_SCANCODE_SPACE]) {
        cam->transform.position.y += speed;
    }
    if (keys[SDL_SCANCODE_LCTRL]) {
        cam->transform.position.y -= speed;
    }

    // Mouse look (relative mode is already on when fpsFlyMode is true)
    int mx = 0, my = 0;
    SDL_GetRelativeMouseState(&mx, &my);
    if (mx != 0 || my != 0) {
        float sensitivity = 0.002f;
        // Yaw (Y axis)
        if (mx != 0) {
            float angle = -mx * sensitivity;
            Vector3 up(0, 1, 0);
            cam->transform.rotation = Quaternion::FromAxisAngle(
                up,
                angle * (180.0f / (float)M_PI)) * cam->transform.rotation;
        }
        // Pitch (local X axis) — right-multiply to stay in local space,
        // otherwise diagonal yaw inverts the vertical axis.
        if (my != 0) {
            float angle = -my * sensitivity;
            Vector3 localRight(1, 0, 0);
            cam->transform.rotation = cam->transform.rotation * Quaternion::FromAxisAngle(
                localRight,
                angle * (180.0f / (float)M_PI));
        }
    }
}

// =============================================================================
//  Camera Controls (Orbit, Pan, Zoom) pour l'éditeur
// =============================================================================
static void UpdateEditorCamera(IDEState& ide, float dt, int vw, int vh,
    const ImVec2& imagePos, const ImVec2& imageSize) {

    // Ne pas interférer avec les modes FPS ou Play
    if (ide.fpsFlyMode || ide.playing) return;

    Camera* cam = ide.sm->currentCamera;
    if (!cam) return;

    ImGuiIO& io = ImGui::GetIO();

    // Vérifier si la souris est dans la zone du viewport
    bool mouseInViewport = (io.MousePos.x >= imagePos.x && io.MousePos.x <= imagePos.x + imageSize.x &&
        io.MousePos.y >= imagePos.y && io.MousePos.y <= imagePos.y + imageSize.y);

    // --- Point focal : centre de l'objet sélectionné ou centre de la scène ---
    glm::vec3 pivotPoint(0.0f, 0.0f, 0.0f);

    if (!ide.selection.Empty()) {
        std::string primary = ide.selection.Primary();
        // Chercher l'objet sélectionné
        for (auto& obj : ide.objects) {
            if (obj.name == primary) {
                pivotPoint = glm::vec3(obj.px, obj.py, obj.pz);
                break;
            }
        }
        // Si non trouvé dans les objets, chercher dans les lumières
        if (pivotPoint == glm::vec3(0.0f)) {
            for (auto& light : ide.lights) {
                if (light.name == primary) {
                    pivotPoint = glm::vec3(light.px, light.py, light.pz);
                    break;
                }
            }
        }
        // Si non trouvé dans les lumières, chercher dans les caméras
        if (pivotPoint == glm::vec3(0.0f)) {
            for (auto& cam : ide.cameras) {
                if (cam.name == primary) {
                    pivotPoint = glm::vec3(cam.px, cam.py, cam.pz);
                    break;
                }
            }
        }
    }

    // Si pas de sélection, utiliser le centre de la scène
    if (pivotPoint == glm::vec3(0.0f) && !ide.objects.empty()) {
        for (auto& obj : ide.objects) {
            pivotPoint += glm::vec3(obj.px, obj.py, obj.pz);
        }
        pivotPoint /= (float)ide.objects.size();
    }

    // --- ZOOM : Molette de souris (SLOWER) ---
    float wheel = io.MouseWheel;
    if (wheel != 0.0f && mouseInViewport) {
        // Move camera along its forward direction
        float speed = 2.0f;  // adjust to taste
        Vector3 forward = cam->transform.forward();
        cam->transform.position = cam->transform.position + forward * (wheel * speed);
    }

    // --- Gestion des états de souris ---
    static bool wasRightDragging = false;
    static bool wasMiddleDragging = false;
    static bool dragStartedInViewport = false;  // Nouveau flag pour suivre où le drag a commencé
    static ImVec2 lastMousePos;

    bool rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    bool middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

    // Vérifier si on vient juste de commencer un drag
    bool rightJustStarted = rightMouseDown && !wasRightDragging;
    bool middleJustStarted = middleMouseDown && !wasMiddleDragging;

    // Empêcher le début d'un drag en dehors du viewport
    if (rightJustStarted && !mouseInViewport) {
        // Ne pas démarrer le drag si on est en dehors
        wasRightDragging = false;
    }
    else if (rightJustStarted && mouseInViewport) {
        dragStartedInViewport = true;
    }

    if (middleJustStarted && !mouseInViewport) {
        // Ne pas démarrer le drag si on est en dehors
        wasMiddleDragging = false;
    }
    else if (middleJustStarted && mouseInViewport) {
        dragStartedInViewport = true;
    }

    // Réinitialiser le flag quand aucun bouton n'est enfoncé
    if (!rightMouseDown && !middleMouseDown) {
        dragStartedInViewport = false;
    }

    // Calculer le delta souris
    ImVec2 currentMousePos = io.MousePos;
    ImVec2 mouseDelta = { 0, 0 };

    // Pour le clic droit (orbit)
    if (rightMouseDown && (wasRightDragging || dragStartedInViewport)) {
        if (wasRightDragging) {
            mouseDelta.x = currentMousePos.x - lastMousePos.x;
            mouseDelta.y = (currentMousePos.y - lastMousePos.y) * -1;
        }
        lastMousePos = currentMousePos;
        wasRightDragging = true;
    }

    // Static variables to remember pan start state
    static bool      panActive = false;
    static glm::vec3 panStartPivot;
    static glm::vec3 panStartCamPos;

    // Pour le clic milieu (pan)
    if (middleJustStarted && dragStartedInViewport) {
        // Start a new pan: capture the current pivot and camera position
        panActive = true;
        panStartPivot = pivotPoint;
        panStartCamPos = cam->transform.position.ToGLM();
    }

    if (middleMouseDown && (wasMiddleDragging || dragStartedInViewport)) {
        if (wasMiddleDragging) {
            mouseDelta.x = currentMousePos.x - lastMousePos.x;
            mouseDelta.y = currentMousePos.y - lastMousePos.y;
        }
        lastMousePos = currentMousePos;
        wasMiddleDragging = true;
    }

    // --- ROTATION : Clic droit (rotation autour de la caméra, pas du pivot) ---
    if (rightMouseDown && !io.KeyAlt && (mouseDelta.x != 0 || mouseDelta.y != 0) && dragStartedInViewport) {
        auto accel = [](float raw) -> float {
            float sign = raw < 0.f ? -1.f : 1.f;
            float mag = std::abs(raw);
            return sign * 0.003f * std::pow(mag, 1.1f);
            };
        float deltaX = accel(mouseDelta.x);
        float deltaY = accel(mouseDelta.y);

        // Read current rotation as GLM quat
        auto& r = cam->transform.rotation;
        glm::quat current(
            static_cast<float>(r.w),
            static_cast<float>(r.x),
            static_cast<float>(r.y),
            static_cast<float>(r.z));

        // Yaw: rotate around world Y
        glm::quat yaw = glm::angleAxis(deltaX, glm::vec3(0.f, 1.f, 0.f));

        // Pitch: rotate around camera's local X (right axis)
        glm::vec3 localRight = current * glm::vec3(1.f, 0.f, 0.f);
        glm::quat pitch = glm::angleAxis(-deltaY, localRight);

        glm::quat next = glm::normalize(yaw * pitch * current);

        // Pitch clamp: extract forward and check elevation
        glm::vec3 forward = next * glm::vec3(0.f, 0.f, -1.f);
        float elevation = glm::asin(glm::clamp(forward.y, -1.f, 1.f));
        const float maxPitch = glm::radians(89.f);
        if (std::abs(elevation) <= maxPitch) {
            cam->transform.rotation = Quaternion(next.w, next.x, next.y, next.z);
        }
        else {
            // Apply only yaw, discard pitch that would exceed the limit
            glm::quat yawOnly = glm::normalize(yaw * current);
            cam->transform.rotation = Quaternion(yawOnly.w, yawOnly.x, yawOnly.y, yawOnly.z);
        }
    }
    else if (middleMouseDown && (mouseDelta.x != 0 || mouseDelta.y != 0) && dragStartedInViewport) {
        // Use the stored start pivot for distance-based speed
        float distanceToPivot = glm::distance(panStartCamPos, panStartPivot);
        float panSpeed;
        if (ide.viewportOrtho) {
            panSpeed = ide.orthoSize * 0.002f;
        }
        else {
            panSpeed = std::clamp(distanceToPivot * 0.002f, 0.5f, 2.0f);
        }

        // Pan: mouse right -> camera moves right; mouse up -> camera moves up
        float deltaX = mouseDelta.x * panSpeed;
        float deltaY = -mouseDelta.y * panSpeed;  // invert Y because mouse down is positive

        glm::vec3 right = cam->transform.right().ToGLM();
        glm::vec3 up = cam->transform.up().ToGLM();
        glm::vec3 panDelta = right * deltaX + up * deltaY;

        cam->transform.position = Vector3(
            panStartCamPos.x + panDelta.x,
            panStartCamPos.y + panDelta.y,
            panStartCamPos.z + panDelta.z
        );
        // Do NOT move the pivot – keep it fixed at the start position.
    }

    // ---------------------------------------------------------
    // RESET DRAG FLAGS WHEN BUTTONS ARE RELEASED (CRITICAL FIX)
    // ---------------------------------------------------------
    if (!rightMouseDown) {
        wasRightDragging = false;
    }
    if (!middleMouseDown) {
        wasMiddleDragging = false;
        panActive = false;
    }
}

// =============================================================================
//  Viewport  — avec contrôles de caméra éditeur (Unity-style)
// =============================================================================
static void HandleShortcuts(IDEState& ide)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard) return;

    // Ctrl+N : New scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N)) {
        ide.bus.send("clearscene");
        ide.sceneDirty = false;
        RefreshSceneList(ide);
    }

    // Ctrl+S : Save scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
        ide.showSaveModal = true;
    }

    // Ctrl+O : Load scene
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) {
        ide.showLoadModal = true;
    }

    // Ctrl+Shift+S : Toggle snap
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        ide.snapEnabled = !ide.snapEnabled;
    }

    // Ctrl+Z : Undo
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (!ide.undoStack.empty()) {
            ide.undoStack.pop_back();
            ide.log.push(ConsoleLog::INFO, "[Edit] Undo");
        }
    }

    // Ctrl+Y or Ctrl+Shift+Z : Redo
    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) ||
        (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))) {
        ide.log.push(ConsoleLog::INFO, "[Edit] Redo (not yet implemented)");
    }

    // Ctrl+D : Duplicate
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        if (!ide.selection.Empty()) {
            std::string newName = ide.selection.Primary() + "_copy";
            ide.bus.send("clone " + ide.selection.Primary() + " " + newName);
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }

    // Delete key
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (!ide.selection.Empty()) {
            for (auto& name : ide.selection.items) {
                ide.bus.send("delete " + name);
            }
            ide.selection.Clear();
            ide.sceneDirty = true;
            RefreshSceneList(ide);
        }
    }

    // Ctrl+Alt+O : Ortho/persp toggle
    if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_O)) {
        ide.viewportOrtho = !ide.viewportOrtho;
        ide.log.push(ConsoleLog::INFO, std::string("Camera: ") + (ide.viewportOrtho ? "Orthographic" : "Perspective"));
    }

    // F : Focus on selected with smooth transition
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl && !ide.selection.Empty()) {
        std::string primary = ide.selection.Primary();
        for (auto& obj : ide.objects) {
            if (obj.name == primary) {
                glm::vec3 target(obj.px, obj.py, obj.pz);
                float objSize = ((std::max))({ obj.sx, obj.sy, obj.sz });
                float distance = (objSize * 1.5f) + 3.0f;
                ide.focus.Start(ide.sm->currentCamera, ide.editorFov,
                    ide.viewportOrtho, ide.orthoSize, target, distance);
                break;
            }
        }
        for (auto& light : ide.lights) {
            if (light.name == primary) {
                glm::vec3 target(light.px, light.py, light.pz);
                float distance = 5.0f;
                ide.focus.Start(ide.sm->currentCamera, ide.editorFov,
                    ide.viewportOrtho, ide.orthoSize, target, distance);
                break;
            }
        }
        for (auto& cam : ide.cameras) {
            if (cam.name == primary) {
                glm::vec3 target(cam.px, cam.py, cam.pz);
                float distance = 5.0f;
                ide.focus.Start(ide.sm->currentCamera, ide.editorFov,
                    ide.viewportOrtho, ide.orthoSize, target, distance);
                break;
            }
        }
    }

    // Tab : FPS fly mode toggle
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyCtrl && !io.KeyAlt) {
        ide.fpsFlyMode = !ide.fpsFlyMode;
        SDL_SetRelativeMouseMode(ide.fpsFlyMode ? SDL_TRUE : SDL_FALSE);
        ide.log.push(ConsoleLog::INFO, std::string("FPS fly mode ") + (ide.fpsFlyMode ? "ON" : "OFF"));
    }

    // Ctrl+1..9 : Restore bookmarks
    if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift) {
        for (int i = 0; i < 9; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey)((int)ImGuiKey_1 + i))) {
                ide.bookmarks.Restore(i, ide.sm->currentCamera, ide.viewportOrtho, ide.orthoSize, ide.editorFov);
                break;
            }
        }
    }

    // Ctrl+Alt+1..9 : Save bookmarks
    if (io.KeyCtrl && io.KeyAlt && !io.KeyShift) {
        for (int i = 0; i < 9; ++i) {
            if (ImGui::IsKeyPressed((ImGuiKey)((int)ImGuiKey_1 + i))) {
                ide.bookmarks.Save(i, ide.sm->currentCamera, ide.viewportOrtho, ide.editorFov, ide.orthoSize);
                ide.log.push(ConsoleLog::INFO, std::string("Bookmark ") + std::to_string(i + 1) + " saved");
                break;
            }
        }
    }

    // Alt+Left / Alt+Right : selection history
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && ide.selectionHistory.CanGoBack()) {
        std::string prev = ide.selectionHistory.Back();
        if (!prev.empty()) {
            ide.selection.SetSingle(prev);
            ide.bus.send("inspect " + prev);
        }
    }
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_RightArrow) && ide.selectionHistory.CanGoForward()) {
        std::string next = ide.selectionHistory.Forward();
        if (!next.empty()) {
            ide.selection.SetSingle(next);
            ide.bus.send("inspect " + next);
        }
    }

    // Q/W/E/R : Tools
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Q)) ide.toolMode = 0;
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_W)) ide.toolMode = 1;
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_E)) ide.toolMode = 2;
    if (!io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_R)) ide.toolMode = 3;

    // Ctrl+P : Play / Stop
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P)) {
        ide.playing = !ide.playing;
        SDL_SetRelativeMouseMode(ide.playing ? SDL_TRUE : SDL_FALSE);
        ide.renderer->SetPlayMode(ide.playing);
    }

    // Ctrl+Shift+P : Pause (if needed)
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P)) {
        ide.log.push(ConsoleLog::INFO, "[Game] Pause (not yet implemented)");
    }

    // F11 : Toggle fullscreen
    if (ImGui::IsKeyPressed(ImGuiKey_F11)) {
        static bool fullscreen = false;
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(ide.window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }

    // Escape : deselect or stop playing or exit FPS mode
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if (ide.fpsFlyMode) {
            ide.fpsFlyMode = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            ide.log.push(ConsoleLog::INFO, "FPS fly mode OFF");
        }
        else if (ide.playing) {
            ide.playing = false;
            SDL_SetRelativeMouseMode(SDL_FALSE);
            ide.renderer->SetPlayMode(false);
            ide.log.push(ConsoleLog::INFO, "Play mode stopped");
        }
        else if (!ide.selection.Empty()) {
            ide.selection.Clear();
            ide.log.push(ConsoleLog::INFO, "Selection cleared");
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  CONSOLE COPY SHORTCUTS (utilise ide.consoleFocused, mis à jour dans DrawConsolePanel)
    // ─────────────────────────────────────────────────────────────────────────

    // Ctrl+C : Copy selected console text
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) {
        if (ide.consoleFocused) {
            if (!ide.log.selectedText.empty()) {
                ImGui::SetClipboardText(ide.log.selectedText.c_str());
                ide.toastMgr.Push("Copied to clipboard", Toast::Success, 1.5f);
            }
            else {
                ide.toastMgr.Push("No text selected", Toast::Warning, 1.5f);
            }
        }
    }

    // Ctrl+Shift+C : Copy all console text
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) {
        if (ide.consoleFocused) {
            std::string allText;
            {
                std::lock_guard<std::mutex> lk(ide.log.mtx);
                for (auto& e : ide.log.entries) {
                    allText += e.text + "\n";
                }
            }
            if (!allText.empty()) {
                ImGui::SetClipboardText(allText.c_str());
                ide.toastMgr.Push("All console text copied (" + std::to_string(ide.log.entries.size()) + " lines)", Toast::Success, 2.0f);
            }
            else {
                ide.toastMgr.Push("Console is empty", Toast::Warning, 1.5f);
            }
        }
    }

    // Ctrl+Shift+X : Clear console
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_X)) {
        if (ide.consoleFocused) {
            ide.log.clear();
            ide.logReadIdx = 0;
            ide.toastMgr.Push("Console cleared", Toast::Info, 1.5f);
        }
    }

    // Ctrl+L : Clear console (alternative)
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_L)) {
        if (ide.consoleFocused) {
            ide.log.clear();
            ide.logReadIdx = 0;
            ide.toastMgr.Push("Console cleared", Toast::Info, 1.5f);
        }
    }

    // Ctrl+A : Select all console text
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A)) {
        if (ide.consoleFocused) {
            std::string allText;
            {
                std::lock_guard<std::mutex> lk(ide.log.mtx);
                for (auto& e : ide.log.entries) {
                    allText += e.text + "\n";
                }
            }
            if (!allText.empty()) {
                ide.log.selectedText = allText;
                ide.toastMgr.Push("All console text selected (press Ctrl+C to copy)", Toast::Info, 2.0f);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  ADDITIONAL UTILITY SHORTCUTS
    // ─────────────────────────────────────────────────────────────────────────

    // Ctrl+Shift+R : Force refresh scene list
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_R)) {
        RefreshSceneList(ide);
        ide.toastMgr.Push("Scene list refreshed", Toast::Info, 1.5f);
    }

    // Ctrl+Shift+E : Export scene as JSON
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E)) {
        ide.bus.send("exportscene scene_export.json");
        ide.toastMgr.Push("Scene exported to scene_export.json", Toast::Success, 2.0f);
    }

    // Ctrl+G : Toggle grid
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G)) {
        ide.showGrid = !ide.showGrid;
        ide.toastMgr.Push(ide.showGrid ? "Grid shown" : "Grid hidden", Toast::Info, 1.0f);
    }

    // Ctrl+I : Toggle 3D icons
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_I)) {
        ide.showIcons3D = !ide.showIcons3D;
        ide.toastMgr.Push(ide.showIcons3D ? "3D Icons shown" : "3D Icons hidden", Toast::Info, 1.0f);
    }

    // Ctrl+W : Toggle wireframe mode
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W)) {
        ide.wireframe = !ide.wireframe;
        ide.bus.send(ide.wireframe ? "wireframe on" : "wireframe off");
        ide.toastMgr.Push(ide.wireframe ? "Wireframe mode ON" : "Wireframe mode OFF", Toast::Info, 1.0f);
    }

    // F1 : Help
    if (ImGui::IsKeyPressed(ImGuiKey_F1)) {
        ide.bus.send("help");
        ide.toastMgr.Push("Help displayed in console", Toast::Info, 2.0f);
    }

    // F2 : Rename selected object
    if (ImGui::IsKeyPressed(ImGuiKey_F2) && !ide.selection.Empty()) {
        std::string primary = ide.selection.Primary();
        strncpy_s(ide.renameOldName, sizeof(ide.renameOldName), primary.c_str(), sizeof(ide.renameOldName) - 1);
        strncpy_s(ide.renameNewName, sizeof(ide.renameNewName), primary.c_str(), sizeof(ide.renameNewName) - 1);
        ide.showRenameModal = true;
    }

    // F5 : Quick save
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        if (strlen(ide.sceneFilePath) > 0) {
            ide.bus.send(std::string("savescene ") + ide.sceneFilePath);
            ide.sceneDirty = false;
            ide.toastMgr.Push("Scene saved", Toast::Success, 1.5f);
        }
        else {
            ide.showSaveModal = true;
        }
    }

    // Ctrl+F : Global search (correction du nom du membre)
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        ide.globalSearch.open = true;
        ide.globalSearch.needsFocus = true;   // remplace focusInput
    }

    // Ctrl+H : Toggle hierarchy panel
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H)) {
        ide.showHierarchy = !ide.showHierarchy;
    }

    // Ctrl+Shift+C : Copy camera position to clipboard
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C)) {
        if (ide.sm && ide.sm->currentCamera) {
            Camera* cam = ide.sm->currentCamera;
            char camPos[128];
            snprintf(camPos, sizeof(camPos), "%.3f %.3f %.3f",
                cam->transform.position.x,
                cam->transform.position.y,
                cam->transform.position.z);
            ImGui::SetClipboardText(camPos);
            ide.toastMgr.Push("Camera position copied", Toast::Success, 1.5f);
        }
    }

    // Ctrl+Shift+V : Paste camera position from clipboard
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V)) {
        if (ide.sm && ide.sm->currentCamera) {
            const char* clipText = ImGui::GetClipboardText();
            if (clipText) {
                float x, y, z;
                if (sscanf_s(clipText, "%f %f %f", &x, &y, &z) == 3) {
                    ide.sm->currentCamera->transform.position = Vector3(x, y, z);
                    ide.toastMgr.Push("Camera position set from clipboard", Toast::Success, 1.5f);
                }
                else {
                    ide.toastMgr.Push("Clipboard does not contain valid position (x y z)", Toast::Warning, 2.0f);
                }
            }
        }
    }

    // Ctrl+0 : Reset camera to default position
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0)) {
        if (ide.sm && ide.sm->currentCamera) {
            ide.sm->currentCamera->transform.position = Vector3(0, 5, 15);
            ide.sm->currentCamera->transform.rotation = Quaternion::LookRotation(Vector3(0, 0, -1));
            ide.toastMgr.Push("Camera reset to default position", Toast::Info, 1.5f);
        }
    }

    // Space : Toggle play mode (alternative to Ctrl+P)
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyCtrl && !io.KeyAlt && !ide.fpsFlyMode) {
        if (!ImGui::IsAnyItemActive()) {
            ide.playing = !ide.playing;
            SDL_SetRelativeMouseMode(ide.playing ? SDL_TRUE : SDL_FALSE);
            ide.renderer->SetPlayMode(ide.playing);
            ide.toastMgr.Push(ide.playing ? "Play mode started" : "Play mode stopped", Toast::Info, 1.5f);
        }
    }
}

// =============================================================================
//  Console
// =============================================================================
static void DrawViewportToolbar(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    ImGui::SetNextWindowPos(ImVec2(imagePos.x + 12, imagePos.y + 12));
    ImGui::SetNextWindowSize(ImVec2(600, 42));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{ 0.10f, 0.10f, 0.14f, 0.85f });
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{ 0.35f, 0.38f, 0.45f, 0.9f });

    ImGui::Begin("##ViewportToolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.22f, 0.22f, 0.28f, 0.95f });
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.35f, 0.35f, 0.45f, 0.95f });
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.5f, 0.65f, 0.85f, 0.95f });

    // View (Q)
    {
        bool isActive = (ide.toolMode == 0);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
        if (ImGui::Button(" Q ", ImVec2(44, 30))) ide.toolMode = 0;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("View mode (Q) - Orbit camera with right click + box selection");
    ImGui::SameLine();

    // Move (W)
    {
        bool isActive = (ide.toolMode == 1);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
        if (ImGui::Button(" W ", ImVec2(44, 30))) ide.toolMode = 1;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move mode (W) - Translate selected object");
    ImGui::SameLine();

    // Rotate (E)
    {
        bool isActive = (ide.toolMode == 2);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
        if (ImGui::Button(" E ", ImVec2(44, 30))) ide.toolMode = 2;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate mode (E) - Rotate selected object");
    ImGui::SameLine();

    // Scale (R)
    {
        bool isActive = (ide.toolMode == 3);
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.4f, 0.6f, 0.9f, 0.95f });
        if (ImGui::Button(" R ", ImVec2(44, 30))) ide.toolMode = 3;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale mode (R) - Scale selected object");
    ImGui::SameLine();

    ImGui::Dummy(ImVec2(8, 0));
    ImGui::SameLine();

    // Local/World toggle
    {
        bool isActive = ide.gizmoLocalMode;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.3f, 0.95f });
        if (ImGui::Button(ide.gizmoLocalMode ? " LOCAL " : " WORLD ", ImVec2(56, 30))) {
            ide.gizmoLocalMode = !ide.gizmoLocalMode;
        }
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Local / World transform space");
    ImGui::SameLine();

    // Pivot / Center toggle
    {
        bool isActive = ide.gizmoPivotCenter;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.3f, 0.95f });
        if (ImGui::Button(ide.gizmoPivotCenter ? " PIVOT " : " CENTER ", ImVec2(56, 30))) {
            ide.gizmoPivotCenter = !ide.gizmoPivotCenter;
        }
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Pivot / Center manipulation");
    ImGui::SameLine();

    // Snap
    {
        bool isActive = ide.snapEnabled;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.6f, 0.3f, 0.95f });
        if (ImGui::Button(ide.snapEnabled ? " SNAP " : " Snap ", ImVec2(56, 30))) {
            ide.snapEnabled = !ide.snapEnabled;
        }
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap to grid (Ctrl+Shift+S)");
    ImGui::SameLine();

    // Grid toggle
    {
        bool isActive = ide.showGrid;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.5f, 0.5f, 0.95f });
        if (ImGui::Button(" GRID ", ImVec2(56, 30))) ide.showGrid = !ide.showGrid;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle grid overlay");
    ImGui::SameLine();

    // Frustum toggle
    {
        bool isActive = ide.showFrustum;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.5f, 0.3f, 0.5f, 0.95f });
        if (ImGui::Button(" FRUSTUM ", ImVec2(64, 30))) ide.showFrustum = !ide.showFrustum;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle camera frustum visualization");
    ImGui::SameLine();

    // Icons 3D toggle
    {
        bool isActive = ide.showIcons3D;
        if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.3f, 0.4f, 0.6f, 0.95f });
        if (ImGui::Button(" ICONS ", ImVec2(56, 30))) ide.showIcons3D = !ide.showIcons3D;
        if (isActive) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle 3D icons for lights and cameras");

    // Pop styles in reverse order of pushes
    ImGui::PopStyleColor(3);  // Button, ButtonHovered, ButtonActive
    ImGui::PopStyleVar(3);    // ItemSpacing, FrameRounding, FramePadding

    ImGui::End();

    // These were pushed BEFORE ImGui::Begin, so pop AFTER ImGui::End
    ImGui::PopStyleColor(2);  // WindowBg, Border
    ImGui::PopStyleVar(2);    // WindowRounding, WindowBorderSize
}

static void DrawViewportStats(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    std::string statsText;
    if (ide.playing) statsText = " " ICON_FA_PLAY " PLAYING ";
    if (ide.showStats) {
        if (!statsText.empty()) statsText += " | ";
        char fpsBuf[64];
        snprintf(fpsBuf, sizeof(fpsBuf), " %d FPS ", ide.statsFps > 0 ? ide.statsFps : 0);
        statsText += fpsBuf;
        char objBuf[64];
        snprintf(objBuf, sizeof(objBuf), " %zu objs ", ide.objects.size());
        statsText += objBuf;
        char lightBuf[64];
        snprintf(lightBuf, sizeof(lightBuf), " %zu lights ", ide.lights.size());
        statsText += lightBuf;
        if (ide.selection.Size() > 1) {
            char selBuf[64];
            snprintf(selBuf, sizeof(selBuf), " %zu selected ", ide.selection.Size());
            statsText += selBuf;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(statsText.c_str());
    ImVec2 statsPos = ImVec2(imagePos.x + imageSize.x - textSize.x - 16, imagePos.y + 12);
    dl->AddRectFilled(statsPos, ImVec2(statsPos.x + textSize.x + 12, statsPos.y + textSize.y + 8),
        IM_COL32(20, 20, 30, 200), 6.0f);
    dl->AddRect(statsPos, ImVec2(statsPos.x + textSize.x + 12, statsPos.y + textSize.y + 8),
        IM_COL32(60, 65, 80, 200), 6.0f);
    dl->AddText(ImVec2(statsPos.x + 6, statsPos.y + 4),
        ide.playing ? IM_COL32(100, 220, 100, 255) : IM_COL32(180, 185, 210, 255),
        statsText.c_str());
}

static void DrawSelectionToast(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::string selText = " Selected: " + ide.selection.Primary();
    if (ide.selection.Size() > 1) {
        selText += " (+" + std::to_string(ide.selection.Size() - 1) + " more)";
    }
    ImVec2 textSize = ImGui::CalcTextSize(selText.c_str());
    ImVec2 toastPos = ImVec2(imagePos.x + 12, imagePos.y + imageSize.y - textSize.y - 12);
    dl->AddRectFilled(toastPos, ImVec2(toastPos.x + textSize.x + 12, toastPos.y + textSize.y + 6),
        IM_COL32(40, 45, 60, 200), 4.0f);
    dl->AddText(ImVec2(toastPos.x + 6, toastPos.y + 3),
        IM_COL32(220, 220, 240, 255), selText.c_str());
}

static void DrawCameraPositionOverlay(IDEState& ide, const ImVec2& imagePos, const ImVec2& imageSize)
{
    Camera* cam = ide.sm ? ide.sm->currentCamera : nullptr;
    if (!cam) return;

    glm::vec3 p = cam->transform.position.ToGLM();
    char buf[80];
    std::snprintf(buf, sizeof(buf), "  CAM  X %.2f  Y %.2f  Z %.2f  ", p.x, p.y, p.z);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    float padX = 10.f, padY = 10.f;
    ImVec2 boxMin = ImVec2(imagePos.x + imageSize.x - textSize.x - padX * 2.f,
        imagePos.y + imageSize.y - textSize.y - padY * 2.f);
    ImVec2 boxMax = ImVec2(imagePos.x + imageSize.x - padX,
        imagePos.y + imageSize.y - padY);

    dl->AddRectFilled(boxMin, boxMax, IM_COL32(20, 22, 32, 210), 5.f);
    dl->AddRect(boxMin, boxMax, IM_COL32(70, 80, 110, 180), 5.f, 0, 1.f);
    dl->AddText(ImVec2(boxMin.x + padX, boxMin.y + padY * 0.5f),
        IM_COL32(140, 200, 255, 255), buf);
}

static void DrawViewportPanel(IDEState& ide, float dt, float reservedTopPx = 0.f)
{
    // GetContentRegionAvail() already accounts for any ImGui items drawn above
    // (e.g. the prefab banner + its separator pixel), so vpW/vpH exactly match
    // the drawable area and imagePos will be at the correct screen position.
    ImVec2 panelSize = ImGui::GetContentRegionAvail();
    int vpW = (int)panelSize.x;
    int vpH = (int)panelSize.y;
    // reservedTopPx is informational — it's already consumed by ImGui layout
    // so we don't subtract it again, but we keep the parameter for future use.
    (void)reservedTopPx;
    if (vpW < 8)  vpW = 8;
    if (vpH < 8)  vpH = 8;
    ide.sceneFBO.resize(vpW, vpH);

    Settings::canvasWidth = vpW;
    Settings::canvasHeight = vpH;

    Camera* cam = ide.sm ? ide.sm->currentCamera : nullptr;
    if (cam) cam->fov = ide.editorFov;

    ide.sceneFBO.bind();
    ide.renderer->Render(dt, ide.sceneFBO.fbo);
    ide.sceneFBO.unbind();

    ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImVec2 imageSize{ (float)vpW, (float)vpH };

    ImGui::Image(
        (ImTextureID)(intptr_t)ide.sceneFBO.color,
        imageSize,
        ImVec2(0, 1), ImVec2(1, 0));

    glm::mat4 rendererView = ide.renderer->GetLastViewMatrix();
    glm::mat4 rendererProj = ide.renderer->GetLastProjMatrix();
    glm::vec3 camPos = cam ? cam->transform.position.ToGLM() : glm::vec3(0.f);

    // The renderer projection uses OpenGL NDC (Y up). The gizmo and raycast
    // functions draw/pick in ImGui screen space (Y down). Flip Y once here so
    // every consumer — gizmo render, gizmo hit-test, raycast, box-select — is
    // consistent with the ImGui Image rect.
    glm::mat4 gizmoProj = rendererProj;
    gizmoProj[1][1] *= -1.f;

    if (cam)
    {
        if (ide.showGrid)
            DrawGrid(rendererView, rendererProj, 20.f, 20, ide.gridPlane);

        if (ide.showIcons3D)
        {
            for (auto& lt : ide.lights)
            {
                glm::vec3 lightColor =
                    lt.lightType == "directional" ? glm::vec3(1.f, 0.95f, 0.6f)
                    : lt.lightType == "ambient" ? glm::vec3(0.6f, 0.8f, 1.f)
                    : glm::vec3(1.f, 0.85f, 0.3f);

                DrawBillboardIcon(glm::vec3(lt.px, lt.py, lt.pz),
                    0.25f, lightColor, rendererView, rendererProj);
            }
        }

        if (ide.showFrustum && !ide.selection.Empty())
        {
            std::string primary = ide.selection.Primary();
            for (auto& obj : ide.objects)
            {
                if (obj.name == primary)
                {
                    glm::vec3 camFwd = cam->transform.forward().ToGLM();
                    glm::vec3 camPos = cam->transform.position.ToGLM();
                    DrawFrustum(camPos, camFwd,
                        glm::radians(ide.editorFov),
                        (float)vpW / (float)vpH,
                        50.f,
                        glm::vec3(0.f, 1.f, 0.f),
                        rendererView, rendererProj);
                    break;
                }
            }
        }
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetDragPayload))
        {
            auto& drop = *static_cast<const AssetDragPayload*>(payload->Data);
            std::string dropExt = fs::path(std::string(drop.path)).extension().string();
            std::string target = ide.selection.Primary();

            // -------------------------------------------------------------
            // 1) Prefab – instantiate directly (no selection needed)
            // -------------------------------------------------------------
            if (dropExt == ".honprefab")
            {
                std::string first = InstantiatePrefab(ide, std::string(drop.path));
                if (!first.empty())
                    ide.pendingSelection = first;
                ide.toastMgr.Push("Prefab instantiated", Toast::Success, 2.f);
            }

            // -------------------------------------------------------------
            // 2) Script – attach to selected object (or warn)
            // -------------------------------------------------------------
            else if (drop.type == AssetType::Script)
            {
                if (target.empty())
                {
                    ide.toastMgr.Push("Select an object first, then drop the script onto it",
                        Toast::Warning, 3.f);
                    ide.log.push(ConsoleLog::INFO,
                        "[Viewport] Drop script: no object selected");
                }
                else
                {
                    AssetRecord* rec = ide.assetDb.FindByGUID(std::string(drop.guidStr));
                    if (rec)
                    {
                        BaseObject* baseObj = nullptr;
                        {
                            std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                            auto it = g_namedObjects.find(target);
                            if (it != g_namedObjects.end()) baseObj = it->second;
                        }
                        if (baseObj)
                        {
                            ScriptComponent newComp;
                            newComp.scriptGUID = drop.guidStr;
                            if (ide.scriptManager &&
                                ide.scriptManager->LoadScript(drop.guidStr, rec->path, newComp))
                            {
                                baseObj->scripts.push_back(newComp);
                                ide.toastMgr.Push("Script attached: " + rec->displayName,
                                    Toast::Success, 2.f);
                                ide.sceneDirty = true;
                            }
                            else
                            {
                                ide.toastMgr.Push("Failed to attach script",
                                    Toast::Error, 2.f);
                            }
                        }
                    }
                }
            }

            // -------------------------------------------------------------
            // 3) Material – assign to selected object (or warn)
            // -------------------------------------------------------------
            else if (drop.type == AssetType::Material)
            {
                if (target.empty())
                {
                    ide.toastMgr.Push("Select an object first, then drop the material",
                        Toast::Warning, 3.f);
                }
                else
                {
                    char cmd[512];
                    std::snprintf(cmd, sizeof(cmd), "setmaterial %s \"%s\"",
                        target.c_str(), drop.path);
                    ide.bus.send(cmd);
                    ide.toastMgr.Push("Material assigned", Toast::Success, 2.f);
                    ide.sceneDirty = true;
                }
            }

            // -------------------------------------------------------------
            // 4) Shader (.honshader, .vert, .frag, or a pair)
            // -------------------------------------------------------------
            else if (drop.type == AssetType::Shader ||
                drop.type == AssetType::ShaderSource ||
                dropExt == ".honshader")
            {
                if (target.empty())
                {
                    ide.toastMgr.Push("Select an object first, then drop the shader",
                        Toast::Warning, 3.f);
                }
                else
                {
                    std::string shaderName;
                    AssetRecord* rec = ide.assetDb.FindByGUID(std::string(drop.guidStr));

                    // Already compiled .honshader
                    if (dropExt == ".honshader")
                    {
                        shaderName = fs::path(std::string(drop.path)).stem().string();
                    }
                    // Multi‑selected .vert + .frag pair
                    else if (drop.type == AssetType::Shader && rec && rec->type == AssetType::ShaderSource)
                    {
                        std::string vertPath, fragPath;
                        std::string stemName = fs::path(rec->path).stem().string();
                        for (auto& g : ide.assetBrowser.selectedGUIDs)
                        {
                            auto* r = ide.assetDb.FindByGUID(g);
                            if (!r) continue;
                            std::string ext = fs::path(r->path).extension().string();
                            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                            if (ext == ".vert" && vertPath.empty()) vertPath = r->path;
                            else if (ext == ".frag" && fragPath.empty()) fragPath = r->path;
                        }
                        if (!vertPath.empty() && !fragPath.empty())
                        {
                            shaderName = stemName;
                            std::string dir = fs::path(vertPath).parent_path().string();
                            fs::path honPath = fs::path(dir) / (shaderName + ".honshader");
                            if (!fs::exists(honPath))
                            {
                                CreateShaderAsset(dir, shaderName, vertPath, fragPath);
                                ide.assetDb.Register(honPath.string());
                                ide.assetBrowser.dirDirty = true;
                            }
                            std::ifstream vf(vertPath), ff(fragPath);
                            std::string vsrc((std::istreambuf_iterator<char>(vf)), {});
                            std::string fsrc((std::istreambuf_iterator<char>(ff)), {});
                            if (!vsrc.empty() && !fsrc.empty())
                            {
                                std::lock_guard<std::mutex> lk(g_deferredShaderMutex);
                                g_deferredShaderTasks.push_back({ shaderName, vsrc, fsrc });
                            }
                        }
                    }
                    // Single .vert or .frag (or any other ShaderSource)
                    else if (rec)
                    {
                        shaderName = fs::path(rec->path).stem().string();
                    }

                    if (!shaderName.empty())
                    {
                        char cmd[512];
                        std::snprintf(cmd, sizeof(cmd), "setshader %s %s",
                            target.c_str(), shaderName.c_str());
                        ide.bus.send(cmd);
                        ide.toastMgr.Push("Shader '" + shaderName + "' assigned",
                            Toast::Success, 2.f);
                        ide.sceneDirty = true;
                    }
                    else
                    {
                        ide.toastMgr.Push("Could not resolve shader from drop",
                            Toast::Error, 2.f);
                    }
                }
            }

            // -------------------------------------------------------------
            // 5) Other asset types (models, textures, audio, ...)
            //    → open import overlay
            // -------------------------------------------------------------
            else
            {
                ide.importOverlayGUID = drop.guidStr;
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = true;
                if (ide.importOverlayAnchorSize.x < 10.f || ide.importOverlayAnchorSize.y < 10.f)
                {
                    ImGuiIO& _io = ImGui::GetIO();
                    ide.importOverlayAnchorPos = ImVec2(_io.DisplaySize.x * 0.6f, _io.DisplaySize.y * 0.1f);
                    ide.importOverlayAnchorSize = ImVec2(_io.DisplaySize.x * 0.22f, _io.DisplaySize.y * 0.5f);
                }
                ide.importOverlayOnImport = [&ide, drop]()
                    {
                        std::string path(drop.path);
                        std::string ext = std::filesystem::path(path).extension().string();
                        std::string stem = std::filesystem::path(path).stem().string();

                        int n = 1;
                        std::string objName = stem;
                        while (g_namedObjects.count(objName) || g_namedLights.count(objName))
                            objName = stem + "_" + std::to_string(n++);

                        glm::vec3 spawnPos = GetCameraSpawnPos(ide);
                        char posBuf[64];
                        std::snprintf(posBuf, sizeof(posBuf), "%.3f %.3f %.3f",
                            spawnPos.x, spawnPos.y, spawnPos.z);

                        std::string cmd;
                        if (ext == ".obj")
                            cmd = "obj " + objName + " \"" + path + "\" " + posBuf;
                        else if (ext == ".gltf" || ext == ".glb")
                            cmd = "gltf " + objName + " \"" + path + "\" " + posBuf;
                        else if (ext == ".hdr")
                            cmd = "loadhdrskybox \"" + path + "\"";

                        if (!cmd.empty())
                        {
                            ide.bus.send(cmd);
                            ide.pendingSelection = objName;
                            ide.sceneDirty = true;
                            RefreshSceneList(ide);
                        }
                        ide.showImportOverlay = false;
                    };
            }
        }
        ImGui::EndDragDropTarget();
    }

    ide.viewportHovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();

    // ── NATIVE CUSTOM GIZMO INTERACTION & RENDERING ──────────────────────────
    bool skipSelection = false;

    if (!ide.selection.Empty() && !ide.playing && cam && ide.toolMode >= 1)
    {
        std::string primary = ide.selection.Primary();
        glm::vec3 objPos(0.0f);
        glm::quat objRot(1.0f, 0.0f, 0.0f, 0.0f);
        bool foundTarget = false;

        // Find Object Target
        {
            std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
            auto it = g_namedObjects.find(primary);
            if (it != g_namedObjects.end() && it->second) {
                objPos = it->second->transform.position.ToGLM();
                objRot = it->second->transform.rotation.ToGLM();
                foundTarget = true;
            }
        }

        // Find Light Target
        if (!foundTarget) {
            std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
            auto it = g_namedLights.find(primary);
            if (it != g_namedLights.end() && it->second) {
                for (auto& l : ide.lights) {
                    if (l.name == primary) {
                        objPos = glm::vec3(l.px, l.py, l.pz);
                        foundTarget = true;
                        break;
                    }
                }
            }
        }

        if (foundTarget) {
            bool mousePressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

            // 1. Logic Update
            bool isGizmoInteracting = ide.gizmo.Update(
                ide.toolMode, objPos, objRot, ide.gizmoLocalMode,
                rendererView, gizmoProj, imagePos.x, imagePos.y, imageSize.x, imageSize.y,
                io.MousePos.x, io.MousePos.y, mousePressed, mouseReleased,
                ide.snapEnabled, ide.snapPosition, ide.snapRotation, ide.snapScale,
                primary, ide.bus
            );

            if (isGizmoInteracting || ide.gizmo.isDragging) {
                skipSelection = true;
                ide.sceneDirty = true;
                if (ide.prefabEditMode) ide.prefabEditDirty = true;
            }

            // 2. FIXED RENDER: Use ImGui's Window DrawList instead of UIRenderer
            // This forces the gizmo to respect ImGui's viewport scissor rectangle
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            ide.gizmo.Render(
                drawList,
                ide.toolMode, objPos, objRot, ide.gizmoLocalMode,
                rendererView, gizmoProj,
                imagePos.x, imagePos.y, imageSize.x, imageSize.y,
                io.MousePos.x, io.MousePos.y
            );
        }
    }

    // ── VIEWPORT SELECTION AND MOUSE PICKING RAYCASTS ───────────────────────
    std::vector<RaycastCandidate> candidates;
    candidates.reserve(ide.objects.size() + ide.lights.size());
    for (auto& obj : ide.objects)
    {
        RaycastCandidate rc;
        rc.name = obj.name;
        rc.center = glm::vec3(obj.px, obj.py, obj.pz);
        rc.halfSize = glm::vec3(obj.sx * 0.5f, obj.sy * 0.5f, obj.sz * 0.5f);
        if (rc.halfSize.x < 0.3f) rc.halfSize.x = 0.3f;
        if (rc.halfSize.y < 0.3f) rc.halfSize.y = 0.3f;
        if (rc.halfSize.z < 0.3f) rc.halfSize.z = 0.3f;
        candidates.push_back(rc);
    }
    for (auto& lt : ide.lights)
    {
        RaycastCandidate rc;
        rc.name = lt.name;
        rc.center = glm::vec3(lt.px, lt.py, lt.pz);
        rc.halfSize = glm::vec3(0.4f, 0.4f, 0.4f);
        candidates.push_back(rc);
    }

    if (ide.viewportHovered && !ide.playing && !ide.fpsFlyMode && !skipSelection)
    {
        bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (leftClicked)
            ide.boxSelect.Begin(io.MousePos);

        if (ide.boxSelect.active)
            ide.boxSelect.Update(io.MousePos);

        if (leftReleased && ide.boxSelect.active)
        {
            if (ide.boxSelect.IsSignificant())
            {
                auto picked = BoxSelectObjects(
                    ide.boxSelect, gizmoProj, rendererView,
                    imagePos.x, imagePos.y, imageSize.x, imageSize.y,
                    candidates);

                if (io.KeyCtrl)
                    for (auto& n : picked) ide.selection.Toggle(n);
                else
                {
                    ide.selection.Clear();
                    for (auto& n : picked) ide.selection.Add(n);
                }

                if (!ide.selection.Empty())
                {
                    PushSelectionHistory(ide, ide.selection.Primary());
                    ide.bus.send("inspect " + ide.selection.Primary());
                }
            }
            else
            {
                std::string hit = RaycastObjects(
                    io.MousePos.x, io.MousePos.y,
                    imagePos.x, imagePos.y, imageSize.x, imageSize.y,
                    gizmoProj, rendererView, camPos, candidates);

                if (!hit.empty())
                {
                    if (io.KeyCtrl)
                        ide.selection.Toggle(hit);
                    else
                        ide.selection.SetSingle(hit);

                    PushSelectionHistory(ide, hit);
                    ide.bus.send("inspect " + ide.selection.Primary());
                }
                else if (!io.KeyCtrl)
                {
                    ide.selection.Clear();
                }
            }
            ide.boxSelect.End();
        }
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ide.boxSelect.End();
    }

    if (ide.boxSelect.active)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ide.boxSelect.Draw(dl);
    }

    // ── FLOATING OVERLAYS ────────────────────────────────────────────────────
    DrawViewportToolbar(ide, imagePos, imageSize);
    DrawViewportStats(ide, imagePos, imageSize);

    if (!ide.selection.Empty() && !ide.playing)
        DrawSelectionToast(ide, imagePos, imageSize);

    if (!ide.playing)
        DrawCameraPositionOverlay(ide, imagePos, imageSize);

    // ── FPS FLY MODE INDICATOR ───────────────────────────────────────────────
    if (ide.fpsFlyMode)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* text = "   FPS FLY MODE — Tab to exit   ";
        ImVec2 tsz = ImGui::CalcTextSize(text);
        ImVec2 bgMin{ imagePos.x + imageSize.x * 0.5f - tsz.x * 0.5f - 8,
                      imagePos.y + 56 };
        ImVec2 bgMax{ bgMin.x + tsz.x + 16, bgMin.y + tsz.y + 8 };
        dl->AddRectFilled(bgMin, bgMax, IM_COL32(30, 120, 30, 220), 6.f);
        dl->AddRect(bgMin, bgMax, IM_COL32(80, 220, 80, 200), 6.f);
        dl->AddText(ImVec2(bgMin.x + 8, bgMin.y + 4),
            IM_COL32(180, 255, 180, 255), text);
    }

    // ── EDITOR-CAMERA ORBIT/PAN/ZOOM ─────────────────────────────────────────
    UpdateEditorCamera(ide, dt, vpW, vpH, imagePos, imageSize);

    // ── STATS FPS COUNTER ────────────────────────────────────────────────────
    if (ide.showStats)
    {
        ide.statsFrameCount++;
        ide.statsTimer += dt;
        if (ide.statsTimer >= 0.5f)
        {
            ide.statsFps = (int)(ide.statsFrameCount / ide.statsTimer);
            ide.statsFrameCount = 0;
            ide.statsTimer = 0.f;
        }
    }
}

static void DrawConsolePanel(IDEState& ide)
{
    // Console log display area
    ImGui::BeginChild("##console_log", { 0,-80 }, false,
        ImGuiWindowFlags_HorizontalScrollbar);
    ide.consoleFocused = ImGui::IsWindowFocused();

    {
        std::lock_guard<std::mutex> lk(ide.log.mtx);

        // Keep track of the current line index for selection
        int lineIdx = 0;

        for (auto& e : ide.log.entries)
        {
            ImVec4 col;
            switch (e.kind) {
            case ConsoleLog::CMD:       col = { 0.70f,0.85f,1.00f,1.f }; break;
            case ConsoleLog::REPLY_OK:  col = { 0.70f,0.95f,0.70f,1.f }; break;
            case ConsoleLog::REPLY_ERR: col = { 1.00f,0.50f,0.45f,1.f }; break;
            default:                    col = { 0.80f,0.80f,0.80f,1.f }; break;
            }

            // Use Selectable for each line to enable text selection/copy
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::PushID(lineIdx);

            // Store text in selectable
            bool selected = false;
            ImGui::Selectable(e.text.c_str(), &selected, ImGuiSelectableFlags_AllowDoubleClick);

            // Handle double-click to select whole line
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ide.log.selectedText = e.text;
                ImGui::SetClipboardText(ide.log.selectedText.c_str());
                ide.toastMgr.Push("Line copied to clipboard", Toast::Success, 1.5f);
            }

            ImGui::PopID();
            ImGui::PopStyleColor();
            lineIdx++;
        }

        // Auto-scroll to bottom
        if (ide.log.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        // Context menu for console
        if (ImGui::BeginPopupContextWindow("ConsoleContext")) {
            if (ImGui::MenuItem("Copy Selected", "Ctrl+C")) {
                // Copy any selected text
                if (!ide.log.selectedText.empty()) {
                    ImGui::SetClipboardText(ide.log.selectedText.c_str());
                }
            }
            if (ImGui::MenuItem("Copy All", "Ctrl+Shift+C")) {
                std::string allText;
                for (auto& e : ide.log.entries) {
                    allText += e.text + "\n";
                }
                ImGui::SetClipboardText(allText.c_str());
                ide.toastMgr.Push("All console text copied", Toast::Success, 2.0f);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Console")) {
                ide.log.clear();
                ide.logReadIdx = 0;
            }
            ImGui::Separator();
            ImGui::Checkbox("Auto-scroll", &ide.log.autoScroll);

            // Optional: Filter menu
            ImGui::Separator();
            if (ImGui::BeginMenu("Filter")) {
                static bool showCmd = true, showOk = true, showErr = true, showInfo = true;
                ImGui::MenuItem("Commands", nullptr, &showCmd);
                ImGui::MenuItem("Success", nullptr, &showOk);
                ImGui::MenuItem("Errors", nullptr, &showErr);
                ImGui::MenuItem("Info", nullptr, &showInfo);
                // TODO: Implement filtering
                ImGui::EndMenu();
            }

            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();

    // Command input area
    ImGui::Separator();

    // Multi-line input buffer - using static buffer for simplicity
    static char inputBuffer[8192] = "";  // 8KB buffer for multi-line commands
    static int historyIndex = -1;

    // Input area with 4 lines height
    ImGui::PushItemWidth(-80.f);

    // Input text with multi-line support
    bool enterPressed = ImGui::InputTextMultiline("##cmdinput", inputBuffer, sizeof(inputBuffer),
        ImVec2(-1, 70),
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CtrlEnterForNewLine |
        ImGuiInputTextFlags_AllowTabInput);

    // Handle keyboard shortcuts for copy/paste in input area
    if (ImGui::IsItemFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !ImGui::GetIO().WantCaptureKeyboard) {
            if (historyIndex > 0) {
                historyIndex--;
                strcpy_s(inputBuffer, sizeof(inputBuffer), ide.cmdHistory[historyIndex].c_str());
                ImGui::SetKeyboardFocusHere(-1);
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !ImGui::GetIO().WantCaptureKeyboard) {
            if (historyIndex < (int)ide.cmdHistory.size() - 1) {
                historyIndex++;
                strcpy_s(inputBuffer, sizeof(inputBuffer), ide.cmdHistory[historyIndex].c_str());
                ImGui::SetKeyboardFocusHere(-1);
            }
            else if (historyIndex == (int)ide.cmdHistory.size() - 1) {
                historyIndex = (int)ide.cmdHistory.size();
                inputBuffer[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1);
            }
        }
    }

    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Send button
    bool sendClicked = ImGui::Button("Send", ImVec2(70, 70));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Execute all commands in the input box\nCtrl+Enter = new line\nUp/Down = command history");
    }

    // Clear button
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(50, 70))) {
        ide.log.clear();
        ide.logReadIdx = 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear console output");
    }

    // Help button
    ImGui::SameLine();
    if (ImGui::Button("?", ImVec2(40, 70))) {
        ide.bus.send("help");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show command list");
    }

    // Process commands when Enter is pressed or Send button clicked
    if (enterPressed || sendClicked) {
        std::string fullInput(inputBuffer);

        if (!fullInput.empty()) {
            // Store in command history (avoid duplicates)
            if (ide.cmdHistory.empty() || ide.cmdHistory.back() != fullInput) {
                ide.cmdHistory.push_back(fullInput);
                // Keep last 100 commands
                while (ide.cmdHistory.size() > 100) {
                    ide.cmdHistory.erase(ide.cmdHistory.begin());
                }
            }
            historyIndex = (int)ide.cmdHistory.size();

            // Split by newline and process each command
            std::vector<std::string> commands;
            std::stringstream ss(fullInput);
            std::string line;

            while (std::getline(ss, line, '\n')) {
                // Trim whitespace
                line.erase(0, line.find_first_not_of(" \t\r\n"));
                line.erase(line.find_last_not_of(" \t\r\n") + 1);
                if (!line.empty()) {
                    commands.push_back(line);
                }
            }

            // Execute all commands
            for (const auto& cmd : commands) {
                ide.log.push(ConsoleLog::CMD, std::string("> ") + cmd);
                ide.bus.send(cmd);
            }

            // Clear input buffer
            inputBuffer[0] = '\0';
        }

        // Refocus the input for next command
        ImGui::SetKeyboardFocusHere(-1);
    }

    // Show helpful hints below input area
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 0.6f, 0.6f, 0.7f, 1.0f });
    ImGui::TextDisabled("  Tips: Ctrl+Enter = new line | Up/Down = command history | Right-click console for copy options");
    ImGui::PopStyleColor();
}

// =============================================================================
//  Modals
// =============================================================================
static void DrawModals(IDEState& ide, bool& quit)
{
    // Add Object modal
    if (ide.showAddObject) {
        ImGui::OpenPopup("Add Object");
        ide.showAddObject = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newObjPos[0] = sp.x; ide.newObjPos[1] = sp.y; ide.newObjPos[2] = sp.z;
    }
    if (ImGui::BeginPopupModal("Add Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static const char* kTypes[] = { "Plane", "OBJ mesh", "glTF / GLB", "Rectangle", "Sphere" };
        static const ObjectType kObjTypeMap[] = { PLANE, OBJ_MESH, GLTF_MESH, RECTANGLE, SPHERE };
        int comboObjIdx = 0;
        for (int i = 0; i < 5; ++i) if (kObjTypeMap[i] == ide.newObjType) { comboObjIdx = i; break; }

        // Fix: Create a modifiable buffer for the name
        static char nameBuffer[256] = "";
        if (comboObjIdx >= 0 && comboObjIdx < 5) {
            strcpy_s(nameBuffer, sizeof(nameBuffer), kTypes[comboObjIdx]);
        }
        ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

        if (ImGui::Combo("Type", &comboObjIdx, kTypes, 5)) ide.newObjType = kObjTypeMap[comboObjIdx];

        ImGui::DragFloat3("Position", ide.newObjPos, 0.1f);
        if (ide.newObjType == CUBE || ide.newObjType == RECTANGLE || ide.newObjType == PLANE)
            ImGui::DragFloat("Scale", &ide.newObjScale, 0.01f, 0.01f, 100.f);
        if (ide.newObjType == SPHERE)
            ImGui::DragFloat("Radius", &ide.newObjScale, 0.01f, 0.01f, 100.f);
        if (ide.newObjType == OBJ_MESH || ide.newObjType == GLTF_MESH)
            ImGui::InputText("File path", ide.newObjFile, sizeof(ide.newObjFile));

        // ── Subdivision controls (Plane & Rectangle only) ─────────────────────
        if (ide.newObjType == PLANE) {
            ImGui::Separator();
            ImGui::TextDisabled("Subdivisions");
            ImGui::DragInt("Segments X##plane", &ide.newObjSubdivX, 1, 1, 256);
            ImGui::DragInt("Segments Z##plane", &ide.newObjSubdivZ, 1, 1, 256);
            ide.newObjSubdivX = (std::max)(1, ide.newObjSubdivX);
            ide.newObjSubdivZ = (std::max)(1, ide.newObjSubdivZ);
        }
        else if (ide.newObjType == RECTANGLE) {
            ImGui::Separator();
            ImGui::TextDisabled("Subdivisions");
            ImGui::DragInt("Segments X##rect", &ide.newObjSubdivX, 1, 1, 256);
            ImGui::DragInt("Segments Y##rect", &ide.newObjSubdivY, 1, 1, 256);
            ImGui::DragInt("Segments Z##rect", &ide.newObjSubdivZ, 1, 1, 256);
            ide.newObjSubdivX = (std::max)(1, ide.newObjSubdivX);
            ide.newObjSubdivY = (std::max)(1, ide.newObjSubdivY);
            ide.newObjSubdivZ = (std::max)(1, ide.newObjSubdivZ);
        }

        ImGui::Combo("Color", &ide.newObjColor, kColorNames, kNumColors);

        if (ImGui::Button("Add", { 120,0 })) {
            char buf[512];
            std::string nm = NameValidator::GetFinalName(nameBuffer, ide.objects, ide.lights);
            float* p = ide.newObjPos;
            switch (ide.newObjType) {
            case CUBE:
                std::snprintf(buf, sizeof(buf), "cube %s %.3f %.3f %.3f %.3f %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjScale,
                    kColorNames[ide.newObjColor]);
                break;
            case RECTANGLE:
                std::snprintf(buf, sizeof(buf), "rect %s %.3f %.3f %.3f %.3f %.3f %d %d %d %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjScale * 2, ide.newObjScale * 2,
                    ide.newObjSubdivX, ide.newObjSubdivY, ide.newObjSubdivZ,
                    kColorNames[ide.newObjColor]);
                break;
            case PLANE:
                std::snprintf(buf, sizeof(buf), "plane %s %.3f %.3f %.3f %.3f %.3f %d %d %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjScale * 2, ide.newObjScale * 2,
                    ide.newObjSubdivX, ide.newObjSubdivZ, kColorNames[ide.newObjColor]);
                break;
            case OBJ_MESH:
                std::snprintf(buf, sizeof(buf), "obj %s \"%s\" %.3f %.3f %.3f",
                    nm.c_str(), ide.newObjFile, p[0], p[1], p[2]);
                break;
            case GLTF_MESH:
                std::snprintf(buf, sizeof(buf), "gltf %s \"%s\" %.3f %.3f %.3f",
                    nm.c_str(), ide.newObjFile, p[0], p[1], p[2]);
                break;
            case SPHERE:
                std::snprintf(buf, sizeof(buf), "sphere %s %.3f %.3f %.3f %.3f %s",
                    nm.c_str(), p[0], p[1], p[2], ide.newObjScale,
                    kColorNames[ide.newObjColor]);
                break;
            default: break;
            }
            ide.log.push(ConsoleLog::CMD, std::string("> ") + buf);
            ide.bus.send(buf);
            ide.pendingSelection = nm;
            ide.sceneDirty = true;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Add Light modal

    if (ide.showAddLight) {
        ImGui::OpenPopup("Add Light");
        ide.showAddLight = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newLightPos[0] = sp.x; ide.newLightPos[1] = sp.y; ide.newLightPos[2] = sp.z;
    }
    if (ImGui::BeginPopupModal("Add Light", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {

        static const char* lightTypes[] = { "Point", "Directional", "Ambient" };
        static const ObjectType kLightTypeMap[] = { POINT_LIGHT, DIRECTIONAL_LIGHT, AMBIENT_LIGHT };

        int comboLightIdx = 0;
        for (int i = 0; i < 3; ++i) if (kLightTypeMap[i] == ide.newLightType) { comboLightIdx = i; break; }
        ImGui::InputText("Name", ide.newLightName, sizeof(ide.newLightName));

        ImGui::Combo("Type", &comboLightIdx, lightTypes, 3);

        ImGui::DragFloat("Intensity", &ide.newLightIntensity, 0.05f, 0.f, 20.f);
        ImGui::ColorEdit3("Color", ide.newLightColor);

        if (ImGui::Button("Add", { 120,0 })) {
            char buf[256];
            int r = (int)(ide.newLightColor[0] * 255),
                g = (int)(ide.newLightColor[1] * 255),
                b = (int)(ide.newLightColor[2] * 255);
            const char* typeStr = (ide.newLightType == DIRECTIONAL_LIGHT) ? "directional"
                : (ide.newLightType == AMBIENT_LIGHT) ? "ambient"
                : "point";
            std::snprintf(buf, sizeof(buf),
                "addlight %s %.2f %d %d %d %s",
                ide.newLightName, ide.newLightIntensity, r, g, b, typeStr);
            ide.log.push(ConsoleLog::CMD, std::string("> ") + buf);
            ide.bus.send(buf);
            ide.pendingSelection = ide.newLightName;
            ide.sceneDirty = true;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Add Camera modal
    if (ide.showAddCamera) {
        ImGui::OpenPopup("Add Camera");
        ide.showAddCamera = false;
        glm::vec3 sp = GetCameraSpawnPos(ide);
        ide.newCameraPos[0] = sp.x; ide.newCameraPos[1] = sp.y; ide.newCameraPos[2] = sp.z;
    }    if (ImGui::BeginPopupModal("Add Camera", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", ide.newCameraName, sizeof(ide.newCameraName));
        ImGui::DragFloat3("Position", ide.newCameraPos, 0.1f);
        ImGui::DragFloat("FOV", &ide.editorFov, 0.5f, 10.f, 170.f);

        if (ImGui::Button("Add", { 120,0 })) {
            // Check for duplicate name among cameras
            bool dupCam = false;
            for (auto& c : ide.cameras) if (c.name == ide.newCameraName) { dupCam = true; break; }
            if (!dupCam) {
                // Create a real Camera in the scene
                Camera* newCam = new Camera(
                    Vector3(ide.newCameraPos[0], ide.newCameraPos[1], ide.newCameraPos[2]),
                    Quaternion::LookRotation(Vector3(0, 0, -1)));
                ide.sm->cameras->push_back(newCam);

                // Register in IDE camera list so Hierarchy shows it
                IDECamera ideCam;
                ideCam.name = ide.newCameraName;
                ideCam.px = ide.newCameraPos[0];
                ideCam.py = ide.newCameraPos[1];
                ideCam.pz = ide.newCameraPos[2];
                ideCam.fov = ide.editorFov;
                ideCam.runtimeCamera = newCam;
                ide.cameras.push_back(ideCam);

                ide.log.push(ConsoleLog::REPLY_OK,
                    std::string("[Camera] '") + ide.newCameraName + "' added to scene");
                ide.sceneDirty = true;
                RebuildHierarchy(ide);
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR,
                    std::string("[Camera] Name '") + ide.newCameraName + "' already exists");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 120,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Create Folder modal (UPDATED for multi-selection)
    if (ide.showCreateFolder) { ImGui::OpenPopup("Create Folder"); ide.showCreateFolder = false; }
    if (ImGui::BeginPopupModal("Create Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Folder name:");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##foldername", ide.newFolderName, sizeof(ide.newFolderName),
            ImGuiInputTextFlags_AutoSelectAll);

        // Option: include selected objects in the folder
        if (!ide.selection.Empty()) {
            ImGui::TextDisabled("  %zu selected item(s) will be moved into folder", ide.selection.Size());
        }

        ImGui::Spacing();
        if (ImGui::Button("Create", { 120,0 })) {
            std::string fname(ide.newFolderName);
            if (!fname.empty()) {
                bool dup = false;
                for (auto& r : ide.hierRoots) {
                    if (r.kind == HierarchyNode::FOLDER && r.name == fname) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    HierarchyNode folder;
                    folder.kind = HierarchyNode::FOLDER;
                    folder.name = fname;
                    folder.folderOpen = true;

                    // Move all selected objects into the folder
                    if (!ide.selection.Empty()) {
                        std::set<std::string> toMove = ide.selection.items;
                        for (auto it = ide.hierRoots.begin(); it != ide.hierRoots.end(); ) {
                            bool isMatch = false;
                            if (it->kind == HierarchyNode::OBJECT || it->kind == HierarchyNode::LIGHT) {
                                if (toMove.count(it->name)) {
                                    folder.children.push_back(*it);
                                    it = ide.hierRoots.erase(it);
                                    isMatch = true;
                                }
                            }
                            if (!isMatch) {
                                ++it;
                            }
                        }
                        // Clear selection after moving
                        ide.selection.Clear();
                    }
                    ide.hierRoots.push_back(folder);
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Multi-object / Folder Prefab modal ─────────────────────────────────────
    // Hierarchy-aware: selecting a parent object automatically includes all its
    // children from the hierarchy (without selecting them in the panel).
    if (ide.showCreatePrefabFromFolder) {
        ImGui::OpenPopup("CreatePrefabFromFolder");
        ide.showCreatePrefabFromFolder = false;
        std::string defaultName = ide.createPrefabFromFolderName.empty()
            ? "MultiPrefab" : ide.createPrefabFromFolderName;
        strncpy_s(ide.createPrefabMultiName, sizeof(ide.createPrefabMultiName),
            defaultName.c_str(), _TRUNCATE);
    }
    if (ImGui::BeginPopupModal("CreatePrefabFromFolder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        bool fromFolder = !ide.createPrefabFromFolderName.empty();

        // ── Header info ───────────────────────────────────────────────────────
        if (fromFolder) {
            ImGui::TextDisabled(ICON_FA_FOLDER "  Folder: %s", ide.createPrefabFromFolderName.c_str());
        }
        else {
            // Count effective objects (selected + all their children in hierarchy)
            // We just display the selected count; children are described below.
            ImGui::TextDisabled(ICON_FA_BOXES_STACKED "  %zu root object(s) selected", ide.selection.Size());
            ImGui::TextDisabled("  Children will be included automatically.");
        }
        ImGui::Spacing();
        ImGui::SetNextItemWidth(280.f);
        ImGui::InputText("Prefab Name", ide.createPrefabMultiName, sizeof(ide.createPrefabMultiName));
        ImGui::Spacing();

        if (ImGui::Button("Save", { 120, 0 }))
        {
            // ── Step 1: Collect all objects + lights to serialize ─────────────
            // For a FOLDER prefab: recursively collect all leaves.
            // For a SELECTION prefab: for each selected name, also collect every
            //   descendant from the hierarchy tree (Unity-style "children follow
            //   parent"), WITHOUT changing the actual selection state.
            std::vector<std::string> objNames;
            std::vector<std::string> lightNames;

            // Helper: recursively collect all leaves under a set of nodes.
            std::function<void(const std::vector<HierarchyNode>&)> collectLeaves =
                [&](const std::vector<HierarchyNode>& nodes) {
                for (auto& n : nodes) {
                    if (n.kind == HierarchyNode::FOLDER)
                        collectLeaves(n.children);
                    else if (n.kind == HierarchyNode::OBJECT)
                        objNames.push_back(n.name);
                    else if (n.kind == HierarchyNode::LIGHT)
                        lightNames.push_back(n.name);
                }
                };

            if (fromFolder) {
                // Gather everything under the named folder.
                for (auto& r : ide.hierRoots) {
                    if (r.kind == HierarchyNode::FOLDER && r.name == ide.createPrefabFromFolderName)
                        collectLeaves(r.children);
                }
            }
            else {
                // For each selected name: find its node in the hierarchy, then
                // recursively collect it AND all its children.  This implements
                // Unity-style "selecting a parent captures the whole subtree".
                std::set<std::string> alreadyAdded;

                std::function<void(const std::vector<HierarchyNode>&, const std::string&)> collectSubtree =
                    [&](const std::vector<HierarchyNode>& nodes, const std::string& targetName) {
                    for (auto& n : nodes) {
                        if (n.kind == HierarchyNode::FOLDER) {
                            collectSubtree(n.children, targetName);
                        }
                        else if (n.name == targetName) {
                            // Found the selected root — emit it plus all children
                            // (leaf objects have no children, but folder wrappers do)
                            if (n.kind == HierarchyNode::OBJECT && !alreadyAdded.count(n.name)) {
                                alreadyAdded.insert(n.name);
                                objNames.push_back(n.name);
                                // Collect any hierarchy children of this node
                                collectLeaves(n.children);
                            }
                            else if (n.kind == HierarchyNode::LIGHT && !alreadyAdded.count(n.name)) {
                                alreadyAdded.insert(n.name);
                                lightNames.push_back(n.name);
                                collectLeaves(n.children);
                            }
                        }
                    }
                    };

                for (auto& nm : ide.selection.items) {
                    collectSubtree(ide.hierRoots, nm);
                }

                // Fallback: if a selected name wasn't found in the hierarchy
                // (e.g. it's a top-level unparented node just added to selection),
                // add it directly.
                for (auto& nm : ide.selection.items) {
                    if (alreadyAdded.count(nm)) continue;
                    bool isLight = false;
                    for (auto& lt : ide.lights) if (lt.name == nm) { isLight = true; break; }
                    if (isLight) lightNames.push_back(nm);
                    else         objNames.push_back(nm);
                }
            }

            // ── Step 2: Serialize to .honprefab ──────────────────────────────
            std::string outDir = std::string(ide.project.rootFolder).empty()
                ? "assets/prefabs/"
                : (fs::path(ide.project.rootFolder) / "assets" / "prefabs" / "").string();
            fs::create_directories(outDir);
            std::string pName(ide.createPrefabMultiName);
            std::string outPath = outDir + pName + ".honprefab";

            std::ofstream pf(outPath);
            if (pf.is_open()) {
                pf << "{\n  \"name\": \"" << pName << "\",\n";
                pf << "  \"objects\": [\n";

                bool firstObj = true;
                glm::vec3 basePos(0.f);
                bool basePosSet = false;
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);

                for (auto& nm : objNames) {
                    auto it = g_namedObjects.find(nm);
                    if (it == g_namedObjects.end()) continue;
                    BaseObject* obj = it->second;

                    // Use the first object's world position as the prefab origin,
                    // so all other transforms are stored relative to it.
                    if (!basePosSet) {
                        basePos = glm::vec3(obj->transform.position.x,
                            obj->transform.position.y,
                            obj->transform.position.z);
                        basePosSet = true;
                    }

                    // Determine object type via RTTI — robust, never wrong.
                    std::string objType = "sphere";
                    if (dynamic_cast<Plane*>(obj))                 objType = "plane";
                    else if (dynamic_cast<HonHengine::Rectangle*>(obj)) objType = "rect";
                    else if (obj->tag.rfind("OBJ:", 0) == 0)           objType = "obj";
                    else if (obj->tag.rfind("GLTF:", 0) == 0)           objType = "gltf";

                    if (!firstObj) pf << ",\n";
                    firstObj = false;

                    pf << "    {\n";
                    pf << "      \"name\": \"" << nm << "\",\n";
                    pf << "      \"type\": \"" << objType << "\",\n";
                    if (objType == "obj" || objType == "gltf") {
                        std::string ap = obj->tag.substr(objType == "obj" ? 4 : 5);
                        pf << "      \"assetPath\": \"" << ap << "\",\n";
                    }
                    pf << "      \"visible\": " << (obj->visible ? "true" : "false") << ",\n";
                    pf << "      \"tag\": " << JStr(obj->tag) << ",\n";

                    // Store position relative to prefab origin (first object).
                    Transform relTransform = obj->transform;
                    relTransform.position.x -= basePos.x;
                    relTransform.position.y -= basePos.y;
                    relTransform.position.z -= basePos.z;
                    WriteTransform(pf, relTransform, 6);
                    pf << ",\n";

                    if (obj->material) WriteMaterial(pf, obj->material, 6);
                    pf << ",\n";
                    pf << "      \"shaderName\": " << JStr(obj->render.shaderName) << ",\n";

                    if (!obj->scripts.empty()) {
                        pf << "      \"scripts\": [\n";
                        for (size_t si = 0; si < obj->scripts.size(); ++si) {
                            WriteScript(pf, obj->scripts[si], 8);
                            if (si + 1 < obj->scripts.size()) pf << ",";
                            pf << "\n";
                        }
                        pf << "      ],\n";
                    }

                    auto rbIt = ide.rigidBodies.find(obj);
                    if (rbIt != ide.rigidBodies.end()) {
                        WriteRigidBody(pf, rbIt->second, 6);
                        pf << ",\n";
                    }

                    auto ctIt = ide.collisionTriggers.find(obj);
                    if (ctIt != ide.collisionTriggers.end()) {
                        WriteCollisionTrigger(pf, ctIt->second, 6);
                        pf << ",\n";
                    }

                    if (obj->animator.active()) {
                        WriteAnimator(pf, obj->animator, 6);
                        pf << ",\n";
                    }

                    WriteRenderComponent(pf, obj->render, ide.renderer->texManager, 6);
                    pf << "\n    }";
                }

                pf << "\n  ],\n";

                // ── Lights ───────────────────────────────────────────────────
                pf << "  \"lights\": [\n";
                bool firstLight = true;
                for (auto& nm : lightNames) {
                    for (auto& lt : ide.lights) {
                        if (lt.name == nm) {
                            if (!firstLight) pf << ",\n";
                            firstLight = false;
                            WriteLightToJson(pf, lt, 4);
                            break;
                        }
                    }
                }
                pf << "\n  ]\n}\n";
                pf.close();

                ide.assetDb.Register(outPath);
                ide.assetDb.Save(ide.HonAssetsPath());
                ide.assetBrowser.dirDirty = true;
                ide.toastMgr.Push("Prefab saved: " + pName + "  (" +
                    std::to_string(objNames.size()) + " objects, " +
                    std::to_string(lightNames.size()) + " lights)", Toast::Success, 3.f);
                ide.log.push(ConsoleLog::REPLY_OK, "[Prefab] Saved: " + outPath);
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Prefab] Could not write: " + outPath);
            }
            ide.createPrefabFromFolderName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) {
            ide.createPrefabFromFolderName.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Add Parent modal ──────────────────────────────────────────────────────
    // Creates a new empty folder that wraps the selected objects (Unity's
    // "Create Empty Parent" / "Add Parent" option).
    if (ide.showAddParentModal) { ImGui::OpenPopup("AddParentModal"); ide.showAddParentModal = false; }
    if (ImGui::BeginPopupModal("AddParentModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("New parent folder name:");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##parentname", ide.addParentName, sizeof(ide.addParentName),
            ImGuiInputTextFlags_AutoSelectAll);
        if (!ide.selection.Empty())
            ImGui::TextDisabled("  %zu object(s) will be parented", ide.selection.Size());
        ImGui::Spacing();
        if (ImGui::Button("Create Parent", { 140, 0 })) {
            std::string pname(ide.addParentName);
            if (!pname.empty()) {
                HierarchyNode parentFolder;
                parentFolder.kind = HierarchyNode::FOLDER;
                parentFolder.name = pname;
                parentFolder.folderOpen = true;

                // Move selected items into the parent folder (same logic as Create Folder)
                std::set<std::string> toMove = ide.selection.items;
                for (auto it = ide.hierRoots.begin(); it != ide.hierRoots.end(); ) {
                    if ((it->kind == HierarchyNode::OBJECT || it->kind == HierarchyNode::LIGHT)
                        && toMove.count(it->name)) {
                        parentFolder.children.push_back(*it);
                        it = ide.hierRoots.erase(it);
                    }
                    else { ++it; }
                }
                ide.hierRoots.push_back(parentFolder);
                ide.selection.Clear();
                ide.toastMgr.Push("Parent folder '" + pname + "' created", Toast::Success, 2.f);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ide.showRenameModal) { ImGui::OpenPopup("Rename Object"); ide.showRenameModal = false; }
    if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("New name for object:");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("##rnname", ide.renameNewName, sizeof(ide.renameNewName),
            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::Spacing();
        if (ImGui::Button("Rename", { 120,0 })) {
            std::string oldN(ide.renameOldName), newN(ide.renameNewName);
            if (!newN.empty() && newN != oldN) {
                ide.bus.send("rename " + oldN + " " + newN);
                // Update selection if the renamed object was selected
                if (ide.selection.Contains(oldN)) {
                    ide.selection.Remove(oldN);
                    ide.selection.Add(newN);
                }
                ide.sceneDirty = true;

                RefreshSceneList(ide);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Quit Confirmation modal
    if (ide.showQuitModal) { ImGui::OpenPopup("QuitConfirmation"); ide.showQuitModal = false; }
    if (ImGui::BeginPopupModal("QuitConfirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The current scene has unsaved changes.");
        ImGui::TextUnformatted("Do you want to save before quitting?");
        ImGui::Separator();
        if (ImGui::Button("Save & Quit", { 120, 0 })) {
            ide.bus.send(std::string("savescene ") + ide.sceneFilePath);
            quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", { 110, 0 })) {
            quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) {
            ide.quitRequested = false;
            ide.showQuitModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Save Scene modal
    if (ide.showSaveModal) { ImGui::OpenPopup("Save Scene"); ide.showSaveModal = false; }
    if (ImGui::BeginPopupModal("Save Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Save scene to file (inside assets/):");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##savepath", ide.sceneFilePath, sizeof(ide.sceneFilePath));
        ImGui::Spacing();
        if (ImGui::Button("Save", { 120,0 })) {
            // Ensure the parent directory exists (scenes live in assets/)
            fs::create_directories(fs::path(ide.sceneFilePath).parent_path());
            // Serialize the live scene to JSON and write to disk
            std::string sceneJson2;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                std::ostringstream oss2;
                oss2 << "{\"objects\":[";
                bool fo2 = true;
                for (auto& [nm, ob] : g_namedObjects) {
                    if (!fo2) oss2 << ","; fo2 = false;
                    oss2 << "{\"name\":" << JStr(nm)
                        << ",\"px\":" << ob->transform.position.x
                        << ",\"py\":" << ob->transform.position.y
                        << ",\"pz\":" << ob->transform.position.z
                        << ",\"sx\":" << ob->transform.scale.x
                        << ",\"sy\":" << ob->transform.scale.y
                        << ",\"sz\":" << ob->transform.scale.z
                        << ",\"rw\":" << ob->transform.rotation.w
                        << ",\"rx\":" << ob->transform.rotation.x
                        << ",\"ry\":" << ob->transform.rotation.y
                        << ",\"rz\":" << ob->transform.rotation.z
                        << ",\"visible\":" << (ob->visible ? "true" : "false")
                        << ",\"cr\":" << (ob->material ? (int)ob->material->color.r : 255)
                        << ",\"cg\":" << (ob->material ? (int)ob->material->color.g : 255)
                        << ",\"cb\":" << (ob->material ? (int)ob->material->color.b : 255)
                        << ",\"ca\":" << (ob->material ? (int)ob->material->color.a : 255)
                        << ",\"shader\":" << JStr(ob->render.shaderName)
                        << ",\"tag\":" << JStr(ob->tag)
                        << ",\"scripts\":[";
                    bool fs2 = true;
                    for (auto& sc : ob->scripts) { if (!fs2) oss2 << ","; fs2 = false; oss2 << JStr(sc.scriptGUID); }
                    oss2 << "]}";
                }
                oss2 << "],\"lights\":[";
                bool fl2 = true;
                for (auto& [nm, lt] : g_namedLights) {
                    if (!fl2) oss2 << ","; fl2 = false;
                    std::string ltype = "point"; float lpx = 0, lpy = 0, lpz = 0;
                    if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) { ltype = "directional"; lpx = dl->transform.position.x; lpy = dl->transform.position.y; lpz = dl->transform.position.z; }
                    else if (auto* pl = dynamic_cast<PointLight*>(lt)) { lpx = pl->transform.position.x; lpy = pl->transform.position.y; lpz = pl->transform.position.z; }
                    oss2 << "{\"name\":" << JStr(nm) << ",\"type\":" << JStr(ltype)
                        << ",\"intensity\":" << lt->intensity
                        << ",\"r\":" << (int)lt->color.r << ",\"g\":" << (int)lt->color.g << ",\"b\":" << (int)lt->color.b
                        << ",\"px\":" << lpx << ",\"py\":" << lpy << ",\"pz\":" << lpz << "}";
                }
                oss2 << "]}";
                sceneJson2 = oss2.str();
            }
            std::ofstream sf2(ide.sceneFilePath);
            if (sf2.is_open()) {
                sf2 << sceneJson2; sf2.close();
                ide.sceneDirty = false;
                ide.assetDb.ScanDirectory(ide.AssetRootDir());
                ide.assetBrowser.dirDirty = true;
                ide.toastMgr.Push("Scene saved: " + std::string(ide.sceneFilePath), Toast::Success, 2.0f);
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Scene] Could not write: " + std::string(ide.sceneFilePath));
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ide.showSceneOpenPrompt) {
        ImGui::OpenPopup("Unsaved Scene");
        ide.showSceneOpenPrompt = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored({ 1.f, 0.85f, 0.3f, 1.f }, ICON_FA_SAVE "  Unsaved Changes");
        ImGui::Separator();
        ImGui::TextWrapped("The current scene has unsaved changes.");
        ImGui::TextWrapped("Do you want to save before opening:");
        ImGui::TextDisabled("  %s", ide.pendingSceneOpenPath.c_str());
        ImGui::Spacing();

        auto doLoad = [&]() {
            ide.bus.send("clearscene");
            ide.bus.send("loadscene " + ide.pendingSceneOpenPath);
            strncpy_s(ide.sceneFilePath, sizeof(ide.sceneFilePath),
                ide.pendingSceneOpenPath.c_str(), sizeof(ide.sceneFilePath) - 1);
            ide.sceneDirty = false;
            ide.selection.Clear();
            RefreshSceneList(ide);
            ide.log.push(ConsoleLog::REPLY_OK, "[Scene] Loaded: " + ide.pendingSceneOpenPath);
            ide.pendingSceneOpenPath.clear();
            ImGui::CloseCurrentPopup();
            };

        if (ImGui::Button("Save & Open", { 130, 0 })) {
            // Serialize current scene to sceneFilePath then load the new one
            fs::create_directories(fs::path(ide.sceneFilePath).parent_path());
            std::string sceneJson;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                std::ostringstream oss;
                oss << "{\"objects\":[";
                bool firstObj = true;
                for (auto& [name, obj] : g_namedObjects) {
                    if (!firstObj) oss << ",";
                    firstObj = false;
                    oss << "{\"name\":" << JStr(name)
                        << ",\"px\":" << obj->transform.position.x
                        << ",\"py\":" << obj->transform.position.y
                        << ",\"pz\":" << obj->transform.position.z
                        << ",\"sx\":" << obj->transform.scale.x
                        << ",\"sy\":" << obj->transform.scale.y
                        << ",\"sz\":" << obj->transform.scale.z
                        << ",\"rw\":" << obj->transform.rotation.w
                        << ",\"rx\":" << obj->transform.rotation.x
                        << ",\"ry\":" << obj->transform.rotation.y
                        << ",\"rz\":" << obj->transform.rotation.z
                        << ",\"visible\":" << (obj->visible ? "true" : "false")
                        << ",\"cr\":" << (obj->material ? (int)obj->material->color.r : 255)
                        << ",\"cg\":" << (obj->material ? (int)obj->material->color.g : 255)
                        << ",\"cb\":" << (obj->material ? (int)obj->material->color.b : 255)
                        << ",\"ca\":" << (obj->material ? (int)obj->material->color.a : 255)
                        << ",\"shader\":" << JStr(obj->render.shaderName)
                        << ",\"tag\":" << JStr(obj->tag)
                        << ",\"scripts\":[";
                    bool firstScript = true;
                    for (auto& sc : obj->scripts) {
                        if (!firstScript) oss << ",";
                        firstScript = false;
                        oss << JStr(sc.scriptGUID);
                    }
                    oss << "]}";
                }
                oss << "],\"lights\":[";
                bool firstLight = true;
                for (auto& [name, lt] : g_namedLights) {
                    if (!firstLight) oss << ",";
                    firstLight = false;
                    std::string lightType = "point";
                    float px = 0, py = 0, pz = 0;
                    if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                        lightType = "directional";
                        px = dl->transform.position.x;
                        py = dl->transform.position.y;
                        pz = dl->transform.position.z;
                    }
                    else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                        px = pl->transform.position.x;
                        py = pl->transform.position.y;
                        pz = pl->transform.position.z;
                    }
                    oss << "{\"name\":" << JStr(name)
                        << ",\"type\":" << JStr(lightType)
                        << ",\"intensity\":" << lt->intensity
                        << ",\"r\":" << (int)lt->color.r
                        << ",\"g\":" << (int)lt->color.g
                        << ",\"b\":" << (int)lt->color.b
                        << ",\"px\":" << px << ",\"py\":" << py << ",\"pz\":" << pz << "}";
                }
                oss << "]}";
                sceneJson = oss.str();
            }
            std::ofstream sf(ide.sceneFilePath);
            if (sf.is_open()) {
                sf << sceneJson;
                sf.close();
                ide.log.push(ConsoleLog::REPLY_OK, "[Scene] Saved: " + std::string(ide.sceneFilePath));
                ide.sceneDirty = false;
                ide.assetDb.ScanDirectory(ide.AssetRootDir());
                ide.assetBrowser.dirDirty = true;
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Scene] Could not save: " + std::string(ide.sceneFilePath));
            }
            doLoad();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", { 110, 0 })) {
            doLoad();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 80, 0 })) {
            ide.pendingSceneOpenPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Load Scene modal
    if (ide.showLoadModal) { ImGui::OpenPopup("Load Scene"); ide.showLoadModal = false; }
    if (ImGui::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Load scene from file:");
        ImGui::SetNextItemWidth(340.f);
        ImGui::InputText("##loadpath", ide.sceneFilePath, sizeof(ide.sceneFilePath));
        ImGui::Spacing();
        ImGui::TextColored({ 1.f,0.6f,0.2f,1.f }, "  Warning: current scene will be replaced.");
        ImGui::Spacing();
        if (ImGui::Button("Load", { 120,0 })) {
            ide.bus.send("clearscene");
            ide.bus.send(std::string("loadscene ") + ide.sceneFilePath);
            ide.selection.Clear();  // Clear selection when loading new scene
            ide.sceneDirty = false;
            RefreshSceneList(ide);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ── Ship Game modal (real compile-and-link build) ─────────────────────────
    if (ide.showShipDialog) {
        ImGui::OpenPopup("Ship Game");
        ide.showShipDialog = false;

        // Refresh scene list from disk every time the dialog opens.
        // Scenes are always stored under <projectDir>/assets/ — scan only there.
        ide.projectScenes.clear();
        ide.shipFirstScene = 0;
        fs::path projRoot(ide.shipSrcDir);
        if (!fs::exists(projRoot)) projRoot = fs::current_path();
        fs::path assetsDir = projRoot / "assets";
        auto scanScenesIntoList = [&](const fs::path& dir) {
            if (!fs::exists(dir)) return;
            try {
                for (auto& entry : fs::recursive_directory_iterator(
                    dir, fs::directory_options::skip_permission_denied))
                {
                    if (!entry.is_regular_file()) continue;
                    if (entry.path().extension() != ".honscene") continue;
                    IDEState::SceneEntry se;
                    se.path = entry.path().string();
                    se.name = entry.path().stem().string();
                    se.selected = true;
                    ide.projectScenes.push_back(se);
                }
            }
            catch (...) {}
            };
        scanScenesIntoList(assetsDir);

        // If no scenes found, add the current scene file as a default entry
        if (ide.projectScenes.empty()) {
            IDEState::SceneEntry se;
            se.path = ide.sceneFilePath;
            se.name = fs::path(ide.sceneFilePath).stem().string();
            se.selected = true;
            ide.projectScenes.push_back(se);
        }
    }

    if (ImGui::BeginPopupModal("Ship Game", nullptr,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::TextColored({ 0.5f,0.85f,1.f,1.f }, ICON_FA_BOXES_PACKING "  Build & Ship");
        ImGui::TextDisabled("Compiles selected scenes' scripts into one standalone executable.");
        ImGui::Separator();

        // ── Paths ──
        ImGui::SetNextItemWidth(380.f);
        if (ImGui::InputText("Project dir##ship", ide.shipSrcDir, sizeof(ide.shipSrcDir))) {
            // Re-scan when project dir changes — always look in <projectDir>/assets/
            ide.projectScenes.clear();
            ide.shipFirstScene = 0;
            fs::path assetsSubDir = fs::path(ide.shipSrcDir) / "assets";
            if (fs::exists(assetsSubDir)) {
                try {
                    for (auto& entry : fs::recursive_directory_iterator(
                        assetsSubDir, fs::directory_options::skip_permission_denied))
                    {
                        if (!entry.is_regular_file() ||
                            entry.path().extension() != ".honscene") continue;
                        IDEState::SceneEntry se;
                        se.path = entry.path().string();
                        se.name = entry.path().stem().string();
                        se.selected = true;
                        ide.projectScenes.push_back(se);
                    }
                }
                catch (...) {}
            }
        }
        ImGui::SameLine(); ImGui::TextDisabled("(root of your project)");

        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("Output dir##ship", ide.shipDstDir, sizeof(ide.shipDstDir));
        ImGui::SameLine(); ImGui::TextDisabled("(dist folder, created if missing)");

        // ── Static ship config stored in IDEState ──
        static char s_gameName[128] = "MyGame";
        static char s_compiler[512] = "";   // auto-detected from ScriptManager
        static char s_engineLib[512] = "";   // path to GameEngine.a
        static char s_sdl2LibDir[512] = "";   // dir with SDL2.lib / libSDL2.a
        static char s_sdl2Dll[512] = "";   // SDL2.dll (Windows copy)
        static bool s_debugBuild = false;
        static bool s_createArchive = true;

        ImGui::Spacing();
        ImGui::SeparatorText("Build Settings");

        ImGui::SetNextItemWidth(200.f);
        ImGui::InputText("Game name##ship", s_gameName, sizeof(s_gameName));
        ImGui::SameLine(); ImGui::TextDisabled("(executable filename, no extension)");

        // ── Scene Selection & Ordering ──────────────────────────────────────
        ImGui::Spacing();
        ImGui::SeparatorText("Scenes  (check to include · drag to reorder · top = startup scene)");

        // Clamp shipFirstScene in case scenes were removed
        if (ide.shipFirstScene >= (int)ide.projectScenes.size())
            ide.shipFirstScene = 0;

        // Show the derived entry scene path (read-only hint)
        {
            std::string entryHint = "(none selected)";
            // Find first selected scene — that's the startup scene
            for (int si = 0; si < (int)ide.projectScenes.size(); ++si) {
                if (ide.projectScenes[si].selected) {
                    entryHint = ide.projectScenes[si].path;
                    break;
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f, 0.85f, 0.5f, 1.f });
            ImGui::TextUnformatted(("  Startup scene: " + entryHint).c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        // Scrollable region for scene list
        ImGui::BeginChild("##scene_list", ImVec2(520, 160), true);

        int swapA = -1, swapB = -1; // deferred swap for drag-reorder
        for (int si = 0; si < (int)ide.projectScenes.size(); ++si) {
            auto& sc = ide.projectScenes[si];
            ImGui::PushID(si);

            // Drag handle (≡)
            ImGui::TextDisabled(" \xE2\x89\xA1 "); // ≡ U+2261
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;
                if (dy < -14.f && si > 0) { swapA = si - 1; swapB = si; ImGui::ResetMouseDragDelta(); }
                else if (dy > 14.f && si < (int)ide.projectScenes.size() - 1) { swapA = si; swapB = si + 1; ImGui::ResetMouseDragDelta(); }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag up/down to reorder");

            ImGui::SameLine();

            // Checkbox — include in build
            ImGui::Checkbox(("##sel" + std::to_string(si)).c_str(), &sc.selected);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Include this scene in the build");

            ImGui::SameLine();

            // Scene name + path
            bool isFirst = sc.selected && [&]() {
                for (int j = 0; j < si; ++j) if (ide.projectScenes[j].selected) return false;
                return true;
                }();

            if (isFirst) ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f, 1.f, 0.4f, 1.f });
            else if (!sc.selected) ImGui::PushStyleColor(ImGuiCol_Text, { 0.45f, 0.45f, 0.45f, 1.f });
            else ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

            ImGui::Text("%-28s", sc.name.c_str());
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::TextDisabled("%s", sc.path.c_str());

            ImGui::PopID();
        }

        // Apply drag-reorder swap
        if (swapA >= 0 && swapB < (int)ide.projectScenes.size()) {
            std::swap(ide.projectScenes[swapA], ide.projectScenes[swapB]);
        }

        ImGui::EndChild();

        // Quick-action buttons for scene list
        if (ImGui::SmallButton("Select All"))
            for (auto& sc : ide.projectScenes) sc.selected = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Deselect All"))
            for (auto& sc : ide.projectScenes) sc.selected = false;
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh List")) {
            ide.projectScenes.clear();
            ide.shipFirstScene = 0;
            fs::path assetsSubDir2 = fs::path(ide.shipSrcDir) / "assets";
            if (fs::exists(assetsSubDir2)) {
                try {
                    for (auto& entry : fs::recursive_directory_iterator(
                        assetsSubDir2, fs::directory_options::skip_permission_denied))
                    {
                        if (!entry.is_regular_file() ||
                            entry.path().extension() != ".honscene") continue;
                        IDEState::SceneEntry se;
                        se.path = entry.path().string();
                        se.name = entry.path().stem().string();
                        se.selected = true;
                        ide.projectScenes.push_back(se);
                    }
                }
                catch (...) {}
            }
            ide.toastMgr.Push("Scene list refreshed", Toast::Info, 1.5f);
        }

        // Derive entry scene string (first selected scene, top-to-bottom)
        std::string s_entryScene;
        for (auto& sc : ide.projectScenes)
            if (sc.selected) { s_entryScene = sc.path; break; }

        ImGui::Spacing();
        ImGui::SeparatorText("Toolchain  (blank = use editor settings automatically)");

        // Show what will actually be used from ScriptManager
        {
            std::string smCompiler = ScriptManager::GetCompilerPath().string();
            std::string smEngLib = ScriptManager::GetEngineLibraryPath().string();
            std::string smSdl2 = ScriptManager::GetSDL2Path().string();
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.5f,0.5f,0.5f,1.f });
            ImGui::Text("  Active compiler : %s", smCompiler.empty() ? "g++ (system)" : smCompiler.c_str());
            ImGui::Text("  Engine lib      : %s", smEngLib.empty() ? "(not found)" : smEngLib.c_str());
            ImGui::Text("  SDL2            : %s", smSdl2.empty() ? "(not found)" : smSdl2.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();

        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("Override compiler##ship", s_compiler, sizeof(s_compiler));

        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("Override engine lib##ship", s_engineLib, sizeof(s_engineLib));

        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("Override SDL2 lib dir##ship", s_sdl2LibDir, sizeof(s_sdl2LibDir));

#ifdef _WIN32
        ImGui::SetNextItemWidth(380.f);
        ImGui::InputText("Override SDL2.dll path##ship", s_sdl2Dll, sizeof(s_sdl2Dll));
#endif

        ImGui::Spacing();
        ImGui::Checkbox("Debug build (-g, no optimisations)", &s_debugBuild);
        ImGui::SameLine(0, 24);
        ImGui::Checkbox("Create .tar.gz archive", &s_createArchive);

        ImGui::Spacing();
        static bool s_bakeSceneToCpp = false;
        ImGui::Checkbox("Bake scene to C++ (faster load, no JSON at runtime)", &s_bakeSceneToCpp);
        if (s_bakeSceneToCpp)
            ImGui::TextDisabled("  The current live scene will be baked directly into the executable.");

        ImGui::Spacing();
        ImGui::Separator();

        // ── Validation hint ──
        bool canBuild = (strlen(ide.shipSrcDir) > 0 && strlen(ide.shipDstDir) > 0
            && !s_entryScene.empty() && strlen(s_gameName) > 0);
        if (!canBuild)
            ImGui::TextColored({ 1.f,0.5f,0.2f,1.f }, "  Fill in Project dir, Output dir, Game name, and select at least one scene.");

        ImGui::Spacing();
        ImGui::BeginDisabled(!canBuild);

        if (ImGui::Button("  " ICON_FA_HAMMER "  Build & Ship  ", { 220,0 })) {
            ImGui::CloseCurrentPopup();

            // s_entryScene is the full path of the first selected scene.
            // We auto-save the current live scene into it so the shipped game
            // always reflects what the editor shows right now.
            fs::path savePath = fs::path(s_entryScene);
            if (savePath.is_relative())
                savePath = fs::path(ide.shipSrcDir) / savePath;
            fs::create_directories(savePath.parent_path());

            std::string sceneJson;
            {
                std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                std::ostringstream oss;
                oss << "{\"objects\":[";
                bool firstObj = true;
                for (auto& [name, obj] : g_namedObjects) {
                    if (!firstObj) oss << ",";
                    firstObj = false;
                    oss << "{\"name\":" << JStr(name)
                        << ",\"px\":" << obj->transform.position.x
                        << ",\"py\":" << obj->transform.position.y
                        << ",\"pz\":" << obj->transform.position.z
                        << ",\"sx\":" << obj->transform.scale.x
                        << ",\"sy\":" << obj->transform.scale.y
                        << ",\"sz\":" << obj->transform.scale.z
                        << ",\"rw\":" << obj->transform.rotation.w
                        << ",\"rx\":" << obj->transform.rotation.x
                        << ",\"ry\":" << obj->transform.rotation.y
                        << ",\"rz\":" << obj->transform.rotation.z
                        << ",\"visible\":" << (obj->visible ? "true" : "false")
                        << ",\"cr\":" << (obj->material ? (int)obj->material->color.r : 255)
                        << ",\"cg\":" << (obj->material ? (int)obj->material->color.g : 255)
                        << ",\"cb\":" << (obj->material ? (int)obj->material->color.b : 255)
                        << ",\"ca\":" << (obj->material ? (int)obj->material->color.a : 255)
                        << ",\"shader\":" << JStr(obj->render.shaderName)
                        << ",\"tag\":" << JStr(obj->tag)
                        << ",\"scripts\":[";
                    bool firstScript = true;
                    for (auto& sc : obj->scripts) {
                        if (!firstScript) oss << ",";
                        firstScript = false;
                        oss << JStr(sc.scriptGUID);
                    }
                    oss << "]}";
                }
                oss << "],\"lights\":[";
                bool firstLight = true;
                for (auto& [name, lt] : g_namedLights) {
                    if (!firstLight) oss << ",";
                    firstLight = false;
                    std::string lightType = "point";
                    float px = 0, py = 0, pz = 0;
                    if (auto* dl = dynamic_cast<DirectionalLight*>(lt)) {
                        lightType = "directional";
                        px = dl->transform.position.x;
                        py = dl->transform.position.y;
                        pz = dl->transform.position.z;
                    }
                    else if (auto* pl = dynamic_cast<PointLight*>(lt)) {
                        lightType = "point";
                        px = pl->transform.position.x;
                        py = pl->transform.position.y;
                        pz = pl->transform.position.z;
                    }
                    oss << "{\"name\":" << JStr(name)
                        << ",\"type\":" << JStr(lightType)
                        << ",\"intensity\":" << lt->intensity
                        << ",\"r\":" << (int)lt->color.r
                        << ",\"g\":" << (int)lt->color.g
                        << ",\"b\":" << (int)lt->color.b
                        << ",\"px\":" << px
                        << ",\"py\":" << py
                        << ",\"pz\":" << pz << "}";
                }
                oss << "]}";
                sceneJson = oss.str();
            }

            std::ofstream f(savePath);
            if (f.is_open()) {
                f << sceneJson;
                f.close();
                ide.log.push(ConsoleLog::REPLY_OK, "[Ship] Auto-saved scene to: " + savePath.string());
            }
            else {
                ide.log.push(ConsoleLog::REPLY_ERR, "[Ship] Failed to auto-save scene to: " + savePath.string());
            }

            HonHengine::ShipConfig cfg;
            cfg.projectDir = ide.shipSrcDir;
            cfg.outputDir = ide.shipDstDir;
            cfg.gameName = s_gameName;
            cfg.entryScene = savePath.string();
            cfg.debugBuild = s_debugBuild;
            cfg.createArchive = s_createArchive;
            cfg.bakeSceneToCpp = s_bakeSceneToCpp;

            // Populate the ordered list of selected scenes.
            // CollectSceneScriptGUIDs will restrict its scan to only these files.
            for (auto& sc : ide.projectScenes)
                if (sc.selected) cfg.selectedScenes.push_back(sc.path);

            // ── Capture live scene snapshot for baked build ───────────────────────
            if (s_bakeSceneToCpp) {
                // Cameras
                for (const auto& cam : ide.cameras) {
                    HonHengine::CameraSnapshot cs;
                    cs.name = cam.name;
                    cs.px = cam.px; cs.py = cam.py; cs.pz = cam.pz;
                    cs.fov = cam.fov;
                    if (cam.runtimeCamera) {
                        cs.rx = cam.runtimeCamera->transform.rotation.x;
                        cs.ry = cam.runtimeCamera->transform.rotation.y;
                        cs.rz = cam.runtimeCamera->transform.rotation.z;
                        cs.rw = cam.runtimeCamera->transform.rotation.w;
                    }
                    else {
                        cs.rx = 0; cs.ry = 0; cs.rz = 0; cs.rw = 1;
                    }
                    cfg.cameras.push_back(cs);
                }

                // Skybox
                cfg.skybox.mode = ide.skyboxMode; // 0=procedural, 2=cubemap
                for (int _i = 0; _i < 6; ++_i)
                    cfg.skybox.faces[_i] = ide.skyboxFaces[_i];
                // sunIntensity / sunColor: derive from first directional light if available
                {
                    std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                    for (const auto& [_lname, _lt] : g_namedLights) {
                        if (dynamic_cast<DirectionalLight*>(_lt)) {
                            cfg.skybox.sunIntensity = _lt->intensity;
                            cfg.skybox.sunColor[0] = _lt->color.r / 255.f;
                            cfg.skybox.sunColor[1] = _lt->color.g / 255.f;
                            cfg.skybox.sunColor[2] = _lt->color.b / 255.f;
                            break;
                        }
                    }
                }

                // Objects
                {
                    std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                    for (const auto& [_oname, _o] : g_namedObjects) {
                        HonHengine::ObjectSnapshot snap;
                        snap.name = _oname;
                        snap.tag = _o->tag;
                        snap.px = _o->transform.position.x;
                        snap.py = _o->transform.position.y;
                        snap.pz = _o->transform.position.z;
                        snap.sx = _o->transform.scale.x;
                        snap.sy = _o->transform.scale.y;
                        snap.sz = _o->transform.scale.z;
                        snap.rx = _o->transform.rotation.x;
                        snap.ry = _o->transform.rotation.y;
                        snap.rz = _o->transform.rotation.z;
                        snap.rw = _o->transform.rotation.w;
                        snap.visible = _o->visible;

                        // Determine kind
                        if (dynamic_cast<Plane*>(_o))      snap.kind = "Plane";
                        else if (dynamic_cast<Sphere*>(_o))    snap.kind = "Sphere";
                        else if (dynamic_cast<HonHengine::Rectangle*> (_o)) snap.kind = "Rectangle";
                        else if (!snap.tag.empty() && snap.tag.rfind("OBJ:", 0) == 0) {
                            snap.kind = "OBJ";
                            snap.assetPath = snap.tag.substr(4);
                        }
                        else if (!snap.tag.empty() && snap.tag.rfind("GLTF:", 0) == 0) {
                            snap.kind = "GLTF";
                            snap.assetPath = snap.tag.substr(5);
                        }
                        else {
                            snap.kind = "Base";
                        }

                        // Material
                        if (_o->material) {
                            snap.hasMaterial = true;
                            snap.specularity = _o->material->specularity;
                            snap.reflectivity = _o->material->reflectivity;
                            snap.cr = _o->material->color.r;
                            snap.cg = _o->material->color.g;
                            snap.cb = _o->material->color.b;
                            snap.ca = _o->material->color.a;
                            for (const auto& _tl : _o->material->textureLayers) {
                                HonHengine::TextureLayerSnapshot tls;
                                tls.texName = _tl.texture.name;
                                tls.texPath = _tl.texture.path;
                                tls.tilingU = _tl.texture.tilingU;
                                tls.tilingV = _tl.texture.tilingV;
                                tls.offsetU = _tl.texture.offsetU;
                                tls.offsetV = _tl.texture.offsetV;
                                tls.blendWeight = _tl.blendWeight;
                                tls.blendMode = (int)_tl.blendMode;
                                snap.textureLayers.push_back(tls);
                            }
                        }

                        // Scripts
                        for (const auto& _sc : _o->scripts)
                            snap.scriptGUIDs.push_back(_sc.scriptGUID);

                        cfg.sceneObjects.push_back(snap);
                    }
                }

                // Lights
                {
                    std::shared_lock<std::shared_mutex> lock(g_sceneMutex);
                    for (const auto& [_lname, _lt] : g_namedLights) {
                        HonHengine::LightSnapshot ls;
                        ls.name = _lname;
                        ls.intensity = _lt->intensity;
                        ls.r = _lt->color.r;
                        ls.g = _lt->color.g;
                        ls.b = _lt->color.b;
                        if (auto* _dl = dynamic_cast<DirectionalLight*>(_lt)) {
                            ls.kind = "directional";
                            ls.px = _dl->transform.position.x;
                            ls.py = _dl->transform.position.y;
                            ls.pz = _dl->transform.position.z;
                            ls.qx = _dl->transform.rotation.x;
                            ls.qy = _dl->transform.rotation.y;
                            ls.qz = _dl->transform.rotation.z;
                            ls.qw = _dl->transform.rotation.w;
                        }
                        else if (auto* _pl = dynamic_cast<PointLight*>(_lt)) {
                            ls.kind = "point";
                            ls.px = _pl->transform.position.x;
                            ls.py = _pl->transform.position.y;
                            ls.pz = _pl->transform.position.z;
                        }
                        else {
                            ls.kind = "base";
                        }
                        cfg.sceneLights.push_back(ls);
                    }
                }
            } // end if (s_bakeSceneToCpp)

            cfg.compilerPath = strlen(s_compiler) > 0 ? s_compiler
                : ScriptManager::GetCompilerPath().string();
            cfg.engineLibPath = strlen(s_engineLib) > 0 ? s_engineLib
                : ScriptManager::GetEngineLibraryPath().string();

            {
                fs::path sm_sdl2 = ScriptManager::GetSDL2Path();
                cfg.sdl2LibDir = strlen(s_sdl2LibDir) > 0 ? s_sdl2LibDir
                    : (sm_sdl2.empty() ? "" : sm_sdl2.parent_path().string());
                cfg.sdl2DllPath = strlen(s_sdl2Dll) > 0 ? s_sdl2Dll
                    : (sm_sdl2.empty() ? "" : sm_sdl2.string());
            }

            ide.log.push(ConsoleLog::INFO,
                "[Ship] Launching build for '" + cfg.gameName + "' ...");

            ConsoleLog* logPtr = &ide.log;
            std::thread([cfg, logPtr] {
                RunShipBuild(cfg, *logPtr);
                }).detach();
        }

        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", { 100,0 })) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
// =============================================================================
//  Menu Bar  (Unity/UE-style: File, Edit, Scene, View, Build, Help,
//                  centred Play/Pause/Step, right-side stats badge)
// =============================================================================
static void DrawMenuBar(IDEState& ide)
{
    if (!ImGui::BeginMainMenuBar()) return;
    // Utiliser des largeurs fixes pour éviter l'étalement
    const float SECTION_WIDTH = 80.0f;
    const float BUTTON_WIDTH = 70.0f;

    // --- FILE (section fixe) ---
    if (ImGui::BeginMenu("File", true))
    {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            ide.bus.send("clearscene");
            ide.sceneDirty = false;
            RefreshSceneList(ide);
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) ide.showSaveModal = true;
        if (ImGui::MenuItem("Load", "Ctrl+O")) ide.showLoadModal = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Ship...")) ide.showShipDialog = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Alt+F4")) ide.quitRequested = true;
        ImGui::EndMenu();
    }

    // --- PROJECT (new) ---
    if (ImGui::BeginMenu("Project", true))
    {
        if (ImGui::MenuItem("New Project...")) ide.projectUI.showNewProject = true;
        if (ImGui::MenuItem("Save Project...")) ide.projectUI.showSaveProjModal = true;
        if (ImGui::MenuItem("Load Project...")) ide.projectUI.showLoadProjModal = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Project Settings...")) ide.projectUI.showProjectSettings = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Package Manager...")) ide.packageUI.show = true;
        ImGui::EndMenu();
    }

    // --- EDIT ---
    if (ImGui::BeginMenu("Edit", true))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !ide.undoStack.empty())) {
            if (!ide.undoStack.empty()) ide.undoStack.pop_back();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !ide.selection.Empty())) {
            if (!ide.selection.Empty()) {
                std::string newName = ide.selection.Primary() + "_copy";
                ide.bus.send("clone " + ide.selection.Primary() + " " + newName);
                ide.sceneDirty = true;
                RefreshSceneList(ide);
            }
        }
        if (ImGui::MenuItem("Delete", "Del", false, !ide.selection.Empty())) {
            ide.bus.send("delete " + ide.selection.Primary());
            ide.sceneDirty = true;
            ide.selection.Clear();
            RefreshSceneList(ide);
        }
        ImGui::EndMenu();
    }

    // --- GAMEOBJECT ---
    if (ImGui::BeginMenu("GameObject", true))
    {
        if (ImGui::BeginMenu("3D Objects"))
        {
            if (ImGui::MenuItem(ICON_FA_SQUARE " Plane")) {
                ide.newObjType = PLANE;
                ide.showAddObject = true;
            }
            if (ImGui::MenuItem(ICON_FA_SQUARE " Rectangle")) {
                ide.newObjType = RECTANGLE;
                ide.showAddObject = true;
            }
            if (ImGui::MenuItem(ICON_FA_CIRCLE " Sphere")) {
                ide.newObjType = SPHERE;
                ide.showAddObject = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Lights"))
        {
            if (ImGui::MenuItem(ICON_FA_LIGHTBULB " Ambient Light")) {
                ide.newLightType = AMBIENT_LIGHT;
                ide.showAddLight = true;
            }
            if (ImGui::MenuItem(ICON_FA_SUN " Directional Light")) {
                ide.newLightType = DIRECTIONAL_LIGHT;
                ide.showAddLight = true;
            }
            if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT " Point Light")) {
                ide.newLightType = POINT_LIGHT;
                ide.showAddLight = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene"))
        {
            if (ImGui::MenuItem(ICON_FA_VIDEO " Camera")) {
                ide.showAddCamera = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- OUTILS ---
    if (ImGui::BeginMenu("Tools", true))
    {
        if (ImGui::MenuItem("View", "Q", ide.toolMode == 0)) ide.toolMode = 0;
        if (ImGui::MenuItem("Move", "W", ide.toolMode == 1)) ide.toolMode = 1;
        if (ImGui::MenuItem("Rotate", "E", ide.toolMode == 2)) ide.toolMode = 2;
        if (ImGui::MenuItem("Scale", "R", ide.toolMode == 3)) ide.toolMode = 3;
        ImGui::Separator();
        if (ImGui::MenuItem("Scripting Toolchain...")) ide.showToolchainWindow = true;
        if (ImGui::MenuItem("Create Script..."))       ide.showScriptWizard = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Profiler", nullptr, ide.showProfiler))     ide.showProfiler = !ide.showProfiler;
        if (ImGui::MenuItem("Memory Stats", nullptr, ide.showMemoryWindow)) ide.showMemoryWindow = !ide.showMemoryWindow;
        ImGui::EndMenu();
    }

    // AI CHAT
    if (ImGui::MenuItem("AI Assistant", nullptr, ide.showAIChat)) {
        ide.showAIChat = !ide.showAIChat;
    }
    // --- VIEW ---
    if (ImGui::BeginMenu("View", true))
    {
        if (ImGui::MenuItem("Wireframe", nullptr, ide.wireframe)) {
            ide.wireframe = !ide.wireframe;
            ide.bus.send(ide.wireframe ? "wireframe on" : "wireframe off");
        }
        if (ImGui::MenuItem("Stats", nullptr, ide.showStats)) {
            ide.showStats = !ide.showStats;
        }
        if (ImGui::MenuItem("  Environment...")) {
            ide.showEnvironmentWindow = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Debug Mode")) {
            if (ImGui::MenuItem("Shaded", nullptr, ide.debugMode == 0)) { ide.debugMode = 0; ide.renderer->debugMode = 0; }
            if (ImGui::MenuItem("Wireframe", nullptr, ide.debugMode == 1)) { ide.debugMode = 1; ide.renderer->debugMode = 1; }
            if (ImGui::MenuItem("Overdraw", nullptr, ide.debugMode == 2)) { ide.debugMode = 2; ide.renderer->debugMode = 2; }
            if (ImGui::MenuItem("Depth", nullptr, ide.debugMode == 3)) { ide.debugMode = 3; ide.renderer->debugMode = 3; }
            if (ImGui::MenuItem("Normals", nullptr, ide.debugMode == 4)) { ide.debugMode = 4; ide.renderer->debugMode = 4; }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    // --- LAYOUT ---
    if (ImGui::BeginMenu("Layout", true)) {
        if (ImGui::MenuItem("Save Layout")) {
            SaveDockLayout(ide.editorSettings.dockLayoutFile);
            ide.toastMgr.Push("Layout saved", Toast::Success, 2.0f);
        }
        if (ImGui::MenuItem("Preferences...")) {
            // Show preferences window
            static bool prefsOpen = true;
            prefsOpen = true;
            DrawPreferencesWindow(ide.editorSettings, prefsOpen);
        }
        ImGui::EndMenu();
    }

    // --- HELP ---
    if (ImGui::BeginMenu("Help", true))
    {
        if (ImGui::MenuItem("Commands")) ide.bus.send("help");
        if (ImGui::MenuItem("Shortcuts")) {
            ide.log.push(ConsoleLog::INFO,
                "F1=Help  Ctrl+N=New  Ctrl+S=Save  Ctrl+O=Load  Ctrl+D=Duplicate  Del=Delete  Q/W/E/R=Tools");
        }
        ImGui::EndMenu();
    }

    // --- Espacement centralisé pour Play/Stop ---
    float menuWidth = ImGui::GetContentRegionAvail().x;
    float playWidth = 100.0f;
    float statsWidth = ide.showStats ? 120.0f : 60.0f;
    float leftSpacer = (menuWidth - playWidth - statsWidth) * 0.45f;

    if (leftSpacer > 0) {
        ImGui::Dummy(ImVec2(leftSpacer, 0.0f));
        ImGui::SameLine();
    }

    // --- Bouton Play/Stop (style compact) ---
    ImGui::PushStyleColor(ImGuiCol_Button,
        ide.playing ? ImVec4{ 0.15f, 0.55f, 0.15f, 1.0f } : ImVec4{ 0.25f, 0.25f, 0.25f, 1.0f });
    if (ImGui::Button(ide.playing ? " STOP " : " PLAY ", ImVec2(playWidth, 0))) {
        ide.playing = !ide.playing;
        SDL_SetRelativeMouseMode(ide.playing ? SDL_TRUE : SDL_FALSE);

        if (ide.playing) {
            // Save the current editor camera so we can restore it on Stop
            ide.savedEditorCamera = ide.sm->currentCamera;
            // Switch to the first scene camera if one exists
            if (!ide.cameras.empty() && ide.cameras[0].runtimeCamera != nullptr) {
                ide.sm->currentCamera = ide.cameras[0].runtimeCamera;
                ide.log.push(ConsoleLog::REPLY_OK,
                    "[Play] Using scene camera '" + ide.cameras[0].name + "'");
            }
            else {
                ide.log.push(ConsoleLog::INFO,
                    "[Play] No scene camera found — using editor camera");
            }
        }
        else {
            // Restore editor camera on Stop
            if (ide.savedEditorCamera) {
                ide.sm->currentCamera = ide.savedEditorCamera;
                ide.savedEditorCamera = nullptr;
            }
            else if (ide.sm->currentCamera) {
                ide.sm->currentCamera->transform.position = Vector3(0, 5, 15);
            }
        }
        // Delegate Start()/OnDestroy() calls and play-state tracking to the
        // renderer so all game-logic lifecycle is in one place.
        ide.renderer->SetPlayMode(ide.playing);
        if (ide.playing)
            ide.log.push(ConsoleLog::REPLY_OK, "Play mode started");
        else
            ide.log.push(ConsoleLog::REPLY_OK, "Play mode stopped");
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();

    // --- Stats (display compact) ---
    if (ide.showStats && ide.statsFps > 0) {
        ImGui::TextDisabled("%d FPS | %zu obj", ide.statsFps, ide.objects.size());
    }
    else {
        ImGui::Dummy(ImVec2(statsWidth - 20, 0.0f));
    }

    // --- Unsaved indicator (position fixe à droite) ---
    float rightMargin = ImGui::GetContentRegionAvail().x - 70.0f;
    if (rightMargin > 0 && ide.sceneDirty) {
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(rightMargin, 0.0f));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{ 1.0f, 0.7f, 0.2f, 1.0f });
        ImGui::Text("*");
        ImGui::PopStyleColor();
    }

    ImGui::EndMainMenuBar();
}

// =============================================================================
//  Configuration du layout fixe (Unity-style)
// =============================================================================
static void SetupDockSpace(IDEState& ide) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceWindow", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    ide.mainDockspaceId = dockspaceId;

    // Appliquer le layout par défaut UNIQUEMENT si c'est la première fois
    static bool firstRun = true;
    if (firstRun && ImGui::DockBuilderGetNode(dockspaceId) != nullptr) {
        firstRun = false;


        // Vérifier si des fenêtres sont déjà dockées
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        if (node && node->IsLeafNode() && node->Windows.Size == 0) {
            // Aucune fenêtre dockée - configurer le layout par défaut
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

            ImGuiID dockLeftAndCenter;
            ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.22f, nullptr, &dockLeftAndCenter);

            ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockLeftAndCenter, ImGuiDir_Left, 0.25f, nullptr, &dockLeftAndCenter);
            ImGuiID dockCenter = dockLeftAndCenter;

            ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.35f, nullptr, &dockCenter);

            ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
            ImGui::DockBuilderDockWindow("Viewport", dockCenter);
            // Inspector docked first so it is the default active tab on the right
            ImGui::DockBuilderDockWindow("AI Assistant", dockRight);
            ImGui::DockBuilderDockWindow("Inspector", dockRight);
            // Asset Browser docked first so it is the default active tab at the bottom
            ImGui::DockBuilderDockWindow("Console", dockBottom);
            ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    ImGui::End();
}


// =============================================================================
//  MainScene_Run — Boucle principale corrigée
// =============================================================================
void MainScene_Run() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) { std::cerr << "SDL_Init failed\n"; return; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    // ═══════════════════════════════════════════════════════════════════════════
    //  STEP 1 — Project Launcher  (runs in its own separate window, Unity-style)
    //  This window is fully created, used, then destroyed before the main editor
    //  window is created.  The chosen project root / name flow in via the two
    //  strings below.
    // ═══════════════════════════════════════════════════════════════════════════
    {
        // ── Create a small launcher window ────────────────────────────────────
        SDL_Window* launchWin = SDL_CreateWindow(
            "HonHon Engine — Project Launcher",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            760, 560,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
        if (!launchWin) {
            std::cerr << "SDL_CreateWindow (launcher) failed\n";
            SDL_Quit();
            return;
        }
        SDL_GLContext launchCtx = SDL_GL_CreateContext(launchWin);
        SDL_GL_MakeCurrent(launchWin, launchCtx);
        SDL_GL_SetSwapInterval(1);

        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            std::cerr << "GLAD failed (launcher)\n";
            SDL_GL_DeleteContext(launchCtx);
            SDL_DestroyWindow(launchWin);
            SDL_Quit();
            return;
        }

        // ── Init ImGui for the launcher window ────────────────────────────────
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        {
            ImGuiIO& lio = ImGui::GetIO();
            lio.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            // Disable viewports / docking — launcher is a single plain window
        }
        ImGui_ImplSDL2_InitForOpenGL(launchWin, launchCtx);
        ImGui_ImplOpenGL3_Init("#version 330 core");

        // ── Load fonts for the launcher (same assets as the IDE) ─────────────
        {
            ImGuiIO& lio = ImGui::GetIO();
            const std::string jbPath = "assets/JetBrainsMono-Medium.ttf";
            const std::string faPath2 = "assets/fa-solid-900.ttf";

            std::ifstream chkJB(jbPath);
            if (chkJB.good()) {
                ImFontConfig cfg;
                cfg.FontDataOwnedByAtlas = false;
                ImFont* lf = lio.Fonts->AddFontFromFileTTF(
                    jbPath.c_str(), 16.f, &cfg,
                    lio.Fonts->GetGlyphRangesDefault());
                if (!lf) lio.Fonts->AddFontDefault();

                // Merge FontAwesome icons if available
                std::ifstream chkFA(faPath2);
                if (chkFA.good()) {
                    static const ImWchar launcherIconRanges[] = {
                        ICON_MIN_FA, ICON_MAX_FA, 0 };
                    ImFontConfig icfg;
                    icfg.MergeMode = true;
                    icfg.FontDataOwnedByAtlas = false;
                    icfg.GlyphMinAdvanceX = 16.f;
                    lio.Fonts->AddFontFromFileTTF(
                        faPath2.c_str(), 16.f, &icfg, launcherIconRanges);
                }
            }
            else {
                lio.Fonts->AddFontDefault();
            }
            lio.Fonts->Build();
        }

        // ── Run the modal picker (blocks until user chooses or closes) ────────
        HonHengine::ProjectLauncher launcher;
        bool accepted = launcher.RunModal(launchWin, launchCtx);

        // ── Tear down launcher ImGui + window ─────────────────────────────────
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_GL_DeleteContext(launchCtx);
        SDL_DestroyWindow(launchWin);

        if (!accepted) {
            // User closed the launcher — exit cleanly.
            SDL_Quit();
            return;
        }

        g_projectRoot = launcher.chosenProjectRoot;
        g_projectName = launcher.chosenProjectName;
    }
    // ── End of launcher ───────────────────────────────────────────────────────

    // ═══════════════════════════════════════════════════════════════════════════
    //  STEP 2 — Main editor window  (created fresh after launcher is gone)
    // ═══════════════════════════════════════════════════════════════════════════
    int ideW = 1600, ideH = 960;
    std::string editorTitle = "HonHon Engine IDE  —  " + g_projectName
        + "  [" + g_projectRoot + "]";
    SDL_Window* win = SDL_CreateWindow(editorTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ideW, ideH, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!win) { std::cerr << "SDL_CreateWindow failed\n"; SDL_Quit(); return; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, ctx);
    SDL_GL_SetSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { std::cerr << "GLAD failed\n"; SDL_DestroyWindow(win); SDL_Quit(); return; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    std::string fontPaths = "assets/JetBrainsMono-Medium.ttf";
    const std::string faPaths = "assets/fa-solid-900.ttf";

    std::string jetbrainsPath;
    std::ifstream f(fontPaths);
    if (f.good()) {
        jetbrainsPath = fontPaths;
    }

    std::string faPath;
    std::ifstream f2(faPaths);
    if (f2.good()) {
        faPath = faPaths;
    }

    static const ImWchar iconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };

    float defaultFontSize = 16.0f;
    float iconFontSize = 16.0f;

    ImFont* defaultFont = nullptr;

    if (!jetbrainsPath.empty()) {
        ImFontConfig fontConfig;
        fontConfig.FontDataOwnedByAtlas = false;

        defaultFont = io.Fonts->AddFontFromFileTTF(
            jetbrainsPath.c_str(),
            defaultFontSize,
            &fontConfig,
            io.Fonts->GetGlyphRangesDefault()
        );
        if (!defaultFont) {
            defaultFont = io.Fonts->AddFontDefault();
        }
    }
    else {
        defaultFont = io.Fonts->AddFontDefault();
    }

    if (!faPath.empty()) {
        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.FontDataOwnedByAtlas = false;
        iconConfig.GlyphMinAdvanceX = iconFontSize;
        io.Fonts->AddFontFromFileTTF(
            faPath.c_str(),
            iconFontSize,
            &iconConfig,
            iconRanges
        );
    }

    if (defaultFont) {
        ImFontConfig titleConfig;
        titleConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromFileTTF(
            jetbrainsPath.c_str(),
            20.0f,
            &titleConfig,
            io.Fonts->GetGlyphRangesDefault()
        );
    }

    ImGui_ImplSDL2_InitForOpenGL(win, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ApplyIDEStyle();

    Settings::canvasWidth = 900; Settings::canvasHeight = 600;
    SceneManager* sm = new SceneManager();
    Camera* camera = new Camera(Vector3(0.0, 5.0, 15.0), Quaternion::LookRotation(Vector3(0, 0, 1)));
    sm->cameras->push_back(camera);
    sm->currentCamera = camera;
    GPURenderer renderer(sm, win, ctx);

    renderer.RegisterShader("skinned", SKINNED_VERT, SKINNED_FRAG);

    // ── Default procedural skybox ─────────────────────────────────────────────
    // Gives every new scene a sky immediately; replaceble via Environment window.
    {
        Skybox* skybox = new Skybox();
        if (!skybox->GenerateProcedural()) {
            std::cerr << "[Main] Failed to generate procedural skybox, creating a simple fallback.\n";
        }        sm->SetSkybox(skybox);
        std::cout << "[Main] Default procedural skybox created\n";
    }


    BasicMovements player(sm);
    player.eyeHeight = 1.7;
    player.moveSpeed = 0.12;
    player.flySpeed = 0.3;
    player.sdlKeys = SdlBindings::WASD();

    IDEState ide;

    // ── Populate project identity from the launcher result ───────────────────
    // ide.project.rootFolder / name drive every path helper (HonAssetsPath(),
    // AssetRootDir(), etc.), so this must happen before any Load/Save/Scan call.
    {
        auto safeSet = [](char* dst, size_t dstLen, const std::string& src) {
            strncpy_s(dst, dstLen, src.c_str(), dstLen - 1);
            dst[dstLen - 1] = '\0';
            };
        safeSet(ide.project.rootFolder, sizeof(ide.project.rootFolder), g_projectRoot);
        safeSet(ide.project.name, sizeof(ide.project.name), g_projectName);

        fs::path defaultScene = fs::path(g_projectRoot) / "assets" / "scenes" / "untitled.honscene";
        safeSet(ide.sceneFilePath, sizeof(ide.sceneFilePath), defaultScene.string());

        safeSet(ide.shipSrcDir, sizeof(ide.shipSrcDir), g_projectRoot);
        fs::path distDir = fs::path(g_projectRoot) / "dist";
        safeSet(ide.shipDstDir, sizeof(ide.shipDstDir), distDir.string());

        fs::create_directories(fs::path(g_projectRoot) / "assets" / "scenes");
    }

    ide.sm = sm;
    ide.renderer = &renderer;
    ide.player = &player;
    ide.sceneFBO.init(Settings::canvasWidth, Settings::canvasHeight);
    ide.bookmarks.InitFromCurrent(camera, false, 60.f, 10.f);
    ide.window = win;

    sm->scriptManager = std::make_unique<ScriptManager>(sm);

    ScriptManager::SetToolchainPath("./tools/mingw64");
    ScriptManager::SetSDL2Path("./tools/sdl2/x64/lib/x64");
    ScriptManager::AddIncludePath("./tools/glm/include");
    ScriptManager::AddLibraryPath("./tools/glm/lib");
    ScriptManager::AddIncludePath("./tools/glad/include");
    ScriptManager::AddIncludePath("./vcpkg/installed/x64-windows/include");

    ScriptManager::AddLibraryPathAfter("./tools/glad/lib");
    ScriptManager::AddLinkLibraryAfter("glad");
    ide.scriptManager = sm->scriptManager.get();

    // Load editor settings — stored inside the project folder
    ide.editorSettings.Load(ide.EditorSettingsPath());

    static bool firstRun = true;

    // ── Project: check for recovery files on startup ──────────────────────────
    {
        auto recoveries = AutoSaveManager::ListRecovery(ide.project.rootFolder);
        if (!recoveries.empty()) {
            ide.projectUI.recoveryFiles = recoveries;
            ide.projectUI.showRecoveryToast = true;
        }
    }

    // ── Asset browser: initial scan (project-relative) ───────────────────────
    std::string assetRoot = ide.AssetRootDir();
    ide.assetBrowser.SetRootPath(assetRoot);      // <-- CRITICAL
    ide.assetBrowser.currentDir = assetRoot;
    ide.assetBrowser.dirDirty = true;

    // Load persisted asset database, then scan full asset root
    ide.assetDb.Load(ide.HonAssetsPath());
    if (fs::exists(assetRoot))
        ide.assetDb.ScanDirectory(assetRoot);

    // Package manager setup (unchanged)
    {
        PackageRecord core;
        core.id = "honhon.core"; core.displayName = "HonHon Core";
        core.description = "Core engine runtime (always present).";
        core.type = PackageType::Library; core.isBuiltIn = true;
        core.version = { 1, 0, 0 }; core.installedVersion = { 1, 0, 0 };
        core.status = PackageStatus::UpToDate;
        ide.packageMgr.packages.push_back(core);

        PackageRecord physics;
        physics.id = "honhon.physics"; physics.displayName = "Physics Plugin";
        physics.description = "Rigid body simulation and collision detection.";
        physics.type = PackageType::PhysicsPlugin;
        physics.version = { 0, 9, 2 };
        physics.status = PackageStatus::Available;
        ide.packageMgr.packages.push_back(physics);

        PackageRecord audio;
        audio.id = "honhon.audio"; audio.displayName = "Audio Plugin";
        audio.description = "3D positional audio and streaming support.";
        audio.type = PackageType::AudioPlugin;
        audio.version = { 0, 7, 0 };
        audio.status = PackageStatus::Available;
        ide.packageMgr.packages.push_back(audio);

        ide.packageMgr.Load(".honpackages");
    }

    CommandRegistry reg;
    BuildCommands(reg, &renderer, &ide.assetDb, sm);
    CmdContext cmdCtx{ sm, &renderer, &g_sceneMutex };
    std::atomic<bool> running{ true };
    std::thread shellThr(ShellThread, &ide.bus, &reg, &cmdCtx, &running, &ide.log);
    ide.bus.send("list");
    // Avant d'entrer dans la boucle, on s'assure que g_deferredLog pointe vers le log de l'IDE
    g_deferredLog = &ide.log;

    using Clock = std::chrono::high_resolution_clock;
    auto tPrev = Clock::now();
    bool quit = false;
    while (!quit) {
        auto tNow = Clock::now();
        float dt = std::chrono::duration<float>(tNow - tPrev).count();
        tPrev = tNow;
        if (dt > 0.1f) dt = 0.1f;

        // --- Exécuter les tâches GLTF différées --------------------------------
        {
            std::vector<DeferredTask> tasksToProcess;
            {
                std::lock_guard<std::mutex> lk(g_deferredTasksMutex);
                tasksToProcess.swap(g_deferredGLTFTasks); // vide la file localement
            }
            for (const auto& task : tasksToProcess) {
                // L'import s'exécute maintenant sur le thread principal, donc avec un contexte GL valide.
                auto result = Importer::ImportFromGLTF<TextureManager>(task.path, &ide.renderer->texManager);
                if (!result.ok) {
                    ide.log.push(ConsoleLog::REPLY_ERR, "[GLTF] " + task.name + " failed: " + result.error);
                    continue;
                }
                BaseObject* obj = result.object;
                if (!obj) {
                    ide.log.push(ConsoleLog::REPLY_ERR, "[GLTF] " + task.name + " returned null");
                    continue;
                }
                obj->transform.position = Vector3(task.x, task.y, task.z);
                obj->tag = "GLTF:" + task.path;
                {
                    std::unique_lock<std::shared_mutex> lock(g_sceneMutex);
                    ide.sm->objects->push_back(obj);
                    g_namedObjects[task.name] = obj;
                }
                ide.log.push(ConsoleLog::REPLY_OK, "[GLTF] " + task.name + " imported successfully");
                ide.sceneDirty = true;
                ide.pendingSelection = task.name; // on veut sélectionner le nouvel objet
                RefreshSceneList(ide);            // demande une mise à jour de la hiérarchie
            }
        }
        // -----------------------------------------------------------------------

        // --- Process deferred HDR skybox loads ---------------------------------
        {
            std::vector<DeferredHDRTask> tasks;
            {
                std::lock_guard<std::mutex> lock(g_deferredHDRMutex);
                tasks.swap(g_deferredHDRTasks);
            }
            for (const auto& task : tasks) {
                Skybox* newSky = new Skybox();
                if (newSky->LoadFromHDR(task.path)) {
                    delete ide.sm->currentSkybox;
                    ide.sm->SetSkybox(newSky);
                    ide.log.push(ConsoleLog::REPLY_OK, "[HDR] Skybox loaded from: " + task.path);
                }
                else {
                    delete newSky;
                    ide.log.push(ConsoleLog::REPLY_ERR, "[HDR] Failed to load: " + task.path);
                }
            }
        }
        // -----------------------------------------------------------------------

        // --- Process deferred shader registrations -----------------------------
        {
            std::vector<DeferredShaderTask> tasks;
            {
                std::lock_guard<std::mutex> lock(g_deferredShaderMutex);
                tasks.swap(g_deferredShaderTasks);
            }
            for (const auto& task : tasks) {
                ide.renderer->RegisterShader(task.name, task.vertSrc.c_str(), task.fragSrc.c_str());
                ide.log.push(ConsoleLog::REPLY_OK, "[Shader] Registered: " + task.name);
            }
        }
        // -----------------------------------------------------------------------
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) ide.quitRequested = true;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE && ide.playing) {
                ide.playing = false;
                SDL_SetRelativeMouseMode(SDL_FALSE);
                ide.renderer->SetPlayMode(false);
                continue;   // Skip ImGui processing for this key
            }
            ImGui_ImplSDL2_ProcessEvent(&ev);
        }

        // Process keyboard shortcuts after ImGui has captured its own keys
        HandleShortcuts(ide);
        if (ide.quitRequested && !ide.showQuitModal) {
            if (ide.sceneDirty) {
                ide.showQuitModal = true;
            }
            else {
                quit = true;   // no unsaved changes, exit immediately
            }
        }

        UpdateFPSCamera(ide, dt);

        if (ide.playing) {
            int mx, my; SDL_GetRelativeMouseState(&mx, &my);
            if (mx || my) player.TickMouse(-mx, -my);
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            player.TickKeys(ks, dt);

            // ── Physics step ─────────────────────────────────────────────────
            {
                float fixedDt = ide.project.physics.fixedTimestep > 0.f
                    ? ide.project.physics.fixedTimestep : 0.02f;
                static float physAccum = 0.f;
                physAccum += dt;
                while (physAccum >= fixedDt) {
                    ide.physicsWorld.Step(fixedDt);
                    physAccum -= fixedDt;
                }
            }

            // ── Collision trigger polling ─────────────────────────────────────
            {
                std::vector<BaseObject*> candidates;
                {
                    std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
                    candidates.reserve(g_namedObjects.size());
                    for (auto& [n, o] : g_namedObjects) candidates.push_back(o);
                }
                for (auto& [obj, trigger] : ide.collisionTriggers) {
                    if (trigger.enabled) trigger.Poll(candidates);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(ide.log.mtx);
            size_t total = ide.log.entries.size();
            if (ide.logReadIdx > total) ide.logReadIdx = total;
            for (size_t ei = ide.logReadIdx; ei < total; ++ei) {
                auto& e = ide.log.entries[ei];
                if (e.kind == ConsoleLog::REPLY_OK) {
                    std::string c = JsonGet(e.text, "cmd");
                    if (c == "list") {
                        // Snapshot object names before parse
                        std::vector<std::string> prevNames;
                        for (auto& o : ide.objects) prevNames.push_back(o.name);

                        ParseListReply(e.text, ide);

                        // ── Purge orphaned physics components ────────────────
                        // After a list refresh, any BaseObject* that no longer
                        // exists in g_namedObjects must be removed from the
                        // physics maps and unregistered from the world.
                        {
                            std::shared_lock<std::shared_mutex> lk(g_sceneMutex);
                            for (auto it = ide.rigidBodies.begin(); it != ide.rigidBodies.end(); ) {
                                bool found = false;
                                for (auto& [n, o] : g_namedObjects) if (o == it->first) { found = true; break; }
                                if (!found) {
                                    ide.physicsWorld.Unregister(&it->second);
                                    it = ide.rigidBodies.erase(it);
                                }
                                else { ++it; }
                            }
                            for (auto it = ide.collisionTriggers.begin(); it != ide.collisionTriggers.end(); ) {
                                bool found = false;
                                for (auto& [n, o] : g_namedObjects) if (o == it->first) { found = true; break; }
                                if (!found) it = ide.collisionTriggers.erase(it);
                                else        ++it;
                            }
                        }

                        // ── Sub-object population ────────────────────────────
                        // If a model was just dropped, find newly-added objects
                        // and register them as sub-objects on the asset record.
                        if (ide.assetBrowser.hasPendingDrop) {
                            ide.assetBrowser.hasPendingDrop = false;
                            std::string dropPath(ide.assetBrowser.pendingDrop.path);
                            AssetRecord* rec = ide.assetDb.FindByPath(dropPath);
                            if (rec) {
                                // Collect names that are new since last list
                                std::unordered_set<std::string> prevSet(prevNames.begin(), prevNames.end());
                                for (auto& obj : ide.objects) {
                                    if (!prevSet.count(obj.name)) {
                                        // Check it isn't already a sub-object
                                        bool already = false;
                                        for (auto& so : rec->subObjects)
                                            if (so.name == obj.name) { already = true; break; }
                                        if (!already) {
                                            AssetSubObject sub;
                                            sub.name = obj.name;
                                            sub.index = (int)rec->subObjects.size();
                                            sub.type = AssetType::Model;
                                            sub.enabled = true;
                                            rec->subObjects.push_back(sub);
                                        }
                                    }
                                }
                                ide.assetDb.Save(ide.HonAssetsPath());
                            }
                        }
                    }
                    if (c == "inspect") ParseInspectReply(e.text, ide);
                }
            }
            ide.logReadIdx = total;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        SetupDockSpace(ide);

        // Draw welcome modal on first run
        DrawWelcomeModal(ide, firstRun);


        if (ide.focus.active) {
            ide.focus.Update(dt, ide.sm->currentCamera, ide.editorFov, ide.viewportOrtho, ide.orthoSize);
        }

        ImGui::Begin("Viewport");

        // ── Prefab Edit Mode banner (Unity-style) ──────────────────────────
        // Drawn BEFORE DrawViewportPanel so it consumes cursor space correctly,
        // and the viewport image + gizmo coordinate space is fully self-consistent.
        float prefabBannerH = 0.f;
        if (ide.prefabEditMode) {
            ImVec2 bannerStart = ImGui::GetCursorScreenPos();
            // Bright cyan banner at the top of the viewport window
            ImGui::PushStyleColor(ImGuiCol_ChildBg, { 0.05f, 0.22f, 0.28f, 1.f });
            ImGui::PushStyleColor(ImGuiCol_Text, { 0.3f,  1.0f,  0.9f,  1.f });
            ImGui::BeginChild("##prefab_banner", { 0.f, 34.f }, false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.f);
            ImGui::Text("  \xef\x86\xb2  Prefab Edit Mode  —  %s", ide.prefabEditName.c_str());
            if (ide.prefabEditDirty) { ImGui::SameLine(); ImGui::TextDisabled("(unsaved)"); }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200.f);
            ImGui::PushStyleColor(ImGuiCol_Button, { 0.1f, 0.55f, 0.45f, 1.f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.15f, 0.7f, 0.6f, 1.f });
            if (ImGui::SmallButton("  Save & Exit  "))
                SaveAndExitPrefabEditMode(ide);
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, { 0.35f, 0.15f, 0.15f, 1.f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0.55f, 0.2f,  0.2f,  1.f });
            if (ImGui::SmallButton("  Discard  "))
                ExitPrefabEditModeDiscard(ide);
            ImGui::PopStyleColor(2);
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            // Thin separator line
            ImDrawList* bdl = ImGui::GetWindowDrawList();
            ImVec2 sepStart = ImGui::GetCursorScreenPos();
            bdl->AddLine({ sepStart.x, sepStart.y },
                { sepStart.x + ImGui::GetContentRegionAvail().x, sepStart.y },
                IM_COL32(50, 200, 180, 120), 1.f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.f);
            // Track how many pixels were consumed so DrawViewportPanel can account for it
            prefabBannerH = ImGui::GetCursorScreenPos().y - bannerStart.y;
        }

        DrawViewportPanel(ide, dt, prefabBannerH);
        ImGui::End();

        ImGui::Begin("Hierarchy");
        DrawHierarchyPanel(ide);
        ImGui::End();
        ImGui::Begin("Inspector");
        // Record Inspector window pos/size so the overlay can anchor above it
        ide.importOverlayAnchorPos = ImGui::GetWindowPos();
        ide.importOverlayAnchorSize = ImGui::GetWindowSize();
        DrawInspectorPanel(ide);
        ImGui::End();
        ImGui::Begin("Console");
        DrawConsolePanel(ide);
        ImGui::End();
        ImGui::Begin("Asset Browser");
        DrawAssetBrowserPanel(
            ide.assetBrowser,
            ide.assetDb,
            dt,
            [&](const std::string& path, AssetType t) {
                std::string ext = fs::path(path).extension().string();
                std::string stem = fs::path(path).stem().string();

                std::string objName = stem;
                {
                    int n = 1;
                    while (g_namedObjects.count(objName) || g_namedLights.count(objName))
                        objName = stem + "_" + std::to_string(n++);
                }

                AssetRecord* rec = ide.assetDb.FindByPath(path);
                float importScale = 1.0f;
                if (rec && rec->type == AssetType::Model)
                    importScale = rec->modelSettings.importScale;

                std::string cmd;
                glm::vec3 dropPos = GetCameraSpawnPos(ide);
                char posBuf[64];
                std::snprintf(posBuf, sizeof(posBuf), "%.3f %.3f %.3f", dropPos.x, dropPos.y, dropPos.z);
                if (ext == ".obj")
                    cmd = "obj " + objName + " \"" + path + "\" " + posBuf;
                else if (ext == ".gltf" || ext == ".glb")
                    cmd = "gltf " + objName + " \"" + path + "\" " + posBuf;

                // ── Prefab: double-click enters Prefab Edit Mode (Unity-style) ─
                if (ext == ".honprefab") {
                    if (ide.prefabEditMode) {
                        // Already editing a prefab — save the current one first
                        if (ide.prefabEditPath != path) {
                            ide.toastMgr.Push("Save or discard current prefab first.", Toast::Warning, 2.f);
                        }
                    }
                    else {
                        EnterPrefabEditMode(ide, path);
                    }
                    return;
                }

                // ── Prefab: explicit "Instantiate in Scene" from context menu ───
                // The asset browser sends path+"?instantiate" to signal this intent.
                if (path.size() > 12 && path.substr(path.size() - 12) == "?instantiate") {
                    std::string realPath = path.substr(0, path.size() - 12);
                    std::string first = InstantiatePrefab(ide, realPath);
                    if (!first.empty()) ide.pendingSelection = first;
                    return;
                }

                if (!cmd.empty()) {
                    ide.log.push(ConsoleLog::CMD, "> " + cmd);
                    ide.bus.send(cmd);
                    if (importScale != 1.0f) {
                        char scaleBuf[128];
                        std::snprintf(scaleBuf, sizeof(scaleBuf),
                            "scale %s %.4f %.4f %.4f",
                            objName.c_str(), importScale, importScale, importScale);
                        ide.bus.send(scaleBuf);
                    }
                    ide.pendingSelection = objName;
                    ide.sceneDirty = true;
                    if (!rec) rec = &ide.assetDb.Register(path);
                    ide.assetBrowser.hasPendingDrop = true;
                    AssetDragPayload fake{};
                    strncpy_s(fake.path, sizeof(fake.path), path.c_str(), _TRUNCATE);
                    fake.type = t;
                    ide.assetBrowser.pendingDrop = fake;
                    RefreshSceneList(ide);
                }
            },
            [&](const std::string& scenePath) {
                if (ide.sceneDirty) {
                    ide.pendingSceneOpenPath = scenePath;
                    ide.showSceneOpenPrompt = true;
                }
                else {
                    ide.bus.send("clearscene");
                    ide.bus.send("loadscene " + scenePath);
                    strncpy_s(ide.sceneFilePath, sizeof(ide.sceneFilePath),
                        scenePath.c_str(), sizeof(ide.sceneFilePath) - 1);
                    ide.sceneDirty = false;
                    ide.selection.Clear();
                    RefreshSceneList(ide);
                    ide.log.push(ConsoleLog::REPLY_OK, "[Scene] Loaded: " + scenePath);
                }
            });
        ImGui::End();

        // ── Open import overlay whenever the browser focuses a new asset ──────
        // Guard: don't overwrite an overlay that was just opened by a drag-drop
        // with a pending import callback — that would silently discard the action.
        if (!ide.assetBrowser.focusedGUID.empty() &&
            ide.assetBrowser.focusedGUID != ide.importOverlayGUID &&
            !(ide.showImportOverlay && ide.importOverlayHasImportBtn))
        {
            AssetRecord* focusedRec = ide.assetDb.FindByGUID(ide.assetBrowser.focusedGUID);
            if (focusedRec) {
                ide.importOverlayGUID = ide.assetBrowser.focusedGUID;
                ide.showImportOverlay = true;
                ide.importOverlayHasImportBtn = false;
                ide.importOverlayOnImport = nullptr;
            }
        }

        // ── Draw the floating Import Settings Overlay ─────────────────────────
        {
            AssetRecord* ovRec = ide.importOverlayGUID.empty()
                ? nullptr
                : ide.assetDb.FindByGUID(ide.importOverlayGUID);
            std::function<void()> importCb = ide.importOverlayHasImportBtn
                ? ide.importOverlayOnImport
                : std::function<void()>(nullptr);
            DrawImportSettingsOverlay(
                ovRec,
                ide.showImportOverlay,
                ide.importOverlayAnchorPos,
                ide.importOverlayAnchorSize,
                importCb);
        }

        // ── Project / Package manager windows (floating, non-docked) ─────────
        DrawProjectSettingsWindow(ide.projectUI, ide.project);
        DrawPackageManagerWindow(ide.packageUI, ide.packageMgr);

        // ── Project modals ────────────────────────────────────────────────────
        DrawNewProjectModal(ide.projectUI, ide.project);
        DrawProjectSaveLoadModals(ide.projectUI, ide.project);

        // ── Auto-save recovery toast ──────────────────────────────────────────
        std::string recovered = DrawRecoveryToast(ide.projectUI, dt);
        if (!recovered.empty()) {
            ide.bus.send("clearscene");
            ide.bus.send("loadscene " + recovered);
            ide.sceneDirty = false;
            RefreshSceneList(ide);
        }

        // ── Environment / Skybox window ───────────────────────────────────────
        if (ide.showEnvironmentWindow) {
            ImGui::SetNextWindowSize(ImVec2(480, 340), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("  Environment##skybox", &ide.showEnvironmentWindow)) {

                ImGui::SeparatorText("Skybox");

                static const char* kSkyboxModes[] = { "Procedural (default)", "Cubemap from files" };
                ImGui::Combo("Mode", &ide.skyboxMode, kSkyboxModes, 2);

                ImGui::Spacing();

                if (ide.skyboxMode == 0) {
                    // Procedural — just a button to (re)generate
                    ImGui::TextDisabled("A gradient sky is generated automatically.");
                    ImGui::Spacing();
                    if (ImGui::Button("Reset to Default Procedural", ImVec2(-1, 0))) {
                        if (ide.sm->currentSkybox) {
                            delete ide.sm->currentSkybox;
                            ide.sm->currentSkybox = nullptr;
                        }
                        Skybox* sky = new Skybox();
                        sky->GenerateProcedural();
                        ide.sm->SetSkybox(sky);
                        ide.toastMgr.Push("Procedural skybox reset", Toast::Success, 2.f);
                    }
                }
                else {
                    // Cubemap from files
                    static const char* kFaceLabels[] = {
                        "Right (+X)", "Left  (-X)", "Top   (+Y)",
                        "Bottom(-Y)", "Front (+Z)", "Back  (-Z)"
                    };
                    ImGui::TextDisabled("Provide 6 face images (PNG/JPG).");
                    ImGui::Spacing();
                    for (int i = 0; i < 6; ++i) {
                        ImGui::PushID(i);
                        ImGui::Text("%-12s", kFaceLabels[i]);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##face", ide.skyboxFaces[i], sizeof(ide.skyboxFaces[i]));
                        // Accept drag-drop from asset browser
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                                const char* droppedPath = static_cast<const char*>(pl->Data);
                                memcpy(ide.skyboxFaces[i], droppedPath, sizeof(ide.skyboxFaces[i]) - 1);
                                ide.skyboxFaces[i][sizeof(ide.skyboxFaces[i]) - 1] = '\0';
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::PopID();
                    }
                    ImGui::Spacing();
                    if (ImGui::Button("Load Cubemap", ImVec2(-1, 0))) {
                        std::vector<std::string> faces;
                        bool allFilled = true;
                        for (int i = 0; i < 6; ++i) {
                            if (ide.skyboxFaces[i][0] == '�') { allFilled = false; break; }
                            faces.push_back(ide.skyboxFaces[i]);
                        }
                        if (!allFilled) {
                            ide.toastMgr.Push("Fill in all 6 face paths first", Toast::Warning, 3.f);
                        }
                        else {
                            Skybox* sky = new Skybox();
                            if (sky->LoadFromFiles(faces)) {
                                if (ide.sm->currentSkybox) delete ide.sm->currentSkybox;
                                ide.sm->SetSkybox(sky);
                                ide.toastMgr.Push("Cubemap skybox loaded!", Toast::Success, 2.f);
                            }
                            else {
                                delete sky;
                                ide.toastMgr.Push("Failed to load cubemap — check paths", Toast::Error, 4.f);
                            }
                        }
                    }
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Sun Glow");
                ImGui::TextDisabled("Sun direction is driven by your Directional Light.");
                if (ide.sm->currentSkybox) {
                    // Show live sun override controls
                    static float sunColor[3] = { 1.f, 0.95f, 0.8f };
                    static float sunIntensity = 0.8f;
                    if (ImGui::ColorEdit3("Sun Color Override", sunColor)) {
                        ide.sm->currentSkybox->SetSunColor(glm::vec3(sunColor[0], sunColor[1], sunColor[2]));
                    }
                    if (ImGui::SliderFloat("Sun Intensity Override", &sunIntensity, 0.f, 5.f)) {
                        ide.sm->currentSkybox->SetSunIntensity(sunIntensity);
                    }
                    ImGui::TextDisabled("(These are overridden each frame if a Directional Light exists.)");
                }
            }
            ImGui::End();
        }

        DrawMenuBar(ide);
        DrawModals(ide, quit);
        DrawScriptWizardModal(ide);
        DrawToolchainWindow(ide);
        DrawProfilerWindow(ide, dt);
        DrawMemoryWindow(ide);
        DrawGlobalSearch(ide.globalSearch, ide,
            [&](const std::string& name) {
                ide.selection.SetSingle(name);
                ide.bus.send("inspect " + name);
            },
            [&](const std::string& guid) {
                ide.assetBrowser.focusedGUID = guid;
                ide.assetBrowser.selectedGUIDs = { guid };
            });

        ide.aiAssistant.Update(&ide.bus);
        if (ide.showAIChat) {
            ImGui::Begin(AIAssistant::GetWindowName(), &ide.showAIChat);
            ide.aiAssistant.DrawInspectorTab();
            ImGui::End();
        }

        // Toast notifications at end of frame
        ide.toastMgr.DrawToasts(dt);

        // Progress bar
        ide.progressState.Draw(dt);

        ImGui::Render();
        glViewport(0, 0, ideW, ideH);
        glClearColor(0.07f, 0.075f, 0.08f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        ide.renderer->GetUIRenderer().flush(0);

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window* bkWin = SDL_GL_GetCurrentWindow();
            SDL_GLContext bkCtx = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(bkWin, bkCtx);
        }
        SDL_GL_SwapWindow(win);

        static float refreshTimer = 0.f;
        refreshTimer += dt;
        if (refreshTimer > 2.0f) { ide.bus.send("list"); refreshTimer = 0.f; }

        // ── Deleted-asset scene notification ──────────────────────────────────
        // If any DB record's file was deleted from disk (e.g. via bulk delete in
        // the browser), remove scene objects whose names match sub-objects of that
        // asset and unregister the record.
        {
            static float deleteScanTimer = 0.f;
            deleteScanTimer += dt;
            if (deleteScanTimer >= 1.5f) {
                deleteScanTimer = 0.f;
                std::vector<std::string> toUnregister;
                for (auto& [gstr, rec] : ide.assetDb.records) {
                    // Skip if file still exists
                    if (fs::exists(fs::path(rec.path))) continue;
                    // File gone — remove any sub-objects from scene
                    for (auto& so : rec.subObjects) {
                        bool inScene = false;
                        for (auto& obj : ide.objects)
                            if (obj.name == so.name) { inScene = true; break; }
                        if (inScene) {
                            ide.bus.send("delete " + so.name);
                            if (ide.selection.Contains(so.name))
                                ide.selection.Remove(so.name);
                            ide.sceneDirty = true;
                        }
                    }
                    toUnregister.push_back(gstr);
                }
                if (!toUnregister.empty()) {
                    for (auto& g : toUnregister)
                        ide.assetDb.Unregister(g);
                    ide.assetDb.Save(ide.HonAssetsPath());
                    RefreshSceneList(ide);
                }
            }
        }

        // ── Auto-save tick ────────────────────────────────────────────────────
        ide.autoSave.Tick(dt, ide.project, ide.sceneDirty, ide.sceneFilePath,
            [&](const std::string& slot) -> bool {
                ide.bus.send("savescene " + slot);
                return true;
            });

        // ── Script file watcher (polls every 3 s, same cadence as asset rescan)
        {
            static float scriptWatchTimer = 0.f;
            scriptWatchTimer += dt;
            if (scriptWatchTimer >= 3.0f) {
                scriptWatchTimer = 0.f;
                for (auto& [gstr, rec] : ide.assetDb.records) {
                    if (rec.type != AssetType::Script) continue;
                    if (!rec.scriptSettings.autoRecompile) continue;
                    if (rec.scriptSettings.treatAsHeader) continue;
                    try {
                        auto mtime = fs::last_write_time(fs::path(rec.path));
                        // Store mtime as a 64-bit count in fileSize temporarily
                        // (we use a parallel static map so we don't corrupt fileSize)
                        static std::unordered_map<std::string, fs::file_time_type> s_mtimes;
                        auto it = s_mtimes.find(gstr);
                        if (it == s_mtimes.end()) {
                            s_mtimes[gstr] = mtime; // first observation
                        }
                        else if (mtime != it->second) {
                            it->second = mtime;
                            // File changed — hot reload via ScriptManager
                            if (ide.scriptManager) {
                                ide.scriptManager->HotReloadScript(gstr);
                                ide.log.push(ConsoleLog::INFO,
                                    "[Script] Hot reload triggered for: " + rec.path);
                            }
                            else {
                                // Fallback: plain recompile
                                std::string compileCmd = rec.scriptSettings.compileCommand;
                                if (compileCmd.empty())
                                    compileCmd = "g++ -std=c++20 -c \"" + rec.path + "\" 2>&1";
                                ide.log.push(ConsoleLog::INFO,
                                    "[Script] File changed, recompiling: " + rec.path);
                                ConsoleLog* logPtr = &ide.log;
                                std::string cmdCopy = compileCmd;
                                std::string pathCopy = rec.path;
                                std::thread([cmdCopy, pathCopy, logPtr] {
                                    int ret = std::system(cmdCopy.c_str());
                                    logPtr->push(
                                        ret == 0 ? ConsoleLog::REPLY_OK : ConsoleLog::REPLY_ERR,
                                        "[Script] " + pathCopy
                                        + (ret == 0 ? " compiled OK" : " compile FAILED"));
                                    }).detach();
                            }
                        }
                    }
                    catch (...) {}
                }
            }
        }
    }

    running = false;
    ide.bus.alive = false;
    if (shellThr.joinable()) shellThr.join();
    ide.sceneFBO.destroy();
    ide.packageMgr.Save(".honpackages");
    ide.assetDb.Save(ide.HonAssetsPath());
    ide.editorSettings.Save(ide.EditorSettingsPath());  // Save editor settings
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    renderer.cleanup();
    delete sm;
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int main(int argc, char* argv[]) {
    SDL_SetMainReady();
    MainScene_Run();
    return 0;
}