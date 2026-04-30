#pragma once
#ifndef TEAPOTOBJECT
#define TEAPOTOBJECT

#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include "vteapot.h"
#include <vector>

// ---------------------------------------------------------------------------
//  Teapot - concrete shape, follows the same multiple-inheritance pattern
//  as Sphere (BaseObject + VShape).
//
//  NOTE: basetype.h must have TEAPOT added to the ObjectType enum,
//  e.g.:   enum ObjectType { NONE, RECTANGLE, PLANE, SPHERE, TEAPOT };
// ---------------------------------------------------------------------------

namespace PEngine
{
    struct Teapot : BaseObject, VTeapot
    {
        Teapot(Vector3 scale, Vector3 position, Quaternion rotation,
            Material* material = Defaults::MissingMaterial);
    };
}

#endif