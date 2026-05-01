#pragma once
#ifndef VPLANEOBJECT
#define VPLANEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>
namespace HonHengine
{
    // Flat horizontal plane facing +Y.  All four vertices share the same normal.
    struct VPlane
    {
        std::array<Vertice, 4> vertices = std::array<Vertice, 4>{
            Vertice(Vector3(-1, -1, -1), Vector3(0, 1, 0)), // 0 LBF
            Vertice(Vector3(1, -1, -1), Vector3(0, 1, 0)), // 1 RBF
            Vertice(Vector3(-1, -1,  1), Vector3(0, 1, 0)), // 2 LBB
            Vertice(Vector3(1, -1,  1), Vector3(0, 1, 0)), // 3 RBB
        };

        std::array<Triangle, 2> triangles = std::array<Triangle, 2>{
            Triangle(0, 3, 2), Triangle(0, 1, 3)
        };
    };
};
#endif