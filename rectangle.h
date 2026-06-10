#pragma once
#ifndef RECTANGLEOBJECT
#define RECTANGLEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vrectangle.h"
#include <vector>

namespace HonHengine
{
    struct Rectangle : BaseObject, VRectangle
    {
        Rectangle(Vector3 scale, Vector3 position, Quaternion rotation, Material* material,
                  int segmentsX = 1, int segmentsY = 1, int segmentsZ = 1);
    };
}
#endif
