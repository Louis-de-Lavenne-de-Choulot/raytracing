#pragma once
#include "vector3.h"
#include "quaternion.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace HonHengine {
    struct Transform {
        Vector3 scale;
        Vector3 position;
        Quaternion rotation;

        inline static const Vector3 absRight{ 1, 0, 0 };
        inline static const Vector3 absUP{ 0, 1, 0 };
        inline static const Vector3 absForward{ 0, 0, 1 };

        Transform(Vector3 s = { 1,1,1 }, Vector3 p = { 0,0,0 }, Quaternion r = Quaternion())
            : scale(s), position(p), rotation(r) {
        }

        Vector3 forward() {
            Vector3 temp(rotation.RotateVector3(&absForward));
            return temp;
        }

        Vector3 left() {
            Vector3 fwd = forward();
            Vector3 flat(fwd.x, 0, fwd.z);
            double len = flat.magnitude();
            if (len > 0.0001) flat = flat * (1.0 / len);
            else flat = absForward;
            return absUP.cross(flat);
        }

        Vector3 right() {
            Vector3 r = left();
            return Vector3(-r.x, -r.y, -r.z);
        }

        Vector3 up() {
            Vector3 fwd = forward();
            Vector3 rgt = right();
            return fwd.cross(rgt);
        }

        void Rotate(Vector3 axis, double angle) {
            Quaternion incremental;
            incremental.Rotate(&position, &axis, angle);
            rotation *= incremental;
        }

        void LookAt(Transform target) {
            rotation = Quaternion::LookRotation(target.position - position);
        }

        glm::mat4 GetViewMatrix() const {
            // Convert to GLM types
            glm::vec3 pos(position.x, position.y, position.z);
            glm::quat rot(rotation.w, rotation.x, rotation.y, rotation.z);

            // Build world matrix
            glm::mat4 world = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);

            // View matrix is inverse of world matrix
            glm::mat4 view = glm::inverse(world);
            return view;
        }

        // NEW: Get world matrix for objects
        glm::mat4 GetWorldMatrix() const {
            glm::vec3 pos(position.x, position.y, position.z);
            glm::vec3 sc(scale.x, scale.y, scale.z);
            glm::quat rot(rotation.w, rotation.x, rotation.y, rotation.z);

            glm::mat4 world = glm::translate(glm::mat4(1.0f), pos);
            world = world * glm::mat4_cast(rot);
            world = glm::scale(world, sc);
            return world;
        }
    };
}