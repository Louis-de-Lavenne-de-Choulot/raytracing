#pragma once
#ifndef GLTFIMPORTER_H
#define GLTFIMPORTER_H

#include "baseobject.h"
#include "animation.h"
#include "skinnedVertex.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <type_traits>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace PEngine
{
    class GltfImporter
    {
    public:
        GltfImporter();
        ~GltfImporter();
        GltfImporter(GltfImporter&&) noexcept;
        GltfImporter& operator=(GltfImporter&&) noexcept;
        GltfImporter(const GltfImporter&) = delete;
        GltfImporter& operator=(const GltfImporter&) = delete;

        struct Result
        {
            bool        ok = false;
            std::string error;
            BaseObject* object = nullptr;
            std::vector<std::shared_ptr<AnimationClip>> clips;
            std::string albedoTexKey;
        };

        template<typename TM>
        Result load(const std::string& filePath, TM* texManager = nullptr)
        {
            Result res;
            _buffers.clear();
            _glbBin.clear();

            size_t sep = filePath.find_last_of("/\\");
            _dir = (sep != std::string::npos) ? filePath.substr(0, sep + 1) : "";

            std::string jsonStr;
            bool isGLB = filePath.size() >= 4 && filePath.substr(filePath.size() - 4) == ".glb";

            if (isGLB) { if (!_loadGLB(filePath, jsonStr)) { res.error = "Failed to open GLB"; return res; } }
            else { if (!_loadGLTF(filePath, jsonStr)) { res.error = "Failed to open GLTF"; return res; } }

            if (!_parseJson(jsonStr, res.error)) return res;

            const nlohmann::json& doc = _getDoc();

            if (doc.contains("buffers")) {
                for (const auto& buf : doc["buffers"]) {
                    std::vector<uint8_t> data;
                    if (buf.contains("uri")) _decodeBuffer(buf["uri"].get<std::string>(), data);
                    else if (isGLB) data = _glbBin;
                    _buffers.push_back(std::move(data));
                }
            }

            int nodeCount = doc.contains("nodes") ? static_cast<int>(doc["nodes"].size()) : 0;
            int meshIndex = -1, skinIndex = -1, meshNodeIdx = -1;

            for (int i = 0; i < nodeCount; ++i) {
                const auto& node = doc["nodes"][i];
                if (node.contains("mesh") && meshIndex < 0) {
                    meshIndex = node["mesh"].get<int>();
                    meshNodeIdx = i;
                    if (node.contains("skin")) skinIndex = node["skin"].get<int>();
                }
            }

            if (meshIndex < 0) { res.error = "No mesh found"; return res; }

            std::vector<int> jointNodes;
            std::vector<glm::mat4> invBinds;
            std::shared_ptr<Skeleton> skel;
            std::vector<int> nodeParent = _parentTable(nodeCount);
            std::vector<glm::mat4> nodeGlobalXforms = _globalXforms(nodeCount);

            if (skinIndex >= 0 && doc.contains("skins")) {
                const auto& skin = doc["skins"][skinIndex];
                jointNodes = skin["joints"].get<std::vector<int>>();
                if (skin.contains("inverseBindMatrices")) {
                    AccessorView av = _accessorView(skin["inverseBindMatrices"].get<int>());
                    invBinds.resize(av.count);
                    for (size_t ji = 0; ji < av.count; ++ji) std::memcpy(glm::value_ptr(invBinds[ji]), av.data + ji * av.stride, 64);
                }
                skel = _buildSkeleton(jointNodes, invBinds, nodeParent);
            }
            else {
                skel = std::make_shared<Skeleton>();
                Bone dummy; dummy.name = "root"; dummy.parentIndex = -1;
                skel->bones.push_back(dummy);
            }

            std::vector<SkinnedVertex> verts;
            std::vector<unsigned int> indices;
            _buildMesh(meshIndex, skinIndex, skel, jointNodes, nodeGlobalXforms, meshNodeIdx, verts, indices);

            res.clips = _buildClips(jointNodes);

            // Albedo texture loading
            std::string albedoKey;
            if (doc.contains("materials") && !doc["materials"].empty()) {
                const auto& mat = doc["materials"][0];
                if (mat.contains("pbrMetallicRoughness")) {
                    const auto& pbr = mat["pbrMetallicRoughness"];
                    if (pbr.contains("baseColorTexture")) {
                        int imgIdx = doc["textures"][pbr["baseColorTexture"]["index"].get<int>()]["source"].get<int>();
                        const auto& img = doc["images"][imgIdx];
                        if constexpr (!std::is_same_v<TM, void>) {
                            if (texManager) {
                                albedoKey = "gltf_albedo_" + std::to_string(imgIdx);
                                stbi_set_flip_vertically_on_load(true);
                                if (img.contains("uri")) {
                                    std::string uri = img["uri"].get<std::string>();
                                    if (uri.rfind("data:", 0) == 0) {
                                        auto raw = _base64Decode(uri.substr(uri.find(',') + 1));
                                        int w, h, ch;
                                        unsigned char* px = stbi_load_from_memory(raw.data(), (int)raw.size(), &w, &h, &ch, 4);
                                        if (px) { texManager->load(albedoKey, px, w, h); stbi_image_free(px); }
                                    }
                                    else { texManager->load(albedoKey, _dir + uri); }
                                }
                                else if (img.contains("bufferView")) {
                                    const auto& bv = doc["bufferViews"][img["bufferView"].get<int>()];
                                    const uint8_t* imgData = _buffers[bv["buffer"].get<int>()].data() + bv.value("byteOffset", 0);
                                    int w, h, ch;
                                    unsigned char* px = stbi_load_from_memory(imgData, (int)bv["byteLength"], &w, &h, &ch, 4);
                                    if (px) { texManager->load(albedoKey, px, w, h); stbi_image_free(px); }
                                }
                            }
                        }
                    }
                }
            }
            res.albedoTexKey = albedoKey;

            BaseObject* obj = new BaseObject();
            obj->render.shaderName = "skinned";
            obj->render.vertexStride = sizeof(SkinnedVertex);
            obj->render.layout = SkinnedVertex::layout();
            if (!albedoKey.empty()) obj->render.textures = { { "uAlbedo", albedoKey, 0 } };
            obj->render.setMesh(verts, indices);
            obj->animator.skeleton = skel;
            obj->animator.bonePalette.assign(MAX_BONES, glm::mat4(1.0f));
            if (!res.clips.empty()) obj->animator.play(res.clips[0]);

            res.object = obj;
            res.ok = true;
            return res;
        }

        Result load(const std::string& filePath);

    private:
        std::string _dir;
        std::vector<uint8_t> _glbBin;
        std::vector<std::vector<uint8_t>> _buffers;

        struct AccessorView { const uint8_t* data = nullptr; size_t count = 0; int compType = 0; int numComp = 0; size_t stride = 0; };
        struct Impl;
        std::unique_ptr<Impl> _impl;

        const nlohmann::json& _getDoc() const;
        bool _parseJson(const std::string& jsonStr, std::string& errorOut);
        AccessorView _accessorView(int accessorIdx) const;
        static float _readFloat(const uint8_t* ptr, int compType);
        bool _loadGLB(const std::string& path, std::string& jsonOut);
        bool _loadGLTF(const std::string& path, std::string& jsonOut);
        bool _decodeBuffer(const std::string& uri, std::vector<uint8_t>& out);
        static std::vector<uint8_t> _base64Decode(const std::string& b64);
        std::shared_ptr<Skeleton> _buildSkeleton(const std::vector<int>& jointNodes, const std::vector<glm::mat4>& invBinds, const std::vector<int>& nodeParent);
        bool _buildMesh(int meshIdx, int skinIdx, const std::shared_ptr<Skeleton>& skel, const std::vector<int>& jointNodes, const std::vector<glm::mat4>& nodeXforms, int nodeIdx, std::vector<SkinnedVertex>& outV, std::vector<unsigned int>& outI);
        std::vector<std::shared_ptr<AnimationClip>> _buildClips(const std::vector<int>& jointNodes);
        glm::mat4 _nodeLocalXform(int nodeIdx) const;
        std::vector<glm::mat4> _globalXforms(int nodeCount) const;
        std::vector<int> _parentTable(int nodeCount) const;
    };
}
#endif