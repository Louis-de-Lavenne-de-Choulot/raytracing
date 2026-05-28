#pragma once
// scene_io.h — Shared scene serialisation / deserialisation for HonHon Engine.
// =============================================================================
// Both the editor command bus (main.cpp) and the shipping pipeline (ide_ship.cpp)
// need to read and write .honscene files.  This header exposes two plain
// functions that operate on the engine's global scene state so that neither
// translation unit has to duplicate the logic.
//
// Thread-safety contract
// ──────────────────────
// SceneSave  acquires a shared (read) lock on g_sceneMutex internally.
// SceneLoad  acquires an exclusive (write) lock per object / light inserted.
// The caller must NOT already hold g_sceneMutex when invoking either function.
// =============================================================================

#include <string>
#include <shared_mutex>
#include "scenemanager.h"
#include "ide_asset_database.h"


namespace HonHengine{
// ---------------------------------------------------------------------------
// SceneSaveResult / SceneLoadResult
// ---------------------------------------------------------------------------
struct SceneSaveResult {
    bool        ok = false;
    std::string errorMsg;          // non-empty when ok == false
};

struct SceneLoadResult {
    bool        ok = false;
    int         objCount = 0;
    int         lightCount = 0;
    std::string warnings;          // non-fatal issues (e.g. failed OBJ import)
    std::string errorMsg;          // fatal error (file not found, etc.)
};

// ---------------------------------------------------------------------------
// SceneSave
// ---------------------------------------------------------------------------
// Serialise g_namedObjects + g_namedLights to a .honscene JSON file at
// `filePath`.  Creates parent directories if needed.
//
// Returns SceneSaveResult::ok == true on success.
// ---------------------------------------------------------------------------
SceneSaveResult SceneSave(const std::string& filePath);

// ---------------------------------------------------------------------------
// SceneLoad
// ---------------------------------------------------------------------------
// Parse a .honscene JSON file and push objects / lights into the engine's
// global scene state (g_namedObjects, g_namedLights, sm.objects, sm.lights).
//
// The caller is responsible for clearing the existing scene first
// (e.g. via the "clearscene" command) if a fresh load is desired.
//
// GLTF meshes are deferred to the main GL thread via g_deferredGLTFTasks —
// the returned counts reflect objects queued, not yet rendered.
//
// Parameters:
//   filePath  — path to the .honscene file.
//   sm        — live SceneManager; objects / lights are appended to its lists.
//   assetDb   — optional; used to resolve script GUIDs to source paths.
//   sceneMgr  — optional; used to hot-load script components at load time.
// ---------------------------------------------------------------------------
SceneLoadResult SceneLoad(const std::string& filePath,
    SceneManager* sm,
    AssetDatabase* assetDb = nullptr,
    SceneManager* sceneMgr = nullptr);
}