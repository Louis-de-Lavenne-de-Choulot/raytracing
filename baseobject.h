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

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>           // std::memcpy

namespace PEngine
{
    // ─────────────────────────────────────────────────────────────────────────
    //  TextureBinding
    //  Associates a shader sampler name with a texture key known to
    //  TextureManager, bound to a specific texture unit slot.
    // ─────────────────────────────────────────────────────────────────────────
    struct TextureBinding
    {
        std::string uniformName;   // e.g. "uSandTex"
        std::string textureName;   // key used with texManager.load(...)
        int         slot = 0;      // GL_TEXTURE0 + slot
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  RenderComponent
    //
    //  Everything the GPU renderer needs to draw this object with a custom
    //  shader, independently of the built-in triangle-soup pipeline.
    //
    //  Workflow (set up once in your scene, before the loop):
    //
    //    obj->render.shaderName  = "beach";
    //    obj->render.vertexStride = sizeof(BeachVert);
    //    obj->render.layout       = { {0,3,Float,false,offsetof(...)}, ... };
    //    obj->render.textures     = { {"uSandTex", "sand", 0} };
    //    obj->render.depthWrite   = true;
    //    obj->render.blend        = false;
    //    obj->render.cullFace     = true;
    //    obj->render.setMesh(vertices, indices);   // templated, any vertex type
    //
    //  The renderer will compile/link the shader (already done via
    //  renderer.RegisterShader), lazy-upload the VAO on the first frame,
    //  and issue a draw call automatically inside Render().
    // ─────────────────────────────────────────────────────────────────────────
    struct RenderComponent
    {
        // ── Shader ───────────────────────────────────────────────────────────
        std::string shaderName;          // must match a name passed to RegisterShader()

        // ── Geometry layout (mirrors shaderLibrary.h createVAO params) ───────
        GLsizei                  vertexStride = 0;
        std::vector<VAOLayout>   layout;

        // ── Raw byte copies of vertex / index data ────────────────────────────
        // Stored as raw bytes so RenderComponent is vertex-type-agnostic.
        std::vector<uint8_t>     vertexBytes;
        std::vector<uint32_t>    indices;

        // ── Textures ──────────────────────────────────────────────────────────
        std::vector<TextureBinding> textures;

        // ── Render state ──────────────────────────────────────────────────────
        bool depthWrite = true;
        bool blend = false;
        bool cullFace = true;

        // ── Runtime handle (managed by GPURenderer — do not set manually) ─────
        ManagedVAO  _vao;
        bool        _uploaded = false;   // true after first-frame GPU upload
        size_t      _indexCount = 0;

        // ── Helpers ───────────────────────────────────────────────────────────

        // Call this with any tightly-packed vertex struct to fill vertexBytes.
        template<typename VT>
        void setMesh(const std::vector<VT>& verts,
            const std::vector<unsigned int>& idx)
        {
            vertexBytes.resize(verts.size() * sizeof(VT));
            std::memcpy(vertexBytes.data(), verts.data(), vertexBytes.size());
            indices = idx;
        }

        bool isValid() const
        {
            return !shaderName.empty()
                && vertexStride > 0
                && !layout.empty()
                && !vertexBytes.empty();
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  BaseObject
    // ─────────────────────────────────────────────────────────────────────────
    struct BaseObject
    {
        // ── Core data (legacy pipeline) ───────────────────────────────────────
        PEngine::Transform        transform;
        Material* material = nullptr;
        std::vector<Vertice>      bVertices;
        std::vector<Triangle>     bTriangles;
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
            transform = PEngine::Transform(scale, pos, rot);
            material = mat;
            type = NONE;
        }

        void setVertices(std::vector<Vertice> vertices) { bVertices = std::move(vertices); }
        void setTriangles(std::vector<Triangle> triangles) { bTriangles = std::move(triangles); }
    };

} // namespace PEngine

#endif // BASEOBJECT