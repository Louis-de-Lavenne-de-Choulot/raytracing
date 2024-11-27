#pragma once
#ifndef CAMERAOBJECT
#define CAMERAOBJECT
#include "vector3.h"
#include "basetype.h"
struct Camera
{
    /* data */
    Vector3* position;
    Vector3* rotation;
    ObjectType type;

    Camera(Vector3* position = new Vector3(0, 0, 0), Vector3* rotation = new Vector3(0, 0, 0));
};
#endif