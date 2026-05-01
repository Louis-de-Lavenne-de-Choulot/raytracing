#pragma once
#ifndef SCENEMANAGER
#define SCENEMANAGER
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include <vector>
#include "settings.h"
namespace HonHengine
{
    struct SceneManager
    {
        // centered Canvas Width & Height
        double centeredCW = Settings::canvasWidth / 2;
        double centeredCH = Settings::canvasWidth / 2;
        Camera *currentCamera = nullptr;
        std::vector<Camera *> *cameras = new std::vector<Camera *>();
        std::vector<BaseObject *> *objects = new std::vector<BaseObject *>();
        std::vector<BaseLight *> *lights = new std::vector<BaseLight *>();

    };
};
#endif