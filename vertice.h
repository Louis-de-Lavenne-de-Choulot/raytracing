#pragma once
#ifndef VERTICE
#define VERTICE
#include "vector3.h"
struct Vertice
{
    Vector3 *position;
    double shade;
    Vertice(Vector3 *pos, double shadeValue){
        position = pos;
        shade = shadeValue;
    }
};
#endif