#include "renderer.h"
#include "baselight.h"
#include "sphere.h"
#include "pointlight.h"
#include "directionallight.h"
#include "scenemanager.h"
#include <cfloat>
#include <SDL2/SDL.h>
#include <cmath>

Vector3 *Renderer::CanvasToViewport(int x, int y)
{
    return new Vector3(x * sceneManager->viewportWidth / sceneManager->canvasWidth,
                       -y * sceneManager->viewportHeight / sceneManager->canvasHeight,
                       sceneManager->viewportDistance);
}

std::pair<double, BaseObject *> Renderer::ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance, double maxDistance, bool returnFirstFound)
{
    double closestDistance = DBL_MAX;
    BaseObject *closestObject = nullptr;
    for (BaseObject *object : *sceneManager->objects)
    {
        if (object->type == SPHERE)
        {
            std::pair<double, double> intersection = ((Sphere *)object)->IntersectRaySphere(rayOrigin, rayDirection, dotDD);
            if (intersection.first < closestDistance && intersection.first > minDistance && intersection.first < maxDistance)
            {
                closestDistance = intersection.first;
                closestObject = object;
            }
            if (intersection.second < closestDistance && intersection.second > minDistance && intersection.second < maxDistance)
            {
                closestDistance = intersection.second;
                closestObject = object;
            }
        }
        if (returnFirstFound && closestObject != nullptr)
        {
            return std::pair<double, BaseObject *>(closestDistance, closestObject);
        }
    }
    return std::pair<double, BaseObject *>(closestDistance, closestObject);
}

Vector3 *Renderer::ReflectRay(Vector3 *point, Vector3 *normal)
{
    return new Vector3(*normal * 2 * normal->dot(point) - *point);
}

Color *Renderer::ComputeLightning(Vector3 *point, Vector3 *normal, Vector3 *viewDirection, Material *material)
{
    double intensity = 0;
    for (BaseLight *light : *sceneManager->lights)
    {
        // above max distance, object casts no shadow
        double maxDistance = DBL_MAX;
        Vector3 *lightDirection;

        if (light->type == ObjectType::AMBIENT_LIGHT)
        {
            intensity += light->intensity;
            continue;
        }
        if (light->type == ObjectType::POINT_LIGHT)
        {
            lightDirection = new Vector3((*((PointLight *)light)->position - *point).normalize());
            maxDistance = lightDirection->len();
        }
        else if (light->type == ObjectType::DIRECTIONAL_LIGHT)
        {
            lightDirection = ((DirectionalLight *)light)->direction;
        }
        else
        {
            continue;
        }
        std::pair<double, BaseObject *> intersection = ClosestIntersection(point, lightDirection, lightDirection->dot(lightDirection), 0.001, maxDistance, true);
        if (intersection.first < maxDistance)
        {
            continue;
        }
        double DotNL = normal->dot(lightDirection);

        // diffuse lighting
        if (DotNL > 0)
        {
            intensity += light->intensity * DotNL / (normal->len() * lightDirection->len());
        }

        // specular lighting
        if (material->specularity >= 0)
        {
            Vector3 *reflectedRay = ReflectRay(lightDirection, normal);
            double DotRV = reflectedRay->dot(viewDirection);
            if (DotRV > 0)
            {
                intensity += light->intensity * pow(DotRV / (reflectedRay->len() * viewDirection->len()), material->specularity);
            }
        }
    }
    return new Color(*material->color * intensity);
}

Color *Renderer::TraceRay(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, int depth, int minDistance)
{

    std::pair<double, BaseObject *> intersection = ClosestIntersection(rayOrigin, rayDirection, dotDD, minDistance);
    if (std::get<1>(intersection) == nullptr)
    {
        return new Color(0, 0, 0, 255);
    }
    Material *material = std::get<1>(intersection)->material;

    Vector3 *point = new Vector3(*rayOrigin + *rayDirection * intersection.first);
    Vector3 *normal = new Vector3((*point - *std::get<1>(intersection)->position).normalize());
    Color *color = ComputeLightning(point, normal, new Vector3(*rayDirection * -1), std::get<1>(intersection)->material);
    if (depth == sceneManager->maxRecursionDepth || material->reflectivity <= 0)
    {
        return color;
    }
    Vector3 *reflectedRay = ReflectRay(new Vector3(*rayDirection * -1), normal);
    Color *reflectedColor = TraceRay(point, reflectedRay, reflectedRay->dot(reflectedRay), depth + 1, minDistance);

    Color *returnColor = new Color(*color * (1 - material->reflectivity) + *reflectedColor * material->reflectivity);
    return returnColor;
}

std::vector<double> Renderer::Interpolate(double i0, double d0, double i1, double d1)
{
    if (i0 == i1)
    {
        return {d0};
    }
    std::vector<double> values = {};
    double a = (d1 - d0) / (i1 - i0);
    double d = d0;
    for (int x = i0; x < i1; x++)
    {
        values.push_back(d);
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
}

void Renderer::render()
{
    // Clear the screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    const double cw2 = sceneManager->canvasWidth / 2;
    const double ch2 = sceneManager->canvasHeight / 2;

    for (int x = -cw2; x < cw2; x++)
    {
        for (int y = -ch2; y < ch2; y++)
        {
            Vector3 *vectorDirection = CanvasToViewport(x, y);
            Color *color = TraceRay(
                sceneManager->currentCamera->position,
                vectorDirection,
                vectorDirection->dot(vectorDirection),
                0,
                1);
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
            SDL_RenderDrawPoint(renderer, x + cw2, y + ch2);
        }
    }
    SDL_RenderPresent(renderer);
}


void Renderer::DrawWireFrameTriangle(Vector3 *P0, Vector3 *P1, Vector3 *P2, Color *color)
{
    DrawLine(P0, P1, color);
    DrawLine(P0, P2, color);
    DrawLine(P1, P2, color);
    SDL_RenderPresent(renderer);
}

void Renderer::DrawLine(Vector3 *P0, Vector3 *P1, Color *color)
{
    const double cw2 = sceneManager->centeredCW;
    const double ch2 = sceneManager->centeredCH;

    if (abs(P1->x - P0->x) > abs(P1->y - P0->y))
    {
        // horizontal because x > y

        if (P0->x > P1->x)
        {
            std::swap(P0, P1);
        }
        std::vector<double> ys = Interpolate(P0->x, P0->y, P1->x, P1->y);
        for (int x = P0->x; x < P1->x; x++)
        {
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
            SDL_RenderDrawPoint(renderer, x + cw2, ch2 - ys[x - P0->x]);
        }
    }
    else
    {
        // vertical because x < y
        if (P0->y > P1->y)
        {
            std::swap(P0, P1);
        }
        std::vector<double> xs = Interpolate(P0->y, P0->x, P1->y, P1->x);
        for (int y = P0->y; y < P1->y; y++)
        {
            SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
            SDL_RenderDrawPoint(renderer, xs[y - P0->y] + cw2, ch2 - y);
        }
    }
}

void Renderer::cleanup()
{
    // Cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
