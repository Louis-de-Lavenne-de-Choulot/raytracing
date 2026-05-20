#pragma once
#ifndef RENDERCOMPONENT
#define RENDERCOMPONENT

#include "baseobject.h"
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
#include <glad/glad.h>

namespace HonHengine
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
    //  GPU Vertex Layout Constants (shared with GPURenderer)
    // ─────────────────────────────────────────────────────────────────────────
    struct GPUVertexLayout
    {
        static constexpr size_t POSITION_OFFSET = 0;   // 12 bytes (3 floats)
        static constexpr size_t NORMAL_OFFSET = 12;  // 12 bytes (3 floats)
        static constexpr size_t COLOR_OFFSET = 24;  // 12 bytes (3 floats: r,g,b)
        static constexpr size_t UV_OFFSET = 36;  // 8 bytes (2 floats)
        static constexpr size_t VERTEX_STRIDE = 44;  // total size in bytes
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

        // ── Flag indicating whether this component should use material colour ──
        // True for primitives (plane, sphere, rectangle), false for imported meshes
        // that have per-vertex colours.
        bool useMaterialColor = true;

        // ── Helpers ───────────────────────────────────────────────────────────

        // Call this with any tightly-packed vertex struct to fill vertexBytes.
        template<typename VT>
        void setMesh(const std::vector<VT>& verts,
            const std::vector<unsigned int>& idx)
        {
            vertexBytes.resize(verts.size() * sizeof(VT));
            std::memcpy(vertexBytes.data(), verts.data(), vertexBytes.size());
            indices = idx;
            vertexStride = sizeof(VT);
            _uploaded = false;  // Force re-upload if mesh changes
        }

        // Update the RGB colour values of all vertices in the vertex buffer.
        // This modifies the CPU-side vertexBytes and re-uploads to the GPU.
        void updateVertexColors(const Color& newColor) {
            if (!_uploaded || vertexBytes.empty()) return;
            if (!useMaterialColor) return;  // Preserve per-vertex colours

            // Only support the standard GPUVertex layout for now
            if (vertexStride != GPUVertexLayout::VERTEX_STRIDE) return;

            const size_t colorOffset = GPUVertexLayout::COLOR_OFFSET;
            const size_t stride = vertexStride;
            const float r = static_cast<float>(newColor.r) / 255.0f;
            const float g = static_cast<float>(newColor.g) / 255.0f;
            const float b = static_cast<float>(newColor.b) / 255.0f;

            for (size_t i = 0; i < vertexBytes.size(); i += stride) {
                float* rgb = reinterpret_cast<float*>(vertexBytes.data() + i + colorOffset);
                rgb[0] = r;
                rgb[1] = g;
                rgb[2] = b;
            }

            // Re-upload the modified vertex data to the GPU
            if (_vao.vao != 0 && _vao.vbo != 0) {
                glBindVertexArray(_vao.vao);
                glBindBuffer(GL_ARRAY_BUFFER, _vao.vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes.size(), vertexBytes.data());
                glBindVertexArray(0);
            }
        }

        // Force re-upload of vertex data without modifying colours.
        // Used when the VAO was created but the buffer needs refreshing.
        void refreshGPUData() {
            if (!_uploaded || vertexBytes.empty()) return;
            if (_vao.vao != 0 && _vao.vbo != 0) {
                glBindVertexArray(_vao.vao);
                glBindBuffer(GL_ARRAY_BUFFER, _vao.vbo);
                glBufferSubData(GL_ARRAY_BUFFER, 0, vertexBytes.size(), vertexBytes.data());
                glBindVertexArray(0);
            }
        }

        bool isValid() const
        {
            return !shaderName.empty()
                && vertexStride > 0
                && !layout.empty()
                && !vertexBytes.empty();
        }
    };

}; // namespace HonHengine

#endif // RENDERCOMPONENT