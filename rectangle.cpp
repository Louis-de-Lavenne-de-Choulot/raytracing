
#include "rectangle.h"
#include "baseobject.h"
#include "basetype.h"
#include <cfloat>
#include <cmath>
#include <iostream>

Rectangle::Rectangle(double scale, Vector3 *position, Vector3 *rotation, Material *material) : BaseObject(position, rotation, material)
{
    radius = scale;
    type = RECTANGLE;
    // init arrays 12 elms
    currentVertices.fill(nullptr);
    currentTriangles.fill(nullptr);
}

void Rectangle::UpdateVertices()
{
    double x = position->x;
    double y = position->y;
    double z = position->z;

    // Define the 8 vertices based on the radius
    currentVertices[0] = new Vertice(new Vector3(x + radius, y + radius, z + radius), 1); // + + +
    currentVertices[1] = new Vertice(new Vector3(x - radius, y + radius, z + radius), 1); // + + -
    currentVertices[2] = new Vertice(new Vector3(x - radius, y - radius, z + radius), 1); // + - +
    currentVertices[3] = new Vertice(new Vector3(x + radius, y - radius, z + radius), 1); // + - -
    currentVertices[4] = new Vertice(new Vector3(x + radius, y + radius, z - radius), 1); // - + +
    currentVertices[5] = new Vertice(new Vector3(x - radius, y + radius, z - radius), 1); // - + -
    currentVertices[6] = new Vertice(new Vector3(x - radius, y - radius, z - radius), 1); // - - +
    currentVertices[7] = new Vertice(new Vector3(x + radius, y - radius, z - radius), 1); // - - -

    prevRad = radius; // Update the previous radius to the current one
}

std::array<Triangle*, 12> Rectangle::GetTriangles(){
    if (prevRad != radius) {
        UpdateVertices();
        currentTriangles[0] =  new Triangle(currentVertices[0], currentVertices[1], currentVertices[2], material);
        currentTriangles[1] =  new Triangle(currentVertices[0], currentVertices[2], currentVertices[3], material);
        currentTriangles[2] =  new Triangle(currentVertices[4], currentVertices[0], currentVertices[3], material);
        currentTriangles[3] =  new Triangle(currentVertices[4], currentVertices[3], currentVertices[7], material);
        currentTriangles[4] =  new Triangle(currentVertices[5], currentVertices[4], currentVertices[7], material);
        currentTriangles[5] =  new Triangle(currentVertices[5], currentVertices[7], currentVertices[6], material);
        currentTriangles[6] =  new Triangle(currentVertices[1], currentVertices[5], currentVertices[6], material);
        currentTriangles[7] =  new Triangle(currentVertices[1], currentVertices[6], currentVertices[2], material);
        currentTriangles[8] =  new Triangle(currentVertices[4], currentVertices[5], currentVertices[1], material);
        currentTriangles[9] =  new Triangle(currentVertices[4], currentVertices[1], currentVertices[0], material);
        currentTriangles[10] = new Triangle(currentVertices[2], currentVertices[6], currentVertices[7], material);
        currentTriangles[11] = new Triangle(currentVertices[2], currentVertices[7], currentVertices[3], material);
        prevRad = radius;
    }
    return currentTriangles;
}