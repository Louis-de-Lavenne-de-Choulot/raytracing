#pragma once
#ifndef TEXTUREMANAGER_H
#define TEXTUREMANAGER_H

#include <glad/glad.h>
#include <string>
#include <unordered_map>

#include "texture.h"   // Texture, WrapMode, FilterMode

namespace PEngine
{
    class TextureManager
    {
    public:
        TextureManager() = default;
        ~TextureManager() { clear(); }


        // Apply GL sampler state from a Texture descriptor to the currently-bound texture.
        static void applySamplerParams(const Texture& tex);

        // ── Load API ─────────────────────────────────────────────────────────

        // Load from a full Texture descriptor (preferred — carries all sampler params).
        // Returns 0 on failure, existing ID if already loaded.
        GLuint load(const Texture& tex);

        // Convenience overload: name + path, all sampler params at defaults.
        GLuint load(const std::string& name, const std::string& path); 
        
        GLuint load(const std::string& name, unsigned char* rgba, int width, int height);


        // ── Query / Bind API ─────────────────────────────────────────────────

        // Bind to a texture unit (GL_TEXTURE0 + slot).
        // Re-applies sampler parameters so they always match the Texture descriptor.
        void   bind(const std::string& name, GLuint slot = 0) const;

        // Return the raw GL handle (0 if not loaded).
        GLuint get(const std::string& name) const;

        // Return the stored descriptor (useful for reading tiling/wrap settings).
        const Texture* getDescriptor(const std::string& name) const;

        // Free all GPU resources.
        void clear();

    private:

        struct Entry {
            GLuint  id = 0;
            Texture desc;
        };

        std::unordered_map<std::string, Entry> textures;
    };

} // namespace PEngine

#endif // TEXTUREMANAGER_H