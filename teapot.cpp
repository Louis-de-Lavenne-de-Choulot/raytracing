#include "teapot.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace PEngine
{
    Teapot::Teapot(Vector3 scale, Vector3 position, Quaternion rotation, Material* material)
        : BaseObject(scale, position, rotation, material), VTeapot(material)
    {
        Color colors[] = {
            Color(190, 50, 50, 255), Color(50, 190, 50, 255), Color(50, 50, 190, 255),
            Color(150, 150, 10, 255), Color(150, 50, 190, 255), Color(50, 150, 190, 255)
        };

        // 1. Force l'utilisation des triangles de VTeapot explicitement
        int colorIndex = 0;
        for (size_t i = 0; i < VTeapot::triangles.size(); i++)
        {
            if (i % 2 == 0) {
                colorIndex = (colorIndex + 1) % 6;
            }
            // On assigne le matériau directement dans le vecteur de VTeapot
            VTeapot::triangles[i].material = new Material(0, 0, colors[colorIndex]);
        }

        this->setVertices(VTeapot::vertices);
        this->setTriangles(VTeapot::triangles);

        this->type = TEAPOT; // requires TEAPOT in the ObjectType enum in basetype.h
    }
}