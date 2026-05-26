#pragma once
// ide_gizmo.h  —  HonHon Engine IDE  —  Gizmos (ImGuizmo)
// =============================================================================

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

struct GizmoState {
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      mode = ImGuizmo::WORLD;
    bool  useSnap = false;
    float snapTranslate[3] = { 0.25f, 0.25f, 0.25f };
    float snapRotate[3] = { 15.f,  15.f,  15.f };
    float snapScale[3] = { 0.1f,  0.1f,  0.1f };
    bool  pivotCenter = false;

    void SetSnapFromIDE(bool snapEnabled, float snapPos, float snapRot, float snapScl) {
        useSnap = snapEnabled;
        snapTranslate[0] = snapTranslate[1] = snapTranslate[2] = snapPos;
        snapRotate[0] = snapRotate[1] = snapRotate[2] = snapRot;
        snapScale[0] = snapScale[1] = snapScale[2] = snapScl;
    }
};

inline glm::mat4 ObjectToMatrix(const glm::vec3& pos,
    const glm::quat& rot,
    const glm::vec3& scale)
{
    glm::mat4 m = glm::translate(glm::mat4(1.f), pos);
    m = m * glm::mat4_cast(rot);
    m = glm::scale(m, scale);
    return m;
}

inline void MatrixToObject(const glm::mat4& m,
    glm::vec3& pos,
    glm::quat& rot,
    glm::vec3& scale)
{
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(m, scale, rot, pos, skew, perspective);
}

inline bool DrawGizmoForObject(const std::string& objName,
    const glm::mat4& view,
    const glm::mat4& proj,
    int                toolMode,
    GizmoState& gizmo,
    glm::vec3& pos,
    glm::quat& rot,
    glm::vec3& scale,
    std::function<void(const std::string&)> sendCmd,
    bool               allowSnap)
{
    ImGuizmo::OPERATION op;
    switch (toolMode) {
    case 1:  op = ImGuizmo::TRANSLATE; break;
    case 2:  op = ImGuizmo::ROTATE;    break;
    case 3:  op = ImGuizmo::SCALE;     break;
    default: return false;
    }
    gizmo.operation = op;

    const glm::vec3 originalPos = pos;
    const glm::quat originalRot = rot;
    const glm::vec3 originalScale = scale;

    glm::mat4 matrix = ObjectToMatrix(pos, rot, scale);

    const float* snap = nullptr;
    if (allowSnap && gizmo.useSnap) {
        if (op == ImGuizmo::TRANSLATE) snap = gizmo.snapTranslate;
        else if (op == ImGuizmo::ROTATE)    snap = gizmo.snapRotate;
        else                                snap = gizmo.snapScale;
    }

    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(proj),
        op,
        gizmo.mode,
        glm::value_ptr(matrix),
        nullptr,
        snap);

    if (!ImGuizmo::IsUsing()) return false;

    glm::vec3 newPos, newScale;
    glm::quat newRot;
    MatrixToObject(matrix, newPos, newRot, newScale);
    newRot = glm::normalize(newRot);  // prevent quaternion drift

    bool changed = false;

    if (op == ImGuizmo::TRANSLATE) {
        glm::vec3 delta = newPos - originalPos;
        if (glm::length(delta) > 1e-5f) {
            // Zero out negligible axes so dragging one arrow never contaminates
            // the others (floating-point drift from matrix decomposition).
            if (std::abs(delta.x) < 1e-4f) newPos.x = originalPos.x;
            if (std::abs(delta.y) < 1e-4f) newPos.y = originalPos.y;
            if (std::abs(delta.z) < 1e-4f) newPos.z = originalPos.z;

            char buf[128];
            std::snprintf(buf, sizeof(buf), "move %s %.4f %.4f %.4f",
                objName.c_str(), newPos.x, newPos.y, newPos.z);
            sendCmd(buf);
            changed = true;
        }
    }
    else if (op == ImGuizmo::ROTATE) {
        if (glm::angle(glm::inverse(originalRot) * newRot) > 1e-5f) {
            glm::vec3 euler = glm::degrees(glm::eulerAngles(newRot));
            char buf[128];
            std::snprintf(buf, sizeof(buf), "rotate %s %.3f %.3f %.3f",
                objName.c_str(), euler.x, euler.y, euler.z);
            sendCmd(buf);
            changed = true;
        }
    }
    else if (op == ImGuizmo::SCALE) {
        if (glm::length(newScale - originalScale) > 1e-5f) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "scale %s %.4f %.4f %.4f",
                objName.c_str(), newScale.x, newScale.y, newScale.z);
            sendCmd(buf);
            changed = true;
        }
    }

    if (changed) {
        pos = newPos;
        rot = newRot;
        scale = newScale;
    }

    return changed;
}