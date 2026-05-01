// ==============================================================================
// CALM SEASHORE PROCEDURAL WATER SHADER
// Features: Gentle, aligned rolling waves, glassy micro-normals, tropical 
// subsurface scattering, and soft shoreline foam.
// ==============================================================================

static const char* WATER_VERT = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vColor;
out vec2 vUV;
out float vJacobian;

uniform mat4  uView;
uniform mat4  uProj;
uniform float uTime;

// Gerstner Wave calculation
void addWave(vec2 pos, vec2 dir, float amp, float steep, float k, float w, float phase, float t,
             inout vec3 disp, inout float dnx, inout float dnz, inout float jacobian)
{
    dir = normalize(dir);
    float phi = k * dot(dir, pos) - w * t + phase;
    float s   = sin(phi);
    float c   = cos(phi);
    float Q   = steep / (amp * k * 8.0); 
    
    disp.x   += Q * amp * dir.x * c;
    disp.y   += amp * s;
    disp.z   += Q * amp * dir.y * c;
    
    float wa  = k * amp;
    dnx      -= wa * dir.x * c;
    dnz      -= wa * dir.y * c;
    
    jacobian -= Q * wa * s;
}

void main()
{
    vec2  pos      = vec2(aPos.x, aPos.z);
    vec3  disp     = vec3(0.0);
    float dnx      = 0.0;
    float dnz      = 0.0;
    float jacobian = 1.0;

    // --- SEASHORE WAVES (Rolling in uniformly) ---
    // Assuming the shore is along the Z axis, waves push mostly along positive X
    //           Dir X, Dir Z, Amp,   Steep, K,    W,   Phase, Time, Disp, dnx, dnz, jacob
    addWave(pos, vec2(1.0, 0.1), 0.150, 0.5, 0.15, 1.2, 0.0, uTime, disp, dnx, dnz, jacobian);
    addWave(pos, vec2(0.9,-0.1), 0.080, 0.4, 0.25, 1.6, 1.5, uTime, disp, dnx, dnz, jacobian);
    addWave(pos, vec2(1.0, 0.2), 0.050, 0.4, 0.40, 2.0, 3.2, uTime, disp, dnx, dnz, jacobian);

    // --- GENTLE CHOP ---
    addWave(pos, vec2(0.8, -0.3), 0.020, 0.3, 0.80, 2.8, 1.1, uTime, disp, dnx, dnz, jacobian);
    addWave(pos, vec2(0.7,  0.4), 0.015, 0.3, 1.20, 3.5, 2.2, uTime, disp, dnx, dnz, jacobian);

    // --- BREEZE RIPPLES (Very subtle) ---
    addWave(pos, vec2(0.6, 0.6),  0.005, 0.2, 2.50, 4.5, 0.0, uTime, disp, dnx, dnz, jacobian);
    addWave(pos, vec2(0.8,-0.2),  0.003, 0.2, 4.00, 6.0, 2.7, uTime, disp, dnx, dnz, jacobian);
    addWave(pos, vec2(0.9, 0.1),  0.001, 0.1, 7.00, 8.5, 4.5, uTime, disp, dnx, dnz, jacobian);

    vec3 worldPos = vec3(aPos.x + disp.x, aPos.y + disp.y, aPos.z + disp.z);
    
    vNormal   = normalize(vec3(-dnx, 1.0, -dnz));
    vWorldPos = worldPos;
    vColor    = aColor; // Assuming aColor.b stores distance to shore
    vUV       = aUV;
    vJacobian = jacobian; 

    gl_Position = uProj * uView * vec4(worldPos, 1.0);
}
)GLSL";


static const char* WATER_FRAG = R"GLSL(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vColor;
in vec2 vUV;
in float vJacobian;

out vec4 FragColor;

uniform vec3  uCamPos;
uniform float uTime;
uniform float uAlpha;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;

vec2 hash22(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float noise2D(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash22(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(hash22(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(hash22(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(hash22(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p) {
    float f = 0.0;
    float amp = 0.5;
    for(int i = 0; i < 3; i++) { // Reduced iterations for smoother water
        f += amp * noise2D(p);
        p = p * 2.0 + vec2(uTime * 0.1); // Slower wind drift
        amp *= 0.5;
    }
    return f;
}

float voronoi(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float minDist = 1.0;
    for(int y = -1; y <= 1; y++) {
        for(int x = -1; x <= 1; x++) {
            vec2 neighbor = vec2(float(x), float(y));
            vec2 point = hash22(i + neighbor);
            point = 0.5 + 0.5 * sin(uTime * 0.8 + 6.2831 * point); // Slower foam animation
            vec2 diff = neighbor + point - f;
            float dist = length(diff);
            minDist = min(minDist, dist);
        }
    }
    return minDist;
}

float fresnel(float cosA, float f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosA, 0.0, 1.0), 5.0);
}

vec3 getSkyColor(vec3 R, vec3 sunDir) {
    float t = clamp(R.y, 0.0, 1.0);
    vec3 horizon = vec3(0.70, 0.85, 0.95); // Brighter, softer sky reflection
    vec3 zenith  = vec3(0.15, 0.30, 0.55);
    vec3 sky = mix(horizon, zenith, t * t);
    
    float sunFocus = max(dot(R, sunDir), 0.0);
    float sunHalo  = pow(sunFocus, 64.0) * 0.5 + pow(sunFocus, 8.0) * 0.2;
    return sky + uSunColor * sunHalo;
}

void main() {
    // 1. GLASSY MICRO-NORMALS
    vec2 nv = vWorldPos.xz * 3.0;
    float n1 = fbm(nv + vec2(uTime * 0.2, uTime * 0.1));
    float n2 = fbm(nv * 2.0 - vec2(uTime * 0.15, -uTime * 0.2));
    // Drastically reduced multipliers (from 0.15 to 0.03) for a calm surface
    vec3 microNormal = normalize(vec3(n1 * 0.03, 1.0, n2 * 0.03));
    
    vec3 N = normalize(vNormal + microNormal);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(uSunDir);
    vec3 H = normalize(L + V);

    // 2. TROPICAL WATER ABSORPTION
    vec3 extCoeff = vec3(0.60, 0.15, 0.02); // Absorbs reds heavily, leaves vibrant cyan/teal
    vec3 shallowColor = vec3(0.1, 0.8, 0.7); // Bright tropical shallow water
    vec3 deepColor    = vec3(0.02, 0.2, 0.4); // Brighter deep water
    
    float shoreOffset = vColor.b;
    float waterDepth = clamp(shoreOffset * 5.0 - vWorldPos.y, 0.1, 15.0);
    
    vec3 transmittance = exp(-extCoeff * waterDepth);
    vec3 waterBaseColor = mix(deepColor, shallowColor, transmittance);

    // 3. SUBSURFACE SCATTERING
    float peakScattering = max(0.0, dot(V, -L));
    float scatterMask = pow(clamp(-vWorldPos.y * 0.5 + 0.5, 0.0, 1.0), 2.0);
    vec3 sss = uSunColor * pow(peakScattering, 4.0) * scatterMask * 0.3 * vec3(0.2, 0.6, 0.5);

    // 4. REFLECTION AND FRESNEL
    vec3 R = reflect(-V, N);
    vec3 skyRefl = getSkyColor(R, L);
    float fr = fresnel(max(dot(N, V), 0.0), 0.02);
    
    // 5. SPECULAR HIGHLIGHT
    float NdotH = max(dot(N, H), 0.0);
    float specBase = pow(NdotH, 128.0) * 0.4; // Tighter base reflection for calm water
    float specCore = pow(NdotH, 1024.0) * 2.0; // Very sharp sun core
    vec3 specular = uSunColor * uSunIntensity * (specBase + specCore) * fr;

    // 6. SOFT SHORE FOAM
    // De-emphasize peak foam (vJacobian) and emphasize shore proximity (shoreOffset)
    float peakFoamMask = clamp(1.0 - vJacobian, 0.0, 1.0) * 0.3; // Much weaker
    float shoreFoamMask = smoothstep(0.1, 0.8, shoreOffset);     // Wider soft shore band
    float foamMask = clamp(peakFoamMask + shoreFoamMask, 0.0, 1.0);
    
    float vNoise = voronoi(vWorldPos.xz * 4.0 + uTime * 0.3);
    vNoise = smoothstep(0.1, 0.9, vNoise); // Softer foam edges
    
    float foamIntensity = smoothstep(0.4, 0.8, foamMask * vNoise);
    
    // Fade out foam naturally near the very edge so it blends into sand
    float shoreFade = smoothstep(0.95, 0.7, shoreOffset); 
    foamIntensity *= shoreFade;
    
    vec3 foamColor = vec3(0.95, 0.98, 1.0);

    // 7. FINAL COMPOSITE
    vec3 finalColor = mix(waterBaseColor, skyRefl, fr);
    finalColor += sss;
    finalColor += specular;
    
    finalColor = mix(finalColor, foamColor, foamIntensity);

    // Tonemapping
    finalColor = vec3(1.0) - exp(-finalColor * 1.2);
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    // Optional: Fade out water alpha near shore if you are blending with a terrain underneath
    float finalAlpha = uAlpha * smoothstep(1.0, 0.8, shoreOffset);

    FragColor = vec4(finalColor, finalAlpha);
}
)GLSL";