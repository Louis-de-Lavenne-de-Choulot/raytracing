#include "rectangle.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>
namespace PEngine
{
    Rectangle::Rectangle(Vector3 scale, Vector3 position, Quaternion rotation, Material *material) : BaseObject(scale, position, rotation, material)
    {
        std::vector<Vertice> vts = std::vector<Vertice>(this->vertices.begin(), this->vertices.end());
        std::vector<Triangle> trs = std::vector<Triangle>(this->triangles.begin(), this->triangles.end());

        // foreach triangle, add material
        int i = -1;
        int inc = 0;
        for (Triangle &triangle : trs)
        {
            if (inc % 2 == 0)
            {
                i++;
            }
            triangle.material = material;
            inc++;
        }
        this->setVertices(vts);
        this->setTriangles(trs);
        type = RECTANGLE;
    }
}