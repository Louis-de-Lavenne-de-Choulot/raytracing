#pragma once
#ifndef SKINNEDSHADER_H
#define SKINNEDSHADER_H

namespace PEngine
{

    static const char* SKINNED_VERT = R"GLSL(
#version 330 core

// ── Vertex attributes ────────────────────────────────────────────────────────
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec2  aUV;
layout(location = 3) in uvec4 aBoneIdx;
layout(location = 4) in vec4  aBoneWgt;

// ── Outputs to fragment shader ───────────────────────────────────────────────
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

// ── Standard PEngine uniforms ────────────────────────────────────────────────
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

// ── Skinning ──────────────────────────────────────────────────────────────────
uniform mat4 uBonePalette[128];
uniform int  uSkinned;

void main()
{
    vec4 skinnedPos    = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);

    if (uSkinned == 1)
    {
        // aBoneIdx is now a true uvec4 — no cast corruption possible.
        // Clamp defensively so an out-of-range index never reads off the array.
        ivec4 bIdx = ivec4(
            clamp(int(aBoneIdx.x), 0, 127),
            clamp(int(aBoneIdx.y), 0, 127),
            clamp(int(aBoneIdx.z), 0, 127),
            clamp(int(aBoneIdx.w), 0, 127)
        );

        // ── Blend up to 4 bone transforms ────────────────────────────────────
        mat4 skinMatrix =
              aBoneWgt.x * uBonePalette[bIdx.x]
            + aBoneWgt.y * uBonePalette[bIdx.y]
            + aBoneWgt.z * uBonePalette[bIdx.z]
            + aBoneWgt.w * uBonePalette[bIdx.w];

        // Apply skin in local space, then bring into world space via uModel.
        skinnedPos = uModel * skinMatrix * vec4(aPos, 1.0);

        // Normal matrix combines both transforms.
        mat3 normalMatrix = transpose(inverse(mat3(uModel) * mat3(skinMatrix)));
        skinnedNormal = normalMatrix * aNormal;
    }
    else
    {
        // Static mesh — only model transform applied.
        skinnedPos    = uModel * vec4(aPos, 1.0);
        skinnedNormal = transpose(inverse(mat3(uModel))) * aNormal;
    }

    vWorldPos   = vec3(skinnedPos);
    vNormal     = normalize(skinnedNormal);
    vUV         = aUV;
    gl_Position = uProj * uView * skinnedPos;
}
)GLSL";

    static const char* SKINNED_FRAG = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

// ── Lighting ──────────────────────────────────────────────────────────────────
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;

// ── Material ──────────────────────────────────────────────────────────────────
uniform sampler2D uAlbedo;    // slot 0 — diffuse/albedo texture
uniform float     uAlpha;     // overall opacity (default 1.0)

// ── Hemisphere ambient ────────────────────────────────────────────────────────
vec3 hemisphereAmbient(vec3 N)
{
    vec3 sky    = vec3(0.53, 0.75, 1.00);
    vec3 ground = vec3(0.20, 0.15, 0.10);
    float t = N.y * 0.5 + 0.5;
    return mix(ground, sky, t) * 0.18;
}

void main()
{
    vec3 N       = normalize(vNormal);
    vec4 albedo  = texture(uAlbedo, vUV);

    // Discard fully transparent fragments (alpha cutout).
    if (albedo.a < 0.01) discard;

    // ── Hemisphere ambient ────────────────────────────────────────────────────
    vec3 ambient = hemisphereAmbient(N);

    // ── Directional sun ───────────────────────────────────────────────────────
    float wrap    = max((dot(N, uSunDir) + 0.2) / 1.2, 0.0);
    vec3  diffuse = uSunColor * uSunIntensity * wrap;

    // ── Simple specular (Blinn-Phong) ─────────────────────────────────────────
    vec3  viewDir = normalize(-vWorldPos);
    vec3  halfVec = normalize(uSunDir + viewDir);
    float spec    = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.3;
    vec3  specular = uSunColor * uSunIntensity * spec;

    vec3 finalColor = (ambient + diffuse) * albedo.rgb + specular;
    FragColor = vec4(finalColor, albedo.a);
}
)GLSL";

} // namespace PEngine

#endif // SKINNEDSHADER_H