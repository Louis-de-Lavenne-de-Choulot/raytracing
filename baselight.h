#pragma once
#ifndef BASELIGHT
#define BASELIGHT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
struct BaseLight
{
    /* data */
    double intensity;
    Color* color;
    ObjectType type;

    BaseLight(double intens, Color* col)
    {
        intensity = intens;
        color = col;
        type = AMBIENT_LIGHT;
    }
};
#endif