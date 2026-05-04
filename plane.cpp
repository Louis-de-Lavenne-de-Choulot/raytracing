#include "plane.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace HonHengine
{
    Plane::Plane(Vector3 scale, Vector3 position, Quaternion rotation, Material* material)
        : BaseObject(scale, position, rotation, material)
    {
        std::vector<Vertice>  vts(this->vertices.begin(), this->vertices.end());
        std::vector<Triangle> trs(this->triangles.begin(), this->triangles.end());

        if (material)
        {
            Vector3 col(
                material->color.r / 255.0,
                material->color.g / 255.0,
                material->color.b / 255.0);

            for (Vertice& v : vts)
                v.vColor = col;
        }

        for (Triangle& triangle : trs)
            triangle.material = material;

        this->setVertices(vts);
        this->setTriangles(trs);
        type = PLANE;
    }
};
