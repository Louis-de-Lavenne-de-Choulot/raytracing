#pragma once
#ifndef VERTICE
#define VERTICE
#include "vector3.h"
namespace PEngine
{
    struct Vertice
    {
        Vector3 *position;
        double shade;
        Vertice(Vector3 *pos, double shadeValue = 1)
        {
            position = pos;
            shade = shadeValue;
        }
    };
};
#endif