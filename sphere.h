#pragma once
#ifndef SPHEREOBJECT
#define SPHEREOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vsphere.h"
#include <vector>

namespace HonHengine
{
    struct Sphere : BaseObject, VSphere
    {
        Sphere(Vector3 scale, Vector3 position, Quaternion rotation, Material* material, int segments = 20, int rings = 20);
    };
};
#endif