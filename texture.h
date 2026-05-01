#pragma once
#ifndef TEXTURE_H
#define TEXTURE_H

#include <string>

namespace HonHengine
{
    // ─────────────────────────────────────────────────────────────────────────
    //  Sampler parameters stored alongside the texture name/path.
    //  These are applied once when the texture is first loaded via
    //  TextureManager::load() and re-applied whenever bind() is called.
    // ─────────────────────────────────────────────────────────────────────────

    enum class WrapMode
    {
        Repeat,         // GL_REPEAT        — tile indefinitely (default)
        MirroredRepeat, // GL_MIRRORED_REPEAT
        ClampToEdge,    // GL_CLAMP_TO_EDGE
        ClampToBorder,  // GL_CLAMP_TO_BORDER
    };

    enum class FilterMode
    {
        Nearest,    // GL_NEAREST / GL_NEAREST_MIPMAP_NEAREST  — pixel-art, crisp
        Linear,     // GL_LINEAR  / GL_LINEAR_MIPMAP_NEAREST   — smooth, cheap
        Trilinear,  // GL_LINEAR  / GL_LINEAR_MIPMAP_LINEAR    — smooth + mip blend (default)
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Blend mode used when compositing one texture layer on top of another.
    // ─────────────────────────────────────────────────────────────────────────
    enum class LayerBlendMode
    {
        Mix,        // lerp(base, layer, weight)  — standard alpha-blend
        Multiply,   // base * layer               — darkening, grunge overlays
        Add,        // base + layer * weight      — glow, emissive detail
        Overlay,    // photoshop-style overlay    — contrast-preserving blend
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Mask type that modulates the blend weight of a layer automatically.
    //  The GPU shader evaluates this per-fragment; no CPU work required.
    // ─────────────────────────────────────────────────────────────────────────
    enum class LayerMaskType
    {
        None,           // weight is constant (= TextureLayer::blendWeight)
        HeightBased,    // weight ramps with world-space Y using maskMin/maskMax
        SlopeBased,     // weight ramps with surface slope (dot(N, up))
        VertexColor,    // weight read from vertex colour channel (r/g/b/a via maskChannel)
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Texture — metadata + all sampler parameters.
    //  The actual GPU handle (GLuint) lives in TextureManager.
    // ─────────────────────────────────────────────────────────────────────────
    struct Texture
    {
        std::string name;           // key used in TextureManager
        std::string path;           // file path on disk

        // UV transform applied in the shader (per-layer tiling/offset)
        float tilingU = 1.0f;     // repeats along U (horizontal)
        float tilingV = 1.0f;     // repeats along V (vertical)
        float offsetU = 0.0f;     // UV scroll / offset U
        float offsetV = 0.0f;     // UV scroll / offset V

        // Sampler state — applied to the GL texture object at load time
        WrapMode   wrapS = WrapMode::Repeat;
        WrapMode   wrapT = WrapMode::Repeat;
        FilterMode filterMode = FilterMode::Trilinear;

        // Anisotropic filtering level (1 = off, 4/8/16 = quality)
        float anisotropy = 4.0f;

        // ── Constructors ─────────────────────────────────────────────────────

        Texture() = default;

        // Minimal constructor — just name + path, all sampler params at defaults
        Texture(const std::string& n, const std::string& p)
            : name(n), path(p) {
        }

        // Full constructor
        Texture(const std::string& n,
            const std::string& p,
            float              tU,
            float              tV,
            WrapMode           wrap = WrapMode::Repeat,
            FilterMode         filter = FilterMode::Trilinear,
            float              aniso = 4.0f)
            : name(n), path(p)
            , tilingU(tU), tilingV(tV)
            , wrapS(wrap), wrapT(wrap)
            , filterMode(filter)
            , anisotropy(aniso)
        {
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  TextureLayer — one entry in a Material's layer stack.
    //  Contains the texture descriptor AND all per-layer compositing parameters.
    // ─────────────────────────────────────────────────────────────────────────
    struct TextureLayer
    {
        Texture         texture;                            // which texture + its sampler params
        LayerBlendMode  blendMode = LayerBlendMode::Mix; // how to composite with layers below
        float           blendWeight = 1.0f;                // constant weight [0..1]

        // Automatic mask
        LayerMaskType   maskType = LayerMaskType::None;
        float           maskMin = 0.0f;  // world-Y or slope value where weight = 0
        float           maskMax = 1.0f;  // world-Y or slope value where weight = 1
        int             maskChannel = 0;     // 0=R, 1=G, 2=B, 3=A  (VertexColor mask only)
        bool            maskInvert = false; // flip the mask ramp

        // ── Convenience constructors ─────────────────────────────────────────

        // Simple layer: just a texture, fully opaque (base layer or single-texture material)
        explicit TextureLayer(const Texture& tex)
            : texture(tex) {
        }

        // Layer with constant blend weight
        TextureLayer(const Texture& tex, float weight, LayerBlendMode mode = LayerBlendMode::Mix)
            : texture(tex), blendMode(mode), blendWeight(weight) {
        }

        // Height-masked layer (e.g. rock above Y=2, sand below Y=0)
        static TextureLayer HeightBlend(const Texture& tex,
            float yMin, float yMax,
            LayerBlendMode mode = LayerBlendMode::Mix)
        {
            TextureLayer l(tex);
            l.blendMode = mode;
            l.maskType = LayerMaskType::HeightBased;
            l.maskMin = yMin;
            l.maskMax = yMax;
            return l;
        }

        // Slope-masked layer (e.g. cliff texture on steep faces)
        static TextureLayer SlopeBlend(const Texture& tex,
            float slopeMin, float slopeMax,
            LayerBlendMode mode = LayerBlendMode::Mix)
        {
            TextureLayer l(tex);
            l.blendMode = mode;
            l.maskType = LayerMaskType::SlopeBased;
            l.maskMin = slopeMin;
            l.maskMax = slopeMax;
            return l;
        }
    };

} // namespace HonHengine

#endif // TEXTURE_H