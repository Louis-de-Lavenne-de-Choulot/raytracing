#include <iostream>
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

// // Library effective with Windows
// #include <windows.h>


SceneManager* sceneManager;


void DrawCube(Renderer renderer) {
    Color *r = new Color(190, 0, 0, 255);
    // Color *g = new Color(0, 190, 0, 255);
    Color *b = new Color(0, 0, 190, 255);
    Rectangle *cube = new Rectangle(.1, new Vector3(-1.5, 0, 7), new Vector3(0, 0, 0), new Material(0, 0, r, b));
    
    for (int a = 0; a < 12; a++) {
    renderer.ForceClean();
        cube->radius+=0.1;
        cube->position->x+=0.2;
    std::array<Triangle*, 12> triangles = cube->GetTriangles();
    // std::array<Triangle*, 12> triangles2 = cube2->GetTriangles();
    for (int i = 0; i < 12; i++) {
        //draw triangle
        renderer.DrawWireFrameTriangle(triangles[i]);
        // renderer.DrawWireFrameTriangle(triangles2[i]);
    }
    renderer.ForceRender();
    // sleep 1000 milliseconds
    // Sleep(1000);
    }
}

int main(int argc, char *argv[]) {
    // create scene manager
    sceneManager = new SceneManager();
    // // create main camera
    // Camera* camera = new Camera();
    // //add camera to scene manager
    // sceneManager->currentCamera = camera;
    // sceneManager->cameras = new std::vector<Camera*>();
    // sceneManager->cameras->push_back(camera);

    // sceneManager->objects = new std::vector<BaseObject*>();
    // sceneManager->objects->push_back(new Sphere(new Vector3(0, -1, 3), new Vector3(0, 0, 0), new Material(500, 0.2, new Color(255, 0, 0, 255)), 1));
    // sceneManager->objects->push_back(new Sphere(new Vector3(2, 0, 4), new Vector3(0, 0, 0), new Material(500, 0.3, new Color(0, 0, 255, 255)), 1));
    // sceneManager->objects->push_back(new Sphere(new Vector3(-2, 0, 4), new Vector3(0, 0, 0), new Material(10, 0.4, new Color(0, 255, 0, 255)), 1));
    // sceneManager->objects->push_back(new Sphere(new Vector3(0, -5001, 4), new Vector3(0, 0, 0), new Material(1000, 0.5, new Color(0, 255, 0, 255)), 5000));

    // sceneManager->lights = new std::vector<BaseLight*>();
    // sceneManager->lights->push_back(new BaseLight(0.2, new Color(255, 255, 255, 255)));
    // sceneManager->lights->push_back(new PointLight(0.6, new Color(255, 255, 255, 255), new Vector3(5, 0, 0)));
    // sceneManager->lights->push_back(new DirectionalLight(0.2, new Color(255, 255, 255, 255), new Vector3(1, 4, 4)));
    
 
    // Vertice *p0 = new Vertice(new Vector3(-200, -250, 0), 1.);
    // Vertice *p1 = new Vertice(new Vector3(200, 50, 0), 0.);
    // Vertice *p2 = new Vertice(new Vector3(20, 250, 0), 0.5);
    // Color *color = new Color(0, 230, 0, 255);
    // Color *colorOutline = new Color(230, 0, 0, 255);
    // Material *triangleMat = new Material(500, 0.2, color, colorOutline);
    // Triangle *triangle = new Triangle(p0, p1, p2, triangleMat);

    
    // Start measuring time
    auto start = std::chrono::high_resolution_clock::now();
    Renderer renderer = Renderer(sceneManager);

    DrawCube(renderer);

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