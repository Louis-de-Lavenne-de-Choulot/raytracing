#pragma once
#ifndef TRIANGLEOBJECT
#define TRIANGLEOBJECT
#include "vector3.h"
#include "vertice.h"
#include "material.h"
#include "defaults.h"
namespace PEngine
{
    struct Triangle
    {
        int p0;
        int p1;
        int p2;
        Material* material;
        float uv[3][2];

        Triangle(int point0, int point1, int point2, Material* mat)
        {
            p0 = point0;
            p1 = point1;
            p2 = point2;
            material = mat;
            uv[0][0] = 0.0f; uv[0][1] = 0.0f;
            uv[1][0] = 0.0f; uv[1][1] = 0.0f;
            uv[2][0] = 0.0f; uv[2][1] = 0.0f;
        }

        Triangle(int point0, int point1, int point2)
        {
            p0 = point0;
            p1 = point1;
            p2 = point2;
            material = Defaults::MissingMaterial;
            uv[0][0] = 0.0f; uv[0][1] = 0.0f;
            uv[1][0] = 0.0f; uv[1][1] = 0.0f;
            uv[2][0] = 0.0f; uv[2][1] = 0.0f;
        }

        Triangle(int point0, int point1, int point2, Material* mat,
            float u0, float v0, float u1, float v1, float u2, float v2)
        {
            p0 = point0;
            p1 = point1;
            p2 = point2;
            material = mat;
            uv[0][0] = u0; uv[0][1] = v0;
            uv[1][0] = u1; uv[1][1] = v1;
            uv[2][0] = u2; uv[2][1] = v2;
        }
    };
};
#endif