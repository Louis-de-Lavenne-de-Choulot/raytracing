#pragma once
// ide_selection.h  —  HonHon Engine IDE  —  Sélection multiple & raycasting
// =============================================================================
// Gère :
//   - Le conteneur de sélection multiple (std::set<std::string>)
//   - Le raycasting souris contre des bounding boxes AABB (from IDEObject positions)
//   - La box-sélection dans le plan écran
//   - Les helpers de sélection (toggle, clear, contains)
// =============================================================================

#include <string>
#include <set>
#include <vector>
#include <optional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  MultiSelection — remplace std::string selectedObject
// ─────────────────────────────────────────────────────────────────────────────
struct MultiSelection {
    std::set<std::string> items;

    bool Contains(const std::string& name) const { return items.count(name) > 0; }
    bool Empty() const { return items.empty(); }
    size_t Size() const { return items.size(); }

    // Sélection unique (remplace selectedObject = name)
    void SetSingle(const std::string& name) {
        items.clear();
        if (!name.empty()) items.insert(name);
    }

    // Bascule un élément (Ctrl+clic)
    void Toggle(const std::string& name) {
        if (items.count(name)) items.erase(name);
        else items.insert(name);
    }

    // Ajoute sans effacer
    void Add(const std::string& name) {
        if (!name.empty()) items.insert(name);
    }

    void Remove(const std::string& name) { items.erase(name); }
    void Clear() { items.clear(); }

    // Retourne le premier élément (compatibilité avec le code legacy selectedObject)
    std::string Primary() const {
        if (items.empty()) return "";
        return *items.begin();
    }

    // Compatibilité: assigner une string directement
    MultiSelection& operator=(const std::string& name) {
        SetSingle(name);
        return *this;
    }

    bool operator==(const std::string& name) const {
        return items.size() == 1 && *items.begin() == name;
    }
    bool operator!=(const std::string& name) const { return !(*this == name); }
};

// ─────────────────────────────────────────────────────────────────────────────
//  AABB Raycast
// ─────────────────────────────────────────────────────────────────────────────

struct RaycastCandidate {
    std::string name;
    glm::vec3   center;    // position monde
    glm::vec3   halfSize;  // demi-taille AABB (sx/2, sy/2, sz/2)
};

// Intersecte rayon vs AABB, retourne la distance (négatif = pas d'intersection)
static float RayAABB(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const glm::vec3& center, const glm::vec3& half)
{
    glm::vec3 mn = center - half;
    glm::vec3 mx = center + half;
    float tmin = -1e9f, tmax = 1e9f;
    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-7f) {
            if (rayOrigin[i] < mn[i] || rayOrigin[i] > mx[i]) return -1.f;
        }
        else {
            float t1 = (mn[i] - rayOrigin[i]) / rayDir[i];
            float t2 = (mx[i] - rayOrigin[i]) / rayDir[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = (glm::max)(tmin, t1);
            tmax = (glm::min)(tmax, t2);
            if (tmin > tmax) return -1.f;
        }
    }
    return tmin > 0.f ? tmin : (tmax > 0.f ? 0.f : -1.f);
}

// Construit la direction du rayon depuis les coordonnées souris normalisées [-1,1]
static glm::vec3 UnprojectRayDir(float ndcX, float ndcY,
    const glm::mat4& proj, const glm::mat4& view)
{
    glm::vec4 clipCoord(ndcX, ndcY, -1.f, 1.f);
    glm::vec4 eyeCoord = glm::inverse(proj) * clipCoord;
    eyeCoord.z = -1.f; eyeCoord.w = 0.f;
    glm::vec4 worldCoord = glm::inverse(view) * eyeCoord;
    return glm::normalize(glm::vec3(worldCoord));
}

// Effectue le raycasting et retourne le nom de l'objet touché, ou "" si rien
static std::string RaycastObjects(
    float mouseX, float mouseY,              // position souris dans le viewport (pixels)
    float vpX, float vpY,                    // coin haut-gauche du viewport
    float vpW, float vpH,                    // taille du viewport
    const glm::mat4& projMatrix,
    const glm::mat4& viewMatrix,
    const glm::vec3& cameraPos,
    const std::vector<RaycastCandidate>& candidates)
{
    // Coordonnées NDC
    float ndcX = ((mouseX - vpX) / vpW) * 2.f - 1.f;
    float ndcY = 1.f - ((mouseY - vpY) / vpH) * 2.f;

    glm::vec3 rayDir = UnprojectRayDir(ndcX, ndcY, projMatrix, viewMatrix);

    float   bestDist = 1e9f;
    std::string bestName;

    for (auto& c : candidates) {
        float t = RayAABB(cameraPos, rayDir, c.center, c.halfSize);
        if (t >= 0.f && t < bestDist) {
            bestDist = t;
            bestName = c.name;
        }
    }
    return bestName;
}

// ─────────────────────────────────────────────────────────────────────────────
//  BoxSelectionState — rectangle de sélection à la souris
// ─────────────────────────────────────────────────────────────────────────────
struct BoxSelectionState {
    bool    active = false;
    ImVec2  startPos;      // coin de départ (screen space)
    ImVec2  currentPos;    // position courante

    void Begin(ImVec2 pos) { active = true; startPos = pos; currentPos = pos; }
    void Update(ImVec2 pos) { currentPos = pos; }
    void End() { active = false; }

    // Retourne le rectangle normalisé (min/max)
    std::pair<ImVec2, ImVec2> GetRect() const {
        ImVec2 mn{ (glm::min)(startPos.x, currentPos.x), (glm::min)(startPos.y, currentPos.y) };
        ImVec2 mx{ (glm::max)(startPos.x, currentPos.x), (glm::max)(startPos.y, currentPos.y) };
        return { mn, mx };
    }

    // Dessine le rectangle de sélection
    void Draw(ImDrawList* dl) const {
        if (!active) return;
        auto [mn, mx] = GetRect();
        dl->AddRectFilled(mn, mx, IM_COL32(100, 160, 240, 40));
        dl->AddRect(mn, mx, IM_COL32(120, 180, 255, 200), 0.f, 0, 1.5f);
    }

    // Teste si une position écran est dans le rectangle
    bool ContainsPoint(ImVec2 pt) const {
        auto [mn, mx] = GetRect();
        return pt.x >= mn.x && pt.x <= mx.x && pt.y >= mn.y && pt.y <= mx.y;
    }

    bool IsSignificant() const {
        float dx = std::abs(currentPos.x - startPos.x);
        float dy = std::abs(currentPos.y - startPos.y);
        return dx > 4.f || dy > 4.f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  ProjectToScreen — projette un point 3D en coordonnées écran
// ─────────────────────────────────────────────────────────────────────────────
static std::optional<ImVec2> ProjectToScreen(
    const glm::vec3& worldPos,
    const glm::mat4& proj, const glm::mat4& view,
    float vpX, float vpY, float vpW, float vpH)
{
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.f);
    if (clip.w <= 0.f) return std::nullopt;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = vpX + (ndc.x * 0.5f + 0.5f) * vpW;
    float sy = vpY + (1.f - (ndc.y * 0.5f + 0.5f)) * vpH;
    return ImVec2{ sx, sy };
}

// Exécute la box-sélection : retourne les noms des objets dont la projection
// tombe dans le rectangle de sélection
static std::set<std::string> BoxSelectObjects(
    const BoxSelectionState& box,
    const glm::mat4& proj, const glm::mat4& view,
    float vpX, float vpY, float vpW, float vpH,
    const std::vector<RaycastCandidate>& candidates)
{
    std::set<std::string> result;
    for (auto& c : candidates) {
        auto screenPos = ProjectToScreen(c.center, proj, view, vpX, vpY, vpW, vpH);
        if (screenPos && box.ContainsPoint(*screenPos))
            result.insert(c.name);
    }
    return result;
}