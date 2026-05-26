#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <string>
#include <vector>
#include <optional>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "UIRenderer.h"

enum class GizmoAxis { NONE, X, Y, Z, XY, XZ, YZ, UNIFORM };

class TransformGizmo {
public:
    bool isDragging = false;

    TransformGizmo() = default;

    // Handles logic, clicking, and raycasting interactions
    bool Update(int toolMode, const glm::vec3& objPos, const glm::quat& objRot, bool isLocal,
        const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
        float mouseX, float mouseY, bool mousePressed, bool mouseReleased,
        bool snapEnabled, float snapPos, float snapRot, float snapScl,
        const std::string& objName, class CommandBus& bus);

    std::optional<glm::vec2> projectToScreenUnclamped(const glm::vec3& worldPos, const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH) const;

    // Renders handles to screen space
    void Render(ImDrawList* drawList, int toolMode, const glm::vec3& objPos, const glm::quat& objRot, bool isLocal,
        const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
        float mouseX, float mouseY) const;

private:
    GizmoAxis activeAxis = GizmoAxis::NONE;

    // Initial states saved at drag start
    glm::vec3 startObjPos;
    glm::quat startObjRot;
    glm::vec3 startObjScale;
    glm::vec3 startIntersectionWorld;
    float startMouseAngle = 0.0f;

    // Projection & ray-casting helpers
    bool ScreenToWorldPlane(float mx, float my, const glm::vec3& planePoint, const glm::vec3& planeNormal,
        const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
        glm::vec3& outIntersection);

    bool RayIntersectsSphere(const glm::vec3& rayOrig, const glm::vec3& rayDir,
        const glm::vec3& sphereCenter, float radius, float& t);

    float SnapValue(float val, float step);
};