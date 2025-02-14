#pragma once
#ifndef RECTANGLEOBJECT
#define RECTANGLEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vrectangle.h"
#include <array>
namespace PEngine
{
    struct Rectangle : BaseObject, VRectangle
    {
        Rectangle(Vector3 *scale, Vector3 *position, Quaternion *rotation, Material *material);
    };
};
#endif