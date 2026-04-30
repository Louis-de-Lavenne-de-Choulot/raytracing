////  exampleScene_gltf_snippet.cpp
////
////  Drop this block inside ExampleScene::Run(), replacing the hand-built
////  "Skinned character (example)" section.
////
////  Prerequisites:
////    1. Download the test model (see below).
////    2. Place it at: assets/fox/Fox.glb
////    3. Ensure "skinned" shader is registered (already done in the scene).
//// ─────────────────────────────────────────────────────────────────────────────
////
////  ══════════════════════════════════════════════════════════════════════════════
////  TEST MODEL — glTF Sample Assets (official Khronos group, CC0 licence)
////
////  Model : Fox
////  URL   : https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/Fox/glTF-Binary/Fox.glb
////  Alt   : https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Fox/glTF-Binary/Fox.glb
////
////  The Fox has 3 animation clips baked in:
////    [0] "Survey"   — the fox looks around (good idle)
////    [1] "Walk"     — walk cycle
////    [2] "Run"      — run cycle
////
////  Other great CC0 options from the same repo:
////    CesiumMan    — simple humanoid walk cycle
////      https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/CesiumMan/glTF-Binary/CesiumMan.glb
////    BrainStem    — mechanical arm, shows complex hierarchy
////      https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/BrainStem/glTF-Binary/BrainStem.glb
////
////  Download with curl / wget (run from your project root):
////    curl -L -o assets/fox/Fox.glb \
////      "https://github.com/KhronosGroup/glTF-Sample-Assets/raw/main/Models/Fox/glTF-Binary/Fox.glb"
////  ══════════════════════════════════════════════════════════════════════════════
//
//#include "Importer.h"      // PEngine::Importer::ImportFromGLTF
//#include "animation.h"     // AnimationClip, AnimatorComponent
//
//// ── Inside ExampleScene::Run(), after the water object block: ─────────────────
//
//        // ── Animated character loaded from glTF ───────────────────────────────
//        {
//            auto result = Importer::ImportFromGLTF("assets/fox/Fox.glb",
//                                                   &renderer.texManager);
//            if (!result.ok)
//            {
//                std::cerr << "[Scene] glTF import failed: " << result.error << "\n";
//            }
//            else
//            {
//                BaseObject* fox = result.object;
//
//                // Scale the fox down — the Khronos Fox model is very large
//                fox->transform.scale = Vector3(0.03f, 0.03f, 0.03f);
//                fox->transform.position = Vector3(0.0f, 0.0f, 0.0f);
//
//                // result.clips[0] = "Survey"  (already auto-started by the importer)
//                // result.clips[1] = "Walk"
//                // result.clips[2] = "Run"
//                //
//                // Cross-fade to Walk after 4 s (illustrating the blend system):
//                //   fox->animator.crossFadeTo(result.clips[1], 0.3f);
//                //
//                // Or switch on input inside the main loop:
//                //   if (someKeyPressed) fox->animator.crossFadeTo(result.clips[2], 0.25f);
//
//                sm->objects->push_back(fox);
//
//                std::cout << "[Scene] Fox loaded with "
//                          << result.clips.size() << " clips.\n";
//            }
//        }
