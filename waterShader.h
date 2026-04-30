
static const char* WATER_VERT = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;

uniform mat4  uView;
uniform mat4  uProj;
uniform float uTime;

void addWave(vec2 pos, float dx, float dz, float amp, float steep,
             float k, float w, float t,
             inout vec3 disp, inout float dnx, inout float dnz)
{
    vec2  dir = normalize(vec2(dx, dz));
    float phi = k * dot(dir, pos) - w * t;
    float s   = sin(phi);
    float c   = cos(phi);
    float Q   = steep * 0.5;
    disp.x   += Q * dir.x * c;
    disp.y   += amp * s;
    disp.z   += Q * dir.y * c; 
    dnx      -= k * amp * dir.x * s;
    dnz      -= k * amp * dir.y * s; 
}

void main()
{
    vec2  pos  = vec2(aPos.x, aPos.z);
    vec3  disp = vec3(0.0);
    float dnx  = 0.0;
    float dnz  = 0.0;

    addWave(pos,  0.8,  0.6,   0.100, 0.40, 0.30, 0.65, uTime, disp, dnx, dnz);
    addWave(pos, -0.5,  0.9,   0.070, 0.35, 0.40, 0.80, uTime, disp, dnx, dnz);
    addWave(pos,  0.3, -0.7,   0.040, 0.45, 0.75, 1.25, uTime, disp, dnx, dnz);
    addWave(pos, -0.9,  0.2,   0.030, 0.40, 0.95, 1.50, uTime, disp, dnx, dnz);
    addWave(pos,  0.6,  0.8,   0.015, 0.25, 1.90, 2.10, uTime, disp, dnx, dnz);
    addWave(pos, -0.2, -0.95,  0.010, 0.20, 2.50, 2.70, uTime, disp, dnx, dnz);

    vec3 worldPos = vec3(aPos.x + disp.x, aPos.y + disp.y, aPos.z + disp.z);

    vNormal   = normalize(vec3(-dnx, 1.0, -dnz));
    vWorldPos = worldPos;
    vColor    = aColor;
    vUV       = aUV;
    gl_Position = uProj * uView * vec4(worldPos, 1.0);
}
)GLSL";

static const char* WATER_FRAG = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;

out vec4 FragColor;

uniform vec3  uCamPos;
uniform float uTime;
uniform float uAlpha;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;

float fresnel(float cosA)
{
    float f = 0.02 + 0.98 * pow(clamp(1.0 - cosA, 0.0, 1.0), 5.0);
    return clamp(f, 0.0, 1.0);
}

vec3 skyColor(vec3 R)
{
    float t = clamp(R.y, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.65, 0.72);
    vec3 zenith  = vec3(0.08, 0.11, 0.16);
    return mix(horizon, zenith, t * t);
}

void main()
{
    vec3  N = normalize(vNormal);
    vec3  V = normalize(uCamPos - vWorldPos);

    vec3 deepCol    = vec3(0.02, 0.05, 0.09);
    vec3 shallowCol = vec3(0.06, 0.16, 0.22);
    float shore     = vColor.b;
    vec3 waterCol   = mix(deepCol, shallowCol, shore * 0.5);

    vec3 R       = reflect(-V, N);
    vec3 skyRefl = skyColor(R);

    float fr = fresnel(max(dot(N, V), 0.0));
    vec3  color = mix(waterCol, skyRefl, fr);

    vec3  H     = normalize(uSunDir + V);
    float NdotH = max(dot(N, H), 0.0);
    float spec  = pow(NdotH, 256.0) * 2.5 + pow(NdotH,  32.0) * 0.20;
    color += uSunColor * uSunIntensity * spec;

    float crest = smoothstep(0.08, 0.18, vWorldPos.y);
    float sFoam = smoothstep(0.60, 1.00, shore) *
                  (0.5 + 0.5 * sin(uTime * 2.0 + vWorldPos.x * 4.0 + vWorldPos.z * 3.5));
    float foam  = clamp(crest * 0.4 + sFoam * 0.3, 0.0, 1.0);
    color       = mix(color, vec3(0.90, 0.92, 0.95), foam);

    FragColor = vec4(clamp(color, 0.0, 1.0), 0.97);
}
)GLSL";