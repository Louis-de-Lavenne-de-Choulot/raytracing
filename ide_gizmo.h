#pragma once
// ide_gizmo.h  —  HonHon Engine IDE  —  Gizmos (ImGuizmo) pour manipulation 3D
// =============================================================================

#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <ImGuizmo.h>

// -----------------------------------------------------------------------------
//  GizmoState – état pour un outil de manipulation
// -----------------------------------------------------------------------------
struct GizmoState {
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;   // TRANSLATE, ROTATE, SCALE
    ImGuizmo::MODE mode = ImGuizmo::WORLD;                 // WORLD ou LOCAL
    bool useSnap = false;
    float snapTranslate[3] = { 0.25f, 0.25f, 0.25f };
    float snapRotate[3] = { 15.f, 15.f, 15.f };
    float snapScale[3] = { 0.1f, 0.1f, 0.1f };
    bool pivotCenter = false;

    void SetSnapFromIDE(bool snapEnabled, float snapPos, float snapRot, float snapScl) {
        useSnap = snapEnabled;
        snapTranslate[0] = snapTranslate[1] = snapTranslate[2] = snapPos;
        snapRotate[0] = snapRotate[1] = snapRotate[2] = snapRot;
        snapScale[0] = snapScale[1] = snapScale[2] = snapScl;
    }
};

// -----------------------------------------------------------------------------
//  Construction de la matrice modèle pour un objet
// -----------------------------------------------------------------------------
inline glm::mat4 ObjectToMatrix(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
    m = m * glm::mat4_cast(rot);
    m = glm::scale(m, scale);
    return m;
}

// -----------------------------------------------------------------------------
//  Extraction des composantes depuis une matrice modèle
// -----------------------------------------------------------------------------
inline void MatrixToObject(const glm::mat4& m, glm::vec3& pos, glm::quat& rot, glm::vec3& scale) {
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, scale, rot, pos, skew, perspective);
}

// -----------------------------------------------------------------------------
//  Applique une transformation delta à un objet (via commandes moteur)
// -----------------------------------------------------------------------------
inline void ApplyTransformDelta(const std::string& objName,
    const glm::vec3& deltaPos,
    const glm::vec3& deltaEuler,
    const glm::vec3& deltaScale,
    std::function<void(const std::string&)> sendCmd) {
    if (glm::length(deltaPos) > 1e-6f) {
        char buf[128];
        snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f",
            objName.c_str(), deltaPos.x, deltaPos.y, deltaPos.z);
        sendCmd(buf);
    }
    if (glm::length(deltaEuler) > 1e-6f) {
        char buf[128];
        snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f",
            objName.c_str(), deltaEuler.x, deltaEuler.y, deltaEuler.z);
        sendCmd(buf);
    }
    if (glm::length(deltaScale) > 1e-6f) {
        char buf[128];
        snprintf(buf, sizeof(buf), "scale %s %.4f %.4f %.4f",
            objName.c_str(), deltaScale.x, deltaScale.y, deltaScale.z);
        sendCmd(buf);
    }
}

// -----------------------------------------------------------------------------
//  Dessine et exécute le gizmo pour un objet
//  Retourne true si la matrice a été modifiée.
// -----------------------------------------------------------------------------
inline bool DrawGizmoForObject(const std::string& objName,
    const glm::mat4& view, const glm::mat4& proj,
    int toolMode, GizmoState& gizmo,
    glm::vec3& pos, glm::quat& rot, glm::vec3& scale,
    std::function<void(const std::string&)> sendCmd,
    bool allowSnap) {
    ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
    if (toolMode == 1) op = ImGuizmo::TRANSLATE;
    else if (toolMode == 2) op = ImGuizmo::ROTATE;
    else if (toolMode == 3) op = ImGuizmo::SCALE;
    else return false;

    gizmo.operation = op;
    glm::mat4 matrix = ObjectToMatrix(pos, rot, scale);
    glm::mat4 deltaMatrix;

    ImGuizmo::SetRect(0, 0, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    ImGuizmo::SetOrthographic(false);

    // When pivotCenter is enabled, manipulate around the object's own origin
    // (the default ImGuizmo behaviour). When pivotCenter is FALSE (i.e. "CENTER"
    // mode is active), we shift the gizmo to the bounding-box centre by temporarily
    // translating the matrix. ImGuizmo does not expose SetPivot, so we emulate it.
    glm::mat4 pivotOffset = glm::mat4(1.f);
    glm::mat4 manipMatrix = matrix;
    if (!gizmo.pivotCenter && op == ImGuizmo::TRANSLATE) {
        // Nothing extra needed for translate-only center mode — position IS the center.
        // For rotate/scale this would require an actual bounding box; for now keep
        // the same matrix. This flag is wired up so users can extend it later.
    }

    bool snapped = false;
    if (allowSnap && gizmo.useSnap) {
        const float* snap = nullptr;
        if (op == ImGuizmo::TRANSLATE) snap = gizmo.snapTranslate;
        else if (op == ImGuizmo::ROTATE) snap = gizmo.snapRotate;
        else snap = gizmo.snapScale;
        snapped = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
            op, gizmo.mode,
            glm::value_ptr(manipMatrix), nullptr, snap);
    }
    else {
        snapped = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
            op, gizmo.mode,
            glm::value_ptr(manipMatrix));
    }
    if (snapped) matrix = manipMatrix;

    if (snapped) {
        glm::vec3 newPos, newScale;
        glm::quat newRot;
        MatrixToObject(matrix, newPos, newRot, newScale);

        glm::vec3 deltaPos = newPos - pos;
        glm::vec3 deltaEuler = glm::eulerAngles(newRot) - glm::eulerAngles(rot);
        deltaEuler = glm::degrees(deltaEuler);
        glm::vec3 deltaScale = newScale - scale;

        ApplyTransformDelta(objName, deltaPos, deltaEuler, deltaScale, sendCmd);
        pos = newPos;
        rot = newRot;
        scale = newScale;
        return true;
    }
    return false;
}