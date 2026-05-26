#include "UIrenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace HonHengine
{
    void UIRenderer::init(int screenWidth, int screenHeight, GLuint shader)
    {
        uiShader = shader;
        ortho = glm::ortho(0.0f, static_cast<float>(screenWidth),
            static_cast<float>(screenHeight), 0.0f,
            -1.0f, 1.0f);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
            reinterpret_cast<void*>(offsetof(UIVertex, x)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
            reinterpret_cast<void*>(offsetof(UIVertex, u)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
            reinterpret_cast<void*>(offsetof(UIVertex, r)));

        glBindVertexArray(0);
    }

    void UIRenderer::resize(int screenWidth, int screenHeight)
    {
        ortho = glm::ortho(0.0f, static_cast<float>(screenWidth),
            static_cast<float>(screenHeight), 0.0f,
            -1.0f, 1.0f);
    }

    void UIRenderer::beginFrame()
    {
        verts.clear();
    }

    void UIRenderer::pushTriangle(const UIVertex& a, const UIVertex& b, const UIVertex& c)
    {
        verts.push_back(a);
        verts.push_back(b);
        verts.push_back(c);
    }

    void UIRenderer::pushQuad(float x, float y, float w, float h,
        float u0, float v0, float u1, float v1,
        float r, float g, float b, float a)
    {
        UIVertex tl{ x,     y,     u0, v0, r, g, b, a };
        UIVertex tr{ x + w, y,     u1, v0, r, g, b, a };
        UIVertex bl{ x,     y + h, u0, v1, r, g, b, a };
        UIVertex br{ x + w, y + h, u1, v1, r, g, b, a };

        pushTriangle(tl, tr, bl);
        pushTriangle(tr, br, bl);
    }

    void UIRenderer::pushCrosshair(float cx, float cy, float halfLen, float thickness,
        float r, float g, float b, float a)
    {
        pushQuad(cx - halfLen, cy - thickness * 0.5f,
            halfLen * 2.0f, thickness,
            0.0f, 0.0f, 1.0f, 1.0f,
            r, g, b, a);

        pushQuad(cx - thickness * 0.5f, cy - halfLen,
            thickness, halfLen * 2.0f,
            0.0f, 0.0f, 1.0f, 1.0f,
            r, g, b, a);
    }

    void UIRenderer::pushRotatedQuad(float cx, float cy, float w, float h, float angleRad, float r, float g, float b, float a)
    {
        float cosA = std::cos(angleRad);
        float sinA = std::sin(angleRad);
        float hx = w * 0.5f;
        float hy = h * 0.5f;

        auto localRot = [&](float lx, float ly) {
            return UIVertex{
                cx + (lx * cosA - ly * sinA),
                cy + (lx * sinA + ly * cosA),
                0.0f, 0.0f, r, g, b, a
            };
            };

        UIVertex v0 = localRot(-hx, -hy);
        UIVertex v1 = localRot(hx, -hy);
        UIVertex v2 = localRot(hx, hy);
        UIVertex v3 = localRot(-hx, hy);

        verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
        verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);
    }

    void UIRenderer::pushLine(float x1, float y1, float x2, float y2, float thickness, float r, float g, float b, float a)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.0001f) return;

        float nx = -dy / len * (thickness * 0.5f);
        float ny = dx / len * (thickness * 0.5f);

        verts.push_back({ x1 + nx, y1 + ny, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x2 + nx, y2 + ny, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x2 - nx, y2 - ny, 0.0f, 0.0f, r, g, b, a });

        verts.push_back({ x1 + nx, y1 + ny, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x2 - nx, y2 - ny, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x1 - nx, y1 - ny, 0.0f, 0.0f, r, g, b, a });
    }

    void UIRenderer::pushTriangle(float x1, float y1, float x2, float y2, float x3, float y3, float r, float g, float b, float a)
    {
        verts.push_back({ x1, y1, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x2, y2, 0.0f, 0.0f, r, g, b, a });
        verts.push_back({ x3, y3, 0.0f, 0.0f, r, g, b, a });
    }

    void UIRenderer::flush(GLuint atlasID)
    {
        if (verts.empty()) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(uiShader);

        glUniformMatrix4fv(glGetUniformLocation(uiShader, "uOrtho"),
            1, GL_FALSE, glm::value_ptr(ortho));

        bool useAtlas = atlasID != 0;
        glUniform1i(glGetUniformLocation(uiShader, "uUseUIAtlas"), useAtlas ? 1 : 0);
        if (useAtlas)
        {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, atlasID);
            glUniform1i(glGetUniformLocation(uiShader, "uUIAtlas"), 0);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(verts.size() * sizeof(UIVertex)),
            verts.data(), GL_STREAM_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));

        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    void UIRenderer::cleanup()
    {
        if (vbo) { glDeleteBuffers(1, &vbo);        vbo = 0; }
        if (vao) { glDeleteVertexArrays(1, &vao);   vao = 0; }
    }
}