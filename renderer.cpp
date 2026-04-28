#include <windows.h>
#include <algorithm>
#include <iostream>
#include "renderer.h"
#include "baseobject.h"
#include "baselight.h"
#include "vertice.h"
#include "verticeDTO.h"
#include "vector3.h"
#include "triangle.h"
#include "pointlight.h"
#include "directionallight.h"
#include "scenemanager.h"
#include "settings.h"
#include <cfloat>
#include <SDL.h>
#include <cmath>
#include <thread>
#include <array>
#include <vector>
#include <synchapi.h>
#include <algorithm>
#include <span>
namespace PEngine
{
    Material* Renderer::DefaultMaterial = new Material(0, 0.0, Color(255, 0, 255, 255));

    //---------------------------------------------------------------------------------------------------
    //               FINDING INTERSECTION BETWEEN 2xVECTOR3 (a, b) AND PLANE p (0, 0, pd)               |
    //                                                                                                  |
    //               the goal is to find the parametric equations of x and z                            |
    //                   then derive x using result of the derivation on z                              |
    //                                                                                                  |
    //               in short, calculate dx = (bx-ax)t+ax with t = any                                  |
    //                                   dy = (by-ay)t+ay with t = any                                  |
    //                                   dz = (bz-az)t+az with t = any                                  |
    //                                                                                                  |
    //              We know dz = pd because plane is on z only so the plan has equation 0x + 0y + z = pd|
    //              With dx, dy, dz we can get  0dx + 0dy + dz = pd   ===>  dz = pd                     |
    //              dz = (bz-az)t+az = pd  ==so==  t = (pd+az)/(bz-az)                                  |
    //              We know that (bz-az) != 0 because only intersecting lines are flagged               |
    //                                                                                                  |
    //                                                                                                  |
    //              We shorten the equations by plugging our newfound t in dx dy dz                     |
    //              dx = (bx - ax) * (pz + az)/(bz-az) + ax                                             |
    //              dy = (by - ay) * (pz + az)/(bz-az) + ay                                             |
    //              dz = pd                                                                             |
    //---------------------------------------------------------------------------------------------------
    std::vector<VerticeDTO> Renderer::checkTriangle(std::array<VerticeDTO, 3> verts)
    {
        short invalidV = 0;
        std::vector<VerticeDTO> result;
        std::vector<VerticeDTO> valids;
        std::vector<VerticeDTO> invalids;

        for (int i = 0; i < 3; i++)
        {
            if (verts[i].position.z <= Settings::viewportDistance)
            {
                invalidV++;
                invalids.emplace_back(verts[i]);
            }
            else
            {
                valids.emplace_back(verts[i]);
            }
        }

        double interZ = 0;
        double intersecX = 0;
        double intersecY = 0;
        double interZ2 = 0;
        double intersecX2 = 0;
        double intersecY2 = 0;

        Vector3 intersecPosWorld;
        Vector3 intersecNormal;
        Vector3 intersecPos2World;
        Vector3 intersecNormal2;
        Vector3 intersecPos;
        Vector3 intersecPos2;
        Color intersecColor;
        Color intersecColor2;
        switch (invalidV)
        {
        case 3:
            // All vertices behind near plane
            break;
        case 2:
            // One valid vertex: clip to two intersection points, forming one triangle
            // Find intersection valid0->invalid0 at near plane Z
            interZ = (Settings::viewportDistance - invalids[0].position.z) / (valids[0].position.z - invalids[0].position.z);
            intersecX = (valids[0].position.x - invalids[0].position.x) * interZ + invalids[0].position.x;
            intersecY = (valids[0].position.y - invalids[0].position.y) * interZ + invalids[0].position.y;

            // Find intersection valid0->invalid1 at near plane Z
            interZ2 = (Settings::viewportDistance - invalids[1].position.z) / (valids[0].position.z - invalids[1].position.z);
            intersecX2 = (valids[0].position.x - invalids[1].position.x) * interZ2 + invalids[1].position.x;
            intersecY2 = (valids[0].position.y - invalids[1].position.y) * interZ2 + invalids[1].position.y;

            intersecPos = Vector3(intersecX2, intersecY2, Settings::viewportDistance);
            intersecPos2 = Vector3(intersecX, intersecY, Settings::viewportDistance);

            if (Settings::shadingMode == Settings::PHONG) {
                intersecPosWorld = invalids[0].worldPos + (valids[0].worldPos - invalids[0].worldPos) * interZ;
                intersecNormal = invalids[0].normalPos + (valids[0].normalPos - invalids[0].normalPos) * interZ;
                intersecPos2World = invalids[1].worldPos + (valids[0].worldPos - invalids[1].worldPos) * interZ2;
                intersecNormal2 = invalids[1].normalPos + (valids[0].normalPos - invalids[1].normalPos) * interZ2;
            }
            else
            {
                intersecPosWorld = invalids[0].worldPos;
                intersecNormal = invalids[0].normalPos;
                intersecPos2World = invalids[1].worldPos;
                intersecNormal2 = invalids[1].normalPos;
            }

            // Interpolate color at each clip point so Gouraud survives clipping.
            // lerp(a,b,t): t=0 → a (invalid side), t=1 → b (valid side).
            intersecColor = Color::lerp(invalids[0].color, valids[0].color, (float)interZ);
            intersecColor2 = Color::lerp(invalids[1].color, valids[0].color, (float)interZ2);

            // Winding order
            result = {
                valids[0],
                VerticeDTO(intersecPos,  intersecPosWorld,  intersecNormal,  intersecColor),
                VerticeDTO(intersecPos2, intersecPos2World, intersecNormal2, intersecColor2)
            };
            break;
        case 1:
            // Two valid vertices: clip to quad (two triangles)
            // Find intersection valid0->invalid0 at near plane Z
            interZ = (Settings::viewportDistance - invalids[0].position.z) / (valids[0].position.z - invalids[0].position.z);
            intersecX = (valids[0].position.x - invalids[0].position.x) * interZ + invalids[0].position.x;
            intersecY = (valids[0].position.y - invalids[0].position.y) * interZ + invalids[0].position.y;

            // Find intersection valid1->invalid0 at near plane Z
            interZ2 = (Settings::viewportDistance - invalids[0].position.z) / (valids[1].position.z - invalids[0].position.z);
            intersecX2 = (valids[1].position.x - invalids[0].position.x) * interZ2 + invalids[0].position.x;
            intersecY2 = (valids[1].position.y - invalids[0].position.y) * interZ2 + invalids[0].position.y;


            intersecPos = Vector3(intersecX2, intersecY2, Settings::viewportDistance);
            intersecPos2 = Vector3(intersecX, intersecY, Settings::viewportDistance);

            if (Settings::shadingMode == Settings::PHONG) {
                intersecPosWorld = invalids[0].worldPos + (valids[0].worldPos - invalids[0].worldPos) * interZ;
                intersecNormal = invalids[0].normalPos + (valids[0].normalPos - invalids[0].normalPos) * interZ;
                intersecPos2World = invalids[0].worldPos + (valids[1].worldPos - invalids[0].worldPos) * interZ2;
                intersecNormal2 = invalids[0].normalPos + (valids[1].normalPos - invalids[0].normalPos) * interZ2;
            }
            else
            {
                intersecPosWorld = invalids[0].worldPos;
                intersecNormal = invalids[0].normalPos;
                intersecPos2World = invalids[0].worldPos;
                intersecNormal2 = invalids[0].normalPos;
            }

            // Interpolate color at each clip point so Gouraud survives clipping.
            intersecColor = Color::lerp(invalids[0].color, valids[0].color, (float)interZ);
            intersecColor2 = Color::lerp(invalids[0].color, valids[1].color, (float)interZ2);

            // Returns 4 vertices: two triangles drawn as (0,1,2) and (0,2,3)
            // Winding order
            result = {
                valids[0],
                valids[1],
                VerticeDTO(intersecPos,  intersecPosWorld,  intersecNormal,  intersecColor),
                VerticeDTO(intersecPos2, intersecPos2World, intersecNormal2, intersecColor2)
            };
            break;
        default:
            // All three vertices in front of near plane
            result = { valids[0], valids[1], valids[2] };
            break;
        }

        return result;
    }

    void Renderer::Get_MouseState(int* x, int* y)
    {
        SDL_GetRelativeMouseState(x, y);
    }

    Vector3 Renderer::CanvasToViewport(double x, double y)
    {
        return Vector3(x * (Settings::canvasWidth / Settings::viewportWidth),
            y * (Settings::canvasHeight / Settings::viewportHeight), 0);
    }

    Vector3 Renderer::ProjectVertex(Vector3* v)
    {
        if (abs(v->z) < 1e-9) return Vector3(0, 0, 0);
        return CanvasToViewport(
            v->x * Settings::viewportDistance / v->z,
            v->y * Settings::viewportDistance / v->z);
    }

    Color Renderer::ComputeIllumination(Vector3 normalized, Vector3 worldPos, Color shadedColor)
    {
        // Normalize surface color to [0, 1] working space.
        // All light math is done in [0,1]; we scale back to [0,255] at the end.
        // This prevents the catastrophic overflow that occurred when raw 0-255
        // color channels were multiplied together (e.g. 200 * 255 * 0.1 = 5100).
        double sr = shadedColor.r / 255.0;
        double sg = shadedColor.g / 255.0;
        double sb = shadedColor.b / 255.0;

        // Accumulate light contributions additively into these channels.
        // Each light adds its own tinted, attenuated contribution independently.
        // Additive accumulation is physically correct: two lights on a surface
        // are brighter than one. The old *= caused each light to *attenuate*
        // the previous result, which is wrong and also why surfaces went white
        // (ambient * point light with diffuse=1 kept multiplying up to 1.0).
        double accR = 0.0, accG = 0.0, accB = 0.0;

        for (BaseLight* light : *sceneManager->lights)
        {
            // Normalize light color to [0, 1]
            double lr = light->color.r / 255.0;
            double lg = light->color.g / 255.0;
            double lb = light->color.b / 255.0;

            if (light->type == AMBIENT_LIGHT)
            {
                // Ambient 
                // Flat contribution, same for every surface point.
                // contribution = surfaceColor * lightColor * intensity
                accR += sr * lr * light->intensity;
                accG += sg * lg * light->intensity;
                accB += sb * lb * light->intensity;
            }
            else if (light->type == POINT_LIGHT)
            {
                // Point light (diffuse / Lambertian) 
                // L = normalize(lightPos - worldPos)
                // contribution = surfaceColor * lightColor * intensity * max(0, dot(N, L))
                //
                // TODO: add depth attenuation (1 / dist^2)
                PointLight* pl = static_cast<PointLight*>(light);

                Vector3 rotated = pl->rotation.RotateVector3(&pl->position);
                Vector3 toLight = rotated - worldPos;

                double dist = toLight.magnitude();
                if (dist < 1e-9) continue;                // vertex ON the light
                Vector3 L = toLight * (1.0 / dist);       // normalize
                double diffuse = normalized.dot(&L);
                if (diffuse > 0.0)
                {
                    accR += sr * lr * light->intensity * diffuse;
                    accG += sg * lg * light->intensity * diffuse;
                    accB += sb * lb * light->intensity * diffuse;
                }
            }
            else if (light->type == DIRECTIONAL_LIGHT)
            {
                // Directional light (diffuse / Lambertian) 
                // L = normalize(direction), it is constant across the scene.
                // contribution = surfaceColor * lightColor * intensity * max(0, dot(N, L))
                DirectionalLight* dl = static_cast<DirectionalLight*>(light);
                Vector3 L = dl->direction.normalize();
                double diffuse = normalized.dot(&L);
                if (diffuse > 0.0)
                {
                    accR += sr * lr * light->intensity * diffuse;
                    accG += sg * lg * light->intensity * diffuse;
                    accB += sb * lb * light->intensity * diffuse;
                }
            }
        }

        // Scale back to [0, 255] and clamp to prevent any overflow from
        // multiple bright lights summing above 1.0.
        return Color(
            std::clamp(accR * 255.0, 0.0, 255.0),
            std::clamp(accG * 255.0, 0.0, 255.0),
            std::clamp(accB * 255.0, 0.0, 255.0),
            255.0
        );
    }

    std::vector<double> Renderer::Interpolate(double i0, double d0, double i1, double d1)
    {
        if (i0 == i1)
        {
            return std::vector<double>{d0};
        }
        std::vector<double> values;
        values.reserve((int)(i1 - i0) + 1);
        double a = (d1 - d0) / (i1 - i0);
        double d = d0;
        for (int x = (int)i0; x <= (int)i1; ++x)
        {
            values.push_back(d);
            d += a;
        }
        return values;
    }

    Renderer::Renderer(SceneManager* sceneManager)
    {
        this->sceneManager = sceneManager;
        if (SDL_Init(SDL_INIT_VIDEO) < 0)
        {
            std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }

        window = SDL_CreateWindow("Set Pixel Example",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            Settings::canvasWidth, Settings::canvasHeight, SDL_WINDOW_SHOWN);
        if (!window)
        {
            std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return;
        }

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow(window);
            SDL_Quit();
            return;
        }

        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        SDL_SetRelativeMouseMode(SDL_TRUE);

        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            windowWidth, windowHeight);
        pixels = new Uint32[windowWidth * windowHeight];
        zBuffer = new float[windowWidth * windowHeight];
        memset(pixels, 0, windowWidth * windowHeight * sizeof(Uint32));
        memset(zBuffer, 0, windowWidth * windowHeight * sizeof(float));


        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0)
            threadCount = 4;
    }

    void Renderer::Render()
    {
        // Clear pixel buffer and z-buffer
        memset(pixels, 0, windowWidth * windowHeight * sizeof(Uint32));
        // how to use :
        // 0.0f = "nothing drawn here",  1/z is positive and grows toward camera
        // if dist is 2m, z is 2, 1/z is 0.5
        // if dist is 1m, z is 1, 1/z is 1.0
        memset(zBuffer, 0, windowWidth * windowHeight * sizeof(float));

        workIndex.store(0);

        std::vector<std::thread> frameWorkers;
        // leave 1 core free for the pc to live
        frameWorkers.reserve(threadCount - 1);
        int objectCount = (int)sceneManager->objects->size();

        auto workerFn = [&]()
            {
                while (true)
                {
                    int index = workIndex.fetch_add(1);
                    if (index >= objectCount)
                        break;

                    RenderInstance(sceneManager->objects->at(index));
                }
            };

        for (int i = 0; i < threadCount - 1; ++i)
        {
            frameWorkers.emplace_back(workerFn);
        }

        for (auto& worker : frameWorkers)
        {
            worker.join();
        }

        SDL_UpdateTexture(texture, NULL, pixels, windowWidth * sizeof(Uint32));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    void Renderer::SetPixel(int x, int y, Color color)
    {
        if (x < 0 || x >= windowWidth || y < 0 || y >= windowHeight)
            return;

        Uint32 pixel = ((Uint32)color.r << 24) |
            ((Uint32)color.g << 16) |
            ((Uint32)color.b << 8) |
            ((Uint32)color.a);

        pixels[y * windowWidth + x] = pixel;
    }

    /*Back Face Culling, WE USE CLOCKWISE WINDING
    https://en.wikipedia.org/wiki/Back-face_culling
    https://cmichel.io/understanding-front-faces-winding-order-and-normals
    */
    bool Renderer::IsFacing(std::array<VerticeDTO, 3> tArr)
    {
        Vector3 edge1 = tArr[1].position - tArr[0].position;
        Vector3 edge2 = tArr[2].position - tArr[0].position;
        Vector3 normal = edge1.cross(edge2);
        double dot_product = tArr[0].position.dot(&normal);
        return dot_product >= 0;
    }

    void Renderer::RenderInstance(BaseObject* obj)
    {
        std::vector<VerticeDTO> projected;
        if (obj->bVertices.empty()) return;

        projected.reserve(obj->bVertices.size());

        Vector3 objScale = obj->transform.scale;
        Vector3 objPos = obj->transform.position;
        Vector3 camPos = sceneManager->currentCamera->transform.position;
        Quaternion camRot = sceneManager->currentCamera->transform.rotation;
        Quaternion camConjugate = camRot.Conjugate(camRot);

        for (const Vertice v : obj->bVertices)
        {
            // conv to world pos
            Vector3 vPos = v.position;
            Vector3 vProj = objScale * vPos;
            vProj = obj->transform.rotation.RotateVector3(&vProj);
            Vector3 vProjWorld = vProj + objPos;

            Vector3 normal = obj->transform.rotation.RotateVector3(&vPos).normalize();
            Color color = obj->material->color * v.shade;

            if (Settings::shadingMode == Settings::ShadingMode::GOURAUD)
                color = ComputeIllumination(normal, vProjWorld, color);

            //conv to camera space
            vProj = vProjWorld - camPos;
            vProj = camConjugate.RotateVector3(&vProj);
            projected.emplace_back(VerticeDTO(vProj, vProjWorld, normal, color));
        }

        std::vector<Triangle> triangles = obj->bTriangles;
        for (const Triangle& triangle : triangles)
        {
            std::array<VerticeDTO, 3> vP = {
                projected[triangle.p0],
                projected.at(triangle.p1),
                projected.at(triangle.p2)
            };

            if (!IsFacing(vP))
            {
                continue;
            }

            std::vector<VerticeDTO> trs = checkTriangle(vP);
            switch (trs.size())
            {
            case 4:
            {
                Triangle tempTriangle(0, 1, 2, triangle.material);
                DrawTriangle(&tempTriangle, &trs);
                Triangle tempTriangle2(0, 2, 3, triangle.material);
                DrawTriangle(&tempTriangle2, &trs);
                break;
            }
            case 3:
            {
                Triangle tempTriangle(0, 1, 2, triangle.material);
                DrawTriangle(&tempTriangle, &trs);
                break;
            }
            default:
                break;
            }
        }
    }

    void Renderer::DrawTriangle(Triangle* triangle, std::vector<VerticeDTO>* projected)
    {
        Vector3 NP0 = projected->at(triangle->p0).position;
        Vector3 NP1 = projected->at(triangle->p1).position;
        Vector3 NP2 = projected->at(triangle->p2).position;

        Vector3 P0 = ProjectVertex(&NP0); P0.z = NP0.z;
        Vector3 P1 = ProjectVertex(&NP1); P1.z = NP1.z;
        Vector3 P2 = ProjectVertex(&NP2); P2.z = NP2.z;

        double h0 = projected->at(triangle->p0).shade;
        double h1 = projected->at(triangle->p1).shade;
        double h2 = projected->at(triangle->p2).shade;

        Color c0 = projected->at(triangle->p0).color;
        Color c1 = projected->at(triangle->p1).color;
        Color c2 = projected->at(triangle->p2).color;

        Vector3 WP0 = projected->at(triangle->p0).worldPos;
        Vector3 WP1 = projected->at(triangle->p1).worldPos;
        Vector3 WP2 = projected->at(triangle->p2).worldPos;

        Vector3 WN0 = projected->at(triangle->p0).normalPos;
        Vector3 WN1 = projected->at(triangle->p1).normalPos;
        Vector3 WN2 = projected->at(triangle->p2).normalPos;

        // Sort by Y
        if (P1.y < P0.y) { std::swap(P1, P0); std::swap(h1, h0); std::swap(c1, c0); std::swap(WP1, WP0); std::swap(WN1, WN0); }
        if (P2.y < P0.y) { std::swap(P2, P0); std::swap(h2, h0); std::swap(c2, c0); std::swap(WP2, WP0); std::swap(WN2, WN0); }
        if (P2.y < P1.y) { std::swap(P2, P1); std::swap(h2, h1); std::swap(c2, c1); std::swap(WP2, WP1); std::swap(WN2, WN1); }

        // Gouraud: interpolate pre-lit vertex colours perspective-correctly (r/z, g/z, b/z)
        std::vector<double> cr01 = Interpolate(P0.y, (double)c0.r / P0.z, P1.y, (double)c1.r / P1.z);
        std::vector<double> cg01 = Interpolate(P0.y, (double)c0.g / P0.z, P1.y, (double)c1.g / P1.z);
        std::vector<double> cb01 = Interpolate(P0.y, (double)c0.b / P0.z, P1.y, (double)c1.b / P1.z);

        std::vector<double> cr12 = Interpolate(P1.y, (double)c1.r / P1.z, P2.y, (double)c2.r / P2.z);
        std::vector<double> cg12 = Interpolate(P1.y, (double)c1.g / P1.z, P2.y, (double)c2.g / P2.z);
        std::vector<double> cb12 = Interpolate(P1.y, (double)c1.b / P1.z, P2.y, (double)c2.b / P2.z);

        std::vector<double> cr02 = Interpolate(P0.y, (double)c0.r / P0.z, P2.y, (double)c2.r / P2.z);
        std::vector<double> cg02 = Interpolate(P0.y, (double)c0.g / P0.z, P2.y, (double)c2.g / P2.z);
        std::vector<double> cb02 = Interpolate(P0.y, (double)c0.b / P0.z, P2.y, (double)c2.b / P2.z);
        std::vector<double> x01 = Interpolate(P0.y, P0.x, P1.y, P1.x);
        std::vector<double> x12 = Interpolate(P1.y, P1.x, P2.y, P2.x);
        std::vector<double> x02 = Interpolate(P0.y, P0.x, P2.y, P2.x);

        std::vector<double> z01 = Interpolate(P0.y, 1.0 / P0.z, P1.y, 1.0 / P1.z);
        std::vector<double> z12 = Interpolate(P1.y, 1.0 / P1.z, P2.y, 1.0 / P2.z);
        std::vector<double> z02 = Interpolate(P0.y, 1.0 / P0.z, P2.y, 1.0 / P2.z);

        // world pos (perspective-correct)
        std::vector<double> wx01 = Interpolate(P0.y, WP0.x / P0.z, P1.y, WP1.x / P1.z);
        std::vector<double> wy01 = Interpolate(P0.y, WP0.y / P0.z, P1.y, WP1.y / P1.z);
        std::vector<double> wz01 = Interpolate(P0.y, WP0.z / P0.z, P1.y, WP1.z / P1.z);

        std::vector<double> wx12 = Interpolate(P1.y, WP1.x / P1.z, P2.y, WP2.x / P2.z);
        std::vector<double> wy12 = Interpolate(P1.y, WP1.y / P1.z, P2.y, WP2.y / P2.z);
        std::vector<double> wz12 = Interpolate(P1.y, WP1.z / P1.z, P2.y, WP2.z / P2.z);

        std::vector<double> wx02 = Interpolate(P0.y, WP0.x / P0.z, P2.y, WP2.x / P2.z);
        std::vector<double> wy02 = Interpolate(P0.y, WP0.y / P0.z, P2.y, WP2.y / P2.z);
        std::vector<double> wz02 = Interpolate(P0.y, WP0.z / P0.z, P2.y, WP2.z / P2.z);

        std::vector<double> nx01 = Interpolate(P0.y, WN0.x / P0.z, P1.y, WN1.x / P1.z);
        std::vector<double> ny01 = Interpolate(P0.y, WN0.y / P0.z, P1.y, WN1.y / P1.z);
        std::vector<double> nz01 = Interpolate(P0.y, WN0.z / P0.z, P1.y, WN1.z / P1.z);

        std::vector<double> nx12 = Interpolate(P1.y, WN1.x / P1.z, P2.y, WN2.x / P2.z);
        std::vector<double> ny12 = Interpolate(P1.y, WN1.y / P1.z, P2.y, WN2.y / P2.z);
        std::vector<double> nz12 = Interpolate(P1.y, WN1.z / P1.z, P2.y, WN2.z / P2.z);

        std::vector<double> nx02 = Interpolate(P0.y, WN0.x / P0.z, P2.y, WN2.x / P2.z);
        std::vector<double> ny02 = Interpolate(P0.y, WN0.y / P0.z, P2.y, WN2.y / P2.z);
        std::vector<double> nz02 = Interpolate(P0.y, WN0.z / P0.z, P2.y, WN2.z / P2.z);

        if (!x01.empty()) x01.pop_back();
        if (!z01.empty()) z01.pop_back();
        if (!wx01.empty()) wx01.pop_back();
        if (!wy01.empty()) wy01.pop_back();
        if (!wz01.empty()) wz01.pop_back();
        if (!nx01.empty()) nx01.pop_back();
        if (!ny01.empty()) ny01.pop_back();
        if (!nz01.empty()) nz01.pop_back();
        if (!cr01.empty()) cr01.pop_back();
        if (!cg01.empty()) cg01.pop_back();
        if (!cb01.empty()) cb01.pop_back();

        std::vector<double> x012, z012;
        std::vector<double> wx012, wy012, wz012;
        std::vector<double> nx012, ny012, nz012;
        std::vector<double> cr012, cg012, cb012;

        x012.insert(x012.end(), x01.begin(), x01.end());
        x012.insert(x012.end(), x12.begin(), x12.end());

        z012.insert(z012.end(), z01.begin(), z01.end());
        z012.insert(z012.end(), z12.begin(), z12.end());

        wx012.insert(wx012.end(), wx01.begin(), wx01.end());
        wx012.insert(wx012.end(), wx12.begin(), wx12.end());
        wy012.insert(wy012.end(), wy01.begin(), wy01.end());
        wy012.insert(wy012.end(), wy12.begin(), wy12.end());
        wz012.insert(wz012.end(), wz01.begin(), wz01.end());
        wz012.insert(wz012.end(), wz12.begin(), wz12.end());

        nx012.insert(nx012.end(), nx01.begin(), nx01.end());
        nx012.insert(nx012.end(), nx12.begin(), nx12.end());
        ny012.insert(ny012.end(), ny01.begin(), ny01.end());
        ny012.insert(ny012.end(), ny12.begin(), ny12.end());
        nz012.insert(nz012.end(), nz01.begin(), nz01.end());
        nz012.insert(nz012.end(), nz12.begin(), nz12.end());

        cr012.insert(cr012.end(), cr01.begin(), cr01.end());
        cr012.insert(cr012.end(), cr12.begin(), cr12.end());
        cg012.insert(cg012.end(), cg01.begin(), cg01.end());
        cg012.insert(cg012.end(), cg12.begin(), cg12.end());
        cb012.insert(cb012.end(), cb01.begin(), cb01.end());
        cb012.insert(cb012.end(), cb12.begin(), cb12.end());

        int m = (int)(x012.size() / 2);
        m = (std::min)(m, (int)x02.size() - 1);

        std::vector<double> xleft = x012, xright = x02;
        std::vector<double> zleft = z012, zright = z02;

        std::vector<double> wxleft = wx012, wxright = wx02;
        std::vector<double> wyleft = wy012, wyright = wy02;
        std::vector<double> wzleft = wz012, wzright = wz02;

        std::vector<double> nxleft = nx012, nxright = nx02;
        std::vector<double> nyleft = ny012, nyright = ny02;
        std::vector<double> nzleft = nz012, nzright = nz02;

        std::vector<double> crleft = cr012, crright = cr02;
        std::vector<double> cgleft = cg012, cgright = cg02;
        std::vector<double> cbleft = cb012, cbright = cb02;

        if (x02[m] < x012[m])
        {
            xleft = x02; xright = x012;
            zleft = z02; zright = z012;

            wxleft = wx02; wxright = wx012;
            wyleft = wy02; wyright = wy012;
            wzleft = wz02; wzright = wz012;

            nxleft = nx02; nxright = nx012;
            nyleft = ny02; nyright = ny012;
            nzleft = nz02; nzright = nz012;

            crleft = cr02; crright = cr012;
            cgleft = cg02; cgright = cg012;
            cbleft = cb02; cbright = cb012;
        }


        Color color = triangle->material->color;

        for (int y = (int)P0.y; y < (int)P2.y; y++)
        {
            int ypy = y - (int)P0.y;
            if ((size_t)ypy >= xleft.size() || (size_t)ypy >= xright.size()) continue;

            double xl = xleft[ypy];
            double xr = xright[ypy];
            if (xl > xr) continue;

            std::vector<double> zsegment = Interpolate(xl, zleft[ypy], xr, zright[ypy]);

            std::vector<double> wxseg = Interpolate(xl, wxleft[ypy], xr, wxright[ypy]);
            std::vector<double> wyseg = Interpolate(xl, wyleft[ypy], xr, wyright[ypy]);
            std::vector<double> wzseg = Interpolate(xl, wzleft[ypy], xr, wzright[ypy]);

            std::vector<double> nxseg = Interpolate(xl, nxleft[ypy], xr, nxright[ypy]);
            std::vector<double> nyseg = Interpolate(xl, nyleft[ypy], xr, nyright[ypy]);
            std::vector<double> nzseg = Interpolate(xl, nzleft[ypy], xr, nzright[ypy]);

            for (int x = (int)xl; x < (int)xr; x++)
            {
                int centeredX = x + sceneManager->centeredCW;
                int centeredY = sceneManager->centeredCH - y;
                if (centeredX < 0 || centeredX >= windowWidth || centeredY < 0 || centeredY >= windowHeight)
                    continue;

                int idx = x - (int)xl;
                if ((size_t)idx >= zsegment.size()) continue;

                double invZ = zsegment[idx];
                int bufIdx = centeredX + centeredY * windowWidth;

                if (invZ < zBuffer[bufIdx]) continue;

                zBuffer[bufIdx] = invZ;

                double realZ = (invZ > 1e-9) ? 1.0 / invZ : 1.0;

                Color c;
                if (Settings::shadingMode == Settings::GOURAUD)
                {
                    // We stored r/z, g/z, b/z along the edges; multiply by realZ to get r,g,b.
                    std::vector<double> crseg = Interpolate(xl, crleft[ypy], xr, crright[ypy]);
                    std::vector<double> cgseg = Interpolate(xl, cgleft[ypy], xr, cgright[ypy]);
                    std::vector<double> cbseg = Interpolate(xl, cbleft[ypy], xr, cbright[ypy]);
                    if ((size_t)idx >= crseg.size()) continue;
                    uint8_t r = (uint8_t)std::clamp(crseg[idx] * realZ, 0.0, 255.0);
                    uint8_t g = (uint8_t)std::clamp(cgseg[idx] * realZ, 0.0, 255.0);
                    uint8_t b = (uint8_t)std::clamp(cbseg[idx] * realZ, 0.0, 255.0);
                    c = Color(r, g, b, 255);
                }
                else // PHONG
                {
                    Vector3 pixelWorldPos(
                        wxseg[idx] * realZ,
                        wyseg[idx] * realZ,
                        wzseg[idx] * realZ
                    );

                    Vector3 pixelNormal(
                        nxseg[idx] * realZ,
                        nyseg[idx] * realZ,
                        nzseg[idx] * realZ
                    );
                    pixelNormal = pixelNormal.normalize();

                    c = ComputeIllumination(pixelNormal, pixelWorldPos, color);
                }

                SetPixel(centeredX, centeredY, c);
            }
        }
    }

    void Renderer::DrawLine(Vertice* V0, Vertice* V1, Color color)
    {
        Vector3 P0 = ProjectVertex(&V0->position);
        Vector3 P1 = ProjectVertex(&V1->position);

        if (abs(P1.x - P0.x) > abs(P1.y - P0.y))
        {
            if (P0.x > P1.x) std::swap(P0, P1);
            std::vector<double> ys = Interpolate(P0.x, P0.y, P1.x, P1.y);
            for (int x = (int)P0.x; x < (int)P1.x; x++)
            {
                int idx = x - (int)P0.x;
                if ((size_t)idx >= ys.size()) continue;
                SetPixel(x + sceneManager->centeredCW, sceneManager->centeredCH - (int)ys.at(idx), color);
            }
        }
        else
        {
            if (P0.y > P1.y) std::swap(P0, P1);
            std::vector<double> xs = Interpolate(P0.y, P0.x, P1.y, P1.x);
            for (int y = (int)P0.y; y < (int)P1.y; y++)
            {
                int idx = y - (int)P0.y;
                if ((size_t)idx >= xs.size()) continue;
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                SDL_RenderDrawPoint(renderer, (int)xs.at(idx) + sceneManager->centeredCW, sceneManager->centeredCH - y);
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
        delete[] pixels;
        delete[] zBuffer;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};