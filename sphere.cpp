#include "sphere.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace HonHengine
{
    Sphere::Sphere(Vector3 scale, Vector3 position, Quaternion rotation,
        Material* material, int segments, int rings)
        : BaseObject(scale, position, rotation, material),
        VSphere(segments, rings, material)
    {
        std::vector<Vertice> vts(VSphere::vertices.begin(), VSphere::vertices.end());

        if (material)
        {
            Vector3 col(
                material->color.r / 255.0,
                material->color.g / 255.0,
                material->color.b / 255.0);

            for (Vertice& v : vts)
                v.vColor = col;
        }

        // Assign material to every triangle.
        for (Triangle& tri : VSphere::triangles)
            tri.material = material;

        this->setVertices(vts);
        this->setTriangles(VSphere::triangles);
        this->type = SPHERE;
    }
}
