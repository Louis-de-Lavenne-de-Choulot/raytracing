#pragma once
#ifndef VRECTANGLEOBJECT
#define VRECTANGLEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>
struct VRectangle {
    std::array<Vertice *, 8> vertices = std::array<Vertice *, 8>{
        new Vertice(new Vector3( 1,  1,  1), 1),
        new Vertice(new Vector3(-1,  1,  1), 1),
        new Vertice(new Vector3(-1, -1,  1), 1),
        new Vertice(new Vector3( 1, -1,  1), 1),
        new Vertice(new Vector3( 1,  1, -1), 1),
        new Vertice(new Vector3(-1,  1, -1), 1),
        new Vertice(new Vector3(-1, -1, -1), 1),
        new Vertice(new Vector3( 1, -1, -1), 1)
    };

    std::array<Triangle*, 12> triangles = std::array<Triangle *, 12>{
        new Triangle(0, 1, 2),
        new Triangle(0, 2, 3),
        new Triangle(4, 0, 3),
        new Triangle(4, 3, 7),
        new Triangle(5, 4, 7),
        new Triangle(5, 7, 6),
        new Triangle(1, 5, 6),
        new Triangle(1, 6, 2),
        new Triangle(4, 5, 1),
        new Triangle(4, 1, 0),
        new Triangle(2, 6, 7),
        new Triangle(2, 7, 3)
    };
};
#endif