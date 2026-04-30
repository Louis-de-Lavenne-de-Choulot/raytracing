#pragma once
#ifndef SHADERLIBRARY_H
#define SHADERLIBRARY_H

#include <glad/glad.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstddef>   // offsetof

namespace PEngine
{
    enum class ShaderType { Opaque = 0, Transparent = 1, UI = 2 };

    // Abstracted Buffer Properties
    enum class VertexDataType { Float, Int, UnsignedByte };
    enum class BufferUsage { Static, Dynamic, Stream };

    struct VAOLayout
    {
        unsigned int   index;
        int            size;
        VertexDataType type;        // Use the abstract enum
        bool           normalized;
        size_t         offset;
    };

    struct ManagedVAO
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;

        template<typename VT>
        void upload(const std::vector<VT>& verts, BufferUsage usage = BufferUsage::Static) const
        {
            GLenum glUsage = (usage == BufferUsage::Static) ? GL_STATIC_DRAW : (usage == BufferUsage::Dynamic) ? GL_DYNAMIC_DRAW : GL_STREAM_DRAW;
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(VT)), verts.data(), glUsage);
            glBindVertexArray(0);
        }

        template<typename VT, typename IT>
        void upload(const std::vector<VT>& verts, const std::vector<IT>& indices, BufferUsage usage = BufferUsage::Static) const
        {
            GLenum glUsage = (usage == BufferUsage::Static) ? GL_STATIC_DRAW : (usage == BufferUsage::Dynamic) ? GL_DYNAMIC_DRAW : GL_STREAM_DRAW;
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(VT)), verts.data(), glUsage);
            if (ebo != 0)
            {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(IT)), indices.data(), glUsage);
            }
            glBindVertexArray(0);
        }

        void destroy()
        {
            if (ebo) { glDeleteBuffers(1, &ebo); ebo = 0; }
            if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
            if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    class ShaderLibrary
    {
    public:
        // ── Built-in pipeline shaders ─────────────────────────────────────────
        void   build();
        GLuint get(ShaderType type) const;

        // ── Named custom shaders ──────────────────────────────────────────────
        // Compile + link a custom shader and store it under `name`.
        // If a shader with that name already exists it is replaced.
        // Returns the program ID (0 on error).
        GLuint AddShader(const std::string& name,
            const char* vertSrc,
            const char* fragSrc);

        // Retrieve a previously added named shader (0 if not found).
        GLuint GetShader(const std::string& name) const;

        // ── Generalised VAO factory ───────────────────────────────────────────
        // Creates a VAO + VBO (and optionally an EBO) with the given layout.
        // The returned ManagedVAO is owned and freed by ShaderLibrary::clear().
        // Pass withEBO=true for indexed draw calls.
        ManagedVAO createVAO(GLsizei                      vertexStride,
            const std::vector<VAOLayout>& layout,
            bool                         withEBO = false);

        // ── Lifecycle ─────────────────────────────────────────────────────────
        void clear();

    private:
        GLuint compile(GLenum type, const char* src);
        GLuint link(GLuint vert, GLuint frag);

        std::unordered_map<int, GLuint>         programs;   // built-in by ShaderType
        std::unordered_map<std::string, GLuint> named;      // custom named shaders
        std::vector<ManagedVAO>                 managedVAOs;
    };

} // namespace PEngine

#endif // SHADERLIBRARY_H