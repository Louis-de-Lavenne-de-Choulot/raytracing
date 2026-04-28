#pragma once
#ifndef VERTICEDTO
#define VERTICEDTO
#include "vector3.h"
#include "vertice.h"
#include "color.h"          // <-- THIS WAS MISSING
namespace PEngine
{
    struct VerticeDTO : public Vertice
    {
        Vector3 worldPos;
        Vector3 normalPos;
        Color color;
        VerticeDTO(Vector3 pos, Vector3 worldP, Vector3 normal, double shd)
            : Vertice(pos, shd), worldPos(worldP), normalPos(normal)  // shd not shade
        {
            color = Color(0, 0, 0, 255);
        }
        VerticeDTO(Vector3 pos, Vector3 worldP, Vector3 normal, Color col)
            : Vertice(pos, 0), worldPos(worldP), normalPos(normal), color(col) 
        {
        }
    };
};
#endif