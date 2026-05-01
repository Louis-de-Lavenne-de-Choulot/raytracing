#pragma once
// collisiontrigger.h  -  HonHengine
// ─────────────────────────────────────────────────────────────────────────────
//  CollisionTrigger
//
//  Attaches an event system to any BaseObject.  When another object (or the
//  player camera position) overlaps the trigger's AABB, the registered
//  callbacks fire.
//
//  Three distinct events:
//    OnEnter  - fires once, the frame the overlap first begins
//    OnStay   - fires every tick while the overlap continues
//    OnExit   - fires once, the frame the overlap ends
//
//  Callbacks receive a CollisionEvent carrying both participants.
//
//  Usage (static trigger zone example):
//
//      CollisionTrigger zone(doorZoneObject);
//      zone.isSolid = false;   // ghost trigger, don't block movement
//
//      zone.OnEnter([&](const CollisionEvent& e) {
//          std::cout << "Player entered door zone!\n";
//          OpenDoor();
//      });
//      zone.OnExit([&](const CollisionEvent& e) {
//          CloseDoor();
//      });
//
//      // In your fixed-step loop, test against candidate objects:
//      zone.Poll({ playerObject, guardBody, crateObject });
//
//  Usage (solid obstacle example):
//
//      CollisionTrigger wall(wallObject);
//      wall.isSolid = true;
//      wall.OnEnter([&](const CollisionEvent& e) {
//          if (e.other->type == PLAYER)
//              e.other->transform.position -= e.penetration; // push back
//      });
//
//  CollisionTriggerWorld:
//      Manages multiple triggers; one Poll() call broadcasts to all.
//
//      CollisionTriggerWorld triggerWorld;
//      triggerWorld.Register(&zone);
//      triggerWorld.Register(&wall);
//      // per tick:
//      triggerWorld.Poll(sm->objects);
// ─────────────────────────────────────────────────────────────────────────────

#include "baseobject.h"
#include "rigidbody.h"       // for AABB
#include <functional>
#include <vector>
#include <unordered_set>
#include <string>

namespace HonHengine {

    // ─────────────────────────────────────────────────────────────────────────
    //  CollisionEvent  - passed to every callback
    // ─────────────────────────────────────────────────────────────────────────
    struct CollisionEvent
    {
        BaseObject* self        = nullptr;   // the trigger's host object
        BaseObject* other       = nullptr;   // the object that entered/stayed/exited
        Vector3     penetration;             // depth vector (self pushed by this to separate)
        bool        isTriggerOnly = false;   // true when isSolid == false
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  CollisionTrigger
    // ─────────────────────────────────────────────────────────────────────────
    class CollisionTrigger
    {
    public:
        using Callback = std::function<void(const CollisionEvent&)>;

        // ── Configuration ─────────────────────────────────────────────────────
        BaseObject* object   = nullptr;     // non-owning host
        bool        isSolid  = true;        // if true, callers should use penetration
                                            // to push colliders apart
        bool        enabled  = true;

        // Optional tag filter: only interact with objects whose tag matches.
        // Leave empty ("") to collide with everything.
        std::string filterTag;

        // ── Construction ──────────────────────────────────────────────────────
        explicit CollisionTrigger(BaseObject* host, bool solid = true)
            : object(host), isSolid(solid)
        {}

        // ── Event registration ────────────────────────────────────────────────
        void OnEnter(Callback cb) { _onEnter.push_back(std::move(cb)); }
        void OnStay (Callback cb) { _onStay .push_back(std::move(cb)); }
        void OnExit (Callback cb) { _onExit .push_back(std::move(cb)); }

        // Remove all callbacks for a given event
        void ClearEnter() { _onEnter.clear(); }
        void ClearStay()  { _onStay .clear(); }
        void ClearExit()  { _onExit .clear(); }
        void ClearAll()   { _onEnter.clear(); _onStay.clear(); _onExit.clear(); }

        // ── Polling ───────────────────────────────────────────────────────────
        // Call once per fixed tick with the list of candidate objects.
        // The trigger's own host object is automatically skipped.
        void Poll(const std::vector<BaseObject*>& candidates)
        {
            if (!enabled || !object) return;

            AABB selfAABB = AABB::FromObject(object);
            std::unordered_set<BaseObject*> currentOverlaps;

            for (BaseObject* other : candidates)
            {
                if (!other || other == object) continue;
                if (!filterTag.empty() && other->tag != filterTag) continue;

                AABB otherAABB = AABB::FromObject(other);
                if (!selfAABB.Overlaps(otherAABB)) continue;

                currentOverlaps.insert(other);
                Vector3 pen = selfAABB.PenetrationDepth(otherAABB);

                CollisionEvent ev;
                ev.self          = object;
                ev.other         = other;
                ev.penetration   = pen;
                ev.isTriggerOnly = !isSolid;

                if (_prevOverlaps.find(other) == _prevOverlaps.end())
                {
                    // New overlap - fire OnEnter
                    for (auto& cb : _onEnter) cb(ev);
                }
                else
                {
                    // Continuing overlap - fire OnStay
                    for (auto& cb : _onStay) cb(ev);
                }
            }

            // Objects that were overlapping last tick but not this tick → OnExit
            for (BaseObject* prev : _prevOverlaps)
            {
                if (currentOverlaps.find(prev) == currentOverlaps.end())
                {
                    CollisionEvent ev;
                    ev.self          = object;
                    ev.other         = prev;
                    ev.penetration   = Vector3(0, 0, 0);
                    ev.isTriggerOnly = !isSolid;
                    for (auto& cb : _onExit) cb(ev);
                }
            }

            _prevOverlaps = std::move(currentOverlaps);
        }

        // Convenience: poll against a raw pointer vector (SceneManager style)
        void Poll(const std::vector<BaseObject*>* candidates)
        {
            if (candidates) Poll(*candidates);
        }

        // Reset overlap memory (e.g. when re-enabling or scene reload)
        void Reset() { _prevOverlaps.clear(); }

    private:
        std::vector<Callback>        _onEnter;
        std::vector<Callback>        _onStay;
        std::vector<Callback>        _onExit;
        std::unordered_set<BaseObject*> _prevOverlaps;
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  CollisionTriggerWorld
    //
    //  Collects all CollisionTriggers in a scene and dispatches Poll() to each.
    //  Pass the scene's full object list once per tick.
    // ─────────────────────────────────────────────────────────────────────────
    class CollisionTriggerWorld
    {
    public:
        void Register(CollisionTrigger* t)   { _triggers.push_back(t); }
        void Unregister(CollisionTrigger* t)
        {
            _triggers.erase(
                std::remove(_triggers.begin(), _triggers.end(), t),
                _triggers.end());
        }

        // One call per tick - broadcasts to all registered triggers.
        void Poll(const std::vector<BaseObject*>& objects)
        {
            for (auto* t : _triggers)
                t->Poll(objects);
        }

        void Poll(const std::vector<BaseObject*>* objects)
        {
            if (objects) Poll(*objects);
        }

        void ResetAll()
        {
            for (auto* t : _triggers) t->Reset();
        }

    private:
        std::vector<CollisionTrigger*> _triggers;
    };

} // namespace HonHengine
