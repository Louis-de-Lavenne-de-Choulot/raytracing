#pragma once
// ide_layout.h  —  HonHon Engine IDE  —  Dock layout persistence
// =============================================================================

#include <string>
#include <imgui.h>
#include <imgui_internal.h>

// Sauvegarde complète de la configuration ImGui (docks, positions, tailles, couleurs)
inline bool SaveDockLayout(const std::string& path) {
    ImGui::SaveIniSettingsToDisk(path.c_str());
    return true;
}

// Charge la configuration ImGui depuis un fichier
inline bool LoadDockLayout(const std::string& path) {
    ImGui::LoadIniSettingsFromDisk(path.c_str());
    return true;
}

// Pour la compatibilité avec le code existant qui utilise SaveDockLayoutSimple
inline bool SaveDockLayoutSimple(const std::string& path) {
    return SaveDockLayout(path);
}

inline bool LoadDockLayoutSimple(const std::string& path) {
    return LoadDockLayout(path);
}