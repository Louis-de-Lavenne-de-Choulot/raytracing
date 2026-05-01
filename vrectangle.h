#pragma once
#ifndef VRECTANGLEOBJECT
#define VRECTANGLEOBJECT

#include "baseobject.h"
#include "basetype.h"
#include "vertice.h"
#include "triangle.h"
#include <array>

namespace HonHengine
{
    // A box needs 24 vertices (4 per face × 6 faces) so every vertex can carry
    // a constant per-face normal.
    //
    // Winding matches the original CW convention (GL_CW front-face).
    // Vertex index layout:
    //   Front  (-Z) : 0-3    Back   (+Z) : 4-7
    //   Left   (-X) : 8-11   Right  (+X) : 12-15
    //   Left   (-X) : 8-11   Right  (+X) : 12-15
    //   Top    (+Y) : 16-19  Bottom (-Y) : 20-23

    struct VRectangle
    {
        std::array<Vertice, 24> vertices = std::array<Vertice, 24>{
            // ── Front face  (normal  0, 0,-1) ──────────────────────────────
            Vertice(Vector3(-1, -1, -1), Vector3(0,  0, -1)), // 0  LBF
            Vertice(Vector3(1, -1, -1), Vector3(0,  0, -1)), // 1  RBF
            Vertice(Vector3(1,  1, -1), Vector3(0,  0, -1)), // 2  RTF
            Vertice(Vector3(-1,  1, -1), Vector3(0,  0, -1)), // 3  LTF

            // ── Back face   (normal  0, 0,+1) ──────────────────────────────
            Vertice(Vector3(-1, -1,  1), Vector3(0,  0,  1)), // 4  LBB
            Vertice(Vector3(1, -1,  1), Vector3(0,  0,  1)), // 5  RBB
            Vertice(Vector3(1,  1,  1), Vector3(0,  0,  1)), // 6  RTB
            Vertice(Vector3(-1,  1,  1), Vector3(0,  0,  1)), // 7  LTB

            // ── Left face   (normal -1, 0, 0) ──────────────────────────────
            Vertice(Vector3(-1, -1, -1), Vector3(-1,  0,  0)), // 8
            Vertice(Vector3(-1,  1, -1), Vector3(-1,  0,  0)), // 9
            Vertice(Vector3(-1,  1,  1), Vector3(-1,  0,  0)), // 10
            Vertice(Vector3(-1, -1,  1), Vector3(-1,  0,  0)), // 11

            // ── Right face  (normal +1, 0, 0) ──────────────────────────────
            Vertice(Vector3(1, -1, -1), Vector3(1,  0,  0)), // 12
            Vertice(Vector3(1, -1,  1), Vector3(1,  0,  0)), // 13
            Vertice(Vector3(1,  1,  1), Vector3(1,  0,  0)), // 14
            Vertice(Vector3(1,  1, -1), Vector3(1,  0,  0)), // 15

            // ── Top face    (normal  0,+1, 0) ──────────────────────────────
            Vertice(Vector3(-1,  1, -1), Vector3(0,  1,  0)), // 16
            Vertice(Vector3(1,  1, -1), Vector3(0,  1,  0)), // 17
            Vertice(Vector3(1,  1,  1), Vector3(0,  1,  0)), // 18
            Vertice(Vector3(-1,  1,  1), Vector3(0,  1,  0)), // 19

            // ── Bottom face (normal  0,-1, 0) ──────────────────────────────
            Vertice(Vector3(-1, -1, -1), Vector3(0, -1,  0)), // 20
            Vertice(Vector3(-1, -1,  1), Vector3(0, -1,  0)), // 21
            Vertice(Vector3(1, -1,  1), Vector3(0, -1,  0)), // 22
            Vertice(Vector3(1, -1, -1), Vector3(0, -1,  0)), // 23
        };

        std::array<Triangle, 12> triangles = std::array<Triangle, 12>{
            // CW winding - same convention as the original VRectangle.

            // Front  (-Z)
            Triangle(0,  1,  2), Triangle(0,  2,  3),
            // Back   (+Z)
            Triangle(4,  6,  5), Triangle(4,  7,  6),
            // Left   (-X)
            Triangle(8,  9, 10), Triangle(8, 10, 11),
            // Right  (+X)
            Triangle(12, 13, 14), Triangle(12, 14, 15),
            // Top    (+Y)
            Triangle(16, 17, 18), Triangle(16, 18, 19),
            // Bottom (-Y)
            Triangle(20, 21, 22), Triangle(20, 22, 23),
        };
    };
};

#endif