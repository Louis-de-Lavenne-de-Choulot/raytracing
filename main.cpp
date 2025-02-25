
// sleep function
#include <windows.h>
#include <iostream>
#include <synchapi.h>
#include <thread>
#include <conio.h>

#include <vector>
#include <chrono>

#include "vector3.h"
#include "plane.h"
#include "triangle.h"
#include "renderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sphere.h"
#include "sceneManager.h"
#include "rectangle.h"

using namespace PEngine;

SceneManager *sceneManager;
int lastX = 0, lastY = 0;
float sensitivity = 0.09f; // Adjust rotation sensitivity
float moveSpeed = 0.1f;
std::atomic<bool> running{true};

DWORD WINAPI ThreadFunc(LPVOID data)
{
    // Check keyboard state
    while (running)
    {
        Transform *cam = sceneManager->currentCamera->transform;

        Vector3 newPosition = *cam->position;

        if (GetAsyncKeyState('Z') & 0x8000)
        {
            newPosition = newPosition + (cam->forward() * moveSpeed);
        }
        if (GetAsyncKeyState('Q') & 0x8000)
        {
            newPosition = newPosition + (cam->left() * moveSpeed);
        }
        if (GetAsyncKeyState('S') & 0x8000)
        {
            newPosition = newPosition - (cam->forward() * moveSpeed);
        }
        if (GetAsyncKeyState('D') & 0x8000)
        {
            newPosition = newPosition + (cam->right() * moveSpeed);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            running = false;
            return 0;
        }
        newPosition.y = 0;
        sceneManager->currentCamera->transform->position = new Vector3(newPosition);
        Sleep(10);
    }
    return 0;
}

void updateMouse(int x, int y)
{
    // Calculate mouse movement
    int deltaX = x - lastX;
    int deltaY = y - lastY;

    lastX = x;
    lastY = y;

    // Apply rotation only if there's movement
    if (deltaX != 0 || deltaY != 0)
    {
        // Convert mouse movement to rotation angles
        float yaw = deltaX * sensitivity;
        float pitch = deltaY * sensitivity;

        // Get current camera transform
        Transform *cam = sceneManager->currentCamera->transform;

        // Create rotation quaternions
        Vector3 upAxis(0, 0, 1);          // Y-axis for pitch rotation (z-axis)
        Vector3 rightAxis = cam->right(); // Local right axis for yaw rotation

        Quaternion tempx = Quaternion();
        Quaternion tempy = Quaternion();
        tempx.Rotate(cam->position, &rightAxis, yaw);
        tempy.Rotate(cam->position, &upAxis, pitch);
        *sceneManager->currentCamera->transform->rotation *= tempx;
        *sceneManager->currentCamera->transform->rotation *= tempy;
    }
}

int main(int argc, char *argv[])
{
    // create scene manager
    sceneManager = new SceneManager();
    // create main camera
    Camera *camera = new Camera();
    // add camera to scene manager
    sceneManager->currentCamera = camera;
    sceneManager->cameras = new std::vector<Camera *>();
    sceneManager->cameras->emplace_back(camera);

    Color *g = new Color(200, 0, 0, 255);
    Color *b = new Color(0, 0, 200, 255);
    PEngine::Rectangle *rec = new PEngine::Rectangle(new Vector3(1, 2, 1), new Vector3(-1.5, 0, 7), new Quaternion(1, 0, 0, 0), new Material(0, 0, g, b));
    sceneManager->objects->emplace_back(rec);

    // PEngine::Sphere *sph = new PEngine::Sphere(new Vector3(1, 1, 1), new Vector3(0, 0, 3), new Quaternion(1, 0, 0, 0), new Material(0, 0, b, g));
    // sceneManager->objects->emplace_back(sph);

    sceneManager->lights->emplace_back(new BaseLight(0.2, new Color(255, 255, 255, 255)));
    sceneManager->lights->emplace_back(new PointLight(0.6, new Color(255, 255, 255, 255), new Vector3(5, 0, 0)));
    sceneManager->lights->emplace_back(new DirectionalLight(0.2, new Color(255, 255, 255, 255), new Vector3(1, 4, 4)));

    Renderer renderer = Renderer(sceneManager);

    std::cout << "Move mouse or press WASD (press ESC to exit)\n";

    // Start input monitoring in separate thread
    HANDLE thread = CreateThread(NULL, 0, ThreadFunc, NULL, 0, NULL);

    int i = 0;
    // render scene
    while (running)
    {
        // Event handling
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
        }
        int x = 0;
        int y = 0;
        renderer.Get_MouseState(&x, &y);
        updateMouse(x, y);

        i++;
        if (i % 360 == 0)
        {
            i = 0;
        }
        // rec->transform->Rotate(new Vector3(1, 0, 0), i);
        // sceneManager->currentCamera->transform->Rotate(new Vector3(1, 0, 0), i);

        // Start measuring time
        auto start = std::chrono::high_resolution_clock::now();

        renderer.Render();
        // rec->transform->Rotate(new Vector3(1, 0, 0), 10);
        // Stop measuring time
        auto end = std::chrono::high_resolution_clock::now();

        // Calculate the duration
        std::chrono::duration<double> duration = end - start;

        // Output the duration in seconds
        std::cout << duration.count() << std::endl;
        // SDL_Delay(16);
    }

    // Wait for input thread to finish
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    // cleanup
    renderer.cleanup();
    return 0;
}