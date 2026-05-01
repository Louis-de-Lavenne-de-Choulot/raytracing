#pragma once
#ifndef QUATERNION
#define QUATERNION
#include "eulerangle.h"
#include "vector3.h"
#include <cmath>
#include <numbers>
#include <iostream>
namespace HonHengine
{
    struct Quaternion
    {
        double w, x, y, z;
        Quaternion(double w = 1, double x = 0, double y = 0, double z = 0) {
            this->w = w;
            this->x = x;
            this->y = y;
            this->z = z;
        }

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
        Quaternion& operator*=(const Quaternion& q) {
            double nw = w * q.w - x * q.x - y * q.y - z * q.z;
            double nx = w * q.x + x * q.w + y * q.z - z * q.y;
            double ny = w * q.y - x * q.z + y * q.w + z * q.x;
            double nz = w * q.z + x * q.y - y * q.x + z * q.w;
            w = nw; x = nx; y = ny; z = nz;
            return *this;
        }

		Quaternion Normalized() const {
			double mag = sqrt(w * w + x * x + y * y + z * z);
			return Quaternion(w / mag, x / mag, y / mag, z / mag);
		}

        void Normalize() {
            double mag = sqrt(w * w + x * x + y * y + z * z);
            w /= mag; x /= mag; y /= mag; z /= mag;
        }

        Quaternion Conjugate(Quaternion q)
        {
            return Quaternion(q.w, -q.x, -q.y, -q.z);
        }

        Vector3 RotateVector3(const Vector3 *v)
        {
            Quaternion qv = Quaternion(0, v->x, v->y, v->z);
            Quaternion qConj = Conjugate(*this);
            Quaternion qRes = *this * qv * qConj;
            return Vector3(qRes.x, qRes.y, qRes.z);
        }

        Vector3 Rotate(const Vector3* toRotate, const Vector3* axis, double angle)
        {
            double halfAngle = angle * 0.5 * (std::numbers::pi / 180.0);
            double sinHalfAngle = sin(halfAngle);

            Quaternion q(
                cos(halfAngle),
                axis->x * sinHalfAngle,
                axis->y * sinHalfAngle,
                axis->z * sinHalfAngle
            );

            Quaternion vQuat(0, toRotate->x, toRotate->y, toRotate->z);
            Quaternion qConjugate(q.w, -q.x, -q.y, -q.z);
            Quaternion rotated = q * vQuat * qConjugate;

            *this = (q * (*this)).Normalized();

            return Vector3(rotated.x, rotated.y, rotated.z);
        }
    };
};
#endif