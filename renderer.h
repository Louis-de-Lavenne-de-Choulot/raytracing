#pragma once
#ifndef RENDERER
#include "renderer.h"
#include "scenemanager.h"

#include <SDL2/SDL.h>
#include <cfloat>
#include <iostream>

class Renderer
{
    
private:
    SDL_Window *window;
    SDL_Renderer *renderer;
    SceneManager *sceneManager;
double d = 0;
    

    Vector3* CanvasToViewport(int x, int y);
    std::pair<double, BaseObject*> ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance = 0, double maxDistance = DBL_MAX, bool returnFirstFound = false);
    Vector3* ReflectRay(Vector3* point, Vector3* normal);
    Color* ComputeLightning(Vector3* point, Vector3* normal, Vector3* viewDirection, Material* material);
    Color* TraceRay(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, int depth, int minDistance);
    std::vector<double> Interpolate(double i0, double d0, double i1, double d1);
public:
    Renderer(SceneManager *sceneManager);

    void render();
    void DrawWireFrameTriangle(Vector3 *P0, Vector3 *P1, Vector3 *P2, Color *color);
    void DrawLine(Vector3 *P0, Vector3 *P1, Color *color);

    void cleanup();
};
#endif