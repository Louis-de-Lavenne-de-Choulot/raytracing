#include "plane.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace HonHengine
{
    Plane::Plane(Vector3 scale, Vector3 position, Quaternion rotation, Material* material,
                 int segmentsX, int segmentsZ)
        : BaseObject(scale, position, rotation, material),
          VPlane(segmentsX, segmentsZ, material)
    {
        std::vector<Vertice> vts(VPlane::vertices.begin(), VPlane::vertices.end());

        if (material)
        {
            Vector3 col(
                material->color.r / 255.0,
                material->color.g / 255.0,
                material->color.b / 255.0);

            for (Vertice& v : vts)
                v.vColor = col;
        }

        for (Triangle& triangle : VPlane::triangles)
            triangle.material = material;

        this->setVertices(vts);
        this->setTriangles(VPlane::triangles);
        type = PLANE;
    }
}
