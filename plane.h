#pragma once
#ifndef PLANEOBJECT
#define PLANEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vplane.h"
#include <array>
namespace PEngine
{
    struct Plane : BaseObject, VPlane
    {
        Plane(Vector3 *scale, Vector3 *position, Quaternion *rotation, Material *material);
    };
};
#endif