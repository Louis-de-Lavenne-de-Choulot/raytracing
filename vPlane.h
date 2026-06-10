#pragma once
#ifndef VPLANEOBJECT
#define VPLANEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <vector>

namespace HonHengine
{
    // Flat horizontal plane facing +Y, subdivided into (segmentsX * segmentsZ) quads
    // (each quad = 2 triangles).  A 1x1 grid reproduces the original 2-triangle plane.
    //
    // Vertices are laid out in a (segmentsX+1) * (segmentsZ+1) grid spanning [-1,1]
    // on both the X and Z axes, with Y fixed at -1.
    struct VPlane
    {
        std::vector<Vertice>  vertices;
        std::vector<Triangle> triangles;

        VPlane(int segmentsX = 1, int segmentsZ = 1,
               Material* mat = Defaults::MissingMaterial)
        {
            const int cols = segmentsX + 1; // vertex columns
            const int rows = segmentsZ + 1; // vertex rows

            vertices.reserve(cols * rows);
            triangles.reserve(segmentsX * segmentsZ * 2);

            // Generate vertices row by row (Z axis) then column by column (X axis)
            for (int row = 0; row < rows; ++row)
            {
                float z = -1.0f + 2.0f * float(row) / float(segmentsZ);
                for (int col = 0; col < cols; ++col)
                {
                    float x = -1.0f + 2.0f * float(col) / float(segmentsX);
                    Vector3 pos(x, -1.0f, z);
                    Vector3 norm(0.0f, 1.0f, 0.0f);
                    vertices.push_back(Vertice(pos, norm, Vector3(1, 1, 1)));
                }
            }

            // Generate 2 CW triangles per quad
            //
            //  row+1 : tl --- tr
            //           |  \  |
            //  row   : bl --- br
            //
            //  Tri A (CW from +Y): bl, br, tl
            //  Tri B (CW from +Y): br, tr, tl
            for (int row = 0; row < segmentsZ; ++row)
            {
                for (int col = 0; col < segmentsX; ++col)
                {
                    int bl = row * cols + col;
                    int br = bl + 1;
                    int tl = bl + cols;
                    int tr = tl + 1;

                    triangles.push_back(Triangle(bl, br, tl, mat));
                    triangles.push_back(Triangle(br, tr, tl, mat));
                }
            }
        }
    };
}
#endif
