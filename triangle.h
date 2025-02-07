#pragma once
#ifndef TRIANGLEOBJECT
#define TRIANGLEOBJECT
#include "vector3.h"
#include "vertice.h"
#include "material.h"
struct Triangle
{
    /* data */
    int p0;
    int p1;
    int p2;
    Material *material;
    Triangle(int point0, int point1, int point2, Material* mat)
    {
        p0 = point0;
        p1 = point1;
        p2 = point2;
        material = mat;
    }
    Triangle(int point0, int point1, int point2)
    {
        p0 = point0;
        p1 = point1;
        p2 = point2;
    }
};
#endif