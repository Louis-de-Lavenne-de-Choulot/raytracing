#pragma once
#ifndef ANIMATION_H
#define ANIMATION_H

// ─────────────────────────────────────────────────────────────────────────────
//  animation.h  —  Skeletal animation system for HonHengine
//
//  Data model (set up once, shared across objects):
//    Skeleton      — bone hierarchy + inverse bind poses
//    AnimationClip — timeline of per-bone keyframes (shared_ptr, ref-counted)
//
//  Per-object runtime (lives inside BaseObject):
//    AnimatorComponent — plays / blends clips, produces bonePalette each frame
//
//  Typical usage:
//
//    // --- Loading (e.g. in your glTF importer) ---
//    auto skeleton = std::make_shared<Skeleton>(...);
//    auto idle     = std::make_shared<AnimationClip>(...);
//    auto run      = std::make_shared<AnimationClip>(...);
//
//    // --- Per object ---
//    obj->animator.skeleton = skeleton;
//    obj->animator.play(idle);
//
//    // --- In your game logic ---
//    obj->animator.crossFadeTo(run, 0.25f);   // 0.25s blend
//
//    // --- In GPURenderer::Render() (automatic) ---
//    obj->animator.update(dt);
//    // upload obj->animator.bonePalette → uBonePalette[128]
//
// ─────────────────────────────────────────────────────────────────────────────

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cassert>

namespace HonHengine
{
    static constexpr int MAX_BONES            = 128;
    static constexpr int MAX_BONE_INFLUENCES  = 4;

    // ─────────────────────────────────────────────────────────────────────────
    //  Bone
    //  One node in the skeleton hierarchy.
    //    parentIndex == -1  →  this is the root bone.
    //    inverseBindPose    →  pre-computed at import time:
    //                          glm::inverse(bindPoseWorldMatrix)
    //                          transforms a vertex from mesh-local space into
    //                          bone-local space. Multiplied with the animated
    //                          world transform each frame to get the final
    //                          skinning matrix.
    // ─────────────────────────────────────────────────────────────────────────
    struct Bone
    {
        std::string name;
        int         parentIndex    = -1;
        glm::mat4   inverseBindPose = glm::mat4(1.0f);
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Skeleton
    //  Shared across every object that uses the same rig (shared_ptr).
    //  Bones are ordered so that a parent always appears before its children —
    //  that invariant is required by AnimatorComponent::_buildPalette().
    // ─────────────────────────────────────────────────────────────────────────
    struct Skeleton
    {
        std::vector<Bone> bones;

        int boneCount() const { return static_cast<int>(bones.size()); }

        // Convenience: find a bone index by name (-1 if not found).
        int indexOf(const std::string& boneName) const
        {
            for (int i = 0; i < static_cast<int>(bones.size()); ++i)
                if (bones[i].name == boneName) return i;
            return -1;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Keyframe
    //  A single point in time for one bone.
    //  Decomposed TRS — avoids gimbal lock and blends cleanly.
    // ─────────────────────────────────────────────────────────────────────────
    struct Keyframe
    {
        float      time     = 0.0f;
        glm::vec3  position = glm::vec3(0.0f);
        glm::quat  rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // identity
        glm::vec3  scale    = glm::vec3(1.0f);
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  BoneTrack
    //  All keyframes for one bone in one clip.
    //  Keys must be sorted by time (ascending) — importers should ensure this.
    // ─────────────────────────────────────────────────────────────────────────
    struct BoneTrack
    {
        int                  boneIndex = -1;
        std::vector<Keyframe> keys;

        // Sample the track at time t, interpolating between the two bracketing
        // keyframes. Returns an interpolated Keyframe.
        Keyframe sample(float t) const
        {
            if (keys.empty())
                return Keyframe{};

            if (keys.size() == 1 || t <= keys.front().time)
                return keys.front();

            if (t >= keys.back().time)
                return keys.back();

            // Binary search for the left bracket.
            int lo = 0, hi = static_cast<int>(keys.size()) - 2;
            while (lo < hi)
            {
                int mid = (lo + hi + 1) / 2;
                if (keys[mid].time <= t) lo = mid;
                else                     hi = mid - 1;
            }

            const Keyframe& a = keys[lo];
            const Keyframe& b = keys[lo + 1];
            float span = b.time - a.time;
            float alpha = (span > 1e-6f) ? (t - a.time) / span : 0.0f;

            Keyframe out;
            out.time     = t;
            out.position = glm::mix(a.position, b.position, alpha);
            out.scale    = glm::mix(a.scale,    b.scale,    alpha);
            out.rotation = glm::normalize(glm::slerp(a.rotation, b.rotation, alpha));
            return out;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  AnimationClip
    //  One animation (e.g. "idle", "run", "attack").
    //  Shared via shared_ptr — multiple objects can reference the same clip
    //  data without copying it.
    // ─────────────────────────────────────────────────────────────────────────
    struct AnimationClip
    {
        std::string            name;
        float                  duration  = 0.0f;   // seconds
        bool                   loops     = true;
        std::vector<BoneTrack> tracks;              // one entry per animated bone

        // Retrieve the track for a given bone index (nullptr if none).
        const BoneTrack* trackForBone(int boneIndex) const
        {
            for (const BoneTrack& t : tracks)
                if (t.boneIndex == boneIndex) return &t;
            return nullptr;
        }
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  AnimatorComponent
    //
    //  Lives inside BaseObject. The GPURenderer calls update(dt) every frame
    //  and then uploads bonePalette to the skinning shader.
    //
    //  Features:
    //    • Single-clip playback with looping / one-shot
    //    • Cross-fade blending between two clips (linear, configurable duration)
    //    • Speed multiplier
    //    • onClipEnd callback (e.g. to chain animations)
    //    • bonePalette is always padded to MAX_BONES identity matrices so the
    //      shader uniform upload is always a single fixed-size call
    // ─────────────────────────────────────────────────────────────────────────
    struct AnimatorComponent
    {
        // ── Rig ──────────────────────────────────────────────────────────────
        std::shared_ptr<Skeleton> skeleton;

        // ── Output (read by GPURenderer, written by update()) ─────────────────
        // Always MAX_BONES entries. Entries beyond skeleton->boneCount() are
        // identity matrices and are never referenced by any vertex.
        std::vector<glm::mat4> bonePalette;

        // ── Callback — fires when a non-looping clip reaches its end ──────────
        std::function<void()> onClipEnd;

        // ── Speed multiplier ──────────────────────────────────────────────────
        float speed = 1.0f;

        // ─────────────────────────────────────────────────────────────────────
        //  Construction
        // ─────────────────────────────────────────────────────────────────────
        AnimatorComponent()
        {
            bonePalette.assign(MAX_BONES, glm::mat4(1.0f));
        }

        // ─────────────────────────────────────────────────────────────────────
        //  play  —  start a clip immediately with no blend
        // ─────────────────────────────────────────────────────────────────────
        void play(std::shared_ptr<AnimationClip> clip, float startTime = 0.0f)
        {
            _current   = { clip, startTime };
            _blend     = {};          // clear any pending crossfade
        }

        // ─────────────────────────────────────────────────────────────────────
        //  crossFadeTo  —  blend from the current clip to a new one over
        //                  fadeDuration seconds. The new clip starts from
        //                  its beginning (or fromTime if specified).
        // ─────────────────────────────────────────────────────────────────────
        void crossFadeTo(std::shared_ptr<AnimationClip> clip,
                         float fadeDuration   = 0.25f,
                         float startTime      = 0.0f)
        {
            if (!clip) return;
            _blend.clip        = clip;
            _blend.time        = startTime;
            _blend.fadeDur     = (fadeDuration > 0.0f) ? fadeDuration : 1e-4f;
            _blend.fadeElapsed = 0.0f;
            _blend.active      = true;
        }

        // ─────────────────────────────────────────────────────────────────────
        //  isPlaying  —  true if any clip is active
        // ─────────────────────────────────────────────────────────────────────
        bool isPlaying() const { return _current.clip != nullptr; }

        // ─────────────────────────────────────────────────────────────────────
        //  currentClipName  —  empty string if nothing is playing
        // ─────────────────────────────────────────────────────────────────────
        const std::string& currentClipName() const
        {
            static const std::string empty;
            return _current.clip ? _current.clip->name : empty;
        }

        // ─────────────────────────────────────────────────────────────────────
        //  update  —  advance time and rebuild bonePalette.
        //             Called automatically by GPURenderer::Render(dt).
        // ─────────────────────────────────────────────────────────────────────
        void update(float dt)
        {
            if (!skeleton || !_current.clip) return;

            dt *= speed;

            // ── Advance current clip ──────────────────────────────────────────
            _advanceSlot(_current, dt);

            // ── Advance + manage crossfade ────────────────────────────────────
            if (_blend.active)
            {
                _advanceSlot(_blend, dt);
                _blend.fadeElapsed += dt;

                if (_blend.fadeElapsed >= _blend.fadeDur)
                {
                    // Fade complete — promote blend to current
                    _current = _blend;
                    _blend   = {};
                }
            }

            // ── Rebuild bone palette ──────────────────────────────────────────
            float blendAlpha = 0.0f;
            if (_blend.active)
                blendAlpha = std::min(_blend.fadeElapsed / _blend.fadeDur, 1.0f);

            _buildPalette(_current.clip.get(), _current.time,
                          _blend.active ? _blend.clip.get() : nullptr,
                          _blend.active ? _blend.time       : 0.0f,
                          blendAlpha);
        }

        // ─────────────────────────────────────────────────────────────────────
        //  active  —  false means the component is fully disabled (skeleton
        //             not set, or play() never called). GPURenderer checks
        //             this to skip the palette upload.
        // ─────────────────────────────────────────────────────────────────────
        bool active() const { return skeleton != nullptr && _current.clip != nullptr; }

    private:
        // ── Internal clip slot ────────────────────────────────────────────────
        struct Slot
        {
            std::shared_ptr<AnimationClip> clip;
            float time        = 0.0f;
            float fadeDur     = 0.0f;     // only used on blend slot
            float fadeElapsed = 0.0f;     // only used on blend slot
            bool  active      = false;    // only used on blend slot
        };

        Slot _current;
        Slot _blend;

        // ── Advance a slot's playback time ────────────────────────────────────
        void _advanceSlot(Slot& slot, float dt)
        {
            if (!slot.clip) return;
            slot.time += dt;
            if (slot.clip->loops)
            {
                if (slot.clip->duration > 0.0f)
                    while (slot.time > slot.clip->duration)
                        slot.time -= slot.clip->duration;
            }
            else
            {
                if (slot.time >= slot.clip->duration)
                {
                    slot.time = slot.clip->duration;
                    if (onClipEnd) onClipEnd();
                }
            }
        }

        // ── Compute a local transform matrix from a sampled keyframe ──────────
        static glm::mat4 _keyframeToMatrix(const Keyframe& kf)
        {
            glm::mat4 T = glm::translate(glm::mat4(1.0f), kf.position);
            glm::mat4 R = glm::mat4_cast(kf.rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), kf.scale);
            return T * R * S;
        }

        // ── Core palette builder ──────────────────────────────────────────────
        //  Samples clipA (and optionally clipB) at their respective times,
        //  blends the results by alpha, then walks the bone hierarchy from
        //  root to leaves accumulating world transforms.
        //
        //  bonePalette[i] = worldTransform[i] * bone[i].inverseBindPose
        // ─────────────────────────────────────────────────────────────────────
        void _buildPalette(const AnimationClip* clipA, float timeA,
                           const AnimationClip* clipB, float timeB,
                           float alpha)
        {
            const int n = skeleton->boneCount();
            assert(n <= MAX_BONES);

            // Accumulate world transforms in a temporary array.
            // We reuse bonePalette storage for the world matrix first,
            // then overwrite with the final skinning matrix in-place.
            // A separate small stack is cleaner and avoids aliasing issues.
            std::vector<glm::mat4> worldTrans(n, glm::mat4(1.0f));

            for (int i = 0; i < n; ++i)
            {
                const Bone& bone = skeleton->bones[i];

                // Sample clip A
                glm::mat4 localA = glm::mat4(1.0f);
                if (clipA)
                {
                    const BoneTrack* track = clipA->trackForBone(i);
                    if (track)
                        localA = _keyframeToMatrix(track->sample(timeA));
                }

                // Sample clip B (for blending)
                glm::mat4 local = localA;
                if (clipB && alpha > 0.0f)
                {
                    glm::mat4 localB = glm::mat4(1.0f);
                    const BoneTrack* track = clipB->trackForBone(i);
                    if (track)
                        localB = _keyframeToMatrix(track->sample(timeB));

                    // Decompose → blend TRS → recompose.
                    // Blending raw matrices would cause scale artifacts.
                    glm::vec3 posA = glm::vec3(localA[3]);
                    glm::vec3 posB = glm::vec3(localB[3]);
                    glm::vec3 scaA = glm::vec3(glm::length(localA[0]),
                                               glm::length(localA[1]),
                                               glm::length(localA[2]));
                    glm::vec3 scaB = glm::vec3(glm::length(localB[0]),
                                               glm::length(localB[1]),
                                               glm::length(localB[2]));
                    glm::quat rotA = glm::normalize(glm::quat_cast(
                        glm::mat3(glm::vec3(localA[0]) / scaA.x,
                                  glm::vec3(localA[1]) / scaA.y,
                                  glm::vec3(localA[2]) / scaA.z)));
                    glm::quat rotB = glm::normalize(glm::quat_cast(
                        glm::mat3(glm::vec3(localB[0]) / scaB.x,
                                  glm::vec3(localB[1]) / scaB.y,
                                  glm::vec3(localB[2]) / scaB.z)));

                    glm::vec3 pos = glm::mix(posA, posB, alpha);
                    glm::vec3 sca = glm::mix(scaA, scaB, alpha);
                    glm::quat rot = glm::normalize(glm::slerp(rotA, rotB, alpha));

                    local = glm::translate(glm::mat4(1.0f), pos)
                          * glm::mat4_cast(rot)
                          * glm::scale(glm::mat4(1.0f), sca);
                }

                // Concatenate with parent (bones are root-first ordered).
                if (bone.parentIndex >= 0)
                    worldTrans[i] = worldTrans[bone.parentIndex] * local;
                else
                    worldTrans[i] = local;

                // Final skinning matrix.
                bonePalette[i] = worldTrans[i] * bone.inverseBindPose;
            }

            // Pad remaining entries with identity (shader reads MAX_BONES always).
            for (int i = n; i < MAX_BONES; ++i)
                bonePalette[i] = glm::mat4(1.0f);
        }
    };

} // namespace HonHengine

#endif // ANIMATION_H
