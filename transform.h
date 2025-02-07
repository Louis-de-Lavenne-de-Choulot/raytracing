#pragma once
#ifndef TRANSFORM
#define TRANSFORM
#include "vector3.h"
#include "material.h"
#include "basetype.h"
#include "quaternion.h"
#include <cmath>
#include <numbers>
struct Transform
{
    /* data */
    Vector3 *scale;
    Vector3 *position;
    Quaternion *rotation;
    Transform(Vector3 *scale, Vector3 *position, Quaternion *rotation)
    {
        this->scale = scale;
        this->position = position;
        this->rotation = rotation;
    }

    Vector3 Rotate(Vector3 *axis, double angle)
    {
        // Convert angle from degrees to radians
        double halfAngle = angle * 0.5 * (std::numbers::pi / 180.0);
        double sinHalfAngle = sin(halfAngle);

        // Create the rotation quaternion
        Quaternion q(
            cos(halfAngle),         // w
            axis->x * sinHalfAngle, // x
            axis->y * sinHalfAngle, // y
            axis->z * sinHalfAngle  // z
        );

        // Convert vector to quaternion (v as quaternion)
        Quaternion vQuat(0, this->position->x, this->position->y, this->position->z);

        // Rotate the vector using the quaternion
        Quaternion qConjugate(q.w, -q.x, -q.y, -q.z);

        // Apply the rotation: q * v * q_conjugate
        Quaternion rotated = q * vQuat * qConjugate;
        this->rotation->w = q.w;
        this->rotation->x = q.x;
        this->rotation->y = q.y;
        this->rotation->z = q.z;
        // Return the rotated vector
        return Vector3(rotated.x, rotated.y, rotated.z);
    }
};
#endif