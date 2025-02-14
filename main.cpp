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
// sleep function
#include <iostream>
#include <synchapi.h>
#include <thread>

#include <conio.h>
#include <windows.h>
#include "rectangle.h"
using namespace PEngine;

SceneManager *sceneManager;
std::atomic<bool> running{true};

DWORD WINAPI ThreadFunc(LPVOID data)
{
    POINT mousePos;
    int lastX = 0, lastY = 0;
    float sensitivity = 0.05f; // Adjust rotation sensitivity
    float moveSpeed = 0.1f;
    // Check keyboard state
    while (running)
    {
        Transform *cam = sceneManager->currentCamera->transform;
        // Get current mouse position
        GetCursorPos(&mousePos);

        // Calculate mouse movement
        int deltaX = mousePos.x - lastX;
        int deltaY = mousePos.y - lastY;

        lastX = mousePos.x;
        lastY = mousePos.y;
        Vector3 newPosition = *cam->position;

        // Apply rotation only if there's movement
        if (deltaX != 0 || deltaY != 0)
        {
            // Convert mouse movement to rotation angles
            float yaw = deltaX * sensitivity;
            float pitch = deltaY * sensitivity;

            // Get current camera transform
            Transform *cam = sceneManager->currentCamera->transform;

            // Create rotation quaternions
            Vector3 upAxis(0, 1, 0);          // Y-axis for yaw rotation
            Vector3 rightAxis = cam->right(); // Local right axis for pitch rotation

            Quaternion yawRotation;
            Quaternion pitchRotation;

            Vector3 *forw = new Vector3(cam->forward());
            // Apply yaw rotation (rotate around global Y-axis)
            yawRotation.Rotate(forw, &upAxis, yaw);

            // Apply pitch rotation (rotate around local right-axis)
            pitchRotation.Rotate(forw, &rightAxis, pitch);

            // Combine rotations
            Quaternion newRotation = yawRotation * pitchRotation;

            // Update camera rotation
            cam->rotation = new Quaternion(newRotation.w, newRotation.x, newRotation.y, newRotation.z);
        }

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

        sceneManager->currentCamera->transform->position = new Vector3(newPosition);
        Sleep(100);
    }
    return 0;
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
    sceneManager->cameras->push_back(camera);

    Color *g = new Color(50, 190, 50, 255);
    Color *b = new Color(50, 50, 190, 255);
    PEngine::Rectangle *rec = new PEngine::Rectangle(new Vector3(1, 2, 1), new Vector3(-1.5, 0, 7), new Quaternion(1, 0, 0, 0), new Material(0, 0, g, b));
    sceneManager->objects->push_back(rec);

    sceneManager->lights->push_back(new BaseLight(0.2, new Color(255, 255, 255, 255)));
    sceneManager->lights->push_back(new PointLight(0.6, new Color(255, 255, 255, 255), new Vector3(5, 0, 0)));
    sceneManager->lights->push_back(new DirectionalLight(0.2, new Color(255, 255, 255, 255), new Vector3(1, 4, 4)));

    Renderer renderer = Renderer(sceneManager);

    std::cout << "Move mouse or press WASD (press ESC to exit)\n";

    // Start input monitoring in separate thread
    HANDLE thread = CreateThread(NULL, 0, ThreadFunc, NULL, 0, NULL);

    //! CRASH UPON +-86°, +-94°, +-176° ???????????? div by 0 smwhere?

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

        // Start measuring time
        auto start = std::chrono::high_resolution_clock::now();

        renderer.Render();
        rec->transform->Rotate(new Vector3(1, 0, 0), 10);
        // Stop measuring time
        auto end = std::chrono::high_resolution_clock::now();

        // Calculate the duration
        std::chrono::duration<double> duration = end - start;

        // Output the duration in seconds
        std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;
        // SDL_Delay(16);
    }

    // Wait for input thread to finish
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    // cleanup
    renderer.cleanup();
    return 0;
}