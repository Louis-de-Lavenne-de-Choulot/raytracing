#pragma once
#include "vector3.h"
#include "quaternion.h"

namespace PEngine {
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
            Vector3 tempF(forward());
			Vector3 temp(Quaternion().Rotate(&tempF, &absUP, -90));
            return temp;
        }

        Vector3 right() {
            Vector3 tempF(forward());
            Vector3 temp(Quaternion().Rotate(&tempF, &absUP, 90));
            return temp;
        }


        void Rotate(Vector3 axis, double angle) {
            Quaternion incremental;
            // Assuming Rotate returns/modifies appropriately
            incremental.Rotate(&position, &axis, angle);
            rotation *= incremental;
        }
    };
}