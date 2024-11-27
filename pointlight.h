#pragma once
#ifndef POINTLIGHT
#define POINTLIGHT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
#include "baselight.h"
struct PointLight : BaseLight
{
    /* data */
    Vector3* position;

    PointLight(double intens, Color* col, Vector3* pos) : BaseLight(intens, col)
    {
        position = pos;
        type = POINT_LIGHT;
    }
};
#endif