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

namespace PEngine
{
    struct VSphere
    {
        std::vector<Vertice> vertices;
        std::vector<Triangle> triangles;
        Vector3* dir = new Vector3(0, 0, 1); // TODO

        VSphere(int segments = 20, int rings = 20, Material *mat = Defaults::MissingMaterial)
        {
            // Generate vertices
            for (int ring = 0; ring <= rings; ring++)
            {
                float phi = std::numbers::pi * float(ring) / float(rings);
                for (int segment = 0; segment <= segments; segment++)
                {
                    float theta = 2.0f * std::numbers::pi * float(segment) / float(segments);
                    
                    float x = std::sin(phi) * std::cos(theta);
                    float y = std::cos(phi);
                    float z = std::sin(phi) * std::sin(theta);

                    vertices.push_back(Vertice(Vector3(x, y, z)));
                }
            }

            // Generate triangles
            for (int ring = 0; ring < rings; ring++)
            {
                int ringStart = ring * (segments + 1);
                int nextRingStart = (ring + 1) * (segments + 1);

                for (int segment = 0; segment < segments; segment++)
                {
                    if (ring != 0)
                    {
                        triangles.push_back(Triangle(
                            ringStart + segment,
                            ringStart + segment + 1,
                            nextRingStart + segment,
                            mat
                        ));
                    }

                    if (ring != rings - 1)
                    {
                        triangles.push_back(Triangle(
                            nextRingStart + segment,
                            ringStart + segment + 1,
                            nextRingStart + segment + 1,
                            mat
                        ));
                    }
                }
            }
        }
    };
};
#endif