#pragma once
#ifndef MATERIAL_H
#define MATERIAL_H

#include "color.h"
#include "texture.h"   
#include <vector>
#include <string>

// Assuming glad or GL headers are available for GLuint, or use unsigned int
typedef unsigned int GLuint;

namespace HonHengine
{
    static constexpr int MAX_TEXTURE_LAYERS = 8;

    struct Material
    {
        double specularity = 0.0;
        double reflectivity = 0.0;
        Color  color;
        Color  outlineColor{ 0, 0, 0, 255 };

        GLuint customShaderProgram = 0; 
        bool isTransparent = false;

        std::vector<TextureLayer> textureLayers;

        Material() = default;

        Material(double spec, double ref, Color col, Color outlining = Color(0, 0, 0, 255))
            : specularity(spec), reflectivity(ref), color(col), outlineColor(outlining) {
        }

        Material(double spec, double ref, Color col, const Texture& tex, Color outlining = Color(0, 0, 0, 255))
            : specularity(spec), reflectivity(ref), color(col), outlineColor(outlining)
        {
            textureLayers.emplace_back(tex);
        }

        Material& addLayer(const TextureLayer& layer)
        {
            if (static_cast<int>(textureLayers.size()) < MAX_TEXTURE_LAYERS)
                textureLayers.push_back(layer);
            return *this;
        }

        bool hasTextures() const { return !textureLayers.empty(); }

        const std::string& baseTextureName() const
        {
            static const std::string empty;
            return textureLayers.empty() ? empty : textureLayers[0].texture.name;
        }
    };
}

#endif // MATERIAL_H