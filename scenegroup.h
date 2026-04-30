#pragma once
// scenegroup.h  -  PEngine
// ─────────────────────────────────────────────────────────────────────────────
//  SceneGroup  -  Unity-style "folder" / parent object
//
//  A SceneGroup owns a transform (position, rotation, scale) and holds
//  references to any number of BaseObject* children and nested SceneGroup*
//  sub-groups.  Moving/rotating/scaling the group propagates to every child
//  relative to its original local offset.
//
//  Analogy
//  ───────
//    Unity:     Create Empty → name it "Guard" → drag children under it
//    PEngine:   SceneGroup* guard = new SceneGroup("Guard", pos, rot, scale);
//               guard->Add(botBody);
//               guard->Add(botHead);
//               guard->Add(torchGroup);        // nested sub-group
//
//  Transform propagation
//  ─────────────────────
//  Each child stores a "local offset" snapshot taken at Add() time (relative
//  to the group's transform at that moment).  ApplyTransform() reconstructs
//  every child's world transform from the group's current transform + its
//  stored local offset.  Call it once per tick after you've moved the group.
//
//  This is intentionally simple: no persistent scene graph, no parent
//  pointer on BaseObject (to keep BaseObject dependency-free).  If you
//  need a full hierarchy at runtime, nest SceneGroups.
//
//  Lifecycle
//  ─────────
//  SceneGroup does NOT own its children's memory.  The scene's object list
//  (SceneManager::objects) still owns them.  The group only holds raw
//  non-owning pointers, matching PEngine's existing ownership model.
//
//  Usage example (guard bot refactored):
//
//      // Build geometry as usual, push into sm->objects
//      Rectangle* botBody  = new Rectangle(...); sm->objects->push_back(botBody);
//      Rectangle* botHead  = new Rectangle(...); sm->objects->push_back(botHead);
//      Rectangle* torchBody = new Rectangle(...); sm->objects->push_back(torchBody);
//
//      // Group them
//      SceneGroup* guard = new SceneGroup("Guard",
//          Vector3(12, 0, 12),   // initial world pos
//          Identity(),
//          Vector3(1, 1, 1));
//      guard->Add(botBody);
//      guard->Add(botHead);
//      guard->Add(torchBody);
//
//      // In patrol loop - just move the group:
//      guard->SetPosition(botPos);
//      guard->SetRotation(qFace);
//      guard->ApplyTransform();  // propagates to all children
// ─────────────────────────────────────────────────────────────────────────────

#include "baseobject.h"
#include "transform.h"
#include "vector3.h"
#include "quaternion.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <stdexcept>

namespace PEngine {

    // ─────────────────────────────────────────────────────────────────────────
    //  LocalOffset  -  child's transform relative to the group's transform
    //  at the time Add() was called.
    // ─────────────────────────────────────────────────────────────────────────
    struct LocalOffset
    {
        Vector3    position;   // offset from group origin
        Quaternion rotation;   // local rotation relative to group
        Vector3    scale;      // local scale (multiplied by group scale)
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  SceneGroup
    // ─────────────────────────────────────────────────────────────────────────
    class SceneGroup
    {
    public:
        std::string name;
        Transform   transform;

        bool        visible = true;  // toggling sets every child's visibility
                                     // (if BaseObject gains a visible flag)

        // ── Construction ──────────────────────────────────────────────────────
        explicit SceneGroup(std::string name,
                            Vector3    pos   = Vector3(0,0,0),
                            Quaternion rot   = Quaternion(1,0,0,0),
                            Vector3    scale = Vector3(1,1,1))
            : name(std::move(name))
        {
            transform.position = pos;
            transform.rotation = rot;
            transform.scale    = scale;
        }

        // ── Child management: BaseObject ──────────────────────────────────────

        // Add a BaseObject child, recording its current world transform as a
        // local offset relative to this group.
        void Add(BaseObject* obj)
        {
            if (!obj) return;
            if (_objectOffsets.count(obj)) return;  // already a child

            LocalOffset lo;
            lo.position = SubtractPositions(obj->transform.position,
                                            transform.position);
            lo.rotation = RelativeRotation(transform.rotation,
                                           obj->transform.rotation);
            lo.scale    = DivideScales(obj->transform.scale, transform.scale);

            _objects.push_back(obj);
            _objectOffsets[obj] = lo;
        }

        // Remove a child without touching its world transform.
        void Remove(BaseObject* obj)
        {
            _objects.erase(std::remove(_objects.begin(), _objects.end(), obj),
                           _objects.end());
            _objectOffsets.erase(obj);
        }

        bool Contains(const BaseObject* obj) const
        {
            return _objectOffsets.count(const_cast<BaseObject*>(obj)) > 0;
        }

        const std::vector<BaseObject*>& Objects() const { return _objects; }

        // ── Child management: nested SceneGroups ──────────────────────────────

        void Add(SceneGroup* sub)
        {
            if (!sub || sub == this) return;
            for (auto* s : _subGroups) if (s == sub) return;

            // Store sub-group's local offset relative to this group
            LocalOffset lo;
            lo.position = SubtractPositions(sub->transform.position,
                                            transform.position);
            lo.rotation = RelativeRotation(transform.rotation,
                                           sub->transform.rotation);
            lo.scale    = DivideScales(sub->transform.scale, transform.scale);

            _subGroups.push_back(sub);
            _subGroupOffsets[sub] = lo;
        }

        void Remove(SceneGroup* sub)
        {
            _subGroups.erase(std::remove(_subGroups.begin(), _subGroups.end(), sub),
                             _subGroups.end());
            _subGroupOffsets.erase(sub);
        }

        const std::vector<SceneGroup*>& SubGroups() const { return _subGroups; }

        // ── Transform API ─────────────────────────────────────────────────────

        void SetPosition(const Vector3& pos)    { transform.position = pos; }
        void SetRotation(const Quaternion& rot)  { transform.rotation = rot; }
        void SetScale   (const Vector3& scale)  { transform.scale    = scale; }

        // Translate the group (and children) by a delta.
        void Translate(const Vector3& delta)
        {
            transform.position += delta;
        }

        // Rotate the whole group around its own origin.
        void Rotate(const Quaternion& deltaRot)
        {
            transform.rotation = (transform.rotation * deltaRot).Normalized();
        }

        // ── Apply ─────────────────────────────────────────────────────────────
        // Propagate group's current transform to all children.
        // Call once per tick after mutating position/rotation/scale.
        void ApplyTransform()
        {
            // Direct children
            for (BaseObject* obj : _objects)
            {
                const LocalOffset& lo = _objectOffsets.at(obj);
                obj->transform.position = AddPositions(
                    transform.position,
                    RotateOffset(lo.position, transform.rotation));
                obj->transform.rotation =
                    (transform.rotation * lo.rotation).Normalized();
                obj->transform.scale    =
                    MultiplyScales(transform.scale, lo.scale);
            }

            // Sub-groups (recurse)
            for (SceneGroup* sub : _subGroups)
            {
                const LocalOffset& lo = _subGroupOffsets.at(sub);
                sub->transform.position = AddPositions(
                    transform.position,
                    RotateOffset(lo.position, transform.rotation));
                sub->transform.rotation =
                    (transform.rotation * lo.rotation).Normalized();
                sub->transform.scale    =
                    MultiplyScales(transform.scale, lo.scale);
                sub->ApplyTransform();  // recurse
            }
        }

        // ── Iteration helpers ─────────────────────────────────────────────────

        // Walk every BaseObject in the group (including nested sub-groups).
        void ForEachObject(const std::function<void(BaseObject*)>& fn) const
        {
            for (auto* obj : _objects) fn(obj);
            for (auto* sub : _subGroups) sub->ForEachObject(fn);
        }

        // Total object count (recursive).
        size_t ObjectCount() const
        {
            size_t n = _objects.size();
            for (auto* sub : _subGroups) n += sub->ObjectCount();
            return n;
        }

        // ── Convenience factory: build a group from an initializer list ────────
        static SceneGroup* Make(const std::string& name,
                                Vector3 pos,
                                std::initializer_list<BaseObject*> objs)
        {
            auto* g = new SceneGroup(name, pos);
            for (auto* o : objs) g->Add(o);
            return g;
        }

    private:
        std::vector<BaseObject*>                     _objects;
        std::unordered_map<BaseObject*, LocalOffset> _objectOffsets;

        std::vector<SceneGroup*>                     _subGroups;
        std::unordered_map<SceneGroup*, LocalOffset> _subGroupOffsets;

        // ── Internal math ─────────────────────────────────────────────────────

        // World-space offset from group origin to child position
        static Vector3 SubtractPositions(const Vector3& world, const Vector3& origin)
        {
            return Vector3(world.x - origin.x,
                           world.y - origin.y,
                           world.z - origin.z);
        }

        static Vector3 AddPositions(const Vector3& a, const Vector3& b)
        {
            return Vector3(a.x + b.x, a.y + b.y, a.z + b.z);
        }

        // Rotate a local offset by the group's rotation quaternion
        static Vector3 RotateOffset(const Vector3& offset, const Quaternion& rot)
        {
            // q * v * q^-1
            Quaternion vq(0, offset.x, offset.y, offset.z);
            Quaternion qc(rot.w, -rot.x, -rot.y, -rot.z);  // conjugate
            Quaternion res = rot * vq * qc;
            return Vector3(res.x, res.y, res.z);
        }

        // Local rotation of child relative to group
        static Quaternion RelativeRotation(const Quaternion& groupRot,
                                           const Quaternion& childWorld)
        {
            // q_local = q_group^-1 * q_child_world
            Quaternion groupInv(groupRot.w, -groupRot.x, -groupRot.y, -groupRot.z);
            return (groupInv * childWorld).Normalized();
        }

        // Scale offset stored as ratio: child_scale / group_scale
        static Vector3 DivideScales(const Vector3& child, const Vector3& group)
        {
            auto safe = [](double c, double g) { return g != 0.0 ? c / g : c; };
            return Vector3(safe(child.x, group.x),
                           safe(child.y, group.y),
                           safe(child.z, group.z));
        }

        static Vector3 MultiplyScales(const Vector3& group, const Vector3& local)
        {
            return Vector3(group.x * local.x,
                           group.y * local.y,
                           group.z * local.z);
        }
    };

} // namespace PEngine
