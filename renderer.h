#pragma once
#ifndef RENDERER
#include "renderer.h"
#include "verticeDTO.h"
#include "scenemanager.h"
#include "Triangle.h"
#include <SDL.h>
#include <cfloat>
#include <array>
#include <map>
#include <unordered_map>

namespace PEngine
{
    class Renderer
    {

    private:
        SDL_Window *window;
        SDL_Renderer *renderer;
        SDL_Texture *texture;
        SceneManager *sceneManager; 

        Uint32 *pixels;
        int windowWidth = 0;
        int windowHeight = 0;
        float* zBuffer = nullptr;
        std::atomic<int> workIndex{ 0 };
        unsigned int threadCount = 1;

        std::vector<VerticeDTO> checkTriangle(std::array<VerticeDTO, 3> verts);
        Vector3 CanvasToViewport(double x, double y);
        Vector3 ProjectVertex(Vector3 *v);
        Color ComputeIllumination(const Vector3 normalized, const Vector3 worldPos, Color shadedColor);
        std::pair<double, BaseObject *> ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance = 0, double maxDistance = DBL_MAX, bool returnFirstFound = false);
        std::vector<double> Interpolate(double i0, double d0, double i1, double d1);

    public:
        static Material* DefaultMaterial;
        Renderer(SceneManager *sceneManager);

        bool IsFacing(std::array<VerticeDTO, 3> tArr);
        //TODO
        //double ComputeShading(double shade);
        void Get_MouseState(int *x, int *y);
        void Render();
        void SetPixel(int x, int y, Color color);
        void RenderInstance(BaseObject *obj);
        void DrawTriangle(Triangle *triangle, std::vector<VerticeDTO> *projected);
        void DrawLine(Vertice *V0, Vertice *V1, Color color);

        void ForceClean();

        void ForceRender();
        void cleanup();
    };
};
#endif