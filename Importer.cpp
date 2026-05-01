#include "importer.h"
#include "defaults.h"
#include "baseobject.h"
#include "material.h"
#include "vector3.h"
#include "triangle.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <iostream>

namespace HonHengine {

    // ─────────────────────────────────────────────────────────────────────────
    //  Internal vertex type used during OBJ import.
    //  Always laid out as [pos(3), normal(3), uv(2)] — 8 floats — so the
    //  GPU layout never changes regardless of what the file contains.
    // ─────────────────────────────────────────────────────────────────────────
    struct ObjVert
    {
        float px, py, pz;   // position
        float nx, ny, nz;   // normal  (computed if the OBJ has none)
        float u, v;        // texcoord (0,0 if the OBJ has none)
    };

    // ─────────────────────────────────────────────────────────────────────────
    //  Flat-normal generation — mirrors Unity's RecalculateNormals().
    //  Accumulates the cross-product of each triangle on its three vertices,
    //  then normalises.  Works even with shared vertices (gives smooth shading
    //  across flat OBJs).
    // ─────────────────────────────────────────────────────────────────────────
    static void RecalculateNormals(std::vector<ObjVert>& verts,
        const std::vector<unsigned int>& indices)
    {
        // Zero out any normals that may already be there
        for (auto& v : verts)
            v.nx = v.ny = v.nz = 0.0f;

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            ObjVert& v0 = verts[indices[i]];
            ObjVert& v1 = verts[indices[i + 2]];
            ObjVert& v2 = verts[indices[i + 1]];

            glm::vec3 p0(v0.px, v0.py, v0.pz);
            glm::vec3 p1(v1.px, v1.py, v1.pz);
            glm::vec3 p2(v2.px, v2.py, v2.pz);

            glm::vec3 n = glm::cross(p1 - p0, p2 - p0);   // un-normalised — area-weighted

            v0.nx += n.x;  v0.ny += n.y;  v0.nz += n.z;
            v1.nx += n.x;  v1.ny += n.y;  v1.nz += n.z;
            v2.nx += n.x;  v2.ny += n.y;  v2.nz += n.z;
        }

        for (auto& v : verts)
        {
            glm::vec3 n(v.nx, v.ny, v.nz);
            float len = glm::length(n);
            if (len > 1e-6f) n /= len;
            v.nx = n.x;  v.ny = n.y;  v.nz = n.z;
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  ImportFromOBJ
    //
    //  Parses positions, texture-coords, normals and faces (tris/quads/ngons).
    //  Always produces an 8-float-per-vertex layout [pos, normal, uv] so that
    //  every OBJ can be rendered with the same "beach" (or any standard)
    //  shader without manually tweaking the layout per object.
    //
    //  If the OBJ has no `vn` lines, normals are computed from triangle
    //  cross-products (equivalent to Unity's RecalculateNormals).
    //  If the OBJ has no `vt` lines, UVs default to (0, 0).
    // ─────────────────────────────────────────────────────────────────────────
    BaseObject* Importer::ImportFromOBJ(const std::string& filePath)
    {
        std::cout << "Importing OBJ file: " << filePath << std::endl;

        // Raw attribute lists from the file
        std::vector<glm::vec3> rawPos;
        std::vector<glm::vec2> rawUV;
        std::vector<glm::vec3> rawNorm;

        // Built vertex / index buffers
        std::vector<ObjVert>       verts;
        std::vector<unsigned int>  indices;

        bool hasNormals = false;
        bool hasUVs = false;

        std::ifstream file(filePath);
        if (!file.is_open())
            throw std::runtime_error("Failed to open OBJ file: " + filePath);

        // ── Parse ──────────────────────────────────────────────────────────
        std::string line;
        while (std::getline(file, line))
        {
            if (line.size() < 2 || line[0] == '#') continue;

            // --------------------------------------------------  v  (position)
            if (line.substr(0, 2) == "v ")
            {
                std::istringstream ss(line.substr(2));
                glm::vec3 p;
                ss >> p.x >> p.y >> p.z;
                rawPos.push_back(p);
            }
            // -------------------------------------------------- vt  (texcoord)
            else if (line.substr(0, 3) == "vt ")
            {
                std::istringstream ss(line.substr(3));
                glm::vec2 uv;
                ss >> uv.x >> uv.y;
                // intentionally ignore optional W component
                rawUV.push_back(uv);
                hasUVs = true;
            }
            // -------------------------------------------------- vn  (normal)
            else if (line.substr(0, 3) == "vn ")
            {
                std::istringstream ss(line.substr(3));
                glm::vec3 n;
                ss >> n.x >> n.y >> n.z;
                rawNorm.push_back(n);
                hasNormals = true;
            }
            // --------------------------------------------------  f  (face)
            else if (line.substr(0, 2) == "f ")
            {
                std::istringstream faceStream(line.substr(2));
                std::string token;

                // Each token is  v  or  v/vt  or  v//vn  or  v/vt/vn
                // We build one ObjVert per unique combination and fan-triangulate.
                std::vector<unsigned int> faceVerts;

                while (faceStream >> token)
                {
                    std::istringstream ts(token);
                    std::string vStr, vtStr, vnStr;

                    std::getline(ts, vStr, '/');
                    std::getline(ts, vtStr, '/');
                    std::getline(ts, vnStr);

                    if (vStr.empty()) continue;

                    int vIdx = std::stoi(vStr) - 1;
                    if (vIdx < 0) vIdx += static_cast<int>(rawPos.size()) + 1;

                    int vtIdx = -1;
                    if (!vtStr.empty()) vtIdx = std::stoi(vtStr) - 1;

                    int vnIdx = -1;
                    if (!vnStr.empty()) vnIdx = std::stoi(vnStr) - 1;

                    ObjVert vert{};
                    vert.px = rawPos[vIdx].x;
                    vert.py = rawPos[vIdx].y;
                    vert.pz = rawPos[vIdx].z;

                    if (vtIdx >= 0 && vtIdx < static_cast<int>(rawUV.size()))
                    {
                        vert.u = rawUV[vtIdx].x;
                        vert.v = rawUV[vtIdx].y;
                    }

                    if (vnIdx >= 0 && vnIdx < static_cast<int>(rawNorm.size()))
                    {
                        vert.nx = rawNorm[vnIdx].x;
                        vert.ny = rawNorm[vnIdx].y;
                        vert.nz = rawNorm[vnIdx].z;
                    }

                    // Naïve de-duplication: for most OBJ files this is fine.
                    // For very large meshes consider an unordered_map keyed on (vi,vti,vni).
                    unsigned int idx = static_cast<unsigned int>(verts.size());
                    verts.push_back(vert);
                    faceVerts.push_back(idx);
                }

                // Fan-triangulate (handles tris, quads and ngons)
                for (size_t i = 1; i + 1 < faceVerts.size(); ++i)
                {
                    indices.push_back(faceVerts[0]);
                    indices.push_back(faceVerts[i + 1]);
                    indices.push_back(faceVerts[i]);
                }
            }
        }
        file.close();

        // ── Generate normals if the OBJ had none ──────────────────────────
        if (!hasNormals)
        {
            std::cout << "[OBJ] No normals found — computing from geometry (like Unity's RecalculateNormals).\n";
            RecalculateNormals(verts, indices);
        }

        if (!hasUVs)
            std::cout << "[OBJ] No UV coordinates found — defaulting to (0, 0).\n";

        // ── Pack into raw bytes for the render component ───────────────────
        // Layout is always:
        //   attrib 0 → position  (3 floats, offset 0)
        //   attrib 1 → normal    (3 floats, offset 12)
        //   attrib 2 → uv        (2 floats, offset 24)
        //   stride = 32 bytes
        std::vector<uint8_t> vertBytes(verts.size() * sizeof(ObjVert));
        std::memcpy(vertBytes.data(), verts.data(), vertBytes.size());

        // ── Create object ─────────────────────────────────────────────────
        BaseObject* obj = new BaseObject();
        obj->render.shaderName = "beach";           // sensible default; caller can override
        obj->render.vertexStride = sizeof(ObjVert);   // 32 bytes

        obj->render.layout = {
            { 0, 3, VertexDataType::Float, false, offsetof(ObjVert, px) },  // position
            { 1, 3, VertexDataType::Float, false, offsetof(ObjVert, nx) },  // normal
            { 2, 2, VertexDataType::Float, false, offsetof(ObjVert, u)  },  // uv
        };

        obj->render.setMesh(vertBytes, indices);

        std::cout << "Import complete: "
            << verts.size() << " vertices, "
            << indices.size() / 3 << " triangles."
            << std::endl;

        return obj;
    }

} // namespace HonHengine