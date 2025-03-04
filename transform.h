#pragma once
#ifndef TRANSFORM
#define TRANSFORM
#include "vector3.h"
#include "material.h"
#include "basetype.h"
#include "quaternion.h"
#include <cmath>
#include <numbers>
namespace PEngine
{
    struct Transform
    {
        /* data */
        Vector3 *scale;
        Vector3 *position;
        Quaternion *rotation = new Quaternion();
        Vector3 *forwardP() { return rotation->RotateVector3(new Vector3(0, 0, 1)); };
        Vector3 forward() { return *forwardP(); };
        Vector3 left() { return Quaternion().Rotate(forwardP(), new Vector3(1, 0, 0), -90); };
        Vector3 right() { return Quaternion().Rotate(forwardP(), new Vector3(1, 0, 0), 90); };
        const Vector3 *absRight = new Vector3(1, 0, 0);
        const Vector3 *absUP = new Vector3(0, 1, 0);
        const Vector3 *absForward = new Vector3(0, 0, 1);


        Transform(Vector3 *scale, Vector3 *position, Quaternion *rotation)
        {
            this->scale = scale;
            this->position = position;
            this->rotation = rotation;
        }

        Vector3 RotateTo(Vector3 *axis, double angle)
        {
            // Return the rotated vector
            return this->rotation->Rotate(this->position, axis, angle);
        }

        void Rotate(Vector3* axis, double angle)
        {
            Quaternion temp = Quaternion();
            temp.Rotate(this->position, axis, angle);
            // Return the rotated vector
            *this->rotation *= temp;
        }
    };
};
#endif