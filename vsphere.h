#pragma once
#ifndef VSPHEREOBJECT
#define VSPHEREOBJECT
#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <vector>
#include <cmath>
#include <numbers>

namespace HonHengine
{
    struct VSphere
    {
        std::vector<Vertice>  vertices;
        std::vector<Triangle> triangles;
        Vector3* dir = new Vector3(0, 0, 1);

        VSphere(int segments = 20, int rings = 20, Material* mat = Defaults::MissingMaterial)
        {
            // Generate vertices - for a unit sphere the position vector IS the
            // outward normal, so we pass it as both pos and norm.
            // White vColor by default; Sphere::Sphere() will overwrite with material color.
            for (int ring = 0; ring <= rings; ring++)
            {
                float phi = std::numbers::pi * float(ring) / float(rings);
                for (int segment = 0; segment <= segments; segment++)
                {
                    float theta = 2.0f * std::numbers::pi * float(segment) / float(segments);

                    float x = std::sin(phi) * std::cos(theta);
                    float y = std::cos(phi);
                    float z = std::sin(phi) * std::sin(theta);

                    Vector3 pos(x, y, z);
                    vertices.push_back(Vertice(pos, pos, Vector3(1, 1, 1))); // pos, norm, col
                }
            }

            // Generate triangles with consistent CLOCKWISE winding
            for (int ring = 0; ring < rings; ring++)
            {
                int ringStart = ring * (segments + 1);
                int nextRingStart = (ring + 1) * (segments + 1);

                for (int segment = 0; segment < segments; segment++)
                {
                    if (ring != 0)
                    {
                        // CW winding when viewed from outside
                        triangles.push_back(Triangle(
                            ringStart + segment,           // bottom-left
                            nextRingStart + segment,       // top-left
                            ringStart + segment + 1,       // bottom-right
                            mat));
                    }

                    if (ring != rings - 1)
                    {
                        // CW winding when viewed from outside
                        triangles.push_back(Triangle(
                            nextRingStart + segment,       // top-left
                            nextRingStart + segment + 1,   // top-right
                            ringStart + segment + 1,       // bottom-right
                            mat));
                    }
                }
            }
        }
    };
};
#endif