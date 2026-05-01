#pragma once
#include "vector3.h"
#include "quaternion.h"

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
    };
}