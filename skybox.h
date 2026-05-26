// skybox.h
#pragma once
#ifndef SKYBOX_H
#define SKYBOX_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace HonHengine
{
    class Skybox
    {
    public:
        Skybox();
        ~Skybox();

        // Load a cubemap from 6 individual textures (front, back, up, down, left, right)
        bool LoadFromFiles(const std::vector<std::string>& faces);

        // Load a cubemap from a single HDRI/equirectangular file (requires stb_image)
        bool LoadFromHDR(const std::string& hdrFile);

        // Generate a procedural cubemap (gradient sky with stars)
        bool GenerateProcedural();

        // Render the skybox
        void Render(const glm::mat4& viewMatrix, const glm::mat4& projMatrix);

        void cleanup();

        // Get the cubemap texture ID for use in other shaders (reflections)
        GLuint GetCubeMapTexture() const { return cubeMapTexture; }

        // Set rotation (for time-of-day effects)
        void SetRotation(float rotation) { skyRotation = rotation; }

        // Set sun properties (driven by the scene's directional light)
        void SetSunDirection(const glm::vec3& dir) { m_sunDirection = dir; }
        void SetSunColor(const glm::vec3& color) { m_sunColor = color; }
        void SetSunIntensity(float intensity) { m_sunIntensity = intensity; }

    private:
        GLuint cubeMapTexture;
        GLuint skyboxVAO;
        GLuint skyboxVBO;
        GLuint skyboxShader;

        float     skyRotation;
        glm::vec3 m_sunDirection = glm::vec3(0.5f, 0.8f, 0.2f);
        glm::vec3 m_sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
        float     m_sunIntensity = 0.8f;

        void CreateCubeGeometry();
        bool CompileShaders();
    };

} // namespace HonHengine

#endif