#include <vector>
#include <chrono>
#include "vector3.h"
#include "rectangle.h"
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

SceneManager *sceneManager;

int main(int argc, char *argv[])
{
    // create scene manager
    sceneManager = new SceneManager();
    // create main camera
    Camera* camera = new Camera();
    //add camera to scene manager
    sceneManager->currentCamera = camera;
    sceneManager->cameras = new std::vector<Camera*>();
    sceneManager->cameras->push_back(camera);

    Color *r = new Color(190, 50, 50, 255);
    Color *g = new Color(50, 190, 50, 255);
    Color *b = new Color(50, 50, 190, 255);
    
    Rectangle *cube = new Rectangle(new Vector3(1, 1, 1), new Vector3(1.5, 0, 15), new Quaternion(1, 0, 0, 0), new Material(0, 0, r, b));    
    
    sceneManager->objects->push_back(cube);
    sceneManager->objects->push_back(new Rectangle(new Vector3(1, 2, 1), new Vector3(-1.5, 0, 7), new Quaternion(1, 0, 0, 0), new Material(0, 0, g, b)));
    
    sceneManager->lights->push_back(new BaseLight(0.2, new Color(255, 255, 255, 255)));
    sceneManager->lights->push_back(new PointLight(0.6, new Color(255, 255, 255, 255), new Vector3(5, 0, 0)));
    sceneManager->lights->push_back(new DirectionalLight(0.2, new Color(255, 255, 255, 255), new Vector3(1, 4, 4)));

    // Start measuring time
    auto start = std::chrono::high_resolution_clock::now();
    Renderer renderer = Renderer(sceneManager);

    // render scene
    for (int x = 10; x < 1000; x++){
        // draw cube
        renderer.Render();
        cube->transform->Rotate(new Vector3(1, 0, 0), x);


        // sleep for 100ms
        Sleep(500);
    }

    // Stop measuring time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate the duration
    std::chrono::duration<double> duration = end - start;

    // Output the duration in seconds
    std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;
    // wait for user input
    std::cin.get();

    // cleanup
    renderer.cleanup();
    return 0;
}