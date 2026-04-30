#include "sphere.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace PEngine
{
    Sphere::Sphere(Vector3 scale, Vector3 position, Quaternion rotation,
        Material* material, int segments, int rings)
        : BaseObject(scale, position, rotation, material),
        VSphere(segments, rings, material)
    {
        // Assign material to every triangle.
        for (Triangle& tri : VSphere::triangles)
            tri.material = material;

        this->setVertices(VSphere::vertices);
        this->setTriangles(VSphere::triangles);
        this->type = SPHERE;
    }
}