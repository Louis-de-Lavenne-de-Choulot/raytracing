#pragma once
#ifndef DIRECTIONALLIGHT
#define DIRECTIONALLIGHT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
#include "baselight.h"
struct DirectionalLight : BaseLight
{
    /* data */
    Vector3* direction;

    DirectionalLight(double intens, Color* col, Vector3* dir) : BaseLight(intens, col)
    {
        direction = dir;
        type = DIRECTIONAL_LIGHT;
    }
};
#endif