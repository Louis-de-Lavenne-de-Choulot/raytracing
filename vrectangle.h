#pragma once
#ifndef VRECTANGLEOBJECT
#define VRECTANGLEOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <vector>

namespace HonHengine
{
    // Box subdivided per-face.  Each axis pair controls one pair of opposite faces:
    //   segmentsX / segmentsY  →  Front & Back  faces  (XY plane)
    //   segmentsZ / segmentsY  →  Left  & Right faces  (ZY plane)
    //   segmentsX / segmentsZ  →  Top   & Bottom faces (XZ plane)
    //
    // Passing all three as 1 reproduces the original 12-triangle box exactly.
    // Winding is CW (GL_CW front-face), normals are per-face constants.
    struct VRectangle
    {
        std::vector<Vertice>  vertices;
        std::vector<Triangle> triangles;

        VRectangle(int segmentsX = 1, int segmentsY = 1, int segmentsZ = 1,
            Material* mat = Defaults::MissingMaterial)
        {
            // Helper: build one subdivided quad-patch and append its vertices/triangles.
            //
            //  origin  – bottom-left corner of the patch in 3-D space
            //  uAxis   – direction of "column" steps  (maps 0..1 → left..right)
            //  vAxis   – direction of "row"    steps  (maps 0..1 → bottom..top)
            //  normal  – constant outward normal for every vertex on this face
            //  uSegs / vSegs – subdivision counts along uAxis / vAxis
            auto addFace = [&](Vector3 origin,
                Vector3 uAxis, Vector3 vAxis,
                Vector3 normal,
                int uSegs, int vSegs)
                {
                    const int uCols = uSegs + 1;
                    const int vRows = vSegs + 1;
                    const int base = static_cast<int>(vertices.size());

                    // Vertices
                    for (int row = 0; row < vRows; ++row)
                    {
                        float v = float(row) / float(vSegs);
                        for (int col = 0; col < uCols; ++col)
                        {
                            float u = float(col) / float(uSegs);
                            Vector3 pos = origin + uAxis * u + vAxis * v;
                            vertices.push_back(Vertice(pos, normal, Vector3(1, 1, 1)));
                        }
                    }

                    // Triangles  (CCW from the outward-normal side)
                    //
                    //  row+1 : tl --- tr
                    //           |  /  |
                    //  row   : bl --- br
                    //
                    //  CCW from outside:  bl, br, tl  and  tl, br, tr
                    for (int row = 0; row < vSegs; ++row)
                    {
                        for (int col = 0; col < uSegs; ++col)
                        {
                            int bl = base + row * uCols + col;
                            int br = bl + 1;
                            int tl = bl + uCols;
                            int tr = tl + 1;

                            triangles.push_back(Triangle(bl, br, tl, mat));
                            triangles.push_back(Triangle(tl, br, tr, mat));
                        }
                    }
                };

            // ── 6 faces, each spanning [-1, 1] on its two tangent axes ──────────
            //   addFace(origin, uAxis * 2, vAxis * 2, normal, uSegs, vSegs)

            // Front  (-Z)  origin bottom-left = (-1,-1,-1), u→+X, v→+Y
            addFace(Vector3(-1, -1, -1), Vector3(2, 0, 0), Vector3(0, 2, 0),
                Vector3(0, 0, -1), segmentsX, segmentsY);

            // Back   (+Z)  origin bottom-left = (+1,-1,+1), u→-X, v→+Y
            addFace(Vector3(1, -1, 1), Vector3(-2, 0, 0), Vector3(0, 2, 0),
                Vector3(0, 0, 1), segmentsX, segmentsY);

            // Left   (-X)  origin bottom-left = (-1,-1,+1), u→-Z, v→+Y
            addFace(Vector3(-1, -1, 1), Vector3(0, 0, -2), Vector3(0, 2, 0),
                Vector3(-1, 0, 0), segmentsZ, segmentsY);

            // Right  (+X)  origin bottom-left = (+1,-1,-1), u→+Z, v→+Y
            addFace(Vector3(1, -1, -1), Vector3(0, 0, 2), Vector3(0, 2, 0),
                Vector3(1, 0, 0), segmentsZ, segmentsY);

            // Top    (+Y)  origin bottom-left = (-1,+1,-1), u→+X, v→+Z
            addFace(Vector3(-1, 1, -1), Vector3(2, 0, 0), Vector3(0, 0, 2),
                Vector3(0, 1, 0), segmentsX, segmentsZ);

            // Bottom (-Y)  origin bottom-left = (-1,-1,+1), u→+X, v→-Z
            addFace(Vector3(-1, -1, 1), Vector3(2, 0, 0), Vector3(0, 0, -2),
                Vector3(0, -1, 0), segmentsX, segmentsZ);
        }
    };
}
#endif