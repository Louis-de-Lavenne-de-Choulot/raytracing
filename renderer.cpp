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
#include <synchapi.h>
namespace PEngine
{

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
    std::vector<Vertice> *Renderer::checkTriangle(std::array<Vertice, 3> verts)
    {
        short invalidV = 0;
        std::vector<Vertice> *result = new std::vector<Vertice>();
        std::vector<Vertice> valids = {};
        std::vector<Vertice> invalids = {};

        for (int i = 0; i < 3; i++)
        {
            if (verts[i].position->z <= sceneManager->viewportDistance)
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
            break;
        case 2:
            // easier than case 1, with 2 invalids, we just find the two collisions
            // valid0 invalid0 and valid0 invalid1 then create a triangle
            // find intersection valid0, invalid0 and viewportDist Z
            interZ = (sceneManager->viewportDistance + invalids[0].position->z) / (valids[0].position->z - invalids[0].position->z);
            intersecX = (valids[0].position->x - invalids[0].position->x) * interZ + invalids[0].position->x;
            intersecY = (valids[0].position->y - invalids[0].position->y) * interZ + invalids[0].position->y;


            // find intersection valid0, invalid1 and viewportDist Z
            interZ2 = (sceneManager->viewportDistance + invalids[1].position->z) / (valids[0].position->z - invalids[1].position->z);
            intersecX2 = (valids[0].position->x - invalids[1].position->x) * interZ2 + invalids[1].position->x;
            intersecY2 = (valids[0].position->y - invalids[1].position->y) * interZ2 + invalids[1].position->y;
            

            result = new std::vector<Vertice>{valids[0], Vertice(new Vector3(intersecX2, intersecY2, sceneManager->viewportDistance), invalids[1].shade), Vertice(new Vector3(intersecX, intersecY, sceneManager->viewportDistance), invalids[0].shade)};
            break;
        case 1:
            // find intersection valid0, invalid0 and viewportDist Z
            interZ = (sceneManager->viewportDistance + invalids[0].position->z) / (valids[0].position->z - invalids[0].position->z);
            intersecX = (valids[0].position->x - invalids[0].position->x) * interZ + invalids[0].position->x;
            intersecY = (valids[0].position->y - invalids[0].position->y) * interZ + invalids[0].position->y;

            // find intersection valid1, invalid0 and viewportDist Z
            interZ2 = (sceneManager->viewportDistance + invalids[0].position->z) / (valids[1].position->z - invalids[0].position->z);
            intersecX2 = (valids[1].position->x - invalids[0].position->x) * interZ + invalids[0].position->x;
            intersecY2 = (valids[1].position->y - invalids[0].position->y) * interZ + invalids[0].position->y;

            // with triangles 0, 1, 2 and 3, 1, 2
            result = new std::vector<Vertice>{valids[0], valids[1], Vertice(new Vector3(intersecX, intersecY, sceneManager->viewportDistance), invalids[0].shade), Vertice(new Vector3(intersecX2, intersecY2, sceneManager->viewportDistance), invalids[0].shade)};
            break;
        default:
            result = new std::vector<Vertice>{valids[0], valids[1], valids[2]};
            break;
        }

        return result;
    }

    void Renderer::Get_MouseState(int *x, int *y)
    {
        SDL_GetMouseState(x, y);
    }

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
            values->emplace_back(d);
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

        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        // Create texture to draw to
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    windowWidth, windowHeight);
        pixels = new Uint32[windowWidth * windowHeight];
        memset(pixels, 0, windowWidth * windowHeight * sizeof(Uint32));
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }

    void Renderer::Render()
    {
        memset(pixels, 0, windowWidth * windowHeight * sizeof(Uint32));
        screenBuffer.clear();

        for (BaseObject *obj : *sceneManager->objects)
        {
            RenderInstance(obj);
        }

        SDL_UpdateTexture(texture, NULL, pixels, windowWidth * sizeof(Uint32));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    void Renderer::SetPixel(int x, int y, Color *color)
    {
        if (x < 0 || x >= windowWidth || y < 0 || y >= windowHeight)
            return;

        Uint32 pixel = ((Uint32)color->r << 24) |
                       ((Uint32)color->g << 16) |
                       ((Uint32)color->b << 8) |
                       ((Uint32)color->a);

        pixels[y * windowWidth + x] = pixel;
    }

    void Renderer::RenderInstance(BaseObject *obj)
    {
        std::vector<Vertice> projected = std::vector<Vertice>();
        projected.reserve(obj->bVertices.size());
        for (Vertice *v : obj->bVertices)
        {
            Vector3 *vProj;
            Vector3 vPos = *v->position;
            // scale
            vProj = new Vector3(*obj->transform->scale * vPos);
            // rotate
            vProj = obj->transform->rotation->RotateVector3(vProj);
            // translate
            vProj = new Vector3(*vProj + *obj->transform->position);

            // now that we got world space, project it camera space
            vProj = new Vector3(*vProj - *sceneManager->currentCamera->transform->position);

            // camera rotation applied
            Quaternion *q = sceneManager->currentCamera->transform->rotation;
            vProj = q->Conjugate(*q).RotateVector3(vProj);
            
            // project
            projected.emplace_back(Vertice(vProj, v->shade));
        }
        std::vector<Triangle *> triangles = obj->bTriangles;
        for (Triangle *triangle : triangles)
        {
            // rotate direction by object rotation
            Vector3 *rd = obj->transform->rotation->RotateVector3(triangle->direction);
            // check if triangle is visible
            double angle = obj->transform->position->angleBetween(rd);
            if (angle > -90 && angle < 90)
            {
                continue;
            }

            std::array<Vertice, 3> vP = {projected[triangle->p0], projected.at(triangle->p1), projected.at(triangle->p2)};

            std::vector<Vertice> *trs = checkTriangle(vP);
            switch (trs->size())
            {
            case 6:
                // with triangles 0, 1, 2 and 3, 1, 2
                DrawTriangle(new Triangle(0, 1, 2, nullptr, triangle->material), trs);
                DrawTriangle(new Triangle(3, 1, 2, nullptr, triangle->material), trs);
                break;
            case 3:
                // with triangle 0, 1, 2
                DrawTriangle(new Triangle(0, 1, 2, nullptr, triangle->material), trs);
                break;
            default:
                break;
            }
        }
    }

    void Renderer::DrawTriangle(Triangle *triangle, std::vector<Vertice> *projected)
    {

        Vector3 *NP0 = projected->at(triangle->p0).position;
        Vector3 *NP1 = projected->at(triangle->p1).position;
        Vector3 *NP2 = projected->at(triangle->p2).position;
        Vector3 *P0 = ProjectVertex(NP0);
        P0->z = NP0->z;
        Vector3 *P1 = ProjectVertex(NP1);
        P1->z = NP1->z;
        Vector3 *P2 = ProjectVertex(NP2);
        P2->z = NP2->z;
        double h0 = projected->at(triangle->p0).shade;
        double h1 = projected->at(triangle->p1).shade;
        double h2 = projected->at(triangle->p2).shade;

        double tempSB = screenBuffer[P0->x + P0->y * windowWidth];
        double tempZ = P0->z;
        if (!std::isnan(tempSB) && tempSB > tempZ)
        {
            double tempSB = screenBuffer[P1->x + P1->y * windowWidth];
            double tempZ = P1->z;
            if (!std::isnan(tempSB) && tempSB > tempZ)
            {
                double tempSB = screenBuffer[P2->x + P2->y * windowWidth];
                double tempZ = P2->z;
                if (!std::isnan(tempSB) && tempSB > tempZ)
                {
                    return;
                }
            }
        }

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
        std::vector<double> *z01 = Interpolate(P0->y, P0->z, P1->y, P1->z);

        std::vector<double> *x12 = Interpolate(P1->y, P1->x, P2->y, P2->x);
        std::vector<double> *h12 = Interpolate(P1->y, h1, P2->y, h2);
        std::vector<double> *z12 = Interpolate(P1->y, P1->z, P2->y, P2->z);

        std::vector<double> *x02 = Interpolate(P0->y, P0->x, P2->y, P2->x);
        std::vector<double> *h02 = Interpolate(P0->y, h0, P2->y, h2);
        std::vector<double> *z02 = Interpolate(P0->y, P0->z, P2->y, P2->z);

        // Concatenate the short sides
        // x01->pop_back();
        // h01->pop_back();
        std::vector<double> *x012 = new std::vector<double>{};
        std::vector<double> *h012 = new std::vector<double>{};
        std::vector<double> *z012 = new std::vector<double>{};

        x012->insert(x012->end(), x01->begin(), x01->end());
        x012->insert(x012->end(), x12->begin(), x12->end());

        h012->insert(h012->end(), h01->begin(), h01->end());
        h012->insert(h012->end(), h12->begin(), h12->end());

        z012->insert(z012->end(), z01->begin(), z01->end());
        z012->insert(z012->end(), z12->begin(), z12->end());

        // Determine left from right
        double m = floor(x012->size() / 2);
        std::vector<double> *xleft = x012;
        std::vector<double> *xright = x02;
        std::vector<double> *hleft = h012;
        std::vector<double> *hright = h02;
        std::vector<double> *zleft = z012;
        std::vector<double> *zright = z02;
        if (x02->at(m) < x012->at(m))
        {
            xleft = x02;
            xright = x012;
            hleft = h02;
            hright = h012;
            zleft = z02;
            zright = z012;
        }

        Color *color = triangle->material->color;
        // bool fullDraw = true;
        for (int y = P0->y; y < P2->y; y++)
        {
            double ypy = y - P0->y;
            double xl = xleft->at(ypy);
            double xr = xright->at(ypy);
            std::vector<double> *hsegment = Interpolate(xl, hleft->at(y - P0->y), xr, hright->at(y - P0->y));
            std::vector<double> *zsegment = Interpolate(xl, 1 / zleft->at(y - P0->y), xr, 1 / zright->at(y - P0->y));

            for (int x = xl; x < xr; x++)
            {
                int centeredX = x + sceneManager->centeredCW;
                int centeredY = sceneManager->centeredCH - y;
                if (centeredX < 0 || centeredX > windowWidth || centeredY < 0 || centeredY > windowHeight)
                {
                    continue;
                }
                double xxl = x - xl;
                double screenElm = screenBuffer[x + y * windowWidth];
                double zsegm = zsegment->at(xxl);

                if (!std::isnan(screenElm) && screenElm > zsegm)
                {
                    // fullDraw = false;
                    continue;
                }

                screenBuffer[x + y * windowWidth] = zsegm;
                Color *c = new Color(*color * hsegment->at(xxl));
                SetPixel(centeredX, centeredY, c);
            }
        }

        // color = triangle->material->outlineColor;
        // if (color && fullDraw)
        // {
        //     DrawLine(projected->at(triangle->p0), projected->at(triangle->p1), color);
        //     DrawLine(projected->at(triangle->p0), projected->at(triangle->p2), color);
        //     DrawLine(projected->at(triangle->p1), projected->at(triangle->p2), color);
        // }
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
                SetPixel(x + sceneManager->centeredCW, sceneManager->centeredCH - ys->at(x - P0->x), color);
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
};