#include "renderer.h"
#include "baselight.h"
#include "sphere.h"
#include "pointlight.h"
#include "directionallight.h"
#include "scenemanager.h"
#include <cfloat>
#include <SDL.h>
#include <cmath>

Vector3 *Renderer::CanvasToViewport(int x, int y)
{
    return new Vector3(x * sceneManager->viewportWidth / sceneManager->canvasWidth,
                       -y * sceneManager->viewportHeight / sceneManager->canvasHeight,
                       sceneManager->viewportDistance);
}

std::tuple<double, BaseObject *> Renderer::ClosestIntersection(Vector3 *rayOrigin, Vector3 *rayDirection, double dotDD, double minDistance, double maxDistance, bool returnFirstFound)
{
    double closestDistance = DBL_MAX;
    BaseObject *closestObject = nullptr;
    for (BaseObject *object : *sceneManager->objects)
    {
        if (object->type == SPHERE)
        {
            std::tuple<double, double> intersection = ((Sphere *)object)->IntersectRaySphere(rayOrigin, rayDirection, dotDD);
            if (std::get<0>(intersection) < closestDistance && std::get<0>(intersection) > minDistance && std::get<0>(intersection) < maxDistance)
            {
                closestDistance = std::get<0>(intersection);
                closestObject = object;
            }
            if (std::get<1>(intersection) < closestDistance && std::get<1>(intersection) > minDistance && std::get<1>(intersection) < maxDistance)
            {
                closestDistance = std::get<1>(intersection);
                closestObject = object;
            }
        }
        if (returnFirstFound && closestObject != nullptr)
        {
            return std::tuple(closestDistance, closestObject);
        }
    }
    return std::tuple(closestDistance, closestObject);
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
        std::tuple<double, BaseObject *> intersection = ClosestIntersection(point, lightDirection, lightDirection->dot(lightDirection), 0.001, maxDistance, true);
        if (std::get<0>(intersection) < maxDistance)
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

    std::tuple<double, BaseObject *> intersection = ClosestIntersection(rayOrigin, rayDirection, dotDD, minDistance);
    if (std::get<1>(intersection) == nullptr)
    {
        return new Color(0, 0, 0, 255);
    }
    Material *material = std::get<1>(intersection)->material;

    Vector3 *point = new Vector3(*rayOrigin + *rayDirection * std::get<0>(intersection));
    Vector3 *normal = new Vector3((*point - *std::get<1>(intersection)->position).normalize());
    Color *color = ComputeLightning(point, normal, new Vector3(*rayDirection * -1), std::get<1>(intersection)->material);
    if (depth == sceneManager->maxRecursionDepth || material->reflectivity <= 0)
    {
        return color;
    }
    Vector3 *reflectedRay = ReflectRay(new Vector3(*rayDirection * -1), normal);
    Color *reflectedColor = TraceRay(point, reflectedRay, reflectedRay->dot(reflectedRay), depth + 1, minDistance);
    
    Color* returnColor = new Color(*color * (1 - material->reflectivity) + *reflectedColor * material->reflectivity);
    return returnColor;
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

void Renderer::cleanup()
{
    // Cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
