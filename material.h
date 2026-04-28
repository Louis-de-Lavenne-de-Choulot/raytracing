#pragma once
#ifndef MATERIAL
#define MATERIAL
#include "color.h"
namespace PEngine
{
    struct Material
    {
        /* data */
        double specularity;
        double reflectivity;
        Color color;
        Color outlineColor;

		Material() : specularity(0), reflectivity(0), color(), outlineColor() {}
        Material(double spec, double ref, Color col, Color outlinning = Color(0, 0, 0, 255))
        {
            specularity = spec;
            reflectivity = ref;
            color = col;
            outlineColor = outlinning;
        }
    };
};
#endif