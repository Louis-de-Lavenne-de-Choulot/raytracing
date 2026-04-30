#include <windows.h>
#include <iostream>
#include <synchapi.h>
#include <thread>
#include <conio.h>

#include <vector>
#include <chrono>
#include <SDL.h>
#include <mutex>

#include "vector3.h"
#include "plane.h"
#include "triangle.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sphere.h"
#include "sceneManager.h"
#include "rectangle.h"
#include "teapot.h"
#include "importer.h"
#include "exampleScene.h"
#include "minecraftScene.h"

using namespace PEngine;


std::mutex cameraMutex;
SceneManager* sceneManager;
float sensitivity = 0.04f;
float moveSpeed = 0.1f;
std::atomic<bool> running{ true };
PointLight *pointLight;

// Store yaw and pitch as plain floats instead of accumulating them
// into the quaternion. This is the only reliable way to prevent roll:
// we never compound rotations - we rebuild the quaternion from scratch
// every time either angle changes.
float cameraYaw = 0.0f;   // horizontal rotation, degrees, around world Y
float cameraPitch = 0.0f;   // vertical   rotation, degrees, around local X
// Clamp pitch so the camera cannot flip upside-down
constexpr float PITCH_LIMIT = 89.0f;

// Rebuild the camera quaternion from the current yaw and pitch scalars.
// Order: yaw first (world Y), then pitch (local X).
// Because we always start from identity and apply both rotations fresh,
// roll can never accumulate.
static void RebuildCameraRotation()
{
    // Half-angles in radians
    float yawRad = cameraYaw * (3.14159265f / 180.0f);
    float pitchRad = cameraPitch * (3.14159265f / 180.0f);

    float hy = yawRad * 0.5f;
    float hp = pitchRad * 0.5f;

    // Quaternion for yaw around world up (0,1,0)
    Quaternion qYaw(cos(hy), 0.0f, sin(hy), 0.0f);   // w, x, y, z

    // Quaternion for pitch around local right (1,0,0)
    Quaternion qPitch(cos(hp), sin(hp), 0.0f, 0.0f);  // w, x, y, z

    // Combined: yaw applied first, then pitch in the yawed frame
    sceneManager->currentCamera->transform.rotation = (qYaw * qPitch).Normalized();
    pointLight->rotation = (qYaw * qPitch).Normalized();
}

DWORD WINAPI ThreadFunc(LPVOID data)
{
    while (running)
    {
        Vector3 newPosition;
        Vector3 fwd, left, right;
        {
            std::lock_guard<std::mutex> lock(cameraMutex);
            newPosition = sceneManager->currentCamera->transform.position;
            fwd = sceneManager->currentCamera->transform.forward();
            left = sceneManager->currentCamera->transform.left();
            right = sceneManager->currentCamera->transform.right();
        }

        if (GetAsyncKeyState('Z') & 0x8000)
            newPosition = newPosition + (fwd * moveSpeed);
        if (GetAsyncKeyState('Q') & 0x8000)
            newPosition = newPosition + (left * moveSpeed);
        if (GetAsyncKeyState('S') & 0x8000)
            newPosition = newPosition - (fwd * moveSpeed);
        if (GetAsyncKeyState('D') & 0x8000)
            newPosition = newPosition + (right * moveSpeed);
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            running = false;
            return 0;
        }

        newPosition.y = 0;

        {
            std::lock_guard<std::mutex> lock(cameraMutex);
            sceneManager->currentCamera->transform.position = newPosition;
            pointLight->position = sceneManager->currentCamera->transform.position;
        }

        Sleep(10);
    }
    return 0;
}

void updateMouse(int x, int y)
{
    if (x == 0 && y == 0) return;

    std::lock_guard<std::mutex> lock(cameraMutex);

    // Accumulate into plain float angles
    cameraYaw += x * sensitivity;
    cameraPitch += y * sensitivity;

    // Keep yaw in [0, 360) to avoid float overflow over long sessions
    if (cameraYaw >= 360.0f) cameraYaw -= 360.0f;
    if (cameraYaw < 0.0f) cameraYaw += 360.0f;

    // Clamp pitch so the camera never flips past vertical
    if (cameraPitch > PITCH_LIMIT) cameraPitch = PITCH_LIMIT;
    if (cameraPitch < -PITCH_LIMIT) cameraPitch = -PITCH_LIMIT;

    // Rebuild the quaternion cleanly from the two angles - no drift possible
    RebuildCameraRotation();
}


int main(int argc, char* argv[])
{
    sceneManager = new SceneManager();
    ExampleScene::Run();
    return 0;
}


//
//int main(int argc, char* argv[])
//{
//
//    sceneManager = new SceneManager();
//    Camera* camera = new Camera();
//    sceneManager->currentCamera = camera;
//    sceneManager->cameras = new std::vector<Camera*>();
//    sceneManager->cameras->emplace_back(camera);
//
//    Color r = Color(200, 0, 0, 255);
//    Color g = Color(0, 200, 0, 255);
//    Color b = Color(0, 0, 200, 255);
//    //PEngine::Teapot* rec = new PEngine::Teapot(Vector3(1, 2, 1), Vector3(-1.5, 0, 7), Quaternion(1, 0, 0, 0), new Material(0, 0, g, r));
//    BaseObject* rec = Importer::ImportFromOBJ("C:/Users/LDL/Downloads/simple-light-switch/source/lightswitch/lightswitch/lightswitch.OBJ");
//    //BaseObject* rec = Importer::ImportFromOBJ("C:/Users/LDL/Downloads/the-utah-teapot/source/ba31ad9775cb44ee861971b805a29f68/teapot.obj");
//    rec->material->color = Color(200, 200, 200, 255);
//    sceneManager->objects->emplace_back(rec);
//
//    pointLight = new PointLight(0.4, Color(255, 25, 255, 255), sceneManager->currentCamera->transform.position);
//    sceneManager->lights->emplace_back(pointLight);
//    sceneManager->lights->emplace_back(new BaseLight(0.7, Color(255, 25, 255, 255)));
//    //sceneManager->lights->emplace_back(new DirectionalLight(0.6, Color(255, 255, 255, 255), Vector3(1, 1, 0)));
//
//    Renderer renderer = Renderer(sceneManager);
//
//    std::cout << "Move mouse or press WASD (press ESC to exit)\n";
//
//    HANDLE thread = CreateThread(NULL, 0, ThreadFunc, NULL, 0, NULL);
//
//    int i = 0;
//    while (running)
//    {
//        SDL_Event event;
//        while (SDL_PollEvent(&event))
//        {
//            if (event.type == SDL_QUIT)
//                running = false;
//        }
//
//        int x = 0, y = 0;
//        renderer.Get_MouseState(&x, &y);
//        updateMouse(x, y);
//
//        i++;
//        if (i % 360 == 0) i = 0;
//
//        auto start = std::chrono::high_resolution_clock::now();
//
//        {
//            std::lock_guard<std::mutex> lock(cameraMutex);
//            renderer.Render();
//        }
//
//        auto end = std::chrono::high_resolution_clock::now();
//        std::chrono::duration<double> duration = end - start;
//        std::cout << duration.count() << std::endl;
//    }
//
//    WaitForSingleObject(thread, INFINITE);
//    CloseHandle(thread);
//    renderer.cleanup();
//    return 0;
//}