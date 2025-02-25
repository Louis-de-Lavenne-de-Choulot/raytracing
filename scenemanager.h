#pragma once
#ifndef SCENEMANAGER
#define SCENEMANAGER
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include <vector>
namespace PEngine
{
    struct SceneManager
    {
        int canvasWidth = 650;
        int canvasHeight = 650;
        // centered Canvas Width & Height
        double centeredCW = canvasWidth / 2;
        double centeredCH = canvasWidth / 2;
        double viewportWidth = 1.0;
        double viewportHeight = 1.0;
        double viewportDistance = 1.0;
        int maxRecursionDepth = 3;
        Camera *currentCamera = nullptr;
        std::vector<Camera *> *cameras = new std::vector<Camera *>();
        std::vector<BaseObject *> *objects = new std::vector<BaseObject *>();
        std::vector<BaseLight *> *lights = new std::vector<BaseLight *>();

    };
};
#endif