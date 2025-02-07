#include "renderer.h"
#include "baseobject.h"
#include "baselight.h"
#include "vertice.h"
#include "vector3.h"
#include "triangle.h"
#include "pointlight.h"
#include "directionallight.h"
#include "scenemanager.h"
#include <cfloat>
#include <SDL2/SDL.h>
#include <cmath>
#include <array>
#include <vector>

// void Renderer::ApplyRotation(std::array<Vertice *, 8> &vertices, Quaternion *rotation)
// {
//     for (Vertice *v : vertices)
//     {
//         v->position = (*rotation) * Vector3(*v->position);
//     }
// }

Vector3 *Renderer::CanvasToViewport(double x, double y)
{
    return new Vector3(x * sceneManager->canvasWidth / sceneManager->viewportWidth,
                       y * sceneManager->canvasHeight / sceneManager->viewportHeight, 0);
}

Vector3 *Renderer::ProjectVertex(Vector3 *v)
{
    return CanvasToViewport(
        v->x * sceneManager->viewportDistance / v->z,
        v->y * sceneManager->viewportDistance / v->z);
}

std::vector<double> *Renderer::Interpolate(double i0, double d0, double i1, double d1)
{
    if (i0 == i1)
    {
        return new std::vector<double>{d0};
    }
    std::vector<double> *values = new std::vector<double>{};
    double a = (d1 - d0) / (i1 - i0);
    double d = d0;
    for (int x = i0; x < i1; x++)
    {
        values->push_back(d);
        d += a;
    }
    return values;
}

Renderer::Renderer(SceneManager *sceneManager)
{
    this->sceneManager = sceneManager;
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
        return;
    }

    // Create a window
    window = SDL_CreateWindow("Set Pixel Example",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              sceneManager->canvasWidth, sceneManager->canvasHeight, SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return;
    }

    // Create a renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
}

void Renderer::Render()
{
    // Clear the screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    for  (BaseObject *obj : *sceneManager->objects)
    {
        RenderInstance(obj);
    }

    SDL_RenderPresent(renderer);
}

void Renderer::RenderInstance(BaseObject *obj)
{
    std::vector<Vertice *> *projected = new std::vector<Vertice *>();
    
    for (Vertice *v : obj->bVertices)
    {
        Vector3 *vProj;
        Vector3 vPos = *v->position;
        // scale
        vProj = new Vector3(*obj->transform->scale * vPos);
        // rotate
        vProj =  obj->transform->rotation->RotateVector3(vProj);
        // translate
        vProj = new Vector3(*vProj + *obj->transform->position);
        // now that we got world space, project it camera space
        vProj = new Vector3(*vProj - *sceneManager->currentCamera->transform->position);
        Quaternion *q = sceneManager->currentCamera->transform->rotation;
        vProj = q->Conjugate(*q).RotateVector3(vProj);

        // project
        projected->push_back(new Vertice(vProj, v->shade));
    }
    for (Triangle *triangle : obj->bTriangles)
    {
        DrawTriangle(triangle, projected);
    }
}

void Renderer::DrawTriangle(Triangle *triangle, std::vector<Vertice *> *projected)
{
    Vector3 *P0 = ProjectVertex(projected->at(triangle->p0)->position);
    Vector3 *P1 = ProjectVertex(projected->at(triangle->p1)->position);
    Vector3 *P2 = ProjectVertex(projected->at(triangle->p2)->position);
    double h0 = projected->at(triangle->p0)->shade;
    double h1 = projected->at(triangle->p1)->shade;
    double h2 = projected->at(triangle->p2)->shade;

    // sort the points so that Y0 <= Y1 <= Y2
    if (P1->y < P0->y)
    {
        std::swap(P1, P0);
    }
    if (P2->y < P0->y)
    {
        std::swap(P2, P0);
    }
    if (P2->y < P1->y)
    {
        std::swap(P2, P1);
    }

    // Compute X coordinates of edges
    std::vector<double> *x01 = Interpolate(P0->y, P0->x, P1->y, P1->x);
    std::vector<double> *h01 = Interpolate(P0->y, h0, P1->y, h1);
    std::vector<double> *x12 = Interpolate(P1->y, P1->x, P2->y, P2->x);
    std::vector<double> *h12 = Interpolate(P1->y, h1, P2->y, h2);
    std::vector<double> *x02 = Interpolate(P0->y, P0->x, P2->y, P2->x);
    std::vector<double> *h02 = Interpolate(P0->y, h0, P2->y, h2);

    // Concatenate the short sides
    // x01->pop_back();
    // h01->pop_back();
    std::vector<double> *x012 = new std::vector<double>{};
    std::vector<double> *h012 = new std::vector<double>{};

    x012->insert(x012->end(), x01->begin(), x01->end());
    x012->insert(x012->end(), x12->begin(), x12->end());

    h012->insert(h012->end(), h01->begin(), h01->end());
    h012->insert(h012->end(), h12->begin(), h12->end());

    // Determine left from right
    double m = floor(x012->size() / 2);
    std::vector<double> *xleft = x012;
    std::vector<double> *xright = x02;
    std::vector<double> *hleft = h012;
    std::vector<double> *hright = h02;
    if (x02->at(m) < x012->at(m))
    {
        xleft = x02;
        xright = x012;
        hleft = h02;
        hright = h012;
    }

    Color *color = triangle->material->color;

    for (int y = P0->y; y < P2->y; y++)
    {
        double ypy = y - P0->y;
        double xl = xleft->at(ypy);
        double xr = xright->at(ypy);
        std::vector<double> *hsegment = Interpolate(xl, hleft->at(y - P0->y), xr, hright->at(y - P0->y));

        for (int x = xl; x < xr; x++)
        {
            double xxl = x - xl;
            Color *c = new Color(*color * hsegment->at(xxl));
            SDL_SetRenderDrawColor(renderer, c->r, c->g, c->b, c->a);
            SDL_RenderDrawPoint(renderer, x + sceneManager->centeredCW, sceneManager->centeredCH - y);
        }
    }

    color = triangle->material->outlineColor;
    if (color)
    {
        DrawLine(projected->at(triangle->p0), projected->at(triangle->p1), color);
        DrawLine(projected->at(triangle->p0), projected->at(triangle->p2), color);
        DrawLine(projected->at(triangle->p1), projected->at(triangle->p2), color);
    }

    SDL_RenderPresent(renderer);
}

void Renderer::DrawLine(Vertice *V0, Vertice *V1, Color *color)
{
    Vector3 *P0 = ProjectVertex(V0->position);
    Vector3 *P1 = ProjectVertex(V1->position);
    if (abs(P1->x - P0->x) > abs(P1->y - P0->y))
    {
        // horizontal because x > y

        if (P0->x > P1->x)
        {
            std::swap(P0, P1);
        }
        std::vector<double> *ys = Interpolate(P0->x, P0->y, P1->x, P1->y);
        for (int x = P0->x; x < P1->x; x++)
        {
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
            SDL_RenderDrawPoint(renderer, x + sceneManager->centeredCW, sceneManager->centeredCH - ys->at(x - P0->x));
        }
    }
    else
    {
        // vertical because x < y
        if (P0->y > P1->y)
        {
            std::swap(P0, P1);
        }
        std::vector<double> *xs = Interpolate(P0->y, P0->x, P1->y, P1->x);
        for (int y = P0->y; y < P1->y; y++)
        {
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
            SDL_RenderDrawPoint(renderer, xs->at(y - P0->y) + sceneManager->centeredCW, sceneManager->centeredCH - y);
        }
    }
}

void Renderer::ForceClean()
{
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
}

void Renderer::ForceRender()
{
    SDL_RenderPresent(renderer);
}

void Renderer::cleanup()
{
    // Cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
