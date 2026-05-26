#pragma once
#ifndef UIRENDERER
#define UIRENDERER

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace HonHengine
{
    struct UIVertex
    {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    class UIRenderer
    {
    public:
        UIRenderer() : uiShader(0), vao(0), vbo(0), ortho(1.0f) {}
        ~UIRenderer() { cleanup(); }

        void init(int screenWidth, int screenHeight, GLuint uiShader);
        void resize(int screenWidth, int screenHeight);
        void beginFrame();
        void pushQuad(float x, float y, float w, float h,
            float u0, float v0, float u1, float v1,
            float r, float g, float b, float a);
        void pushCrosshair(float cx, float cy, float halfLen, float thickness,
            float r, float g, float b, float a);
        void pushRotatedQuad(float cx, float cy, float w, float h, float angleRad, float r, float g, float b, float a);
        void pushLine(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a);
        void pushTriangle(float x1, float y1, float x2, float y2, float x3, float y3, float r, float g, float b, float a);
        void flush(GLuint atlasID);
        void cleanup();

    private:
        GLuint uiShader = 0;
        GLuint vao = 0;
        GLuint vbo = 0;
        glm::mat4 ortho;
        std::vector<UIVertex> verts;

        void pushTriangle(const UIVertex& a, const UIVertex& b, const UIVertex& c);
    };
}

#endif