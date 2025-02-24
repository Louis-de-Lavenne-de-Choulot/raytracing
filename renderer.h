#pragma once
#ifndef RENDERER
#include "renderer.h"
#include "scenemanager.h"
#include "Triangle.h"

#include <SDL2/SDL.h>
#include <cfloat>
#include <array>
#include <map>

namespace PEngine
{
    class Renderer
    {

    private:
        SDL_Window *window;
        SDL_Renderer *renderer;
        SDL_Texture *texture;
        SceneManager *sceneManager;

        std::map<int, double> screenBuffer = std::map<int, double>();
        Uint32 *pixels;
        int windowWidth = 0;
        int windowHeight = 0;

        std::vector<Vertice *> *checkTriangle(std::array<Vertice *, 3> verts);
        // void ApplyRotation(array<Vertice *, 8> &vertices, Quaternion *rotation);
        Vector3 *CanvasToViewport(double x, double y);
        Vector3 *ProjectVertex(Vector3 *v);
        std::pair<double, BaseObject *> ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance = 0, double maxDistance = DBL_MAX, bool returnFirstFound = false);
        Vector3 *ReflectRay(Vector3 *point, Vector3 *normal);
        Color *ComputeLightning(Vector3 *point, Vector3 *normal, Vector3 *viewDirection, Material *material);
        Color *TraceRay(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, int depth, int minDistance);
        std::vector<double> *Interpolate(double i0, double d0, double i1, double d1);

    public:
        Renderer(SceneManager *sceneManager);

        void Get_MouseState(int *x, int *y);
        void Render();
        void SetPixel(int x, int y, Color *color);
        void RenderInstance(BaseObject *obj);
        void DrawTriangle(Triangle *triangle, std::vector<Vertice *> *projected);
        void DrawLine(Vertice *V0, Vertice *V1, Color *color);

        void ForceClean();

        void ForceRender();
        void cleanup();
    };
};
#endif