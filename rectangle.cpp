
#include "rectangle.h"
#include "baseobject.h"
#include "basetype.h"
#include "quaternion.h"
#include <vector>
namespace PEngine
{
    Rectangle::Rectangle(Vector3 *scale, Vector3 *position, Quaternion *rotation, Material *material) : BaseObject(scale, position, rotation, material)
    {
        std::vector<Vertice *> vts = std::vector<Vertice *>(this->vertices.begin(), this->vertices.end());
        std::vector<Triangle *> trs = std::vector<Triangle *>(this->triangles.begin(), this->triangles.end());

        Color *r = new Color(190, 50, 50, 255);
        Color *g = new Color(50, 190, 50, 255);
        Color *b = new Color(50, 50, 190, 255);
        Color *a = new Color(150, 150, 10, 255);
        Color *z = new Color(150, 50, 190, 255);
        Color *c = new Color(150, 50, 190, 255);
        Color *arr[] = {r, g, b, a, z, c};
        // foreach triangle, add material
        int i = -1;
        int inc = 0;
        for (Triangle *triangle : trs)
        {
            if (inc % 2 == 0)
            {
                i++;
            }
            Material *mat = new Material(0, 0, arr[i]);
            triangle->material = mat;
            inc++;
        }
        this->setVertices(vts);
        this->setTriangles(trs);
        type = RECTANGLE;
    }
}