#include "gizmo.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include "commandBus.h"

float TransformGizmo::SnapValue(float val, float step) {
    if (step <= 0.0f) return val;
    return std::floor((val + step * 0.5f) / step) * step;
}

bool TransformGizmo::ScreenToWorldPlane(float mx, float my, const glm::vec3& planePoint, const glm::vec3& planeNormal,
    const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
    glm::vec3& outIntersection)
{
    // Convert screen mouse space to NDC
    float x = ((mx - vpX) / vpW) * 2.0f - 1.0f;
    float y = ((my - vpY) / vpH) * 2.0f - 1.0f;

    glm::mat4 invVP = glm::inverse(proj * view);
    glm::vec4 rayStartNDC(x, y, -1.0f, 1.0f);
    glm::vec4 rayEndNDC(x, y, 1.0f, 1.0f);

    glm::vec4 rayStartWorld = invVP * rayStartNDC;
    rayStartWorld /= rayStartWorld.w;
    glm::vec4 rayEndWorld = invVP * rayEndNDC;
    rayEndWorld /= rayEndWorld.w;

    glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld) - glm::vec3(rayStartWorld));
    glm::vec3 rayOrig = glm::vec3(rayStartWorld);

    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) > 1e-6f) {
        float t = glm::dot(planePoint - rayOrig, planeNormal) / denom;
        if (t >= 0.0f) {
            outIntersection = rayOrig + t * rayDir;
            return true;
        }
    }
    return false;
}

bool TransformGizmo::RayIntersectsSphere(const glm::vec3& rayOrig, const glm::vec3& rayDir,
    const glm::vec3& sphereCenter, float radius, float& t)
{
    glm::vec3 oc = rayOrig - sphereCenter;
    float b = glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0f) return false;
    h = std::sqrt(h);
    t = -b - h;
    if (t < 0.0f) t = -b + h;
    return t >= 0.0f;
}

bool TransformGizmo::Update(int toolMode, const glm::vec3& objPos, const glm::quat& objRot, bool isLocal,
    const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
    float mouseX, float mouseY, bool mousePressed, bool mouseReleased,
    bool snapEnabled, float snapPos, float snapRot, float snapScl,
    const std::string& objName, CommandBus& bus)
{
    if (toolMode == 0 || objName.empty()) {
        isDragging = false;
        activeAxis = GizmoAxis::NONE;
        return false;
    }

    // Determine Axis Orientation
    glm::vec3 axisX = isLocal ? objRot * glm::vec3(1, 0, 0) : glm::vec3(1, 0, 0);
    glm::vec3 axisY = isLocal ? objRot * glm::vec3(0, 1, 0) : glm::vec3(0, 1, 0);
    glm::vec3 axisZ = isLocal ? objRot * glm::vec3(0, 0, 1) : glm::vec3(0, 0, 1);

    glm::mat4 invVP = glm::inverse(proj * view);
    float ndcX = ((mouseX - vpX) / vpW) * 2.0f - 1.0f;
    float ndcY = ((mouseY - vpY) / vpH) * 2.0f - 1.0f;
    glm::vec4 r0 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f); r0 /= r0.w;
    glm::vec4 r1 = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);  r1 /= r1.w;
    glm::vec3 rayOrig = glm::vec3(r0);
    glm::vec3 rayDir = glm::normalize(glm::vec3(r1) - rayOrig);

    // 1. Mouse Down Action (Hit Testing)
    if (mousePressed && !isDragging) {
        auto projectToScreenLocal = [&](const glm::vec3& wp) -> glm::vec2 {
            glm::vec4 clip = proj * view * glm::vec4(wp, 1.f);
            if (clip.w <= 0.f) return glm::vec2(-9999);
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return glm::vec2(vpX + (ndc.x * 0.5f + 0.5f) * vpW, vpY + (ndc.y * 0.5f + 0.5f) * vpH);
            };

        glm::vec2 sCenter = projectToScreenLocal(objPos);
        float handleLength = 1.2f;

        if (toolMode == 1 || toolMode == 3) { // Translation & Scale Screen Handle checks
            glm::vec2 sX = projectToScreenLocal(objPos + axisX * handleLength);
            glm::vec2 sY = projectToScreenLocal(objPos + axisY * handleLength);
            glm::vec2 sZ = projectToScreenLocal(objPos + axisZ * handleLength);

            float threshold = 20.0f;
            if (glm::distance(glm::vec2(mouseX, mouseY), sX) < threshold) activeAxis = GizmoAxis::X;
            else if (glm::distance(glm::vec2(mouseX, mouseY), sY) < threshold) activeAxis = GizmoAxis::Y;
            else if (glm::distance(glm::vec2(mouseX, mouseY), sZ) < threshold) activeAxis = GizmoAxis::Z;

            // Planar selection boxes checks
            if (activeAxis == GizmoAxis::NONE) {
                glm::vec2 sXY = projectToScreenLocal(objPos + (axisX + axisY) * 0.3f);
                glm::vec2 sXZ = projectToScreenLocal(objPos + (axisX + axisZ) * 0.3f);
                glm::vec2 sYZ = projectToScreenLocal(objPos + (axisY + axisZ) * 0.3f);
                if (glm::distance(glm::vec2(mouseX, mouseY), sXY) < 15.0f) activeAxis = GizmoAxis::XY;
                else if (glm::distance(glm::vec2(mouseX, mouseY), sXZ) < 15.0f) activeAxis = GizmoAxis::XZ;
                else if (glm::distance(glm::vec2(mouseX, mouseY), sYZ) < 15.0f) activeAxis = GizmoAxis::YZ;
            }
            if (toolMode == 3 && activeAxis == GizmoAxis::NONE && glm::distance(glm::vec2(mouseX, mouseY), sCenter) < 15.0f) {
                activeAxis = GizmoAxis::UNIFORM;
            }
        }
        else if (toolMode == 2) { // Rotation Arcball Rings Hit Testing
            float tSphere;
            if (RayIntersectsSphere(rayOrig, rayDir, objPos, handleLength, tSphere)) {
                glm::vec3 hitPoint = rayOrig + tSphere * rayDir;
                glm::vec3 localHit = hitPoint - objPos;

                float devX = std::abs(glm::dot(localHit, axisX));
                float devY = std::abs(glm::dot(localHit, axisY));
                float devZ = std::abs(glm::dot(localHit, axisZ));

                float ringThicknessThresh = 0.15f;
                if (devX < ringThicknessThresh) activeAxis = GizmoAxis::X;      // YZ circle
                else if (devY < ringThicknessThresh) activeAxis = GizmoAxis::Y; // XZ circle
                else if (devZ < ringThicknessThresh) activeAxis = GizmoAxis::Z; // XY circle
            }
        }

        if (activeAxis != GizmoAxis::NONE) {
            isDragging = true;
            startObjPos = objPos;
            startObjRot = objRot;
            startMouseAngle = std::atan2(mouseY - sCenter.y, mouseX - sCenter.x);

            // Setup alignment drag plane
            glm::vec3 normal = glm::normalize(rayOrig - objPos);
            if (activeAxis == GizmoAxis::X) normal = glm::cross(glm::cross(axisX, normal), axisX);
            else if (activeAxis == GizmoAxis::Y) normal = glm::cross(glm::cross(axisY, normal), axisY);
            else if (activeAxis == GizmoAxis::Z) normal = glm::cross(glm::cross(axisZ, normal), axisZ);
            else if (activeAxis == GizmoAxis::XY) normal = axisZ;
            else if (activeAxis == GizmoAxis::XZ) normal = axisY;
            else if (activeAxis == GizmoAxis::YZ) normal = axisX;

            ScreenToWorldPlane(mouseX, mouseY, objPos, normal, view, proj, vpX, vpY, vpW, vpH, startIntersectionWorld);
            return true;
        }
    }

    // 2. Continuous Drag Action
    if (isDragging && !mouseReleased) {
        glm::vec3 normal = glm::normalize(rayOrig - startObjPos);
        if (activeAxis == GizmoAxis::X) normal = glm::cross(glm::cross(axisX, normal), axisX);
        else if (activeAxis == GizmoAxis::Y) normal = glm::cross(glm::cross(axisY, normal), axisY);
        else if (activeAxis == GizmoAxis::Z) normal = glm::cross(glm::cross(axisZ, normal), axisZ);
        else if (activeAxis == GizmoAxis::XY) normal = axisZ;
        else if (activeAxis == GizmoAxis::XZ) normal = axisY;
        else if (activeAxis == GizmoAxis::YZ) normal = axisX;

        glm::vec3 currentIntersection;
        if (ScreenToWorldPlane(mouseX, mouseY, startObjPos, normal, view, proj, vpX, vpY, vpW, vpH, currentIntersection)) {
            glm::vec3 delta = currentIntersection - startIntersectionWorld;
            char cmd[128] = { 0 };

            if (toolMode == 1) { // Process translation delta along locked axis configurations
                glm::vec3 moveVec(0.0f);
                if (activeAxis == GizmoAxis::X) moveVec = axisX * glm::dot(delta, axisX);
                else if (activeAxis == GizmoAxis::Y) moveVec = axisY * glm::dot(delta, axisY);
                else if (activeAxis == GizmoAxis::Z) moveVec = axisZ * glm::dot(delta, axisZ);
                else if (activeAxis == GizmoAxis::XY) moveVec = axisX * glm::dot(delta, axisX) + axisY * glm::dot(delta, axisY);
                else if (activeAxis == GizmoAxis::XZ) moveVec = axisX * glm::dot(delta, axisX) + axisZ * glm::dot(delta, axisZ);
                else if (activeAxis == GizmoAxis::YZ) moveVec = axisY * glm::dot(delta, axisY) + axisZ * glm::dot(delta, axisZ);

                glm::vec3 newPos = startObjPos + moveVec;
                if (snapEnabled) {
                    newPos.x = SnapValue(newPos.x, snapPos);
                    newPos.y = SnapValue(newPos.y, snapPos);
                    newPos.z = SnapValue(newPos.z, snapPos);
                }
                std::snprintf(cmd, sizeof(cmd), "move %s %.4f %.4f %.4f", objName.c_str(), newPos.x, newPos.y, newPos.z);
                bus.send(cmd);
            }
            else if (toolMode == 2) { // Arcball calculations
                glm::vec4 clip = proj * view * glm::vec4(startObjPos, 1.f);
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                glm::vec2 sCenter(vpX + (ndc.x * 0.5f + 0.5f) * vpW, vpY + (1.f - (ndc.y * 0.5f + 0.5f)) * vpH);

                float currentAngle = std::atan2(mouseY - sCenter.y, mouseX - sCenter.x);
                float angleDelta = glm::degrees(currentAngle - startMouseAngle);
                if (snapEnabled) angleDelta = SnapValue(angleDelta, snapRot);

                glm::vec3 rotAxis(0);
                if (activeAxis == GizmoAxis::X) rotAxis = axisX;
                else if (activeAxis == GizmoAxis::Y) rotAxis = axisY;
                else if (activeAxis == GizmoAxis::Z) rotAxis = axisZ;

                glm::quat deltaRot = glm::angleAxis(glm::radians(angleDelta), rotAxis);
                glm::vec3 euler = glm::degrees(glm::eulerAngles(deltaRot * startObjRot));
                std::snprintf(cmd, sizeof(cmd), "rotate %s %.3f %.3f %.3f", objName.c_str(), euler.x, euler.y, euler.z);
                bus.send(cmd);
            }
            else if (toolMode == 3) { // Scaling delta calculations
                float scaleFactor = 1.0f + glm::dot(delta, axisX + axisY + axisZ) * 0.5f;
                if (snapEnabled) scaleFactor = SnapValue(scaleFactor, snapScl);

                if (activeAxis == GizmoAxis::X) std::snprintf(cmd, sizeof(cmd), "scale %s %.3f 1.000 1.000", objName.c_str(), scaleFactor);
                else if (activeAxis == GizmoAxis::Y) std::snprintf(cmd, sizeof(cmd), "scale %s 1.000 %.3f 1.000", objName.c_str(), scaleFactor);
                else if (activeAxis == GizmoAxis::Z) std::snprintf(cmd, sizeof(cmd), "scale %s 1.000 1.000 %.3f", objName.c_str(), scaleFactor);
                else if (activeAxis == GizmoAxis::UNIFORM) std::snprintf(cmd, sizeof(cmd), "scale %s %.3f %.3f %.3f", objName.c_str(), scaleFactor, scaleFactor, scaleFactor);

                if (cmd[0] != '\0') bus.send(cmd);
            }
        }
    }

    if (mouseReleased) {
        isDragging = false;
        activeAxis = GizmoAxis::NONE;
    }

    return isDragging;
}

void TransformGizmo::Render(ImDrawList* drawList, int toolMode, const glm::vec3& objPos, const glm::quat& objRot, bool isLocal,
    const glm::mat4& view, const glm::mat4& proj, float vpX, float vpY, float vpW, float vpH,
    float mouseX, float mouseY) const
{
    if (toolMode == 0 || !drawList) return;

    // Safe projection with NDC clamping
    auto projectToScreen = [&](const glm::vec3& wp) -> std::optional<glm::vec2> {
        glm::vec4 clip = proj * view * glm::vec4(wp, 1.f);
        if (clip.w <= 0.f) return std::nullopt;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        // Clamp to valid NDC range to avoid huge screen coordinates
        ndc.x = glm::clamp(ndc.x, -1.0f, 1.0f);
        ndc.y = glm::clamp(ndc.y, -1.0f, 1.0f);
        float sx = vpX + (ndc.x * 0.5f + 0.5f) * vpW;
        float sy = vpY + (ndc.y * 0.5f + 0.5f) * vpH;
        return glm::vec2(sx, sy);
        };

    // Object center screen position
    auto sCenter = projectToScreen(objPos);
    if (!sCenter) return;
    glm::vec2 center = *sCenter;

    // Skip if the center is way outside the viewport (performance)
    if (center.x < vpX - 200 || center.x > vpX + vpW + 200 ||
        center.y < vpY - 200 || center.y > vpY + vpH + 200)
        return;

    // Axis directions
    glm::vec3 axisX = isLocal ? objRot * glm::vec3(1, 0, 0) : glm::vec3(1, 0, 0);
    glm::vec3 axisY = isLocal ? objRot * glm::vec3(0, 1, 0) : glm::vec3(0, 1, 0);
    glm::vec3 axisZ = isLocal ? objRot * glm::vec3(0, 0, 1) : glm::vec3(0, 0, 1);

    float handleLen = 1.2f;
    auto sX = projectToScreen(objPos + axisX * handleLen);
    auto sY = projectToScreen(objPos + axisY * handleLen);
    auto sZ = projectToScreen(objPos + axisZ * handleLen);

    auto GetImColor = [&](GizmoAxis current, GizmoAxis target, ImU32 defaultColor) -> ImU32 {
        return (current == target) ? IM_COL32(255, 255, 0, 255) : defaultColor;
        };

    ImU32 colX = GetImColor(activeAxis, GizmoAxis::X, IM_COL32(235, 60, 60, 255));
    ImU32 colY = GetImColor(activeAxis, GizmoAxis::Y, IM_COL32(60, 235, 60, 255));
    ImU32 colZ = GetImColor(activeAxis, GizmoAxis::Z, IM_COL32(60, 100, 245, 255));

    // Helper: draw line + arrowhead
    auto drawArrowLine = [&](const glm::vec2& from, const glm::vec2& to, ImU32 color, float thickness = 3.0f) {
        drawList->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, thickness);
        glm::vec2 dir = to - from;
        float len = glm::length(dir);
        if (len < 0.001f) return;
        glm::vec2 fwd = dir / len;
        glm::vec2 side(-fwd.y, fwd.x);
        float arrowSize = 12.0f;
        float arrowWidth = 6.0f;
        // Scale arrow size by screen distance
        float scale = glm::clamp(len / 60.0f, 0.5f, 1.2f);
        arrowSize *= scale;
        arrowWidth *= scale;
        glm::vec2 apex = to;
        glm::vec2 left = to - fwd * arrowSize + side * arrowWidth;
        glm::vec2 right = to - fwd * arrowSize - side * arrowWidth;
        drawList->AddTriangleFilled(ImVec2(apex.x, apex.y), ImVec2(left.x, left.y), ImVec2(right.x, right.y), color);
        };

    // Helper: draw planar handle (square)
    auto drawPlanarHandle = [&](const glm::vec2& pos, ImU32 color, float size = 12.0f) {
        drawList->AddRectFilled(
            ImVec2(pos.x - size / 2, pos.y - size / 2),
            ImVec2(pos.x + size / 2, pos.y + size / 2),
            color, 3.0f);
        };

    if (toolMode == 1) { // Translate
        if (sX) drawArrowLine(center, *sX, colX);
        if (sY) drawArrowLine(center, *sY, colY);
        if (sZ) drawArrowLine(center, *sZ, colZ);

        auto sXY = projectToScreen(objPos + (axisX + axisY) * 0.35f);
        auto sXZ = projectToScreen(objPos + (axisX + axisZ) * 0.35f);
        auto sYZ = projectToScreen(objPos + (axisY + axisZ) * 0.35f);

        ImU32 colXY = (activeAxis == GizmoAxis::XY) ? IM_COL32(255, 255, 0, 220) : IM_COL32(255, 255, 0, 120);
        ImU32 colXZ = (activeAxis == GizmoAxis::XZ) ? IM_COL32(255, 0, 255, 220) : IM_COL32(255, 0, 255, 120);
        ImU32 colYZ = (activeAxis == GizmoAxis::YZ) ? IM_COL32(0, 255, 255, 220) : IM_COL32(0, 255, 255, 120);

        if (sXY) drawPlanarHandle(*sXY, colXY);
        if (sXZ) drawPlanarHandle(*sXZ, colXZ);
        if (sYZ) drawPlanarHandle(*sYZ, colYZ);
    }
    else if (toolMode == 2) { // Rotate
        int segments = 48;
        float radius = 1.2f;
        auto drawRing = [&](const glm::vec3& u, const glm::vec3& v, ImU32 color) {
            std::optional<glm::vec2> last;
            for (int i = 0; i <= segments; ++i) {
                float theta = (i / (float)segments) * 2.0f * 3.14159265f;
                glm::vec3 wPt = objPos + (u * std::cos(theta) + v * std::sin(theta)) * radius;
                auto sPt = projectToScreen(wPt);
                if (sPt && last) {
                    drawList->AddLine(ImVec2(last->x, last->y), ImVec2(sPt->x, sPt->y), color, 2.0f);
                }
                last = sPt;
            }
            };
        drawRing(axisY, axisZ, colX);
        drawRing(axisX, axisZ, colY);
        drawRing(axisX, axisY, colZ);
    }
    else if (toolMode == 3) { // Scale
        if (sX) drawArrowLine(center, *sX, colX);
        if (sY) drawArrowLine(center, *sY, colY);
        if (sZ) drawArrowLine(center, *sZ, colZ);

        ImU32 colUniform = (activeAxis == GizmoAxis::UNIFORM) ? IM_COL32(255, 255, 0, 220) : IM_COL32(255, 255, 255, 180);
        // Diamond for uniform scale
        float diamondSize = 10.0f;
        ImVec2 pts[4] = {
            ImVec2(center.x, center.y - diamondSize),
            ImVec2(center.x + diamondSize, center.y),
            ImVec2(center.x, center.y + diamondSize),
            ImVec2(center.x - diamondSize, center.y)
        };
        drawList->AddConvexPolyFilled(pts, 4, colUniform);
    }
}