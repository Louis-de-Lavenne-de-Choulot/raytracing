#pragma once
#ifndef VERTICE
#define VERTICE
#include "vector3.h"

namespace PEngine
{
    struct Vertice
    {
        Vector3 position;
        Vector3 normal;
        Vector3 vColor;
        double  shade;

        Vertice() : position(Vector3(0, 0, 0)), normal(Vector3(0, 0, 0)),
            vColor(Vector3(1, 1, 1)), shade(1) {
        }

        Vertice(Vector3 pos, Vector3 col = Vector3(1, 1, 1), double shadeValue = 1)
            : position(pos), normal(Vector3(0, 0, 0)), vColor(col), shade(shadeValue)
        {
        }

        Vertice(Vector3 pos, Vector3 norm, Vector3 col, double shadeValue = 1)
            : position(pos), normal(norm), vColor(col), shade(shadeValue)
        {
        }
    };
};
#endif