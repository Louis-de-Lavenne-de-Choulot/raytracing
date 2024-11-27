#pragma once
#ifndef SCENEMANAGER
#define SCENEMANAGER
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include <vector>
struct SceneManager
{
    int canvasWidth = 650;
    int canvasHeight = 650;
    double viewportWidth = 1.0;
    double viewportHeight = 1.0;
    double viewportDistance = 1.0;
    int maxRecursionDepth = 3;
    Camera *currentCamera;
    std::vector<Camera *> *cameras;
    std::vector<BaseObject *> *objects;
    std::vector<BaseLight *> *lights;
};
#endif