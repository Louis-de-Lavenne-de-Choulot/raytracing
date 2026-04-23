#pragma once
#ifndef VRECTANGLEOBJECT
#define VRECTANGLEOBJECT

#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>

namespace PEngine
{
    struct VRectangle
    {
        std::array<Vertice, 8> vertices = std::array<Vertice, 8>{
            Vertice(Vector3(-1, -1, -1)), // LBF - Left Bottom Front (0)
            Vertice(Vector3(1, -1, -1)), // RBF - Right Bottom Front (1)
            Vertice(Vector3(1, 1, -1)), // RTF - Right Top Front (2)
            Vertice(Vector3(-1, 1, -1)), // LTF - Left Top Front (3)

            Vertice(Vector3(-1, -1, 1)), // LBB - Left Bottom Back (4)
            Vertice(Vector3(1, -1, 1)), // RBB - Right Bottom Back (5)
            Vertice(Vector3(1, 1, 1)), // RTB - Right Top Back (6)
            Vertice(Vector3(-1, 1, 1))  // LTB - Left Top Back (7)
        };

        std::array<Triangle, 12> triangles = std::array<Triangle, 12>{
            // in CW, Front facing means in order (0,1,2), Back facing means reverse clock order (0,2,1)
            // Front Face
            Triangle(0, 1, 2), Triangle(0, 2, 3),

            // Back Face
            Triangle(4, 6, 5), Triangle(4, 7, 6),

            // Left Face
            Triangle(0, 3, 7), Triangle(0, 7, 4),

            // Right Face
            Triangle(1, 5, 6), Triangle(1, 6, 2),

            // Top Face
            Triangle(3, 2, 6), Triangle(3, 6, 7),

            // Bottom Face
            Triangle(0, 4, 5), Triangle(0, 5, 1)
        };
    };
};

#endif
