#pragma once
#ifndef QUATERNION
#define QUATERNION
#include "eulerangle.h"
#include "vector3.h"
#include <cmath>
#include <numbers>
#include <iostream>
#include <glm/gtc/quaternion.hpp>

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

        static Quaternion FromAxisAngle(const Vector3& axis, double angleDeg)
        {
            Vector3 normAxis = axis.normalize();

            double halfAngle = angleDeg * 0.5 * (std::numbers::pi / 180.0);
            double s = sin(halfAngle);

            return Quaternion(
                cos(halfAngle),
                normAxis.x * s,
                normAxis.y * s,
                normAxis.z * s
            );
        }

        static Quaternion LookRotation(const Vector3& forward, const Vector3& up = Vector3(0, 1, 0))
        {
            Vector3 f = forward.normalize();

            // Handle degenerate forward
            if (f.magnitude() < 1e-6)
                return Quaternion(); // identity

            Vector3 u = up.normalize();

            // If forward and up are collinear, fix up
            if (fabs(f.dot(&u)) > 0.999)
            {
                u = fabs(f.y) < 0.999 ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
            }

            Vector3 r = u.cross(f).normalize(); // right
            u = f.cross(r);                      // recompute orthogonal up

            // Rotation matrix (column-major)
            double m00 = r.x, m01 = u.x, m02 = f.x;
            double m10 = r.y, m11 = u.y, m12 = f.y;
            double m20 = r.z, m21 = u.z, m22 = f.z;

            double trace = m00 + m11 + m22;
            Quaternion q;

            if (trace > 0.0)
            {
                double s = sqrt(trace + 1.0) * 2.0;
                q.w = 0.25 * s;
                q.x = (m21 - m12) / s;
                q.y = (m02 - m20) / s;
                q.z = (m10 - m01) / s;
            }
            else if (m00 > m11 && m00 > m22)
            {
                double s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
                q.w = (m21 - m12) / s;
                q.x = 0.25 * s;
                q.y = (m01 + m10) / s;
                q.z = (m02 + m20) / s;
            }
            else if (m11 > m22)
            {
                double s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
                q.w = (m02 - m20) / s;
                q.x = (m01 + m10) / s;
                q.y = 0.25 * s;
                q.z = (m12 + m21) / s;
            }
            else
            {
                double s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
                q.w = (m10 - m01) / s;
                q.x = (m02 + m20) / s;
                q.y = (m12 + m21) / s;
                q.z = 0.25 * s;
            }

            return q.Normalized();
        }
        
        glm::quat ToGLM() const { return glm::quat((float)w, (float)x, (float)y, (float)z); }
    };
};
#endif