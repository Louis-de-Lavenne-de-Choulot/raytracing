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

    Material(int spec, double ref, Color* col)
    {
        specularity = spec;
        reflectivity = ref;
        color = col;
    }
};
#endif