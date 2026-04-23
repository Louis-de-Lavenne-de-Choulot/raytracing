#pragma once
#ifndef BASELIGHT
#define BASELIGHT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
namespace PEngine {
struct BaseLight
{
    /* data */
    double intensity;
    Color color;
    ObjectType type;

    BaseLight() : intensity(0.0), color(), type(AMBIENT_LIGHT) {}
    BaseLight(double intens, Color col)
		: intensity(intens), color(col), type(AMBIENT_LIGHT)
    {
    }
};
};
#endif