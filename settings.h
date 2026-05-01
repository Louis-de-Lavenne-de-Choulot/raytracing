#pragma once
#include "material.h"
#include "basetype.h"

namespace HonHengine {
    namespace Settings {
		enum ShadingMode {
			GOURAUD,
			PHONG
		};
		static ShadingMode shadingMode = PHONG;
        static int canvasWidth = 1080;
        static int canvasHeight = 720;
        static double viewportWidth = 1.0;
        static double viewportHeight = 1.0;
        static double viewportDistance = 1.0;
    }
}