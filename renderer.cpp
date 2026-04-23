#include <windows.h>
#include <algorithm>
#include <iostream>
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
#include <SDL.h>
#include <cmath>
#include <array>
#include <vector>
#include <synchapi.h>
#include <span>
namespace PEngine
{
    Material* Renderer::DefaultMaterial = new Material(0.0, 0.0, Color(255, 0, 255, 255));

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
    std::vector<Vertice> Renderer::checkTriangle(std::array<Vertice, 3> verts)
    {
        short invalidV = 0;
        std::vector<Vertice> result;
        std::vector<Vertice> valids;
        std::vector<Vertice> invalids;

        for (int i = 0; i < 3; i++)
        {
            if (verts[i].position.z <= sceneManager->viewportDistance)
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
        switch (invalidV)
        {
        case 3:
            // All vertices behind near plane — return empty, triangle is fully clipped
            break;
        case 2:
            // One valid vertex: clip to two intersection points, forming one triangle
            // Find intersection valid0->invalid0 at near plane Z
            interZ = (sceneManager->viewportDistance - invalids[0].position.z) / (valids[0].position.z - invalids[0].position.z);
            intersecX = (valids[0].position.x - invalids[0].position.x) * interZ + invalids[0].position.x;
            intersecY = (valids[0].position.y - invalids[0].position.y) * interZ + invalids[0].position.y;

            // Find intersection valid0->invalid1 at near plane Z
            interZ2 = (sceneManager->viewportDistance - invalids[1].position.z) / (valids[0].position.z - invalids[1].position.z);
            intersecX2 = (valids[0].position.x - invalids[1].position.x) * interZ2 + invalids[1].position.x;
            intersecY2 = (valids[0].position.y - invalids[1].position.y) * interZ2 + invalids[1].position.y;

            // Winding order — valid0, inter(invalid1 side), inter(invalid0 side)
            result = {
                valids[0],
                Vertice(Vector3(intersecX2, intersecY2, sceneManager->viewportDistance), invalids[1].shade),
                Vertice(Vector3(intersecX,  intersecY,  sceneManager->viewportDistance), invalids[0].shade)
            };
            break;
        case 1:
            // Two valid vertices: clip to quad (two triangles)
            // Find intersection valid0->invalid0 at near plane Z
            interZ = (sceneManager->viewportDistance - invalids[0].position.z) / (valids[0].position.z - invalids[0].position.z);
            intersecX = (valids[0].position.x - invalids[0].position.x) * interZ + invalids[0].position.x;
            intersecY = (valids[0].position.y - invalids[0].position.y) * interZ + invalids[0].position.y;

            // Find intersection valid1->invalid0 at near plane Z
            interZ2 = (sceneManager->viewportDistance - invalids[0].position.z) / (valids[1].position.z - invalids[0].position.z);
            intersecX2 = (valids[1].position.x - invalids[0].position.x) * interZ2 + invalids[0].position.x;
            intersecY2 = (valids[1].position.y - invalids[0].position.y) * interZ2 + invalids[0].position.y;

            // Returns 4 vertices: two triangles drawn as (0,1,2) and (0,2,3)
            // Winding order — both sub-triangles wound consistently with original
            result = {
                valids[0],
                valids[1],
                Vertice(Vector3(intersecX2, intersecY2, sceneManager->viewportDistance), invalids[0].shade),
                Vertice(Vector3(intersecX,  intersecY,  sceneManager->viewportDistance), invalids[0].shade)
            };
            break;
        default:
            // All three vertices in front of near plane — pass through unchanged
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
        return Vector3(x * (sceneManager->canvasWidth / sceneManager->viewportWidth),
            y * (sceneManager->canvasHeight / sceneManager->viewportHeight), 0);
    }

    Vector3 Renderer::ProjectVertex(Vector3* v)
    {
        if (abs(v->z) < 1e-9) return Vector3(0, 0, 0);
        return CanvasToViewport(
            v->x * sceneManager->viewportDistance / v->z,
            v->y * sceneManager->viewportDistance / v->z);
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
            sceneManager->canvasWidth, sceneManager->canvasHeight, SDL_WINDOW_SHOWN);
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
    }

    void Renderer::Render()
    {
        // Clear pixel buffer and z-buffer
        memset(pixels, 0, windowWidth * windowHeight * sizeof(Uint32));
        // how to use :
        // 0.0f = "nothing drawn here" - 1/z is positive and grows toward camera
		// if dist is 2m, z is 2, 1/z is 0.5
		// if dist is 1m, z is 1, 1/z is 1.0
        memset(zBuffer, 0, windowWidth * windowHeight * sizeof(float));

        for (BaseLight* l : *sceneManager->lights)
        {
            if (l->type == AMBIENT_LIGHT)
            {
                globalIllumination = l;
            }
        }

        if (!globalIllumination)
        {
            globalIllumination = new BaseLight(0.2, Color(255, 255, 255, 255));
        }

        for (BaseObject* obj : *sceneManager->objects)
        {
            RenderInstance(obj);
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
    bool Renderer::IsFacing(std::array<Vertice, 3> tArr)
    {
        Vector3 edge1 = tArr[1].position - tArr[0].position;
        Vector3 edge2 = tArr[2].position - tArr[0].position;
        Vector3 normal = edge1.cross(edge2);
        double dot_product = tArr[0].position.dot(&normal);
        return dot_product >= 0;
    }

    void Renderer::RenderInstance(BaseObject* obj)
    {
        std::vector<Vertice> projected;
        if (obj->bVertices.empty()) return;

        projected.reserve(obj->bVertices.size());

        Vector3 objScale = obj->transform.scale;
        Vector3 objPos = obj->transform.position;
        Vector3 camPos = sceneManager->currentCamera->transform.position;
        Quaternion camRot = sceneManager->currentCamera->transform.rotation;
        Quaternion camConjugate = camRot.Conjugate(camRot);

        for (Vertice v : obj->bVertices)
        {
            Vector3 vPos = v.position;
            Vector3 vProj = objScale * vPos;
            vProj = obj->transform.rotation.RotateVector3(&vProj);
            vProj = vProj + objPos;
            vProj = vProj - camPos;
            vProj = camConjugate.RotateVector3(&vProj);
            projected.emplace_back(Vertice(vProj, v.shade));
        }

        std::vector<Triangle> triangles = obj->bTriangles;
        for (const Triangle& triangle : triangles)
        {
            std::array<Vertice, 3> vP = {
                projected[triangle.p0],
                projected.at(triangle.p1),
                projected.at(triangle.p2)
            };

            if (!IsFacing(vP))
            {
                continue;
            }

            std::vector<Vertice> trs = checkTriangle(vP);
            switch (trs.size())
            {
            case 4:
            {
                // First triangle: (0,1,2)
                Triangle tempTriangle(0, 1, 2, triangle.material);
                DrawTriangle(&tempTriangle, &trs);
                // Second triangle uses (0,2,3)
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

    void Renderer::DrawTriangle(Triangle* triangle, std::vector<Vertice>* projected)
    {
        Vector3 NP0 = projected->at(triangle->p0).position;
        Vector3 NP1 = projected->at(triangle->p1).position;
        Vector3 NP2 = projected->at(triangle->p2).position;

        Vector3 P0 = ProjectVertex(&NP0);
        P0.z = NP0.z;
        Vector3 P1 = ProjectVertex(&NP1);
        P1.z = NP1.z;
        Vector3 P2 = ProjectVertex(&NP2);
        P2.z = NP2.z;

        double h0 = projected->at(triangle->p0).shade;
        double h1 = projected->at(triangle->p1).shade;
        double h2 = projected->at(triangle->p2).shade;

        // The per-pixel z-buffer check inside the scanline loop handles occlusion correctly.

        // Sort points so that Y0 <= Y1 <= Y2
        if (P1.y < P0.y) { std::swap(P1, P0); std::swap(h1, h0); }
        if (P2.y < P0.y) { std::swap(P2, P0); std::swap(h2, h0); }
        if (P2.y < P1.y) { std::swap(P2, P1); std::swap(h2, h1); }

        // Compute X coordinates of edges
        // 1/z for perspective-correct interpolation across the scanline
        // interpolate 1/z along edges now so zsegment does not need to later
        std::vector<double> x01 = Interpolate(P0.y, P0.x, P1.y, P1.x);
        std::vector<double> h01 = Interpolate(P0.y, h0, P1.y, h1);
        std::vector<double> z01 = Interpolate(P0.y, 1.0 / P0.z, P1.y, 1.0 / P1.z);

        std::vector<double> x12 = Interpolate(P1.y, P1.x, P2.y, P2.x);
        std::vector<double> h12 = Interpolate(P1.y, h1, P2.y, h2);
        std::vector<double> z12 = Interpolate(P1.y, 1.0 / P1.z, P2.y, 1.0 / P2.z);

        std::vector<double> x02 = Interpolate(P0.y, P0.x, P2.y, P2.x);
        std::vector<double> h02 = Interpolate(P0.y, h0, P2.y, h2);
        std::vector<double> z02 = Interpolate(P0.y, 1.0 / P0.z, P2.y, 1.0 / P2.z);

        // Remove overlapping vertex at P1 before concatenation
        if (!x01.empty()) x01.pop_back();
        if (!h01.empty()) h01.pop_back();
        if (!z01.empty()) z01.pop_back();

        std::vector<double> x012, h012, z012;

        x012.reserve(x01.size() + x12.size());
        x012.insert(x012.end(), x01.begin(), x01.end());
        x012.insert(x012.end(), x12.begin(), x12.end());

        h012.reserve(h01.size() + h12.size());
        h012.insert(h012.end(), h01.begin(), h01.end());
        h012.insert(h012.end(), h12.begin(), h12.end());

        z012.reserve(z01.size() + z12.size());
        z012.insert(z012.end(), z01.begin(), z01.end());
        z012.insert(z012.end(), z12.begin(), z12.end());

        if (x012.empty() || x02.empty())
        {
            return;
        }

        int m = (int)(x012.size() / 2);
        m = (std::min)(m, (int)x02.size() - 1);

        std::vector<double> xleft = x012, xright = x02;
        std::vector<double> hleft = h012, hright = h02;
        std::vector<double> zleft = z012, zright = z02;

        if (x02.at(m) < x012.at(m))
        {
            xleft = x02;  xright = x012;
            hleft = h02;  hright = h012;
            zleft = z02;  zright = z012;
        }

        Color color = triangle->material->color;
        bool fullDraw = true;

        for (int y = (int)P0.y; y < (int)P2.y; y++)
        {
            int ypy = y - (int)P0.y;
            if ((size_t)ypy >= xleft.size() || (size_t)ypy >= xright.size()) continue;

            double xl = xleft.at(ypy);
            double xr = xright.at(ypy);
            if (xl > xr) continue;

            // Guard against out-of-bounds on h/z lookups
            if ((size_t)ypy >= hleft.size() || (size_t)ypy >= hright.size()) continue;
            if ((size_t)ypy >= zleft.size() || (size_t)ypy >= zright.size()) continue;

            std::vector<double> hsegment = Interpolate(xl, hleft.at(ypy), xr, hright.at(ypy));
            std::vector<double> zsegment = Interpolate(xl, zleft.at(ypy), xr, zright.at(ypy));

            for (int x = (int)xl; x < (int)xr; x++)
            {
                int centeredX = x + sceneManager->centeredCW;
                int centeredY = sceneManager->centeredCH - y;
                if (centeredX < 0 || centeredX >= windowWidth || centeredY < 0 || centeredY >= windowHeight)
                    continue;

                int xxl = x - (int)xl;
                if ((size_t)xxl >= zsegment.size() || (size_t)xxl >= hsegment.size()) continue;

                double zsegm = zsegment[xxl];

                // We store 1/z — larger value = closer to camera.
                // Skip this pixel only if something CLOSER (larger 1/z) is already drawn.
                int bufIdx = centeredX + centeredY * windowWidth;
                if (zsegm < zBuffer[bufIdx])
                {
                    fullDraw = false;
                    continue;
                }

                zBuffer[bufIdx] = zsegm;
                Color c = color * hsegment[xxl];
                SetPixel(centeredX, centeredY, c);

            }
        }
        color = triangle->material->outlineColor;

        //deactivate this, this should become an option in the future.
        if (fullDraw && false)
        {
            DrawLine(&projected->at(triangle->p0), &projected->at(triangle->p1), color);
            DrawLine(&projected->at(triangle->p0), &projected->at(triangle->p2), color);
            DrawLine(&projected->at(triangle->p1), &projected->at(triangle->p2), color);
        }
    }

    void Renderer::DrawLine(Vertice* V0, Vertice* V1, Color color)
    {
        // FIX: ProjectVertex returns by value now
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
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};