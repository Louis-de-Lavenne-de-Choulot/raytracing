#pragma once
// ide_camera_bookmarks.h  —  HonHon Engine IDE  —  Camera bookmarks, focus
// =============================================================================

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "camera.h"

// -----------------------------------------------------------------------------
//  État d'un bookmark
// -----------------------------------------------------------------------------
struct CameraBookmark {
    glm::vec3 position;
    glm::quat rotation;
    float fov;
    bool ortho;
    float orthoSize;
};

// -----------------------------------------------------------------------------
//  Gestionnaire de bookmarks
// -----------------------------------------------------------------------------
struct CameraBookmarks {
    std::array<CameraBookmark, 9> slots;
    bool initialized = false;

    void InitFromCurrent(HonHengine::Camera* cam, bool ortho, float fov, float orthoSize) {
        if (!cam) return;
        for (int i = 0; i < 9; ++i) {
            slots[i].position = cam->transform.position.ToGLM();
            slots[i].rotation = cam->transform.rotation.ToGLM();
            slots[i].fov = fov;
            slots[i].ortho = ortho;
            slots[i].orthoSize = orthoSize;
        }
        initialized = true;
    }

    void Save(int idx, HonHengine::Camera* cam, bool ortho, float fov, float orthoSize) {
        if (!cam || idx < 0 || idx >= 9) return;
        slots[idx].position = cam->transform.position.ToGLM();
        slots[idx].rotation = cam->transform.rotation.ToGLM();
        slots[idx].fov = fov;
        slots[idx].ortho = ortho;
        slots[idx].orthoSize = orthoSize;
    }

    void Restore(int idx, HonHengine::Camera* cam, bool& orthoFlag, float& orthoSize, float& fov) {
        if (!cam || idx < 0 || idx >= 9) return;
        auto& s = slots[idx];
        cam->transform.position = HonHengine::Vector3(s.position.x, s.position.y, s.position.z);
        cam->transform.rotation = HonHengine::Quaternion(s.rotation.w, s.rotation.x, s.rotation.y, s.rotation.z);
        fov = s.fov;
        orthoFlag = s.ortho;
        orthoSize = s.orthoSize;
    }
};

// -----------------------------------------------------------------------------
//  Transition douce (focus) - FIXED VERSION
// -----------------------------------------------------------------------------
struct FocusTransition {
    bool active = false;
    float duration = 1.0f;      // 1 second smooth transition
    float timer = 0.f;

    // Start state
    glm::vec3 startPos;
    glm::quat startRot;
    float startFov;
    bool startOrtho;
    float startOrthoSize;

    // End state
    glm::vec3 endPos;
    glm::quat endRot;
    float endFov;
    bool endOrtho;
    float endOrthoSize;

    // Store the camera pointer for updates
    HonHengine::Camera* targetCamera = nullptr;

    // Call this from IDEState when you want to focus on an object
    // with a smooth camera transition that preserves view direction
    void Start(HonHengine::Camera* cam, float currentFov, bool ortho, float orthoSize,
        const glm::vec3& targetWorldPos, float targetDistance = 5.f) {

        if (!cam) return;

        active = true;
        timer = 0.f;
        targetCamera = cam;

        // Store start state
        startPos = cam->transform.position.ToGLM();
        startRot = cam->transform.rotation.ToGLM();
        startFov = currentFov;
        startOrtho = ortho;
        startOrthoSize = orthoSize;

        // Calculate the direction from the object to the camera
        // (preserve the relative viewing angle)
        glm::vec3 dir = startPos - targetWorldPos;
        float currentDist = glm::length(dir);

        // If we're extremely close or at the pivot, use a default diagonal view
        if (currentDist < 0.1f) {
            dir = glm::vec3(-5.0f, 3.0f, -5.0f);
            currentDist = glm::length(dir);
        }

        // Normalize the direction
        dir = glm::normalize(dir);

        // Calculate new camera position at the desired distance
        // Clamp target distance based on object size (optional - could be passed as parameter)
        float finalDistance = (std::max)(targetDistance, 2.0f);
        endPos = targetWorldPos + dir * finalDistance;

        // Ensure camera doesn't go below ground (optional)
        if (endPos.y < 1.0f) {
            // Adjust to maintain same angle but higher position
            float heightDiff = 1.0f - endPos.y;
            endPos.y = 1.0f;
            endPos.x += dir.x * heightDiff;
            endPos.z += dir.z * heightDiff;
        }

        // Calculate rotation to look at the target
        glm::vec3 lookDir = glm::normalize(targetWorldPos - endPos);
        glm::vec3 up = glm::vec3(0, 1, 0);

        // Handle edge case where lookDir is parallel to up
        if (glm::abs(glm::dot(lookDir, up)) > 0.9999f) {
            up = glm::vec3(0, 0, 1);
        }

        endRot = glm::quatLookAt(lookDir, up);

        // Keep same FOV and ortho settings
        endFov = currentFov;
        endOrtho = ortho;
        endOrthoSize = orthoSize;
    }

    // Alternative: Start with explicit target camera position (for more control)
    void StartTargetAndCamera(HonHengine::Camera* cam,
        const HonHengine::Vector3& newCameraPos,
        const HonHengine::Vector3& lookAtTarget,
        float currentFov, bool ortho, float orthoSize, float animDuration = 0.5f) {

        if (!cam) return;

        active = true;
        timer = 0.f;
        targetCamera = cam;
        duration = animDuration;

        // Store start state
        startPos = cam->transform.position.ToGLM();
        startRot = cam->transform.rotation.ToGLM();
        startFov = currentFov;
        startOrtho = ortho;
        startOrthoSize = orthoSize;

        // Set end position
        endPos = glm::vec3(newCameraPos.x, newCameraPos.y, newCameraPos.z);

        // Calculate rotation to look at target
        glm::vec3 lookDir = glm::normalize(glm::vec3(lookAtTarget.x, lookAtTarget.y, lookAtTarget.z) - endPos);
        glm::vec3 up = glm::vec3(0, 1, 0);

        if (glm::abs(glm::dot(lookDir, up)) > 0.9999f) {
            up = glm::vec3(0, 0, 1);
        }

        endRot = glm::quatLookAt(lookDir, up);
        endFov = currentFov;
        endOrtho = ortho;
        endOrthoSize = orthoSize;
    }

    bool Update(float dt, HonHengine::Camera* cam, float& fov, bool& ortho, float& orthoSize) {
        if (!active) return false;

        timer += dt;
        float t = glm::clamp(timer / duration, 0.f, 1.f);

        // Ease in-out curve for smoother animation
        t = t * t * (3.f - 2.f * t);

        // Interpolate position
        glm::vec3 pos = glm::mix(startPos, endPos, t);
        cam->transform.position = HonHengine::Vector3(pos.x, pos.y, pos.z);

        // Interpolate rotation (slerp for quaternions)
        glm::quat rot = glm::slerp(startRot, endRot, t);
        cam->transform.rotation = HonHengine::Quaternion(rot.w, rot.x, rot.y, rot.z);

        // Interpolate FOV
        fov = glm::mix(startFov, endFov, t);

        // Handle ortho mode transition (could also interpolate orthoSize)
        if (t >= 1.f) {
            ortho = endOrtho;
            orthoSize = endOrthoSize;
        }
        else {
            ortho = startOrtho;
            orthoSize = glm::mix(startOrthoSize, endOrthoSize, t);
        }

        if (t >= 1.f) {
            active = false;
            targetCamera = nullptr;
            return false;
        }
        return true;
    }

    // Reset the focus transition
    void Reset() {
        active = false;
        timer = 0.f;
        targetCamera = nullptr;
    }

    // Check if focus is currently active
    bool IsActive() const { return active; }
};