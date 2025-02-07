#pragma once
#ifndef CAMERAOBJECT
#define CAMERAOBJECT
#include "vector3.h"
#include "basetype.h"
#include "transform.h"
struct Camera
{
    /* data */
    Transform *transform;
    ObjectType type;

    Camera(Vector3* position = new Vector3(0, 0, 0), Quaternion* rotation = new Quaternion(1, 0, 0, 0));
};
#endif