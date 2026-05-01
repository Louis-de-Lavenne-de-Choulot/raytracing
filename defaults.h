#pragma once
#include "material.h"
#include "basetype.h"
#include "color.h"

namespace PEngine {
    namespace Defaults {

        // ── Material fallbacks ────────────────────────────────────────────────
        // Shown on any object whose material or texture fails to load —
        // the hot-pink "error" colour used by Unity and Unreal for the same purpose.
        inline Color Pink = Color(255, 0, 255, 255);
        inline Material* MissingMaterial = new Material(0, 0.0, Pink);
    } // namespace Defaults
} // namespace PEngine