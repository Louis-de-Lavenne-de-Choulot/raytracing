#include "sphere.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace PEngine
{
    Sphere::Sphere(Vector3* scale, Vector3* position, Quaternion* rotation, Material* material, int segments, int rings) 
        : BaseObject(scale, position, rotation, material), VSphere(segments, rings)
    {
        Color* colors[] = {
            new Color(190, 50, 50, 255),   // Red
            new Color(50, 190, 50, 255),   // Green
            new Color(50, 50, 190, 255),   // Blue
            new Color(150, 150, 10, 255),  // Yellow
            new Color(150, 50, 190, 255),  // Purple
            new Color(50, 150, 190, 255)   // Cyan
        };

        // Apply materials to triangles
        int colorIndex = 0;
        for (size_t i = 0; i < triangles.size(); i++)
        {
            if (i % 2 == 0)
            {
                colorIndex = (colorIndex + 1) % 6;
            }
            Material* mat = new Material(0, 0, colors[colorIndex]);
            triangles[i]->material = mat;
        }

        this->setVertices(vertices);
        this->setTriangles(triangles);
        type = SPHERE;
    }
}