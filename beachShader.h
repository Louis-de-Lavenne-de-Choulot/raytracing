#pragma once
#ifndef BEACHSHADER_H
#define BEACHSHADER_H

// ─────────────────────────────────────────────────────────────────────────────
//  beachShader.h
//
//  Vertex layout expected (matches exampleScene.cpp BeachVert):
//    location 0 — position  (vec3)
//    location 1 — normal    (vec3)
//    location 2 — uv        (vec2)
//
//  Uniforms provided automatically by GPURenderer::drawRenderComponent():
//    uModel, uView, uProj, uCamPos, uTime
//    uSunDir, uSunColor, uSunIntensity
//    uLightSpaceMatrix   ← shadow MVP
//    uShadowMap          ← slot 1, sampler2DShadow
//    uAlbedo             ← slot 0, sand texture
// ─────────────────────────────────────────────────────────────────────────────

namespace PEngine
{

    static const char* BEACH_VERT = R"GLSL(
#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpacePos;   // position in shadow-map clip space

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightSpaceMatrix;

void main()
{
    vec4 worldPos   = uModel * vec4(aPos, 1.0);
    vWorldPos       = vec3(worldPos);
    vNormal         = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    vUV             = aUV;
    vLightSpacePos  = uLightSpaceMatrix * worldPos;
    gl_Position     = uProj * uView * worldPos;
}
)GLSL";

    static const char* BEACH_FRAG = R"GLSL(
#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;

out vec4 FragColor;

// ── Textures ──────────────────────────────────────────────────────────────────
uniform sampler2D       uAlbedo;     // slot 0 — sand texture
uniform sampler2DShadow uShadowMap;  // slot 1 — depth comparison texture

// ── Sun / lighting ────────────────────────────────────────────────────────────
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;

// ── Misc ──────────────────────────────────────────────────────────────────────
uniform float uTime;
uniform vec3  uCamPos;

// ── Hemisphere ambient ────────────────────────────────────────────────────────
vec3 hemisphereAmbient(vec3 N)
{
    vec3 sky    = vec3(0.53, 0.75, 1.00);
    vec3 ground = vec3(0.20, 0.15, 0.10);
    float t = N.y * 0.5 + 0.5;
    return mix(ground, sky, t) * 0.15;
}

// ── 3×3 PCF shadow lookup ─────────────────────────────────────────────────────
// Returns 1.0 = fully lit,  0.0 = fully in shadow.
float sampleShadow(vec4 lsPos)
{
    // Perspective divide → NDC, remap [-1,1] → [0,1] for texture space.
    vec3 proj = lsPos.xyz / lsPos.w;
    proj = proj * 0.5 + 0.5;

    // Fragments outside the shadow frustum → treat as fully lit.
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0
                      || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // Small constant bias to suppress self-shadowing acne on flat sand.
    float bias = 0.002;

    // Texel size matches the 2048×2048 shadow map in GPURenderer::initShadowMap().
    vec2 texelSize = vec2(1.0 / 2048.0);

    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
        vec3 coord = vec3(proj.xy + vec2(x, y) * texelSize,
                          proj.z - bias);
        shadow += texture(uShadowMap, coord);
    }

    return shadow / 9.0;
}

void main()
{
    vec3 N      = normalize(vNormal);
    vec4 albedo = texture(uAlbedo, vUV);

    // ── Hemisphere ambient (never shadowed) ───────────────────────────────────
    vec3 ambient = hemisphereAmbient(N);

    // ── Directional sun ───────────────────────────────────────────────────────
    float wrap    = max((dot(N, uSunDir) + 0.25) / 1.25, 0.0);
    vec3  diffuse = uSunColor * uSunIntensity * wrap;

    // ── Specular (subtle on sand) ─────────────────────────────────────────────
    vec3  viewDir  = normalize(uCamPos - vWorldPos);
    vec3  halfVec  = normalize(uSunDir + viewDir);
    float spec     = pow(max(dot(N, halfVec), 0.0), 48.0) * 0.15;
    vec3  specular = uSunColor * uSunIntensity * spec;

    // ── Shadow ────────────────────────────────────────────────────────────────
    float shadow = sampleShadow(vLightSpacePos);

    // Ambient always on; diffuse + specular gated by shadow factor.
    vec3 finalColor = (ambient + shadow * (diffuse + specular)) * albedo.rgb;
    FragColor = vec4(finalColor, albedo.a);
}
)GLSL";

} // namespace PEngine

#endif // BEACHSHADER_H