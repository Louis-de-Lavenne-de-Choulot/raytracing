#pragma once
#ifndef PLANEOBJECT
#define PLANEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vplane.h"
#include <vector>

namespace HonHengine
{
    struct Plane : BaseObject, VPlane
    {
        Plane(Vector3 scale, Vector3 position, Quaternion rotation, Material* material,
              int segmentsX = 1, int segmentsZ = 1);
    };
}
#endif
