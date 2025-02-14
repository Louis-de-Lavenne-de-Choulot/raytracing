#pragma once
#ifndef VPLANEOBJECT
#define VPLANEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>
namespace PEngine
{
    struct VPlane
    {
        std::array<Vertice *, 4> vertices = std::array<Vertice *, 4>{
            new Vertice(new Vector3(1, 1, 1), 1),
            new Vertice(new Vector3(-1, 1, 1), 1),
            new Vertice(new Vector3(-1, -1, 1), 1),
            new Vertice(new Vector3(1, -1, 1), 1)};

        Vector3 *forward = new Vector3(0, 0, -1);

        std::array<Triangle *, 2> triangles = std::array<Triangle *, 2>{
            new Triangle(0, 1, 2, forward),
            new Triangle(0, 2, 3, forward)};
    };
};
#endif