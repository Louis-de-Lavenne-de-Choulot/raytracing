#pragma once
// ide_viewport_overlays.h  —  HonHon Engine IDE  —  Overlays 3D (grille, axes, icônes, frustum)
// =============================================================================
// Gère :
//   - Grille XZ / XY / YZ
//   - Axes RGB (X=rouge, Y=vert, Z=bleu)
//   - Icônes sphériques pour lumières (point, directionnelle)
//   - Frustum des caméras sélectionnées
// =============================================================================

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

// -----------------------------------------------------------------------------
//  Dessine une grille dans un plan donné (XZ, XY, YZ)
//  Utilise des lignes GL_LINES (immédiat) ; à appeler après le rendu principal
//  avec un shader simple ou en mode debug.
// -----------------------------------------------------------------------------
inline void DrawGrid(const glm::mat4& view, const glm::mat4& proj,
    float size = 20.f, int divisions = 20,
    int plane = 0) {  // 0=XZ, 1=XY, 2=YZ
    // Sauvegarde les états OpenGL
    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);  // permet aux lignes d'être visibles même derrière objets
    glLineWidth(1.0f);

    // Charge les matrices
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));

    float step = size / (float)divisions;
    float start = -size * 0.5f;
    float end = size * 0.5f;

    glBegin(GL_LINES);
    // Couleur gris clair pour les lignes
    glColor3f(0.5f, 0.5f, 0.5f);
    for (int i = 0; i <= divisions; ++i) {
        float t = start + i * step;
        if (plane == 0) { // XZ
            glVertex3f(t, 0, start); glVertex3f(t, 0, end);
            glVertex3f(start, 0, t); glVertex3f(end, 0, t);
        }
        else if (plane == 1) { // XY
            glVertex3f(t, start, 0); glVertex3f(t, end, 0);
            glVertex3f(start, t, 0); glVertex3f(end, t, 0);
        }
        else { // YZ
            glVertex3f(0, t, start); glVertex3f(0, t, end);
            glVertex3f(0, start, t); glVertex3f(0, end, t);
        }
    }
    glEnd();

    // Axes centraux plus visibles (rouge X, vert Y, bleu Z)
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    if (plane == 0) {
        glColor3f(1, 0, 0); glVertex3f(0, 0, 0); glVertex3f(size * 0.5f, 0, 0);
        glColor3f(0, 1, 0); glVertex3f(0, 0, 0); glVertex3f(0, size * 0.5f, 0);
        glColor3f(0, 0, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, size * 0.5f);
    }
    else if (plane == 1) {
        glColor3f(1, 0, 0); glVertex3f(0, 0, 0); glVertex3f(size * 0.5f, 0, 0);
        glColor3f(0, 1, 0); glVertex3f(0, 0, 0); glVertex3f(0, size * 0.5f, 0);
        glColor3f(0, 0, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, size * 0.5f);
    }
    else {
        glColor3f(1, 0, 0); glVertex3f(0, 0, 0); glVertex3f(size * 0.5f, 0, 0);
        glColor3f(0, 1, 0); glVertex3f(0, 0, 0); glVertex3f(0, size * 0.5f, 0);
        glColor3f(0, 0, 1); glVertex3f(0, 0, 0); glVertex3f(0, 0, size * 0.5f);
    }
    glEnd();

    glDepthMask(GL_TRUE);
    glPopAttrib();
}

// -----------------------------------------------------------------------------
//  Dessine une sphère billboardée (icône pour luminaire)
//  (simplifié : dessine un cercle toujours face à la caméra)
// -----------------------------------------------------------------------------
inline void DrawBillboardIcon(const glm::vec3& center, float radius, const glm::vec3& color,
    const glm::mat4& view, const glm::mat4& proj) {
    // Récupère les vecteurs caméra depuis la matrice de vue
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
    glm::vec3 up = glm::normalize(glm::vec3(invView[1]));

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);   // toujours visible par dessus
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));

    glBegin(GL_TRIANGLE_FAN);
    glColor4f(color.r, color.g, color.b, 0.8f);
    glVertex3f(center.x, center.y, center.z);
    const int segments = 16;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / (float)segments;
        glm::vec3 offset = right * cosf(angle) * radius + up * sinf(angle) * radius;
        glVertex3f(center.x + offset.x, center.y + offset.y, center.z + offset.z);
    }
    glEnd();
    glPopAttrib();
}

// -----------------------------------------------------------------------------
//  Dessine le frustum d'une caméra (lignes)
//  cameraPos, cameraDir (direction), fov rad, aspect, far, up
// -----------------------------------------------------------------------------
inline void DrawFrustum(const glm::vec3& pos, const glm::vec3& dir, float fovRad, float aspect, float farDist,
    const glm::vec3& up, const glm::mat4& view, const glm::mat4& proj) {
    // Calcule les 8 sommets du frustum
    glm::vec3 forward = glm::normalize(dir);
    glm::vec3 right = glm::normalize(glm::cross(forward, up));
    glm::vec3 realUp = glm::cross(right, forward);

    float tanHalfFov = tanf(fovRad * 0.5f);
    float farH = tanHalfFov * farDist;
    float farW = farH * aspect;

    glm::vec3 farCenter = pos + forward * farDist;
    glm::vec3 farTopRight = farCenter + realUp * farH + right * farW;
    glm::vec3 farTopLeft = farCenter + realUp * farH - right * farW;
    glm::vec3 farBotRight = farCenter - realUp * farH + right * farW;
    glm::vec3 farBotLeft = farCenter - realUp * farH - right * farW;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);
    glColor3f(0.6f, 0.8f, 1.0f);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(proj));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(view));

    glBegin(GL_LINES);
    // Lignes depuis le centre vers les coins du fond
    glVertex3f(pos.x, pos.y, pos.z); glVertex3f(farTopRight.x, farTopRight.y, farTopRight.z);
    glVertex3f(pos.x, pos.y, pos.z); glVertex3f(farTopLeft.x, farTopLeft.y, farTopLeft.z);
    glVertex3f(pos.x, pos.y, pos.z); glVertex3f(farBotRight.x, farBotRight.y, farBotRight.z);
    glVertex3f(pos.x, pos.y, pos.z); glVertex3f(farBotLeft.x, farBotLeft.y, farBotLeft.z);
    // Rectangle du fond
    glVertex3f(farTopLeft.x, farTopLeft.y, farTopLeft.z); glVertex3f(farTopRight.x, farTopRight.y, farTopRight.z);
    glVertex3f(farTopRight.x, farTopRight.y, farTopRight.z); glVertex3f(farBotRight.x, farBotRight.y, farBotRight.z);
    glVertex3f(farBotRight.x, farBotRight.y, farBotRight.z); glVertex3f(farBotLeft.x, farBotLeft.y, farBotLeft.z);
    glVertex3f(farBotLeft.x, farBotLeft.y, farBotLeft.z); glVertex3f(farTopLeft.x, farTopLeft.y, farTopLeft.z);
    glEnd();
    glPopAttrib();
}