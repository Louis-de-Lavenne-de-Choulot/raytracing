#pragma once
#ifndef SCENEMANAGER
#define SCENEMANAGER
#include "camera.h"
#include "baseobject.h"
#include "baselight.h"
#include <vector>
#include "settings.h"
#include "script_manager.h"
#include "skybox.h"

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
		std::unique_ptr<ScriptManager> scriptManager = nullptr;
        Skybox* currentSkybox = nullptr;
		~SceneManager()
		{
			delete cameras;
			delete objects;
			delete lights;
		}

        void SetSkybox(Skybox* skybox) { currentSkybox = skybox; }
        Skybox* GetSkybox() const { return currentSkybox; }
    };
};
#endif