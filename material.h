#pragma once
#ifndef MATERIAL
#define MATERIAL
#include "color.h"
struct Material
{
    /* data */
    int specularity;
    double reflectivity;
    Color* color;
    Color* outlineColor;

    Material(int spec, double ref, Color* col, Color* outlinning = nullptr)
    {
        specularity = spec;
        reflectivity = ref;
        color = col;
        outlineColor = outlinning;
    }
};
#endif