static const char* BEACH_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

uniform mat4 uView;
uniform mat4 uProj;

void main()
{
    vWorldPos = aPos;
    vNormal   = aNormal;
    vUV       = aUV;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)GLSL";

static const char* BEACH_FRAG = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

uniform sampler2D uSandTex;
uniform sampler2D uRockTex;
uniform vec3 uSunDir;
uniform vec3 uSunColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uSunDir);

    float slope   = clamp(dot(N, vec3(0, 1, 0)), 0.0, 1.0);
    float rockMask = 1.0 - smoothstep(0.7, 0.9, slope);

    vec3 sand = texture(uSandTex, vUV).rgb;
    vec3 rock = texture(uRockTex, vUV * 2.0).rgb;

    vec3 texColor = mix(sand, rock, rockMask);
    float diff    = max(dot(N, L), 0.3);
    FragColor = vec4(texColor * uSunColor * diff, 1.0);
}
)GLSL";