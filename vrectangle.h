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
        std::array<Vertice*, 8> vertices = std::array<Vertice*, 8>{
            new Vertice(new Vector3(-1, -1, -1)), // LBF - Left Bottom Front (0)
            new Vertice(new Vector3(1, -1, -1)), // RBF - Right Bottom Front (1)
            new Vertice(new Vector3(1, 1, -1)), // RTF - Right Top Front (2)
            new Vertice(new Vector3(-1, 1, -1)), // LTF - Left Top Front (3)

            new Vertice(new Vector3(-1, -1, 1)), // LBB - Left Bottom Back (4)
            new Vertice(new Vector3(1, -1, 1)), // RBB - Right Bottom Back (5)
            new Vertice(new Vector3(1, 1, 1)), // RTB - Right Top Back (6)
            new Vertice(new Vector3(-1, 1, 1))  // LTB - Left Top Back (7)
        };

        std::array<Triangle*, 12> triangles = std::array<Triangle*, 12>{
            // in CW, Front facing means in order (0,1,2), Back facing means reverse clock order (0,2,1)
            // Front Face
            new Triangle(0, 1, 2), new Triangle(0, 2, 3),

            // Back Face
            new Triangle(4, 6, 5), new Triangle(4, 7, 6),

            // Left Face
            new Triangle(0, 3, 7), new Triangle(0, 7, 4),

            // Right Face
            new Triangle(1, 5, 6), new Triangle(1, 6, 2),

            // Top Face
            new Triangle(3, 2, 6), new Triangle(3, 6, 7),

            // Bottom Face
            new Triangle(0, 4, 5), new Triangle(0, 5, 1)
        };
    };
};

#endif
