// GltfImporter.cpp

#include "GltfImporter.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <stb_image.h>

#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <stdexcept>
#include <set>

namespace PEngine
{
    struct GltfImporter::Impl
    {
        json doc;
    };

    GltfImporter::GltfImporter() = default;
    GltfImporter::~GltfImporter() = default;
    GltfImporter::GltfImporter(GltfImporter&&) noexcept = default;
    GltfImporter& GltfImporter::operator=(GltfImporter&&) noexcept = default;

    bool GltfImporter::_parseJson(const std::string& jsonStr, std::string& errorOut)
    {
        _impl = std::make_unique<Impl>();
        try
        {
            _impl->doc = json::parse(jsonStr);
            return true;
        }
        catch (const std::exception& e)
        {
            errorOut = std::string("JSON parse error: ") + e.what();
            return false;
        }
    }

    const json& GltfImporter::_getDoc() const
    {
        return _impl->doc;
    }

    static constexpr int GL_BYTE_ = 5120;
    static constexpr int GL_UNSIGNED_BYTE_ = 5121;
    static constexpr int GL_SHORT_ = 5122;
    static constexpr int GL_UNSIGNED_SHORT_ = 5123;
    static constexpr int GL_UNSIGNED_INT_ = 5125;
    static constexpr int GL_FLOAT_ = 5126;

    static int compTypeSize(int ct)
    {
        switch (ct)
        {
        case GL_BYTE_:
        case GL_UNSIGNED_BYTE_:  return 1;
        case GL_SHORT_:
        case GL_UNSIGNED_SHORT_: return 2;
        case GL_UNSIGNED_INT_:
        case GL_FLOAT_:          return 4;
        default:                 return 1;
        }
    }

    static int typeNumComp(const std::string& t)
    {
        if (t == "SCALAR") return 1;
        if (t == "VEC2")   return 2;
        if (t == "VEC3")   return 3;
        if (t == "VEC4")   return 4;
        if (t == "MAT2")   return 4;
        if (t == "MAT3")   return 9;
        if (t == "MAT4")   return 16;
        return 1;
    }

    float GltfImporter::_readFloat(const uint8_t* ptr, int ct)
    {
        switch (ct)
        {
        case GL_FLOAT_: { float    v; std::memcpy(&v, ptr, 4); return v; }
        case GL_UNSIGNED_SHORT_: { uint16_t v; std::memcpy(&v, ptr, 2); return static_cast<float>(v) / 65535.0f; }
        case GL_SHORT_: { int16_t  v; std::memcpy(&v, ptr, 2); return (std::max)(static_cast<float>(v) / 32767.0f, -1.0f); }
        case GL_UNSIGNED_BYTE_:  return static_cast<float>(*ptr) / 255.0f;
        case GL_BYTE_:           return (std::max)(static_cast<float>(static_cast<int8_t>(*ptr)) / 127.0f, -1.0f);
        case GL_UNSIGNED_INT_: { uint32_t v; std::memcpy(&v, ptr, 4); return static_cast<float>(v); }
        default:                 return 0.0f;
        }
    }

    std::vector<uint8_t> GltfImporter::_base64Decode(const std::string& b64)
    {
        static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<uint8_t> out;
        out.reserve(b64.size() * 3 / 4);
        int val = 0, valb = -8;
        for (unsigned char c : b64)
        {
            if (c == '=') break;
            auto pos = chars.find(c);
            if (pos == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(pos);
            valb += 6;
            if (valb >= 0) { out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF)); valb -= 8; }
        }
        return out;
    }

    bool GltfImporter::_decodeBuffer(const std::string& uri, std::vector<uint8_t>& out)
    {
        if (uri.rfind("data:", 0) == 0)
        {
            size_t comma = uri.find(',');
            if (comma == std::string::npos) return false;
            out = _base64Decode(uri.substr(comma + 1));
            return true;
        }
        std::string path = _dir + uri;
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        out.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    }

    GltfImporter::AccessorView GltfImporter::_accessorView(int accessorIdx) const
    {
        const json& doc = _impl->doc;
        const json& acc = doc["accessors"][accessorIdx];
        AccessorView av;
        av.count = acc["count"].get<size_t>();
        av.compType = acc["componentType"].get<int>();
        av.numComp = typeNumComp(acc["type"].get<std::string>());
        int bvIdx = acc.value("bufferView", -1);
        size_t byteOffset = acc.value("byteOffset", 0);
        if (bvIdx < 0) return av;
        const json& bv = doc["bufferViews"][bvIdx];
        int bufIdx = bv["buffer"].get<int>();
        size_t bvOffset = bv.value("byteOffset", 0);
        size_t bvStride = bv.value("byteStride", 0);
        size_t elemBytes = static_cast<size_t>(av.numComp) * compTypeSize(av.compType);
        av.stride = (bvStride > 0) ? bvStride : elemBytes;
        av.data = _buffers[bufIdx].data() + bvOffset + byteOffset;
        return av;
    }

    bool GltfImporter::_loadGLB(const std::string& path, std::string& jsonOut)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f.is_open()) return false;
        uint32_t magic, version, length;
        f.read(reinterpret_cast<char*>(&magic), 4);
        f.read(reinterpret_cast<char*>(&version), 4);
        f.read(reinterpret_cast<char*>(&length), 4);
        if (magic != 0x46546C67u) return false;
        uint32_t chunk0Len, chunk0Type;
        f.read(reinterpret_cast<char*>(&chunk0Len), 4);
        f.read(reinterpret_cast<char*>(&chunk0Type), 4);
        jsonOut.resize(chunk0Len);
        f.read(jsonOut.data(), chunk0Len);
        if (!f.eof())
        {
            uint32_t chunk1Len, chunk1Type;
            if (f.read(reinterpret_cast<char*>(&chunk1Len), 4) && f.read(reinterpret_cast<char*>(&chunk1Type), 4))
            {
                _glbBin.resize(chunk1Len);
                f.read(reinterpret_cast<char*>(_glbBin.data()), chunk1Len);
            }
        }
        return true;
    }

    bool GltfImporter::_loadGLTF(const std::string& path, std::string& jsonOut)
    {
        std::ifstream f(path);
        if (!f.is_open()) return false;
        jsonOut.assign(std::istreambuf_iterator<char>(f), {});
        return true;
    }

    glm::mat4 GltfImporter::_nodeLocalXform(int nodeIdx) const
    {
        const json& doc = _impl->doc;
        const json& node = doc["nodes"][nodeIdx];
        if (node.contains("matrix"))
        {
            auto m = node["matrix"].get<std::vector<float>>();
            return glm::make_mat4(m.data());
        }
        glm::vec3 t(0.0f);
        glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 s(1.0f);
        if (node.contains("translation")) { auto v = node["translation"].get<std::vector<float>>(); t = { v[0], v[1], v[2] }; }
        if (node.contains("rotation")) { auto v = node["rotation"].get<std::vector<float>>(); r = glm::quat(v[3], v[0], v[1], v[2]); }
        if (node.contains("scale")) { auto v = node["scale"].get<std::vector<float>>(); s = { v[0], v[1], v[2] }; }
        return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
    }

    std::vector<int> GltfImporter::_parentTable(int nodeCount) const
    {
        const json& doc = _impl->doc;
        std::vector<int> parent(nodeCount, -1);
        if (!doc.contains("nodes")) return parent;
        for (int i = 0; i < nodeCount; ++i)
        {
            const json& node = doc["nodes"][i];
            if (node.contains("children"))
                for (int child : node["children"].get<std::vector<int>>())
                    if (child < nodeCount) parent[child] = i;
        }
        return parent;
    }

    std::vector<glm::mat4> GltfImporter::_globalXforms(int nodeCount) const
    {
        std::vector<int>       parent = _parentTable(nodeCount);
        std::vector<glm::mat4> global(nodeCount, glm::mat4(1.0f));
        for (int pass = 0; pass < nodeCount; ++pass)
        {
            bool changed = false;
            for (int i = 0; i < nodeCount; ++i)
            {
                glm::mat4 local = _nodeLocalXform(i);
                glm::mat4 newG = (parent[i] >= 0) ? global[parent[i]] * local : local;
                if (newG != global[i]) { global[i] = newG; changed = true; }
            }
            if (!changed) break;
        }
        return global;
    }

    std::shared_ptr<Skeleton> GltfImporter::_buildSkeleton(
        const std::vector<int>& jointNodes,
        const std::vector<glm::mat4>& invBinds,
        const std::vector<int>& nodeParent)
    {
        const json& doc = _impl->doc;
        auto skel = std::make_shared<Skeleton>();
        int numBones = static_cast<int>(jointNodes.size());
        if (numBones > MAX_BONES) numBones = MAX_BONES;
        skel->bones.resize(numBones);
        std::unordered_map<int, int> nodeToJoint;
        for (int j = 0; j < numBones; ++j) nodeToJoint[jointNodes[j]] = j;
        for (int j = 0; j < numBones; ++j)
        {
            Bone& bone = skel->bones[j];
            int   nodeIdx = jointNodes[j];
            bone.name = doc["nodes"][nodeIdx].value("name", "bone" + std::to_string(j));
            auto it = nodeToJoint.find(nodeParent[nodeIdx]);
            bone.parentIndex = (it != nodeToJoint.end()) ? it->second : -1;
            bone.inverseBindPose = (j < static_cast<int>(invBinds.size())) ? invBinds[j] : glm::mat4(1.0f);
        }
        return skel;
    }

    bool PEngine::GltfImporter::_buildMesh(
        int                              meshIndex,
        int                              skinIndex,
        const std::shared_ptr<Skeleton>& skel,
        const std::vector<int>& jointNodes,
        const std::vector<glm::mat4>& nodeGlobalXforms,
        int                              meshNodeIndex,
        std::vector<SkinnedVertex>& outVerts,
        std::vector<unsigned int>& outIdx)
    {
        const json& doc = _getDoc();
        const json& mesh = doc["meshes"][meshIndex];
        bool flipWinding = true;
        if (meshNodeIndex >= 0 && meshNodeIndex < (int)nodeGlobalXforms.size()) {
            if (glm::determinant(nodeGlobalXforms[meshNodeIndex]) < 0.0f) flipWinding = !flipWinding;
        }

        for (const json& prim : mesh["primitives"])
        {
            unsigned int indexOffset = static_cast<unsigned int>(outVerts.size());
            const json& attrs = prim["attributes"];
            int accPos = attrs.value("POSITION", -1);
            int accNorm = attrs.value("NORMAL", -1);
            int accUV = attrs.value("TEXCOORD_0", -1);
            int accJoints = attrs.value("JOINTS_0", -1);
            int accWgts = attrs.value("WEIGHTS_0", -1);

            if (accPos < 0) continue;
            AccessorView avPos = _accessorView(accPos);
            AccessorView avNorm = (accNorm >= 0) ? _accessorView(accNorm) : AccessorView{};
            AccessorView avUV = (accUV >= 0) ? _accessorView(accUV) : AccessorView{};
            AccessorView avJoints = (accJoints >= 0) ? _accessorView(accJoints) : AccessorView{};
            AccessorView avWgts = (accWgts >= 0) ? _accessorView(accWgts) : AccessorView{};

            bool hasSkin = (accJoints >= 0 && accWgts >= 0);
            size_t vertCount = avPos.count;

            for (size_t vi = 0; vi < vertCount; ++vi)
            {
                SkinnedVertex sv{};
                std::memcpy(&sv.px, avPos.data + vi * avPos.stride, 12);
                if (avNorm.data) std::memcpy(&sv.nx, avNorm.data + vi * avNorm.stride, 12);
                else sv.ny = 1.0f;

                if (avUV.data) {
                    std::memcpy(&sv.u, avUV.data + vi * avUV.stride, 8);
                    sv.v = 1.0f - sv.v; // Correct for OpenGL bottom-left origin
                }

                if (hasSkin) {
                    const uint8_t* pj = avJoints.data + vi * avJoints.stride;
                    const uint8_t* pw = avWgts.data + vi * avWgts.stride;
                    for (int k = 0; k < 4; ++k) {
                        uint32_t jIdx = 0;
                        if (avJoints.compType == 5121) jIdx = *(pj + k);
                        else if (avJoints.compType == 5123) { uint16_t v; std::memcpy(&v, pj + k * 2, 2); jIdx = v; }
                        sv.boneIdx[k] = (uint8_t)std::min(jIdx, (uint32_t)MAX_BONES - 1);
                        sv.boneWgt[k] = _readFloat(pw + k * compTypeSize(avWgts.compType), avWgts.compType);
                    }
                    sv.normaliseWeights();
                }
                else { sv.boneIdx[0] = 0; sv.boneWgt[0] = 1.0f; }
                outVerts.push_back(sv);
            }

            if (prim.contains("indices"))
            {
                AccessorView avIdx = _accessorView(prim["indices"].get<int>());
                for (size_t ii = 0; ii < avIdx.count; ii += 3)
                {
                    uint32_t tri[3];
                    for (int k = 0; k < 3; ++k) {
                        const uint8_t* p = avIdx.data + (ii + k) * avIdx.stride;
                        if (avIdx.compType == 5125) std::memcpy(&tri[k], p, 4);
                        else if (avIdx.compType == 5123) { uint16_t v; std::memcpy(&v, p, 2); tri[k] = v; }
                        else tri[k] = *p;
                    }
                    outIdx.push_back(tri[0] + indexOffset);
                    if (flipWinding) { outIdx.push_back(tri[2] + indexOffset); outIdx.push_back(tri[1] + indexOffset); }
                    else { outIdx.push_back(tri[1] + indexOffset); outIdx.push_back(tri[2] + indexOffset); }
                }
            }
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  _buildClips
    //
    //  Converts glTF animation data into engine BoneTrack / AnimationClip
    //  structures.
    //
    //  ─────────────────────────────────────────────
    //  A glTF animation may target nodes that are NOT listed in the skin's
    //  joints[] array.  The Khronos Fox model does this: it animates an
    //  "Armature" root node that parents all joint nodes but is itself not a
    //  deformer joint.  The previous code assigned such nodes a synthetic
    //  boneIndex >= jointNodes.size(), then immediately did:
    //
    //      int nodeIdx = jointNodes[boneIdx];   // ← OOB read / UB
    //
    //  to look up the node's rest-pose TRS for use as a fallback when a
    //  channel doesn't provide a value at a merged timestamp.  This corrupted
    //  memory (or crashed under ASAN/sanitizers) and produced garbage
    //  rest-pose data for every bone that shared the same clip.
    // ─────────────────────────────────────────────────────────────────────────
    std::vector<std::shared_ptr<AnimationClip>> GltfImporter::_buildClips(const std::vector<int>& jointNodes)
    {
        const json& doc = _impl->doc;
        std::vector<std::shared_ptr<AnimationClip>> clips;
        if (!doc.contains("animations")) return clips;

        const int nodeCount = doc.contains("nodes") ? static_cast<int>(doc["nodes"].size()) : 0;

        std::unordered_map<int, int> baseNodeToJoint;
        for (int j = 0; j < (int)jointNodes.size(); ++j)
            baseNodeToJoint[jointNodes[j]] = j;

        for (const json& anim : doc["animations"])
        {
            auto clip = std::make_shared<AnimationClip>();
            clip->name = anim.value("name", "clip" + std::to_string(clips.size()));
            clip->loops = true;
            clip->duration = 0.0f;

            std::unordered_map<int, int> nodeToJoint = baseNodeToJoint;

            std::unordered_map<int, int> boneToNode;
            for (int j = 0; j < (int)jointNodes.size(); ++j)
                boneToNode[j] = jointNodes[j];

            {
                int nextSyntheticIdx = static_cast<int>(jointNodes.size());
                for (const json& chan : anim["channels"])
                {
                    const json& target = chan["target"];
                    if (!target.contains("node")) continue;
                    int nodeIdx = target["node"].get<int>();
                    if (nodeToJoint.find(nodeIdx) == nodeToJoint.end())
                    {
                        nodeToJoint[nodeIdx] = nextSyntheticIdx;
                        boneToNode[nextSyntheticIdx] = nodeIdx; 
                        ++nextSyntheticIdx;
                    }
                }
            }

            // Intermediate storage for channels
            struct RawChannel { std::vector<float> times; std::vector<glm::vec3> v3; std::vector<glm::quat> q4; };
            struct BoneData { RawChannel t, r, s; };
            std::unordered_map<int, BoneData> rawAnimData;

            for (const json& chan : anim["channels"])
            {
                const json& target = chan["target"];
                if (!target.contains("node")) continue;
                int nodeIdx = target["node"].get<int>();
                auto it = nodeToJoint.find(nodeIdx);
                if (it == nodeToJoint.end()) continue;

                int boneIdx = it->second;
                std::string path = target["path"].get<std::string>();
                const json& sampler = anim["samplers"][chan["sampler"].get<int>()];

                AccessorView avTime = _accessorView(sampler["input"].get<int>());
                AccessorView avVal = _accessorView(sampler["output"].get<int>());

                auto& raw = rawAnimData[boneIdx];
                for (size_t ki = 0; ki < avTime.count; ++ki) {
                    float t; std::memcpy(&t, avTime.data + ki * avTime.stride, 4);
                    clip->duration = (std::max)(clip->duration, t);
                    const uint8_t* vp = avVal.data + ki * avVal.stride;

                    if (path == "translation") {
                        raw.t.times.push_back(t);
                        glm::vec3 v; std::memcpy(&v, vp, 12); raw.t.v3.push_back(v);
                    }
                    else if (path == "rotation") {
                        raw.r.times.push_back(t);
                        // glTF quaternion storage order is xyzw; glm::quat ctor is (w,x,y,z).
                        glm::quat q; std::memcpy(&q, vp, 16);
                        raw.r.q4.push_back(glm::normalize(glm::quat(q.w, q.x, q.y, q.z)));
                    }
                    else if (path == "scale") {
                        raw.s.times.push_back(t);
                        glm::vec3 v; std::memcpy(&v, vp, 12); raw.s.v3.push_back(v);
                    }
                }
            }

            // Merge channels into BoneTracks
            for (auto& [boneIdx, data] : rawAnimData) {

                BoneTrack track;
                track.boneIndex = boneIdx;

                // Collect all unique timestamps for this bone
                std::set<float> timeSet;
                for (float t : data.t.times) timeSet.insert(t);
                for (float t : data.r.times) timeSet.insert(t);
                for (float t : data.s.times) timeSet.insert(t);

                glm::vec3 restT(0.0f);
                glm::quat restR(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 restS(1.0f);

                auto bnIt = boneToNode.find(boneIdx);
                if (bnIt != boneToNode.end())
                {
                    int nodeIdx = bnIt->second;
                    if (nodeIdx >= 0 && nodeIdx < nodeCount)
                    {
                        const json& node = doc["nodes"][nodeIdx];
                        if (node.contains("translation")) {
                            auto v = node["translation"].get<std::vector<float>>();
                            restT = { v[0], v[1], v[2] };
                        }
                        if (node.contains("rotation")) {
                            // glTF stores rotation as [x, y, z, w]
                            auto v = node["rotation"].get<std::vector<float>>();
                            restR = glm::normalize(glm::quat(v[3], v[0], v[1], v[2]));
                        }
                        if (node.contains("scale")) {
                            auto v = node["scale"].get<std::vector<float>>();
                            restS = { v[0], v[1], v[2] };
                        }
                    }
                }

                // Build one merged keyframe per unique timestamp.
                // Use a small epsilon for timestamp matching: different accessors
                // can produce values that differ by a ULP even when they represent
                // the same moment, so exact equality would silently fall back to
                // the rest-pose value.
                static constexpr float kTimeEps = 1e-5f;

                for (float t : timeSet) {
                    Keyframe k;
                    k.time = t;
                    k.position = restT;
                    k.rotation = restR;
                    k.scale = restS;

                    auto itT = std::lower_bound(data.t.times.begin(), data.t.times.end(), t);
                    if (itT != data.t.times.end() && std::abs(*itT - t) < kTimeEps)
                        k.position = data.t.v3[std::distance(data.t.times.begin(), itT)];

                    auto itR = std::lower_bound(data.r.times.begin(), data.r.times.end(), t);
                    if (itR != data.r.times.end() && std::abs(*itR - t) < kTimeEps)
                        k.rotation = data.r.q4[std::distance(data.r.times.begin(), itR)];

                    auto itS = std::lower_bound(data.s.times.begin(), data.s.times.end(), t);
                    if (itS != data.s.times.end() && std::abs(*itS - t) < kTimeEps)
                        k.scale = data.s.v3[std::distance(data.s.times.begin(), itS)];

                    track.keys.push_back(k);
                }
                clip->tracks.push_back(std::move(track));
            }
            clips.push_back(clip);
        }
        return clips;
    }

    GltfImporter::Result GltfImporter::load(const std::string& filePath)
    {
        return load<void>(filePath, static_cast<void*>(nullptr));
    }
}