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
            // Flatten to XZ plane for horizontal strafe
            Vector3 flat(fwd.x, 0, fwd.z);
            double len = flat.magnitude();
            if (len > 0.0001) flat = flat * (1.0 / len);
            else flat = absForward;
            // left = cross(flat, up)  (where up is (0,1,0))
            return flat.cross(absUP);
        }

        Vector3 right() {
            // right = -left
            Vector3 l = left();
            return Vector3(-l.x, -l.y, -l.z);
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

        glm::mat4 GetViewMatrix() {
            glm::vec3 pos(position.x, position.y, position.z);
            glm::vec3 fwd = forward().ToGLM();
            glm::vec3 up = this->up().ToGLM();
            return glm::lookAt(pos, pos + fwd, up);
        }

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