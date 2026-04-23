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
        std::array<Vertice, 4> vertices = std::array<Vertice, 4>{
                Vertice(Vector3(-1, -1, -1)), // LBF - Left Bottom Front (0)
                Vertice(Vector3(1, -1, -1)), // RBF - Right Bottom Front (1)
                Vertice(Vector3(-1, -1, 1)), // LBB - Left Bottom Back (2)
                Vertice(Vector3(1, -1, 1)), // RBB - Right Bottom Back (3)
        };

        std::array<Triangle, 2> triangles = std::array<Triangle, 2>{
            Triangle(0, 3, 2), Triangle(0, 1, 3)
        };
    };
};
#endif