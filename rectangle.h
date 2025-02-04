#pragma once
#ifndef RECTANGLEOBJECT
#define RECTANGLEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>
struct Rectangle : BaseObject{
    double radius;
    double prevRad;
    std::array<Vertice*, 8> currentVertices;
    std::array<Triangle*, 12> currentTriangles;
    Rectangle(double radius, Vector3* position, Vector3* rotation, Material* material);
    void UpdateVertices();
    std::array<Triangle*, 12> GetTriangles();
};
#endif