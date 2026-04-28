#pragma once
#ifndef POINTLIGHT
#define POINTLIGHT
#include "vector3.h"
#include "quaternion.h"
#include "material.h"
#include "basetype.h"
#include "baselight.h"
namespace PEngine
{
    struct PointLight : BaseLight
    {
        /* data */
        Vector3 position;
        Quaternion rotation;

        PointLight(double intens, Color col, Vector3 pos, Quaternion rot = Quaternion()) : BaseLight(intens, col)
        {
            position = pos;
            rotation = rot;
            type = POINT_LIGHT;
        }
    };
};
#endif