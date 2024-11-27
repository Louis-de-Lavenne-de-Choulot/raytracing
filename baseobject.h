#pragma once
#ifndef BASEOBJECT
#define BASEOBJECT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
struct BaseObject
{
    /* data */
    Vector3* position;
    Vector3* rotation;
    Material* material;
    ObjectType type;
    BaseObject(Vector3* pos, Vector3* rot, Material* mat)
    {
        position = pos;
        rotation = rot;
        material = mat;
        type = NONE;
    }
};
#endif