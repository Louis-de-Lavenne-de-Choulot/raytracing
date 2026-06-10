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
#include "shaderLibrary.h"   // ManagedVAO, VAOLayout, BufferUsage, VertexDataType
#include "animation.h"       // AnimatorComponent
#include "renderComponent.h" // RenderComponent
#include "script_component.h"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>           // std::memcpy

namespace HonHengine
{

    // ─────────────────────────────────────────────────────────────────────────
    //  BaseObject
    // ─────────────────────────────────────────────────────────────────────────
    struct BaseObject
    {
        virtual ~BaseObject() = default;

        HonHengine::Transform        transform;
        Material* material = nullptr;
        std::vector<Vertice>      bVertices;
        std::vector<Triangle>     bTriangles;
        std::vector<ScriptComponent> scripts;
        ObjectType                type = NONE;
        std::string               tag;
        bool                      visible = true;

        // ── Custom-shader rendering component ─────────────────────────────────
        // If render.isValid() is true the GPURenderer will use this instead of
        // the built-in triangle-soup pipeline for this object.
        RenderComponent           render;

        // ── Skeletal animation component ──────────────────────────────────────
        // If animator.active() is true the renderer will:
        //   1. Call animator.update(dt) each frame before drawing.
        //   2. Upload animator.bonePalette → uBonePalette[128].
        //   3. Set uSkinned = 1 in the shader.
        // If render is valid but animator is not active, uSkinned = 0
        // (static mesh path — the skinned shader handles both cases).
        AnimatorComponent         animator;

        // ── Constructors ──────────────────────────────────────────────────────
        BaseObject() = default;

        BaseObject(Vector3 scale, Vector3 pos, Quaternion rot, Material* mat)
        {
            transform = HonHengine::Transform(scale, pos, rot);
            material = mat;
            type = NONE;
        }

        void setVertices(std::vector<Vertice> vertices) { bVertices = std::move(vertices); }
        void setTriangles(std::vector<Triangle> triangles) { bTriangles = std::move(triangles); }

        void refreshVertexColors() {
            if (!material) return;

            Vector3 col(
                material->color.r / 255.0,
                material->color.g / 255.0,
                material->color.b / 255.0);

            for (Vertice& v : bVertices) {
                v.vColor = col;
            }
        }
    };

} // namespace HonHengine

#endif // BASEOBJECT