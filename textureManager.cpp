#include "textureManager.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <algorithm>   // std::clamp

namespace PEngine
{
    // ─────────────────────────────────────────────────────────────────────────
    //  Internal helpers: convert our enums to GL constants
    // ─────────────────────────────────────────────────────────────────────────

    static GLenum wrapModeToGL(WrapMode m)
    {
        switch (m)
        {
        case WrapMode::MirroredRepeat:  return GL_MIRRORED_REPEAT;
        case WrapMode::ClampToEdge:     return GL_CLAMP_TO_EDGE;
        case WrapMode::ClampToBorder:   return GL_CLAMP_TO_BORDER;
        case WrapMode::Repeat:
        default:                        return GL_REPEAT;
        }
    }

    static GLenum filterModeMin(FilterMode m)
    {
        switch (m)
        {
        case FilterMode::Nearest:   return GL_NEAREST_MIPMAP_NEAREST;
        case FilterMode::Linear:    return GL_LINEAR_MIPMAP_NEAREST;
        case FilterMode::Trilinear:
        default:                    return GL_LINEAR_MIPMAP_LINEAR;
        }
    }

    static GLenum filterModeMag(FilterMode m)
    {
        // Mag filter has no mip component
        return (m == FilterMode::Nearest) ? GL_NEAREST : GL_LINEAR;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  applySamplerParams
    //  Assumes the texture is already bound to GL_TEXTURE_2D.
    // ─────────────────────────────────────────────────────────────────────────
    void TextureManager::applySamplerParams(const Texture& tex)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapModeToGL(tex.wrapS));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapModeToGL(tex.wrapT));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterModeMin(tex.filterMode));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterModeMag(tex.filterMode));

        // Anisotropic filtering — query hardware max and clamp to it
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
        float aniso = std::clamp(tex.anisotropy, 1.0f, maxAniso);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, aniso);

        // If clamping to border, set a sensible default border colour (transparent black)
        if (tex.wrapS == WrapMode::ClampToBorder || tex.wrapT == WrapMode::ClampToBorder)
        {
            constexpr float border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  load(Texture)  — primary entry point
    // ─────────────────────────────────────────────────────────────────────────
    GLuint TextureManager::load(const Texture& tex)
    {
        // Return existing handle immediately — guarantees load-once
        auto it = textures.find(tex.name);
        if (it != textures.end())
            return it->second.id;

        stbi_set_flip_vertically_on_load(true);
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(tex.path.c_str(), &w, &h, &channels, 4);
        if (!data)
        {
            std::cerr << "[TextureManager] Failed to load '" << tex.name
                << "' from: " << tex.path << "\n";
            return 0;
        }

        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        applySamplerParams(tex);

        stbi_image_free(data);
        glBindTexture(GL_TEXTURE_2D, 0);

        textures[tex.name] = { id, tex };
        std::cout << "[TextureManager] Loaded '" << tex.name
            << "' (" << w << "x" << h << ")\n";
        return id;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  load(name, path)  — convenience overload, default sampler params
    // ─────────────────────────────────────────────────────────────────────────
    GLuint TextureManager::load(const std::string& name, const std::string& path)
    {
        return load(Texture(name, path));
    }

    GLuint TextureManager::load(const std::string& name, unsigned char* rgba, int width, int height)
    {
        // Return existing handle if already loaded
        auto it = textures.find(name);
        if (it != textures.end())
            return it->second.id;

        if (!rgba || width <= 0 || height <= 0)
        {
            std::cerr << "[TextureManager] Invalid pixel data for '" << name << "'\n";
            return 0;
        }

        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Build a default Texture descriptor (no file path — embedded data)
        Texture desc;
        desc.name = name;
        // path left empty — data came from memory
        applySamplerParams(desc);

        glBindTexture(GL_TEXTURE_2D, 0);

        textures[name] = { id, desc };
        std::cout << "[TextureManager] Loaded (memory) '" << name
            << "' (" << width << "x" << height << ")\n";
        return id;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  bind
    // ─────────────────────────────────────────────────────────────────────────
    void TextureManager::bind(const std::string& name, GLuint slot) const
    {
        auto it = textures.find(name);
        if (it == textures.end()) return;

        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, it->second.id);

        // Re-apply sampler params in case a different texture was sharing this
        // unit (some drivers carry state across binds).
        applySamplerParams(it->second.desc);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  get / getDescriptor
    // ─────────────────────────────────────────────────────────────────────────
    GLuint TextureManager::get(const std::string& name) const
    {
        auto it = textures.find(name);
        return (it != textures.end()) ? it->second.id : 0;
    }

    const Texture* TextureManager::getDescriptor(const std::string& name) const
    {
        auto it = textures.find(name);
        return (it != textures.end()) ? &it->second.desc : nullptr;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  clear
    // ─────────────────────────────────────────────────────────────────────────
    void TextureManager::clear()
    {
        for (auto& [name, entry] : textures)
            glDeleteTextures(1, &entry.id);
        textures.clear();
    }

} // namespace PEngine