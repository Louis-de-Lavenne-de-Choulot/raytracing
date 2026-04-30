#pragma once
#include "material.h"
#include "basetype.h"
#include "color.h"
#include "material.h"

namespace PEngine {
    namespace Defaults {
        inline Color Pink = Color(255, 0, 255, 255);
        inline Material* MissingMaterial = new Material(0, 0.0, Pink);

    }
}