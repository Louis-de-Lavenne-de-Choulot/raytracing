#pragma once
// basicmovements.h  —  PEngine
// ─────────────────────────────────────────────────────────────────────────────
//  BasicMovements
//
//  Self-contained first-person player controller, extracted from guardScene.cpp
//  and generalised so any scene can drop it in with minimal wiring.
//
//  Responsibilities
//  ────────────────
//    • ZQSD / WASD movement (configurable key set)
//    • Mouse-look  (yaw + pitch, with configurable sensitivity and pitch clamp)
//    • Eye-height lock or free vertical movement
//    • Optional room-bounds clamping
//    • Optional RigidBody integration (enables jumping, gravity)
//    • Toggle-key support for arbitrary actions (e.g. flashlight, crouch)
//
//  The controller does NOT own the camera or the SceneManager; it only holds
//  non-owning pointers.
//
//  Usage:
//
//      // Scene setup
//      Camera* cam = new Camera(Vector3(0, 1.7, 0), Identity(), 75);
//      sm->currentCamera = cam;
//
//      BasicMovements player(sm);
//      player.eyeHeight = 1.7;
//      player.moveSpeed = 0.08;
//      player.bounds    = AABB{ Vector3(-14.5,-10,-14.5), Vector3(14.5,10,14.5) };
//      player.useBounds = true;
//
//      // Optional: register a toggle action (E key)
//      player.RegisterToggle('E', [&](bool isOn) {
//          playerTorch->intensity = isOn ? 0.75 : 0.0;
//          std::cout << "Torch " << (isOn ? "ON" : "OFF") << "\n";
//      });
//
//      // In your animation thread fixed-step loop:
//      player.TickInput(FIXED_DT);
//
//      // In your render/event loop:
//      player.TickMouse(dx, dy);
// ─────────────────────────────────────────────────────────────────────────────

#include "camera.h"
#include "scenemanager.h"
#include "vector3.h"
#include "quaternion.h"
#include "rigidbody.h"      // for AABB
#include <functional>
#include <vector>
#include <cmath>
#include <numbers>

#ifdef _WIN32
#include <windows.h>        // GetAsyncKeyState
#else
// ── Non-Windows stub ──────────────────────────────────────────────────────────
// On Linux/macOS you'd read key state from SDL instead.
// Replace these stubs with your platform's key query if not using Windows.
static short GetAsyncKeyState(int) { return 0; }
static constexpr int VK_ESCAPE = 0x1B;
static constexpr int VK_SPACE  = 0x20;
#endif

namespace PEngine {

    // ─────────────────────────────────────────────────────────────────────────
    //  Key scheme  — choose ZQSD (French AZERTY) or WASD (English QWERTY)
    // ─────────────────────────────────────────────────────────────────────────
    enum class KeyScheme { ZQSD, WASD };

    struct KeyBindings
    {
        int forward  = 'Z';
        int backward = 'S';
        int left     = 'Q';
        int right    = 'D';
        int jump     = VK_SPACE;
        int quit     = VK_ESCAPE;

        static KeyBindings ZQSD()
        {
            return { 'Z', 'S', 'Q', 'D', VK_SPACE, VK_ESCAPE };
        }
        static KeyBindings WASD()
        {
            return { 'W', 'S', 'A', 'D', VK_SPACE, VK_ESCAPE };
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  ToggleAction  — key + callback registered via RegisterToggle()
    // ─────────────────────────────────────────────────────────────────────────
    struct ToggleAction
    {
        int  key        = 0;
        bool state      = false;   // current on/off
        bool wasDown    = false;   // debounce
        std::function<void(bool)> callback;  // called with new state
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  BasicMovements
    // ─────────────────────────────────────────────────────────────────────────
    class BasicMovements
    {
    public:
        // ── Configuration ─────────────────────────────────────────────────────
        double moveSpeed   = 0.08;          // units per tick (at 60 Hz feel)
        double sensitivity = 0.04f;         // mouse degrees-per-pixel
        float  pitchLimit  = 89.f;          // degrees

        double eyeHeight   = 1.7;           // if > 0, Y is locked to this
                                            // set ≤ 0 for free vertical movement

        bool   useBounds   = false;
        AABB   bounds      = {};            // world AABB the player is clamped to

        bool   canJump     = false;         // enable jump via jump key
        double jumpForce   = 5.0;           // m/s upward (needs rigidbody attached)

        KeyBindings keys   = KeyBindings::ZQSD();

        // Optional: link a RigidBody for physics-driven movement
        RigidBody*  rigidbody = nullptr;    // non-owning; null = no physics

        // ── Constructor ───────────────────────────────────────────────────────
        explicit BasicMovements(SceneManager* sm)
            : _sm(sm)
        {}

        // ── Toggle actions ────────────────────────────────────────────────────
        // Register a key that toggles a bool and calls cb(newState) on change.
        void RegisterToggle(int key, std::function<void(bool)> cb,
                            bool initialState = true)
        {
            _toggles.push_back({ key, initialState, false, std::move(cb) });
        }

        // ── Per-tick input processing (call from fixed-step thread) ───────────
        // Returns false when the player pressed Quit.
        bool TickInput(double dt)
        {
            if (!_sm || !_sm->currentCamera) return true;

            // ── Quit ──────────────────────────────────────────────────────────
            if (GetAsyncKeyState(keys.quit) & 0x8000) return false;

            // ── WASD / ZQSD movement ──────────────────────────────────────────
            {
                Camera*  cam  = _sm->currentCamera;
                Vector3  pos  = cam->transform.position;
                Vector3  fwd  = cam->transform.forward();
                Vector3  left = cam->transform.left();
                Vector3  rgt  = cam->transform.right();

                // Flatten forward/strafe vectors to XZ plane for FPS feel
                fwd  = Vector3(fwd.x,  0, fwd.z ).normalize();
                left = Vector3(left.x, 0, left.z).normalize();
                rgt  = Vector3(rgt.x,  0, rgt.z ).normalize();

                // Rescale speed to be frame-rate independent (60 Hz baseline)
                double step = moveSpeed * dt * 60.0;

                if (GetAsyncKeyState(keys.forward)  & 0x8000) pos = pos + fwd  * step;
                if (GetAsyncKeyState(keys.left)      & 0x8000) pos = pos + left * step;
                if (GetAsyncKeyState(keys.backward)  & 0x8000) pos = pos - fwd  * step;
                if (GetAsyncKeyState(keys.right)     & 0x8000) pos = pos + rgt  * step;

                // ── Eye height lock ───────────────────────────────────────────
                if (eyeHeight > 0.0) pos.y = eyeHeight;

                // ── Bounds clamp ──────────────────────────────────────────────
                if (useBounds)
                {
                    if (pos.x < bounds.min.x) pos.x = bounds.min.x;
                    if (pos.x > bounds.max.x) pos.x = bounds.max.x;
                    if (pos.y < bounds.min.y) pos.y = bounds.min.y;
                    if (pos.y > bounds.max.y) pos.y = bounds.max.y;
                    if (pos.z < bounds.min.z) pos.z = bounds.min.z;
                    if (pos.z > bounds.max.z) pos.z = bounds.max.z;
                }

                // ── Physics integration ───────────────────────────────────────
                if (rigidbody)
                {
                    // Drive the RigidBody's velocity on XZ so physics handles Y
                    Vector3 inputVel(
                        pos.x - cam->transform.position.x,
                        rigidbody->velocity.y,  // preserve gravity-driven Y
                        pos.z - cam->transform.position.z
                    );
                    rigidbody->velocity = inputVel;
                    // Don't write pos.y — let the rigidbody do it
                    pos.y = cam->transform.position.y;
                }

                cam->transform.position = pos;
            }

            // ── Jump ──────────────────────────────────────────────────────────
            if (canJump && rigidbody && rigidbody->isGrounded)
            {
                if (GetAsyncKeyState(keys.jump) & 0x8000)
                    rigidbody->Jump(jumpForce);
            }

            // ── Toggle actions ────────────────────────────────────────────────
            for (auto& t : _toggles)
            {
                bool down = (GetAsyncKeyState(t.key) & 0x8000) != 0;
                if (down && !t.wasDown)
                {
                    t.state = !t.state;
                    if (t.callback) t.callback(t.state);
                }
                t.wasDown = down;
            }

            return true;  // still running
        }

        // ── Mouse look (call from render/event loop with SDL relative motion) ─
        // dx, dy: pixels moved this frame (from SDL_GetRelativeMouseState or
        // your Get_MouseState wrapper).
        void TickMouse(int dx, int dy)
        {
            if (!_sm || !_sm->currentCamera) return;

            _yaw   += static_cast<float>(dx) * sensitivity;
            _pitch += static_cast<float>(dy) * sensitivity;

            if (_yaw >= 360.f) _yaw -= 360.f;
            if (_yaw <    0.f) _yaw += 360.f;
            if (_pitch >  pitchLimit) _pitch =  pitchLimit;
            if (_pitch < -pitchLimit) _pitch = -pitchLimit;

            RebuildCameraRotation();
        }

        // ── Direct yaw/pitch setters (for scene init or cutscenes) ───────────
        void SetYaw  (float deg) { _yaw   = deg; RebuildCameraRotation(); }
        void SetPitch(float deg) { _pitch = deg; RebuildCameraRotation(); }

        float GetYaw()   const { return _yaw; }
        float GetPitch() const { return _pitch; }

    private:
        SceneManager*             _sm      = nullptr;
        float                     _yaw     = 0.f;
        float                     _pitch   = 0.f;
        std::vector<ToggleAction> _toggles;

        void RebuildCameraRotation()
        {
            float yr = _yaw   * (3.14159265f / 180.f);
            float pr = _pitch * (3.14159265f / 180.f);
            float hy = yr * 0.5f, hp = pr * 0.5f;
            Quaternion qY(std::cos(hy), 0.f, std::sin(hy), 0.f);
            Quaternion qP(std::cos(hp), std::sin(hp), 0.f, 0.f);
            _sm->currentCamera->transform.rotation = (qY * qP).Normalized();
        }
    };

} // namespace PEngine
