#include "sphere.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace PEngine
{
    Sphere::Sphere(Vector3 scale, Vector3 position, Quaternion rotation, Material* material, int segments, int rings)
        : BaseObject(scale, position, rotation, material), VSphere(segments, rings, material)
    {
        Color colors[] = {
            Color(190, 50, 50, 255), Color(50, 190, 50, 255), Color(50, 50, 190, 255),
            Color(150, 150, 10, 255), Color(150, 50, 190, 255), Color(50, 150, 190, 255)
        };

        // 1. Force l'utilisation des triangles de VSphere explicitement
        int colorIndex = 0;
        for (size_t i = 0; i < VSphere::triangles.size(); i++)
        {
            if (i % 2 == 0) {
                colorIndex = (colorIndex + 1) % 6;
            }
            // On assigne le matériau directement dans le vecteur de VSphere
            VSphere::triangles[i].material = new Material(0, 0, colors[colorIndex]);
        }

        // 2. On synchronise BaseObject avec les données de VSphere qui viennent d'être modifiées
        this->setVertices(VSphere::vertices);
        this->setTriangles(VSphere::triangles);

        this->type = SPHERE;
    }
}