// skybox.cpp
#include "skybox.h"
#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <fstream>
#include <iostream>
#include <cmath>

// STB_IMAGE_IMPLEMENTATION is defined in textureManager.cpp — do not redefine here.
#include <stb_image.h>

namespace HonHengine
{

    // Skybox vertex shader source
    static const char* skyboxVertSrc = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        
        out vec3 TexCoords;
        
        uniform mat4 uView;
        uniform mat4 uProj;
        uniform float uRotation;
        
        void main()
        {
            // Remove translation from view matrix for skybox (always centered on camera)
            mat4 rotView = mat4(mat3(uView));
            
            // Apply rotation around Y axis for time-of-day effects
            float cosR = cos(uRotation);
            float sinR = sin(uRotation);
            mat4 rotMatrix = mat4(
                cosR, 0.0, sinR, 0.0,
                0.0,  1.0, 0.0,  0.0,
                -sinR,0.0, cosR, 0.0,
                0.0,  0.0, 0.0,  1.0
            );
            
            vec4 pos = uProj * rotView * rotMatrix * vec4(aPos, 1.0);
            gl_Position = pos.xyww;  // Set depth to max (always behind everything)
            TexCoords = aPos;
        }
    )";

    // Skybox fragment shader source
    static const char* skyboxFragSrc = R"(
        #version 330 core
        out vec4 FragColor;
        
        in vec3 TexCoords;
        
        uniform samplerCube uSkybox;
        uniform vec3 uFogColor;
        uniform float uFogDensity;
        uniform vec3 uSunDir;
        uniform vec3 uSunColor;
        uniform float uSunIntensity;
        
        void main()
        {
            vec4 texColor = texture(uSkybox, TexCoords);
            vec3 color = texColor.rgb;
            
            // Add sun glow effect based on direction
            float sunDot = max(0.0, dot(normalize(TexCoords), normalize(uSunDir)));
            float sunGlow = pow(sunDot, 50.0) * uSunIntensity * 1.5;
            color += uSunColor * sunGlow;
            
            // Add horizon glow
            float horizonFactor = 1.0 - abs(TexCoords.y);
            color += vec3(0.3, 0.25, 0.2) * horizonFactor * horizonFactor * 0.3;
            
            FragColor = vec4(color, 1.0);
        }
    )";

    // Add this helper function before the Skybox methods
    static void CreateFullscreenQuad(GLuint& vao, GLuint& vbo)
    {
        float vertices[] = {
            -1.0f,  1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    static bool CompileShader(GLuint& shader, GLenum type, const char* source, const char* name)
    {
        shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        int success;
        char infoLog[512];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            std::cerr << "[Skybox] Shader compile error (" << name << "): " << infoLog << "\n";
            return false;
        }
        return true;
    }

    Skybox::Skybox()
        : cubeMapTexture(0)
        , skyboxVAO(0)
        , skyboxVBO(0)
        , skyboxShader(0)
        , skyRotation(0.0f)
        , m_sunDirection(glm::normalize(glm::vec3(0.5f, 0.8f, 0.2f)))  // Default sun direction
        , m_sunColor(glm::vec3(1.0f, 0.95f, 0.8f))                      // Warm sun color
        , m_sunIntensity(0.8f)                                          // Default intensity
    {
        CreateCubeGeometry();
        CompileShaders();
    }

    Skybox::~Skybox()
    {
        cleanup();
    }

    void Skybox::CreateCubeGeometry()
    {
        // Cube vertices for skybox (positions only)
        float vertices[] = {
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &skyboxVAO);
        glGenBuffers(1, &skyboxVBO);
        glBindVertexArray(skyboxVAO);
        glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    bool Skybox::CompileShaders()
    {
        GLuint vertexShader, fragmentShader;

        if (!CompileShader(vertexShader, GL_VERTEX_SHADER, skyboxVertSrc, "Vertex")) return false;
        if (!CompileShader(fragmentShader, GL_FRAGMENT_SHADER, skyboxFragSrc, "Fragment")) return false;

        skyboxShader = glCreateProgram();
        glAttachShader(skyboxShader, vertexShader);
        glAttachShader(skyboxShader, fragmentShader);
        glLinkProgram(skyboxShader);

        int success;
        char infoLog[512];
        glGetProgramiv(skyboxShader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(skyboxShader, 512, nullptr, infoLog);
            std::cerr << "[Skybox] Program link error: " << infoLog << "\n";
            return false;
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        return true;
    }

    bool Skybox::LoadFromHDR(const std::string& hdrFile)
    {
        // 1. Load HDR image using stb_image
        int width, height, nrComponents;
        stbi_set_flip_vertically_on_load(true);
        float* data = stbi_loadf(hdrFile.c_str(), &width, &height, &nrComponents, 0);
        if (!data) {
            std::cerr << "[Skybox] Failed to load HDR: " << hdrFile << "\n";
            return false;
        }
        std::cout << "[Skybox] Loaded HDR " << width << "x" << height << ", components=" << nrComponents << "\n";

        // 2. Create 2D texture from equirectangular map
        GLuint equirectTexture;
        glGenTextures(1, &equirectTexture);
        glBindTexture(GL_TEXTURE_2D, equirectTexture);
        GLenum format = (nrComponents == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, format, GL_FLOAT, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);

        // 3. Create cubemap texture (output)
        const int cubeSize = 512;
        glGenTextures(1, &cubeMapTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
        for (unsigned int i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                cubeSize, cubeSize, 0, GL_RGB, GL_FLOAT, nullptr);
        }
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // 4. Conversion shader (unchanged, works correctly)
        static const char vertSrc[] =
            "#version 330 core\n"
            "layout (location = 0) in vec3 aPos;\n"
            "out vec3 WorldPos;\n"
            "uniform mat4 uProjection;\n"
            "uniform mat4 uView;\n"
            "void main()\n"
            "{\n"
            "    WorldPos = aPos;\n"
            "    gl_Position = uProjection * uView * vec4(WorldPos, 1.0);\n"
            "}\n";

        static const char fragSrc[] =
            "#version 330 core\n"
            "out vec4 FragColor;\n"
            "in vec3 WorldPos;\n"
            "uniform sampler2D uEquirectangularMap;\n"
            "const vec2 invAtan = vec2(0.1591, 0.3183);\n"
            "vec2 SampleSphericalMap(vec3 v)\n"
            "{\n"
            "    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));\n"
            "    uv *= invAtan;\n"
            "    uv += 0.5;\n"
            "    return uv;\n"
            "}\n"
            "void main()\n"
            "{\n"
            "    vec2 uv = SampleSphericalMap(normalize(WorldPos));\n"
            "    vec3 color = texture(uEquirectangularMap, uv).rgb;\n"
            "    FragColor = vec4(color, 1.0);\n"
            "}\n";

        // Compile shaders
        auto compileShader = [](GLenum type, const char* src, const char* name) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLint logLen = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
                if (logLen > 1) {
                    std::vector<char> log(logLen + 1);
                    glGetShaderInfoLog(shader, logLen, nullptr, log.data());
                    std::cerr << "[Skybox] Shader compile error (" << name << "): " << log.data() << "\n";
                }
                glDeleteShader(shader);
                return 0;
            }
            return shader;
            };

        GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertSrc, "vertex");
        GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragSrc, "fragment");
        if (!vertShader || !fragShader) {
            glDeleteTextures(1, &equirectTexture);
            glDeleteTextures(1, &cubeMapTexture);
            cubeMapTexture = 0;
            return false;
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vertShader);
        glAttachShader(program, fragShader);
        glLinkProgram(program);
        GLint linkStatus;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (!linkStatus) {
            GLint logLen = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 1) {
                std::vector<char> log(logLen + 1);
                glGetProgramInfoLog(program, logLen, nullptr, log.data());
                std::cerr << "[Skybox] Program link error: " << log.data() << "\n";
            }
            glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            glDeleteProgram(program);
            glDeleteTextures(1, &equirectTexture);
            glDeleteTextures(1, &cubeMapTexture);
            cubeMapTexture = 0;
            return false;
        }
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);

        // 5. Build unit cube mesh (36 vertices = 12 triangles)
        float cubeVertices[] = {
            // Back face
            -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,
             // Front face
             -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
              1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
              // Left face
              -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,
              -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,
              // Right face
               1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
               1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
               // Bottom face
               -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,
                1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,
                // Top face
                -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
                 1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f
        };

        GLuint cubeVAO, cubeVBO;
        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);
        glBindVertexArray(cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        // 6. Capture framebuffer
        GLuint captureFBO;
        glGenFramebuffers(1, &captureFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

        glm::mat4 captureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        glm::mat4 captureViews[] = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
        };

        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1, GL_FALSE, glm::value_ptr(captureProjection));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, equirectTexture);
        glUniform1i(glGetUniformLocation(program, "uEquirectangularMap"), 0);

        // Save current viewport so we can restore it after the capture pass.
        GLint prevViewport[4];
        glGetIntegerv(GL_VIEWPORT, prevViewport);

        glViewport(0, 0, cubeSize, cubeSize);
        glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
        glBindVertexArray(cubeVAO);

        glDisable(GL_CULL_FACE);
        for (unsigned int i = 0; i < 6; ++i) {
            glUniformMatrix4fv(glGetUniformLocation(program, "uView"), 1, GL_FALSE, glm::value_ptr(captureViews[i]));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubeMapTexture, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        glEnable(GL_CULL_FACE);

        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        // 7. Cleanup — restore FBO and viewport before returning.
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glDeleteFramebuffers(1, &captureFBO);
        glDeleteVertexArrays(1, &cubeVAO);
        glDeleteBuffers(1, &cubeVBO);
        glDeleteProgram(program);
        glDeleteTextures(1, &equirectTexture);

        std::cout << "[Skybox] HDR to cubemap conversion completed.\n";
        return true;
    }

    bool Skybox::LoadFromFiles(const std::vector<std::string>& faces)
    {
        if (faces.size() != 6) {
            std::cerr << "[Skybox] Need exactly 6 faces (right, left, top, bottom, front, back)\n";
            return false;
        }

        glGenTextures(1, &cubeMapTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);

        stbi_set_flip_vertically_on_load(false);

        for (unsigned int i = 0; i < faces.size(); i++) {
            int width, height, nrChannels;
            unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data) {
                GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                stbi_image_free(data);
            }
            else {
                std::cerr << "[Skybox] Failed to load texture: " << faces[i] << "\n";
                return false;
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        return true;
    }

    bool Skybox::GenerateProcedural()
    {
        // Each cubemap face is 512x512.  For every pixel we compute the 3-D
        // direction the texel represents, then shade it as sky/horizon/ground.
        //
        // Face layout (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i):
        //   0 = +X   1 = -X   2 = +Y   3 = -Y   4 = +Z   5 = -Z
        //
        // For face i, pixel (px, py) in [0, size) maps to UV in [-1, +1]:
        //   u = (px + 0.5) / size * 2 - 1
        //   v = (py + 0.5) / size * 2 - 1   (v grows downward in GL)
        // Then the 3-D direction vector is:
        //   +X: dir = ( 1,  v, -u)
        //   -X: dir = (-1,  v,  u)
        //   +Y: dir = ( u,  1, -v)   (top face — sky zenith)
        //   -Y: dir = ( u, -1,  v)   (bottom face — ground)
        //   +Z: dir = ( u,  v,  1)
        //   -Z: dir = (-u,  v, -1)
        // We then normalize and use the Y component to drive the gradient.

        const int size = 512;

        // Sky colours
        const float zenithR = 0.10f, zenithG = 0.35f, zenithB = 0.85f;   // deep blue
        const float horizR = 0.65f, horizG = 0.80f, horizB = 0.95f;   // pale sky blue
        const float sunR = 1.00f, sunG = 0.90f, sunB = 0.60f;   // warm sun tint
        const float groundR = 0.28f, groundG = 0.22f, groundB = 0.16f;   // earthy brown
        const float fogR = 0.75f, fogG = 0.72f, fogB = 0.68f;   // horizon haze

        // Sun direction (matches default shader uniform)
        const float sdx = 0.5f, sdy = 0.8f, sdz = 0.2f;
        const float sdLen = std::sqrt(sdx * sdx + sdy * sdy + sdz * sdz);
        const float sunDirX = sdx / sdLen, sunDirY = sdy / sdLen, sunDirZ = sdz / sdLen;

        auto clampByte = [](float v) -> unsigned char {
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            return (unsigned char)(v * 255.0f + 0.5f);
            };

        glGenTextures(1, &cubeMapTexture);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);

        for (int face = 0; face < 6; ++face)
        {
            std::vector<unsigned char> facePixels(size * size * 3);

            for (int py = 0; py < size; ++py)
            {
                for (int px = 0; px < size; ++px)
                {
                    // UV in [-1, +1], centred on pixel
                    float u = ((px + 0.5f) / size) * 2.0f - 1.0f;
                    float v = ((py + 0.5f) / size) * 2.0f - 1.0f;

                    // 3-D direction for this texel
                    float dx = 0, dy = 0, dz = 0;
                    switch (face) {
                    case 0: dx = 1; dy = -v; dz = -u; break; // +X
                    case 1: dx = -1; dy = -v; dz = u; break; // -X
                    case 2: dx = u; dy = 1; dz = v; break; // +Y  (straight up)
                    case 3: dx = u; dy = -1; dz = -v; break; // -Y  (straight down)
                    case 4: dx = u; dy = -v; dz = 1; break; // +Z
                    case 5: dx = -u; dy = -v; dz = -1; break; // -Z
                    }
                    float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                    dx /= len; dy /= len; dz /= len;

                    // dy is the elevation: +1 = zenith, 0 = horizon, -1 = nadir
                    float elevation = dy; // [-1 .. +1]

                    float r, g, b;

                    if (elevation >= 0.0f)
                    {
                        // Sky hemisphere: lerp from horizon to zenith colour
                        float t = elevation; // 0 at horizon, 1 at zenith
                        // Slight S-curve for a more natural gradient
                        t = t * t * (3.0f - 2.0f * t);

                        r = horizR + t * (zenithR - horizR);
                        g = horizG + t * (zenithG - horizG);
                        b = horizB + t * (zenithB - horizB);

                        // Horizon haze band (very thin strip at elevation ~ 0)
                        float hazeFactor = std::exp(-elevation * 12.0f);
                        r = r * (1 - hazeFactor) + fogR * hazeFactor;
                        g = g * (1 - hazeFactor) + fogG * hazeFactor;
                        b = b * (1 - hazeFactor) + fogB * hazeFactor;

                        // Sun glow
                        float sunDot = dx * sunDirX + dy * sunDirY + dz * sunDirZ;
                        if (sunDot > 0.0f) {
                            float glow = std::pow(sunDot, 48.0f) * 1.8f;  // halo
                            float disc = std::pow(sunDot, 512.0f) * 4.0f; // disc centre
                            float total = glow + disc;
                            r = r + sunR * total;
                            g = g + sunG * total;
                            b = b + sunB * total;
                        }

                        // Occasional stars (only visible away from sun / horizon)
                        if (elevation > 0.05f) {
                            float starNoise = std::sin(px * 0.11f + face * 37.3f)
                                * std::cos(py * 0.13f + face * 19.7f)
                                * std::sin((px * 0.07f + py * 0.09f));
                            if (starNoise > 0.975f) {
                                float star = (starNoise - 0.975f) / 0.025f;
                                float brightness = star * (1.0f - elevation * 0.5f) * 0.9f;
                                r += brightness;
                                g += brightness;
                                b += brightness;
                            }
                        }
                    }
                    else
                    {
                        // Ground hemisphere
                        float t = -elevation; // 0 at horizon, 1 at nadir
                        // Fade from haze colour at horizon to ground colour below
                        float s = t * t * (3.0f - 2.0f * t);
                        r = fogR + s * (groundR - fogR);
                        g = fogG + s * (groundG - fogG);
                        b = fogB + s * (groundB - fogB);
                    }

                    int idx = (py * size + px) * 3;
                    facePixels[idx + 0] = clampByte(r);
                    facePixels[idx + 1] = clampByte(g);
                    facePixels[idx + 2] = clampByte(b);
                }
            }

            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0, GL_RGB, size, size, 0, GL_RGB,
                GL_UNSIGNED_BYTE, facePixels.data());
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            std::cerr << "[Skybox] OpenGL error during procedural generation: " << err << "\n";
            return false;
        }
        return true;
    }

    void Skybox::Render(const glm::mat4& viewMatrix, const glm::mat4& projMatrix)
    {
        if (cubeMapTexture == 0 || skyboxShader == 0) return;

        glDepthFunc(GL_LEQUAL);

        glDisable(GL_CULL_FACE);

        glUseProgram(skyboxShader);

        // Set matrices
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "uView"), 1, GL_FALSE, glm::value_ptr(viewMatrix));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader, "uProj"), 1, GL_FALSE, glm::value_ptr(projMatrix));
        glUniform1f(glGetUniformLocation(skyboxShader, "uRotation"), skyRotation);

        // Sun uniforms
        glUniform3fv(glGetUniformLocation(skyboxShader, "uSunDir"), 1, glm::value_ptr(m_sunDirection));
        glUniform3fv(glGetUniformLocation(skyboxShader, "uSunColor"), 1, glm::value_ptr(m_sunColor));
        glUniform1f(glGetUniformLocation(skyboxShader, "uSunIntensity"), m_sunIntensity);

        // Fog (optional)
        glUniform3f(glGetUniformLocation(skyboxShader, "uFogColor"), 0.5f, 0.6f, 0.7f);
        glUniform1f(glGetUniformLocation(skyboxShader, "uFogDensity"), 0.0f);

        // Bind cubemap texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubeMapTexture);
        glUniform1i(glGetUniformLocation(skyboxShader, "uSkybox"), 0);

        // Draw
        glBindVertexArray(skyboxVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        // Re-enable culling for the rest of the scene
        glEnable(GL_CULL_FACE);
        glDepthFunc(GL_LESS);
    }

    void Skybox::cleanup()
    {
        if (skyboxVAO) glDeleteVertexArrays(1, &skyboxVAO);
        if (skyboxVBO) glDeleteBuffers(1, &skyboxVBO);
        if (cubeMapTexture) glDeleteTextures(1, &cubeMapTexture);
        if (skyboxShader) glDeleteProgram(skyboxShader);

        skyboxVAO = 0;
        skyboxVBO = 0;
        cubeMapTexture = 0;
        skyboxShader = 0;
    }

} // namespace HonHengine