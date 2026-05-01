#pragma once
#ifndef SKINNEDSHADER_H
#define SKINNEDSHADER_H

namespace HonHengine
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
out vec4 vLightSpacePos;

// ── Standard HonHengine uniforms ────────────────────────────────────────────────
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

// ── Shadow ────────────────────────────────────────────────────────────────────
uniform mat4 uLightSpaceMatrix;

// ── Skinning ──────────────────────────────────────────────────────────────────
uniform mat4 uBonePalette[128];
uniform int  uSkinned;

void main()
{
    vec4 skinnedPos    = vec4(0.0);
    vec3 skinnedNormal = vec3(0.0);

    if (uSkinned == 1)
    {
        ivec4 bIdx = ivec4(
            clamp(int(aBoneIdx.x), 0, 127),
            clamp(int(aBoneIdx.y), 0, 127),
            clamp(int(aBoneIdx.z), 0, 127),
            clamp(int(aBoneIdx.w), 0, 127)
        );

        mat4 skinMatrix =
              aBoneWgt.x * uBonePalette[bIdx.x]
            + aBoneWgt.y * uBonePalette[bIdx.y]
            + aBoneWgt.z * uBonePalette[bIdx.z]
            + aBoneWgt.w * uBonePalette[bIdx.w];

        skinnedPos = uModel * skinMatrix * vec4(aPos, 1.0);

        mat3 normalMatrix = transpose(inverse(mat3(uModel) * mat3(skinMatrix)));
        skinnedNormal = normalMatrix * aNormal;
    }
    else
    {
        skinnedPos    = uModel * vec4(aPos, 1.0);
        skinnedNormal = transpose(inverse(mat3(uModel))) * aNormal;
    }

    vWorldPos      = vec3(skinnedPos);
    vNormal        = normalize(skinnedNormal);
    vUV            = aUV;
    vLightSpacePos = uLightSpaceMatrix * skinnedPos;
    gl_Position    = uProj * uView * skinnedPos;
}
)GLSL";

    static const char* SKINNED_FRAG = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;

out vec4 FragColor;

// ── Lighting ──────────────────────────────────────────────────────────────────
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;

// ── Shadow ────────────────────────────────────────────────────────────────────
uniform sampler2DShadow uShadowMap;  

// ── Material ──────────────────────────────────────────────────────────────────
uniform sampler2D uAlbedo;
uniform float     uAlpha;

// ── Hemisphere ambient ────────────────────────────────────────────────────────
vec3 hemisphereAmbient(vec3 N)
{
    vec3 sky    = vec3(0.53, 0.75, 1.00);
    vec3 ground = vec3(0.20, 0.15, 0.10);
    float t = N.y * 0.5 + 0.5;
    return mix(ground, sky, t) * 0.18;
}

// ── 3×3 PCF shadow lookup ─────────────────────────────────────────────────────
// Returns [0,1] — 0 = fully shadowed, 1 = fully lit.
float sampleShadow(vec4 lsPos)
{
    // Perspective divide → NDC, then remap [-1,1] → [0,1] for texture coords.
    vec3 proj = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;

    // Reject fragments outside the shadow-map frustum — treat as fully lit.
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0
                      || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // Constant bias to suppress self-shadowing acne.
    float bias   = 0.003;
    float shadow = 0.0;

    // Texel size for a 2048×2048 shadow map (matches initShadowMap).
    vec2 texelSize = vec2(1.0 / 2048.0);

    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        vec3 sampleCoord = vec3(proj.xy + vec2(x, y) * texelSize,
                                proj.z - bias);
        shadow += texture(uShadowMap, sampleCoord);
    }

    return shadow / 9.0;
}

void main()
{
    vec3 N      = normalize(vNormal);
    vec4 albedo = texture(uAlbedo, vUV);

    if (albedo.a < 0.01) discard;

    // ── Hemisphere ambient ────────────────────────────────────────────────────
    vec3 ambient = hemisphereAmbient(N);

    // ── Directional sun ───────────────────────────────────────────────────────
    float wrap    = max((dot(N, uSunDir) + 0.2) / 1.2, 0.0);
    vec3  diffuse = uSunColor * uSunIntensity * wrap;

    // ── Specular ──────────────────────────────────────────────────────────────
    vec3  viewDir  = normalize(-vWorldPos);
    vec3  halfVec  = normalize(uSunDir + viewDir);
    float spec     = pow(max(dot(N, halfVec), 0.0), 32.0) * 0.3;
    vec3  specular = uSunColor * uSunIntensity * spec;

    // ── Shadow ────────────────────────────────────────────────────────────────
    float shadow = sampleShadow(vLightSpacePos);

    // Blend: ambient is unaffected; diffuse+specular are attenuated by shadow.
    vec3 finalColor = (ambient + shadow * (diffuse + specular)) * albedo.rgb;
    FragColor = vec4(finalColor, albedo.a);
}
)GLSL";

    // ── Depth-only shadow-cast shader ─────────────────────────────────────────
    // Used during the shadow pass for ALL geometry (both skinned and static).
    // Outputs nothing — the GPU only writes the depth buffer.
    static const char* SHADOW_VERT = R"GLSL(
#version 330 core

layout(location = 0) in vec3  aPos;
layout(location = 3) in uvec4 aBoneIdx;
layout(location = 4) in vec4  aBoneWgt;

uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

uniform mat4 uBonePalette[128];
uniform int  uSkinned;

void main()
{
    vec4 worldPos;

    if (uSkinned == 1)
    {
        ivec4 bIdx = ivec4(
            clamp(int(aBoneIdx.x), 0, 127),
            clamp(int(aBoneIdx.y), 0, 127),
            clamp(int(aBoneIdx.z), 0, 127),
            clamp(int(aBoneIdx.w), 0, 127)
        );

        mat4 skinMatrix =
              aBoneWgt.x * uBonePalette[bIdx.x]
            + aBoneWgt.y * uBonePalette[bIdx.y]
            + aBoneWgt.z * uBonePalette[bIdx.z]
            + aBoneWgt.w * uBonePalette[bIdx.w];

        worldPos = uModel * skinMatrix * vec4(aPos, 1.0);
    }
    else
    {
        worldPos = uModel * vec4(aPos, 1.0);
    }

    gl_Position = uLightSpaceMatrix * worldPos;
}
)GLSL";

    static const char* SHADOW_FRAG = R"GLSL(
#version 330 core
// Depth-only pass — no colour output needed.
void main() {}
)GLSL";

} // namespace HonHengine

#endif // SKINNEDSHADER_H