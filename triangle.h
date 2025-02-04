#pragma once
#ifndef TRIANGLEOBJECT
#define TRIANGLEOBJECT
#include "vector3.h"
#include "vertice.h"
#include "material.h"
struct Triangle
{
    /* data */
    Vertice* p0;
    Vertice* p1;
    Vertice* p2;
    Material *material;
    Triangle(Vertice* point0, Vertice* point1, Vertice* point2, Material* mat)
    {
        p0 = point0;
        p1 = point1;
        p2 = point2;
        material = mat;
    }
};
#endif