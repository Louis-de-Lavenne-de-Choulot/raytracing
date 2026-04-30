#include "plane.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace PEngine
{
    Plane::Plane(Vector3 scale, Vector3 position, Quaternion rotation, Material* material)
        : BaseObject(scale, position, rotation, material)
    {
        std::vector<Vertice>  vts(this->vertices.begin(), this->vertices.end());
        std::vector<Triangle> trs(this->triangles.begin(), this->triangles.end());

        // Must iterate by reference
        for (Triangle& triangle : trs)
            triangle.material = material;

        this->setVertices(vts);
        this->setTriangles(trs);
        type = PLANE;
    }
};