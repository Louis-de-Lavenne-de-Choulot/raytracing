#pragma once
#ifndef QUATERNION
#define QUATERNION
#include "eulerangle.h"
#include "vector3.h"
#include <iostream>
#include <cmath>
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

    Quaternion Conjugate(Quaternion q){
        return Quaternion(q.w, -q.x, -q.y, -q.z);
    } 

    Vector3 *RotateVector3(Vector3 *v)
    {
        Quaternion qv = Quaternion(0, v->x, v->y, v->z);
        Quaternion qConj = Conjugate(*this);
        Quaternion qRes = *this * qv * qConj;
        return new Vector3(qRes.x, qRes.y, qRes.z);
    }
};

#endif