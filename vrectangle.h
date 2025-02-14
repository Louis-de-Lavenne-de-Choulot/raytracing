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
        std::array<Vertice *, 8> vertices = std::array<Vertice *, 8>{
            new Vertice(new Vector3(1, 1, -1),   1), // RTF right top front
            new Vertice(new Vector3(-1, 1, -1),  1), // LTF left top front
            new Vertice(new Vector3(-1, -1, -1), 1), // LBF left bottom front
            new Vertice(new Vector3(1, -1, -1),  1), // RBF right bottom front
            
            new Vertice(new Vector3(1, 1, 1),    1), // RTB right top back
            new Vertice(new Vector3(-1, 1, 1),   1), // LTB left top back
            new Vertice(new Vector3(-1, -1, 1),  1), // LBB left bottom back
            new Vertice(new Vector3(1, -1, 1),   1)  // RBB right bottom back
        };

        Vector3 *forward =  new Vector3( 0,  0, -1 );
        Vector3 *back =     new Vector3( 0,  0,  1 );
        Vector3 *top =       new Vector3( 0,  1,  0 ); 
        Vector3 *bottom =   new Vector3( 0, -1,  0 ); 
        Vector3 *right =    new Vector3( 1,  0,  0 );
        Vector3 *left =     new Vector3(-1,  0,  0 );

        std::array<Triangle *, 12> triangles = std::array<Triangle *, 12>{
            new Triangle(0, 1, 2, forward),
            new Triangle(0, 2, 3, forward),
            new Triangle(4, 5, 6, back),
            new Triangle(4, 6, 7, back),
            new Triangle(1, 2, 5, left),
            new Triangle(2, 5, 6, left),
            new Triangle(0, 3, 4, right),
            new Triangle(3, 4, 7, right),
            new Triangle(1, 4, 5, top),
            new Triangle(0, 1, 4, top),
            new Triangle(2, 6, 7, bottom),
            new Triangle(2, 3, 7, bottom)};
    };
};
#endif