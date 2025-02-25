#pragma once
#ifndef QUATERNION
#define QUATERNION
#include "eulerangle.h"
#include "vector3.h"
#include <cmath>
#include <numbers>
namespace PEngine
{
    struct Quaternion
    {
        double w, x, y, z;

        // this implementation assumes normalized quaternion
        // converts to Euler angles in 3-2-1 sequence
        EulerAngles ToEulerAngle();

        // Multiply two quaternions
        Quaternion operator*(const Quaternion &q) const
        {
            return Quaternion(
                w * q.w - x * q.x - y * q.y - z * q.z,
                w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w);
        }

        // Multiply two quaternions
        Quaternion &operator*=(const Quaternion &q)
        {
            
            this->w = w * q.w - x * q.x - y * q.y - z * q.z;
            this->x = w * q.x + x * q.w + y * q.z - z * q.y;
            this->y = w * q.y - x * q.z + y * q.w + z * q.x;
            this->z = w * q.z + x * q.y - y * q.x + z * q.w;
            return *this;
        }

        Quaternion Conjugate(Quaternion q)
        {
            return Quaternion(q.w, -q.x, -q.y, -q.z);
        }

        Vector3 *RotateVector3(Vector3 *v)
        {
            Quaternion qv = Quaternion(0, v->x, v->y, v->z);
            Quaternion qConj = Conjugate(*this);
            Quaternion qRes = *this * qv * qConj;
            return new Vector3(qRes.x, qRes.y, qRes.z);
        }

        Vector3 Rotate(Vector3 *toRotate, Vector3 *axis, double angle)
        {
            // Convert angle from degrees to radians
            double halfAngle = angle * 0.5 * (std::numbers::pi / 180.0);
            double sinHalfAngle = sin(halfAngle);

            // Create the rotation quaternion
            Quaternion q(
                cos(halfAngle),         // w
                axis->z * sinHalfAngle, // x
                axis->x * sinHalfAngle, // y
                axis->y * sinHalfAngle  // z
            );

            // Convert vector to quaternion (v as quaternion)
            Quaternion vQuat(0, toRotate->x, toRotate->y, toRotate->z);

            // Rotate the vector using the quaternion
            Quaternion qConjugate(q.w, -q.x, -q.y, -q.z);

            // Apply the rotation: q * v * q_conjugate
            Quaternion rotated = q * vQuat * qConjugate;
            this->w = q.w;
            this->x = q.x;
            this->y = q.y;
            this->z = q.z;
            // Return the rotated vector
            return Vector3(rotated.x, rotated.y, rotated.z);
        }
    };
};
#endif