#include "rectangle.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>

namespace HonHengine
{
    Rectangle::Rectangle(Vector3 scale, Vector3 position, Quaternion rotation, Material* material,
                         int segmentsX, int segmentsY, int segmentsZ)
        : BaseObject(scale, position, rotation, material),
          VRectangle(segmentsX, segmentsY, segmentsZ, material)
    {
        std::vector<Vertice> vts(VRectangle::vertices.begin(), VRectangle::vertices.end());

        if (material)
        {
            Vector3 col(
                material->color.r / 255.0,
                material->color.g / 255.0,
                material->color.b / 255.0);

            for (Vertice& v : vts)
                v.vColor = col;
        }

        for (Triangle& tri : VRectangle::triangles)
            tri.material = material;

        this->setVertices(vts);
        this->setTriangles(VRectangle::triangles);
        type = RECTANGLE;
    }
}
