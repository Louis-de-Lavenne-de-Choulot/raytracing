#pragma once
#ifndef POINTLIGHT
#define POINTLIGHT
#include "vector3.h"
#include "quaternion.h"
#include "material.h"
#include "basetype.h"
#include "baselight.h"
namespace HonHengine
{
    struct PointLight : BaseLight
    {
        /* data */
		Transform transform;

        PointLight(double intens, Color col, Vector3 pos, Quaternion rot = Quaternion()) : BaseLight(intens, col)
        {
            transform.position = pos;
            transform.rotation = rot;
            type = POINT_LIGHT;
        }
    };
};
#endif