#pragma once
#ifndef DIRECTIONALLIGHT
#define DIRECTIONALLIGHT
#include "vector3.h"
#include "material.h"
#include "basetype.h"
#include "baselight.h"
namespace HonHengine
{
    struct DirectionalLight : BaseLight
    {
        /* data */
		Transform transform = Transform();

		DirectionalLight() : BaseLight() {
			type = DIRECTIONAL_LIGHT;
		}
        DirectionalLight(double intens, Color col, Vector3 pos = Vector3(), Quaternion rot = Quaternion()) : BaseLight(intens, col)
        {
			transform.position = pos;
			transform.rotation = rot;
            type = DIRECTIONAL_LIGHT;
        }
    };
};
#endif