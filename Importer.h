#pragma once
#ifndef IMPORTER
#define IMPORTER

#include "baseobject.h"
#include "GltfImporter.h"
#include <string>

namespace PEngine
{
    struct Importer
    {
        // ── OBJ import (legacy — geometry only, no skeleton) ──────────────────
        static BaseObject* ImportFromOBJ(const std::string& filePath);

        // ── glTF 2.0 import (.gltf or .glb) ──────────────────────────────────
        // Returns a fully wired BaseObject with SkinnedVertex mesh,
        // a Skeleton, and all animation clips parsed from the file.
        //
        // The first clip is automatically set as the active animation.
        // Switch clips at runtime with:
        //   obj->animator.play(result.clips[1]);          // immediate
        //   obj->animator.crossFadeTo(result.clips[2], 0.25f);  // blended
        //
        // texManager — pointer to your renderer's TextureManager.
        //              Pass nullptr to skip texture loading.
        //
        // Example:
        //   auto r = Importer::ImportFromGLTF("assets/soldier/soldier.glb",
        //                                     &renderer.texManager);
        //   if (!r.ok) { std::cerr << r.error; return; }
        //   sm->objects->push_back(r.object);
        //   // r.clips[0] = first animation, already playing
        //
        template<typename TM = void>
        static GltfImporter::Result ImportFromGLTF(const std::string& filePath,
                                                    TM* texManager = nullptr)
        {
            GltfImporter imp;
            return imp.load<TM>(filePath, texManager);
        }
    };
}

#endif
