#pragma once
// rigidbody.h  -  PEngine
// ─────────────────────────────────────────────────────────────────────────────
//  RigidBody
//
//  A physics component you attach to any BaseObject*.  It owns the object's
//  linear and angular dynamics.  The host object keeps full ownership of its
//  own transform - RigidBody reads & writes it every Integrate() call.
//
//  Design notes
//  ────────────
//  • No global physics world is required.  Scenes can opt in by creating a
//    PhysicsWorld (rigidbody.h ships that too) and registering bodies, or they
//    can call Integrate() / ResolveCollision() manually per body.
//
//  • Gravity is applied as an acceleration (m/s²) along –Y by default, but
//    any direction is accepted so you can do zero-g or side-scrollers.
//
//  • Angular dynamics are intentionally simplified: a single Vector3 for
//    angularVelocity (degrees/s around each axis) applied as Euler–Quaternion
//    integration.  Good enough for most gameplay use-cases without a full
//    inertia-tensor implementation.
//
//  • "Kinematic" bodies have infinite effective mass: gravity and linear
//    impulses are ignored, but the body still participates in collision
//    detection so it can push dynamic bodies.
//
//  Usage:
//      RigidBody rb(myObject);
//      rb.mass = 2.0;
//      rb.restitution = 0.4;
//
//      // per-frame fixed-step loop:
//      rb.AddForce(Vector3(0, 50, 0));   // one-shot impulse this tick
//      rb.Integrate(FIXED_DT);
// ─────────────────────────────────────────────────────────────────────────────

#include "baseobject.h"
#include "vector3.h"
#include "quaternion.h"
#include <cmath>
#include <functional>

namespace PEngine {

    // ─────────────────────────────────────────────────────────────────────────
    //  AABB  (Axis-Aligned Bounding Box) - used by both RigidBody and
    //  CollisionTrigger for fast broad-phase tests.
    // ─────────────────────────────────────────────────────────────────────────
    struct AABB
    {
        Vector3 min;
        Vector3 max;

        // Build from object transform (centre ± half-extents)
        static AABB FromObject(const BaseObject* obj)
        {
            const Vector3& c = obj->transform.position;
            const Vector3& s = obj->transform.scale;
            return AABB{
                Vector3(c.x - s.x, c.y - s.y, c.z - s.z),
                Vector3(c.x + s.x, c.y + s.y, c.z + s.z)
            };
        }

        bool Overlaps(const AABB& other) const
        {
            return (min.x <= other.max.x && max.x >= other.min.x) &&
                   (min.y <= other.max.y && max.y >= other.min.y) &&
                   (min.z <= other.max.z && max.z >= other.min.z);
        }

        // Penetration depth vector (smallest axis push-out)
        // Returns zero vector if no overlap.
        Vector3 PenetrationDepth(const AABB& other) const
        {
            if (!Overlaps(other)) return Vector3(0, 0, 0);

            double ox = (std::min)(max.x, other.max.x) - (std::max)(min.x, other.min.x);
            double oy = (std::min)(max.y, other.max.y) - (std::max)(min.y, other.min.y);
            double oz = (std::min)(max.z, other.max.z) - (std::max)(min.z, other.min.z);

            // Pick smallest axis
            if (ox <= oy && ox <= oz)
                return Vector3(ox * ((min.x < other.min.x) ? -1.0 : 1.0), 0, 0);
            if (oy <= ox && oy <= oz)
                return Vector3(0, oy * ((min.y < other.min.y) ? -1.0 : 1.0), 0);
            return Vector3(0, 0, oz * ((min.z < other.min.z) ? -1.0 : 1.0));
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  RigidBody
    // ─────────────────────────────────────────────────────────────────────────
    class RigidBody
    {
    public:
        // ── Host ─────────────────────────────────────────────────────────────
        BaseObject* object = nullptr;   // non-owning

        // ── Physical properties ───────────────────────────────────────────────
        double mass        = 1.0;       // kg  (≤0 → treated as kinematic)
        double drag        = 0.02;      // linear damping  [0,1)
        double angularDrag = 0.05;      // angular damping [0,1)
        double restitution = 0.3;       // bounciness      [0,1]
        double friction    = 0.4;       // approximate coulomb friction

        bool   useGravity  = true;
        bool   isKinematic = false;     // if true, mass/gravity ignored

        // ── State ─────────────────────────────────────────────────────────────
        Vector3 velocity        = Vector3(0, 0, 0);  // m/s  (world space)
        Vector3 angularVelocity = Vector3(0, 0, 0);  // deg/s around each axis

        bool    isGrounded = false;     // set by PhysicsWorld after integration

        // ── Constructor ───────────────────────────────────────────────────────
        explicit RigidBody(BaseObject* host, double mass = 1.0)
            : object(host), mass(mass)
        {}

        // ── Forces ────────────────────────────────────────────────────────────

        // Accumulate a world-space force (N) for this tick only.
        void AddForce(const Vector3& force) { _accumulatedForce += force; }

        // Apply an immediate velocity change (impulse / mass, skips accumulator).
        void AddImpulse(const Vector3& impulse)
        {
            if (isKinematic || mass <= 0.0) return;
            velocity += impulse * (1.0 / mass);
        }

        // Shorthand: make the body "jump" with a given upward speed (m/s).
        void Jump(double speed) { AddImpulse(Vector3(0, speed * mass, 0)); }

        // ── Integration ───────────────────────────────────────────────────────
        // Call once per fixed time step.  dt in seconds.
        void Integrate(double dt, const Vector3& gravity = Vector3(0, -9.81, 0))
        {
            if (isKinematic || !object) { _accumulatedForce = Vector3(0,0,0); return; }

            double invMass = (mass > 0.0) ? (1.0 / mass) : 0.0;

            // ── Linear ────────────────────────────────────────────────────────
            Vector3 acceleration = _accumulatedForce * invMass;
            if (useGravity) acceleration += gravity;

            velocity += acceleration * dt;

            // Damping (exponential decay approximation)
            double ld = 1.0 - drag * dt * 60.0;   // tuned for 60 Hz feel
            if (ld < 0.0) ld = 0.0;
            velocity = velocity * ld;

            object->transform.position += velocity * dt;

            // ── Angular ───────────────────────────────────────────────────────
            double ad = 1.0 - angularDrag * dt * 60.0;
            if (ad < 0.0) ad = 0.0;
            angularVelocity = angularVelocity * ad;

            if (angularVelocity.magnitude() > 1e-6)
            {
                double ax = angularVelocity.x * dt;
                double ay = angularVelocity.y * dt;
                double az = angularVelocity.z * dt;
                Quaternion qx = AxisAngle(Vector3(1,0,0), ax);
                Quaternion qy = AxisAngle(Vector3(0,1,0), ay);
                Quaternion qz = AxisAngle(Vector3(0,0,1), az);
                object->transform.rotation =
                    (object->transform.rotation * qx * qy * qz).Normalized();
            }

            // Reset per-tick accumulator
            _accumulatedForce = Vector3(0, 0, 0);
        }

        // ── Collision response ────────────────────────────────────────────────
        // Simple impulse-based resolution between two bodies.
        // Call after Integrate() when overlap is detected.
        static void ResolveCollision(RigidBody& a, RigidBody& b,
                                     const Vector3& normal, double penetration)
        {
            // Separate objects (positional correction)
            double totalMass = (a.mass + b.mass);
            if (totalMass <= 0.0) return;

            double aShare = a.isKinematic ? 0.0 : (b.mass / totalMass);
            double bShare = b.isKinematic ? 0.0 : (a.mass / totalMass);

            if (a.object) a.object->transform.position -= normal * (penetration * aShare);
            if (b.object) b.object->transform.position += normal * (penetration * bShare);

            // Relative velocity along normal
            Vector3 relVel = a.velocity - b.velocity;
            double  vRel   = relVel.dot(&normal);

            if (vRel > 0.0) return;  // already separating

            double e = (a.restitution + b.restitution) * 0.5;
            double aInvM = a.isKinematic ? 0.0 : (a.mass > 0.0 ? 1.0/a.mass : 0.0);
            double bInvM = b.isKinematic ? 0.0 : (b.mass > 0.0 ? 1.0/b.mass : 0.0);
            double j = -(1.0 + e) * vRel / (aInvM + bInvM);

            if (!a.isKinematic) a.velocity += normal * (j * aInvM);
            if (!b.isKinematic) b.velocity -= normal * (j * bInvM);
        }

        // Current AABB in world space.
        AABB GetAABB() const
        {
            return object ? AABB::FromObject(object) : AABB{};
        }

    private:
        Vector3 _accumulatedForce = Vector3(0, 0, 0);

        static Quaternion AxisAngle(const Vector3& axis, double deg)
        {
            double r  = deg * (3.14159265358979 / 180.0);
            double h  = r * 0.5;
            double s  = std::sin(h);
            return Quaternion(std::cos(h), axis.x*s, axis.y*s, axis.z*s).Normalized();
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  PhysicsWorld
    //
    //  Optional convenience container.  Registers bodies, runs integration and
    //  broad-phase collision detection in one Step() call.
    //
    //  Usage:
    //      PhysicsWorld physics;
    //      physics.gravity = Vector3(0, -9.81, 0);
    //      physics.Register(&rb1);
    //      physics.Register(&rb2);
    //      // in fixed-step loop:
    //      physics.Step(FIXED_DT);
    // ─────────────────────────────────────────────────────────────────────────
    class PhysicsWorld
    {
    public:
        Vector3 gravity = Vector3(0, -9.81, 0);

        void Register(RigidBody* rb)   { _bodies.push_back(rb); }
        void Unregister(RigidBody* rb)
        {
            _bodies.erase(std::remove(_bodies.begin(), _bodies.end(), rb),
                          _bodies.end());
        }

        void Step(double dt)
        {
            // 1) Integrate every body
            for (auto* rb : _bodies)
                rb->Integrate(dt, gravity);

            // 2) Broad-phase AABB collision - O(n²), fine for small scenes
            for (size_t i = 0; i < _bodies.size(); ++i)
            {
                for (size_t j = i + 1; j < _bodies.size(); ++j)
                {
                    RigidBody* a = _bodies[i];
                    RigidBody* b = _bodies[j];
                    if (!a->object || !b->object) continue;

                    AABB aabb_a = a->GetAABB();
                    AABB aabb_b = b->GetAABB();

                    if (aabb_a.Overlaps(aabb_b))
                    {
                        Vector3 pen = aabb_a.PenetrationDepth(aabb_b);
                        double  depth = pen.magnitude();
                        if (depth > 1e-6)
                        {
                            Vector3 normal = pen.normalize();
                            RigidBody::ResolveCollision(*a, *b, normal, depth);
                        }
                    }
                }
            }
        }

    private:
        std::vector<RigidBody*> _bodies;
    };

} // namespace PEngine
