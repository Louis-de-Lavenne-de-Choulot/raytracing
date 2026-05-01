#pragma once
#ifndef SKINNEDVERTEX_H
#define SKINNEDVERTEX_H

// ─────────────────────────────────────────────────────────────────────────────
//  skinnedVertex.h
//
//  Standard vertex layout for skinned (animated) meshes.
//  Matches the attribute locations expected by the skinning shader in
//  skinnedShader.h.
//
//  Attribute slots:
//    location 0  — position    (vec3)
//    location 1  — normal      (vec3)
//    location 2  — uv          (vec2)
//    location 3  — boneIndices (uvec4)  ← integer attribute, glVertexAttribIPointer
//    location 4  — boneWeights (vec4)
//
//  IMPORTANT — integer vs float attribute paths
//  ─────────────────────────────────────────────
//  OpenGL has two separate VAO attribute upload functions:
//
//    glVertexAttribPointer  → feeds float/normalised attributes  → GLSL vec/mat
//    glVertexAttribIPointer → feeds raw integer attributes       → GLSL ivec/uvec
//
//  These are NOT interchangeable.  boneIdx stores joint indices as raw bytes;
//  it MUST go through glVertexAttribIPointer and be declared as `uvec4` in the
//  shader.  If glVertexAttribPointer is used by mistake the byte values are
//  normalised into [0,1] (e.g. index 2 → 0.00784) and every ivec4() cast
//  rounds to 0, permanently binding all vertices to bone 0.
//
//  The engine's createVAO chooses the upload path based on VertexDataType:
//    • Float, HalfFloat, … → glVertexAttribPointer
//    • UnsignedByte (with normalized=false) → glVertexAttribIPointer  ← this slot
//
//  Usage with RenderComponent:
//
//    std::vector<SkinnedVertex> verts = myImporter.loadVerts(...);
//    std::vector<unsigned int>  idx   = myImporter.loadIndices(...);
//
//    obj->render.shaderName   = "skinned";
//    obj->render.vertexStride = sizeof(SkinnedVertex);
//    obj->render.layout       = SkinnedVertex::layout();
//    obj->render.setMesh(verts, idx);
//
// ─────────────────────────────────────────────────────────────────────────────

#include "shaderLibrary.h"   // VAOLayout, VertexDataType
#include <cstdint>
#include <vector>

namespace HonHengine
{
    struct SkinnedVertex
    {
        // ── Geometry ──────────────────────────────────────────────────────────
        float   px, py, pz;        // position
        float   nx, ny, nz;        // normal
        float   u, v;              // UV

        // ── Skin weights ──────────────────────────────────────────────────────
        // Up to MAX_BONE_INFLUENCES (4) influences per vertex.
        // boneIdx[k] is an index into the skeleton's bone array.
        // boneWgt[k] is the contribution weight; all four weights must sum to 1.
        // Unused influences should have idx=0, wgt=0.
        uint8_t boneIdx[4] = { 0, 0, 0, 0 };
        float   boneWgt[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

        // ── Convenience constructor ───────────────────────────────────────────
        SkinnedVertex() = default;

        SkinnedVertex(float px, float py, float pz,
            float nx, float ny, float nz,
            float u, float v)
            : px(px), py(py), pz(pz)
            , nx(nx), ny(ny), nz(nz)
            , u(u), v(v)
        {
        }

        // Attach one bone influence (call up to 4 times, then normalise).
        void addInfluence(uint8_t boneIndex, float weight)
        {
            for (int i = 0; i < 4; ++i)
            {
                if (boneWgt[i] == 0.0f)
                {
                    boneIdx[i] = boneIndex;
                    boneWgt[i] = weight;
                    return;
                }
            }
            // All 4 slots full — replace the weakest if this influence is stronger.
            int weakest = 0;
            for (int i = 1; i < 4; ++i)
                if (boneWgt[i] < boneWgt[weakest]) weakest = i;
            if (weight > boneWgt[weakest])
            {
                boneIdx[weakest] = boneIndex;
                boneWgt[weakest] = weight;
            }
        }

        // Renormalise weights so they sum to 1. Call after all addInfluence().
        void normaliseWeights()
        {
            float sum = boneWgt[0] + boneWgt[1] + boneWgt[2] + boneWgt[3];
            if (sum > 1e-6f)
                for (float& w : boneWgt) w /= sum;
            else
            {
                // Degenerate — bind entirely to bone 0.
                boneIdx[0] = 0; boneWgt[0] = 1.0f;
                boneIdx[1] = 0; boneWgt[1] = 0.0f;
                boneIdx[2] = 0; boneWgt[2] = 0.0f;
                boneIdx[3] = 0; boneWgt[3] = 0.0f;
            }
        }

        // ── Canonical VAO layout — pass to RenderComponent::layout ────────────
        //
        //  boneIdx uses VertexDataType::UnsignedByte with normalized=false.
        //  The engine's createVAO implementation MUST route this through
        //  glVertexAttribIPointer (not glVertexAttribPointer) so that the raw
        //  byte values reach the shader as integers.  The shader declares this
        //  attribute as `uvec4` — see skinnedShader.h.
        static std::vector<VAOLayout> layout()
        {
            return {
                { 0, 3, VertexDataType::Float,        false, offsetof(SkinnedVertex, px)      }, // position
                { 1, 3, VertexDataType::Float,        false, offsetof(SkinnedVertex, nx)      }, // normal
                { 2, 2, VertexDataType::Float,        false, offsetof(SkinnedVertex, u)       }, // uv
                { 3, 4, VertexDataType::UnsignedByte, false, offsetof(SkinnedVertex, boneIdx) }, // bone indices (integer)
                { 4, 4, VertexDataType::Float,        false, offsetof(SkinnedVertex, boneWgt) }, // bone weights
            };
        }
    };

} // namespace HonHengine

#endif // SKINNEDVERTEX_H