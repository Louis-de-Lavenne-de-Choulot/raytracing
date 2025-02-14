#pragma once
#ifndef CAMERAOBJECT
#define CAMERAOBJECT
#include "vector3.h"
#include "basetype.h"
#include "transform.h"
namespace PEngine
{
    struct Camera
    {
        /* data */
        Transform *transform;
        double fov;
        ObjectType type;

        Camera(Vector3 *position = new Vector3(0, 0, 0), Quaternion *rotation = new Quaternion(1, 0, 0, 0), double fov = 90);
    };
};
#endif