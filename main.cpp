#include <iostream>
#include <vector>
#include <chrono>
#include "vector3.h"
#include "renderer.h"
#include "camera.h"
#include "baselight.h"
#include "pointlight.h"
#include "directionallight.h"
#include "sphere.h"
#include "sceneManager.h"
// sleep function
#include <iostream>

// Library effective with Windows
#include <windows.h>


SceneManager* sceneManager;

int main(int argc, char *argv[]) {
    // create scene manager
    sceneManager = new SceneManager();
    // create main camera
    Camera* camera = new Camera();
    //add camera to scene manager
    sceneManager->currentCamera = camera;
    sceneManager->cameras = new std::vector<Camera*>();
    sceneManager->cameras->push_back(camera);

    sceneManager->objects = new std::vector<BaseObject*>();
    sceneManager->objects->push_back(new Sphere(new Vector3(0, -1, 3), new Vector3(0, 0, 0), new Material(500, 0.2, new Color(255, 0, 0, 255)), 1));
    sceneManager->objects->push_back(new Sphere(new Vector3(2, 0, 4), new Vector3(0, 0, 0), new Material(500, 0.3, new Color(0, 0, 255, 255)), 1));
    sceneManager->objects->push_back(new Sphere(new Vector3(-2, 0, 4), new Vector3(0, 0, 0), new Material(10, 0.4, new Color(0, 255, 0, 255)), 1));
    sceneManager->objects->push_back(new Sphere(new Vector3(0, -5001, 4), new Vector3(0, 0, 0), new Material(1000, 0.5, new Color(0, 255, 0, 255)), 5000));

    sceneManager->lights = new std::vector<BaseLight*>();
    sceneManager->lights->push_back(new BaseLight(0.2, new Color(255, 255, 255, 255)));
    sceneManager->lights->push_back(new PointLight(0.6, new Color(255, 255, 255, 255), new Vector3(5, 0, 0)));
    sceneManager->lights->push_back(new DirectionalLight(0.2, new Color(255, 255, 255, 255), new Vector3(1, 4, 4)));
    
    Renderer renderer = Renderer(sceneManager);
    // Start measuring time
    auto start = std::chrono::high_resolution_clock::now();
    
    // render scene
    renderer.render();
    
    // Stop measuring time
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate the duration
    std::chrono::duration<double> duration = end - start;

    // Output the duration in seconds
    std::cout << "Time taken: " << duration.count() << " seconds" << std::endl;
    //wait for user input
    std::cin.get();
    // cleanup
    renderer.cleanup();
    return 0;
}