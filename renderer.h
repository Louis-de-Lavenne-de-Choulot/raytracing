#pragma once
#ifndef RENDERER
#include "renderer.h"
#include "scenemanager.h"
#include "Triangle.h"

#include <SDL2/SDL.h>
#include <cfloat>
#include <array>

class Renderer
{
    
private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    SceneManager *sceneManager;

    std::vector<Vertice *> *checkTriangle(std::array<Vertice*, 3> verts);

    // void ApplyRotation(std::array<Vertice *, 8> &vertices, Quaternion *rotation);
    Vector3 *CanvasToViewport(double x, double y);
    Vector3 *ProjectVertex(Vector3 *v);
    std::pair<double, BaseObject *> ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance = 0, double maxDistance = DBL_MAX, bool returnFirstFound = false);
    Vector3* ReflectRay(Vector3* point, Vector3* normal);
    Color* ComputeLightning(Vector3* point, Vector3* normal, Vector3* viewDirection, Material* material);
    Color* TraceRay(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, int depth, int minDistance);
    std::vector<double> *Interpolate(double i0, double d0, double i1, double d1);
public:
    Renderer(SceneManager *sceneManager);

    void Render();
    void RenderInstance(BaseObject *obj);
    void DrawTriangle(Triangle *triangle, std::vector<Vertice *> *projected);
    void DrawLine(Vertice *V0, Vertice *V1, Color *color);

    void ForceClean();

    void ForceRender();
    void cleanup();
};
#endif