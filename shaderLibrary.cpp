#include "shaderLibrary.h"
#include <iostream>
#include <unordered_set>
#include <string>

namespace HonHengine
{
    // ─────────────────────────────────────────────────────────────────────────
    //  WORLD_VERT: Standard vertex pass-through with world positions for masking
    // ─────────────────────────────────────────────────────────────────────────
    static const char* WORLD_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    vWorldPos = aPos;
    vNormal   = normalize(aNormal);
    vColor    = aColor;
    vUV       = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

    // ─────────────────────────────────────────────────────────────────────────
    //  WORLD_FRAG: The core of the multi-layer blending system
    // ─────────────────────────────────────────────────────────────────────────
    static const char* WORLD_FRAG = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;

out vec4 FragColor;

#define MAX_LIGHTS 64
#define MAX_TEX_LAYERS 8

// --- Lighting Uniforms ---
uniform int   uLightCount;
uniform int   uLightType[MAX_LIGHTS];
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightIntensity[MAX_LIGHTS];
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];

// --- Texture Layer Uniforms ---
uniform int       uLayerCount;
uniform sampler2D uTexLayer[MAX_TEX_LAYERS];
uniform vec4      uLayerTiling[MAX_TEX_LAYERS];   // (tilingU, tilingV, offsetU, offsetV)
uniform int       uLayerBlendMode[MAX_TEX_LAYERS]; // 0=Mix, 1=Multiply, 2=Add, 3=Overlay
uniform float     uLayerWeight[MAX_TEX_LAYERS];
uniform int       uLayerMaskType[MAX_TEX_LAYERS];  // 0=None, 1=Height, 2=Slope, 3=VtxCol
uniform float     uLayerMaskMin[MAX_TEX_LAYERS];
uniform float     uLayerMaskMax[MAX_TEX_LAYERS];
uniform int       uLayerMaskChannel[MAX_TEX_LAYERS]; // 0=R, 1=G, 2=B, 3=A
uniform int       uLayerMaskInvert[MAX_TEX_LAYERS];

uniform float     uAlpha;

// Helper: Calculate the mask weight for a specific layer
float getLayerMask(int i, vec3 N)
{
    float mask = 1.0;
    
    if (uLayerMaskType[i] == 1) // HeightBased
    {
        mask = clamp((vWorldPos.y - uLayerMaskMin[i]) / (uLayerMaskMax[i] - uLayerMaskMin[i]), 0.0, 1.0);
    }
    else if (uLayerMaskType[i] == 2) // SlopeBased
    {
        float slope = clamp(dot(N, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
        mask = clamp((slope - uLayerMaskMin[i]) / (uLayerMaskMax[i] - uLayerMaskMin[i]), 0.0, 1.0);
    }
    else if (uLayerMaskType[i] == 3) // VertexColor
    {
        mask = (uLayerMaskChannel[i] == 0) ? vColor.r :
               (uLayerMaskChannel[i] == 1) ? vColor.g :
               (uLayerMaskChannel[i] == 2) ? vColor.b : 1.0;
    }

    return (uLayerMaskInvert[i] == 1) ? (1.0 - mask) : mask;
}

// Helper: Composite a layer onto the base color
vec3 blendLayers(vec3 base, vec3 layer, float weight, int mode)
{
    if (mode == 1) return base * mix(vec3(1.0), layer, weight); // Multiply
    if (mode == 2) return base + (layer * weight);              // Add
    if (mode == 3) // Overlay
    {
        vec3 check = step(vec3(0.5), base);
        vec3 result = mix(2.0 * base * layer, 1.0 - 2.0 * (1.0 - base) * (1.0 - layer), check);
        return mix(base, result, weight);
    }
    return mix(base, layer, weight); // Mix/Standard
}

vec3 hemisphereAmbient(vec3 N)
{
    vec3 skyCol    = vec3(0.53, 0.75, 1.00);
    vec3 groundCol = vec3(0.20, 0.15, 0.10);
    float t = N.y * 0.5 + 0.5;
    return mix(groundCol, skyCol, t) * 0.15;
}

void main()
{
    vec3 N = normalize(vNormal);
    
    // 1. Process Texture Layers
    vec4 texAcc = vec4(1.0);
    if (uLayerCount > 0)
    {
        // Sample Base Layer (Layer 0)
        vec2 uv0 = vUV * uLayerTiling[0].xy + uLayerTiling[0].zw;
        texAcc = texture(uTexLayer[0], uv0);
        
        // Blend subsequent layers
        for (int i = 1; i < uLayerCount; ++i)
        {
            vec2 uv = vUV * uLayerTiling[i].xy + uLayerTiling[i].zw;
            vec4 layerSample = texture(uTexLayer[i], uv);
            float weight = uLayerWeight[i] * getLayerMask(i, N);
            
            texAcc.rgb = blendLayers(texAcc.rgb, layerSample.rgb, weight * layerSample.a, uLayerBlendMode[i]);
        }
    }

    // 2. Lighting Calculation
    vec3 acc = hemisphereAmbient(N);
    vec3 spec = vec3(0.0);

    for (int i = 0; i < uLightCount; ++i)
    {
        vec3  lc     = uLightColor[i];
        float intens = uLightIntensity[i];

        if (uLightType[i] == 0) // Ambient
        {
            acc += lc * intens * (N.y * 0.3 + 0.7);
        }
        else if (uLightType[i] == 1) // Point
        {
            vec3 L = normalize(uLightPos[i] - vWorldPos);
            float wrap = max((dot(N, L) + 0.3) / 1.3, 0.0);
            acc += lc * intens * wrap;
            
            vec3 H = normalize(L + normalize(-vWorldPos));
            spec += lc * intens * pow(max(dot(N, H), 0.0), 32.0) * 0.25;
        }
        else if (uLightType[i] == 2) // Directional
        {
            vec3 L = normalize(-uLightDir[i]);
            float wrap = max((dot(N, L) + 0.25) / 1.25, 0.0);
            acc += lc * intens * wrap;
            
            vec3 H = normalize(L + normalize(vec3(6.0, 4.0, 6.0) - vWorldPos));
            spec += lc * intens * pow(max(dot(N, H), 0.0), 48.0) * 0.35;
        }
    }

    vec3 finalColor = (acc * texAcc.rgb * vColor) + spec;
    FragColor = vec4(finalColor, texAcc.a * uAlpha);
}
)GLSL";

    // ─────────────────────────────────────────────────────────────────────────
    //  UI Shaders
    // ─────────────────────────────────────────────────────────────────────────
    static const char* UI_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
out vec2 vUV;
out vec4 vColor;
uniform mat4 uOrtho;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
}
)GLSL";

    static const char* UI_FRAG = R"GLSL(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uUIAtlas;
uniform bool uUseUIAtlas;
void main() {
    vec4 tex = uUseUIAtlas ? texture(uUIAtlas, vUV) : vec4(1.0);
    FragColor = vColor * tex;
}
)GLSL";

    // ─────────────────────────────────────────────────────────────────────────
    //  Internal compile / link helpers
    // ─────────────────────────────────────────────────────────────────────────
    GLuint ShaderLibrary::compile(GLenum type, const char* src)
    {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &src, nullptr);
        glCompileShader(id);
        GLint ok = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(id, sizeof(log), nullptr, log);
            std::cerr << "[ShaderLibrary] Compile error:\n" << log << "\n";
            glDeleteShader(id);
            return 0;
        }
        return id;
    }

    GLuint ShaderLibrary::link(GLuint vert, GLuint frag)
    {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert);
        glAttachShader(prog, frag);
        glLinkProgram(prog);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            std::cerr << "[ShaderLibrary] Link error:\n" << log << "\n";
            glDeleteProgram(prog);
            return 0;
        }
        glDeleteShader(vert);
        glDeleteShader(frag);
        return prog;
    }

    void ShaderLibrary::build()
    {
        programs[static_cast<int>(ShaderType::Opaque)] = link(compile(GL_VERTEX_SHADER, WORLD_VERT), compile(GL_FRAGMENT_SHADER, WORLD_FRAG));
        programs[static_cast<int>(ShaderType::Transparent)] = link(compile(GL_VERTEX_SHADER, WORLD_VERT), compile(GL_FRAGMENT_SHADER, WORLD_FRAG));
        programs[static_cast<int>(ShaderType::UI)] = link(compile(GL_VERTEX_SHADER, UI_VERT), compile(GL_FRAGMENT_SHADER, UI_FRAG));
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  get  — retrieve a built-in program by type
    // ─────────────────────────────────────────────────────────────────────────
    GLuint ShaderLibrary::get(ShaderType type) const
    {
        auto it = programs.find(static_cast<int>(type));
        return it != programs.end() ? it->second : 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  AddShader  — compile + store a named custom shader
    //
    //  Usage:
    //    GLuint prog = renderer.shaderLib.AddShader("beach", BEACH_VERT, BEACH_FRAG);
    //
    //  If a shader already exists under that name its GL program is deleted
    //  first and replaced with the newly compiled one.
    // ─────────────────────────────────────────────────────────────────────────
    GLuint ShaderLibrary::AddShader(const std::string& name,
        const char* vertSrc,
        const char* fragSrc)
    {
        // Delete any pre-existing program under this name
        auto it = named.find(name);
        if (it != named.end())
        {
            glDeleteProgram(it->second);
            named.erase(it);
        }

        GLuint vert = compile(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (!vert || !frag)
        {
            if (vert) glDeleteShader(vert);
            if (frag) glDeleteShader(frag);
            std::cerr << "[ShaderLibrary] AddShader(\"" << name << "\") failed.\n";
            return 0;
        }

        GLuint prog = link(vert, frag);   // link() deletes both shaders on success
        if (!prog) return 0;

        named[name] = prog;
        std::cout << "[ShaderLibrary] Added shader \"" << name << "\" (prog=" << prog << ")\n";
        return prog;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  GetShader  — retrieve a previously added named shader
    // ─────────────────────────────────────────────────────────────────────────
    GLuint ShaderLibrary::GetShader(const std::string& name) const
    {
        auto it = named.find(name);
        return (it != named.end()) ? it->second : 0;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  createVAO  — generalised VAO + VBO (+ optional EBO) factory
    //
    //  All created objects are tracked internally and freed by clear().
    //
    //  Example — beach mesh (pos + normal + uv, indexed):
    //
    //    ManagedVAO bVAO = renderer.shaderLib.createVAO(
    //        sizeof(BeachVert),
    //        {
    //            { 0, 3, GL_FLOAT, GL_FALSE, offsetof(BeachVert, px) },
    //            { 1, 3, GL_FLOAT, GL_FALSE, offsetof(BeachVert, nx) },
    //            { 2, 2, GL_FLOAT, GL_FALSE, offsetof(BeachVert, u)  },
    //        }, true);
    //    bVAO.upload(beachVerts, beachIdx);
    // ─────────────────────────────────────────────────────────────────────────
    ManagedVAO ShaderLibrary::createVAO(GLsizei                       vertexStride,
        const std::vector<VAOLayout>& layout,
        bool                          withEBO)
    {
        ManagedVAO mv;

        glGenVertexArrays(1, &mv.vao);
        glGenBuffers(1, &mv.vbo);
        if (withEBO) glGenBuffers(1, &mv.ebo);

        glBindVertexArray(mv.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mv.vbo);

        if (withEBO)
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mv.ebo);

        for (const VAOLayout& attr : layout)
        {
            GLenum glType = GL_FLOAT;
            if (attr.type == VertexDataType::Int)         glType = GL_INT;
            else if (attr.type == VertexDataType::UnsignedByte) glType = GL_UNSIGNED_BYTE;

            glEnableVertexAttribArray(attr.index);

            // Integer attribute types (Int, UnsignedByte) that are NOT
            // normalised must be submitted via glVertexAttribIPointer so the
            // shader can read them as exact integers (ivec / uvec).
            // Using glVertexAttribPointer for these would silently convert them
            // to floats, breaking bone index lookups in the skinning shader.
            bool isIntegerType = (attr.type == VertexDataType::Int ||
                attr.type == VertexDataType::UnsignedByte);

            if (isIntegerType && !attr.normalized)
            {
                glVertexAttribIPointer(
                    attr.index,
                    attr.size,
                    glType,
                    vertexStride,
                    reinterpret_cast<const void*>(attr.offset));
            }
            else
            {
                glVertexAttribPointer(
                    attr.index,
                    attr.size,
                    glType,
                    attr.normalized ? GL_TRUE : GL_FALSE,
                    vertexStride,
                    reinterpret_cast<const void*>(attr.offset));
            }
        }

        glBindVertexArray(0);

        managedVAOs.push_back(mv);
        return mv;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  clear  — free every GL resource owned by this library
    // ─────────────────────────────────────────────────────────────────────────
    void ShaderLibrary::clear()
    {
        // Built-in programs (may alias — use a set to avoid double-delete)
        std::unordered_set<GLuint> deleted;
        for (auto& [key, prog] : programs)
            if (deleted.insert(prog).second)
                glDeleteProgram(prog);
        programs.clear();

        // Named custom programs
        for (auto& [name, prog] : named)
            glDeleteProgram(prog);
        named.clear();

        // Managed VAOs
        for (auto& mv : managedVAOs)
            mv.destroy();
        managedVAOs.clear();
    }
}