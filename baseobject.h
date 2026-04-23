#pragma once
#ifndef BASEOBJECT
#define BASEOBJECT
#include "vector3.h"
#include "material.h"
#include "vertice.h"
#include "triangle.h"
#include "basetype.h"
#include "transform.h"
#include "quaternion.h"
#include <vector>
namespace PEngine
{
    struct BaseObject
    {
        /* data */
        PEngine::Transform transform;
        Material *material;
        std::vector<Vertice> bVertices;
        std::vector<Triangle> bTriangles;
        ObjectType type;
        BaseObject(Vector3 scale, Vector3 pos, Quaternion rot, Material *mat)
        {
            transform = PEngine::Transform(scale, pos, rot);
            material = mat;
            type = NONE;
        }

        void setVertices(std::vector<Vertice> vertices)
        {
            this->bVertices = vertices;
        }

        void setTriangles(std::vector<Triangle> triangles)
        {
            this->bTriangles = triangles;
        }
    };
};
#endif