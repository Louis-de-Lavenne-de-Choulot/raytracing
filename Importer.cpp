#include "importer.h"
#include "defaults.h"
#include "baseobject.h"
#include "material.h"
#include "vector3.h"
#include "triangle.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

namespace PEngine {

    BaseObject* Importer::ImportFromOBJ(const std::string& filePath)
    {
        std::cout << "Importing OBJ file: " << filePath << std::endl;

        std::vector<Vector3> tempVertices;
        std::vector<Triangle> tempTriangles;

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open OBJ file");
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.size() < 2) continue;
            if (line[0] == '#') continue;

            // -------------------------
            // VERTICES
            // -------------------------
            if (line.substr(0, 2) == "v ")
            {
                std::istringstream vertexStream(line.substr(2));
                double x, y, z;

                vertexStream >> x >> y >> z;

                tempVertices.emplace_back(Vector3(x, y, z));
            }

            // -------------------------
            // FACES
            // -------------------------
            else if (line.substr(0, 2) == "f ")
            {
                std::istringstream faceStream(line.substr(2));
                std::string token;
                std::vector<int> indices;

                while (faceStream >> token)
                {
                    std::istringstream tokenStream(token);
                    std::string vStr;

                    // Extract vertex index (before first '/')
                    std::getline(tokenStream, vStr, '/');

                    if (vStr.empty()) continue;

                    int vIndex = std::stoi(vStr);

                    // Handle negative indices
                    if (vIndex < 0)
                        vIndex = static_cast<int>(tempVertices.size()) + vIndex + 1;

                    // Convert to 0-based index
                    vIndex -= 1;

                    indices.push_back(vIndex);
                }

                // Triangulation
                if (indices.size() == 3)
                {
                    tempTriangles.emplace_back(indices[0], indices[1], indices[2]);
                }
                else if (indices.size() == 4)
                {
                    tempTriangles.emplace_back(indices[0], indices[1], indices[2]);
                    tempTriangles.emplace_back(indices[0], indices[2], indices[3]);
                }
                else if (indices.size() > 4)
                {
                    // Fan triangulation (for ngons)
                    for (size_t i = 1; i + 1 < indices.size(); i++)
                    {
                        tempTriangles.emplace_back(indices[0], indices[i], indices[i + 1]);
                    }
                }

                // Debug
                std::cout << "Parsed face:";
                for (int idx : indices)
                    std::cout << " " << idx;
                std::cout << std::endl;
            }
        }

        file.close();

        // -------------------------
        // CREATE OBJECT
        // -------------------------
        BaseObject* obj = new BaseObject(
            Vector3(1, 1, 1),
            Vector3(0, 0, 0),
            Quaternion(),
            Defaults::MissingMaterial
        );

        obj->setVertices(std::vector<Vertice>(tempVertices.begin(), tempVertices.end()));
        obj->setTriangles(tempTriangles);

        std::cout << "Import complete: "
            << tempVertices.size() << " vertices, "
            << tempTriangles.size() << " triangles."
            << std::endl;

        return obj;
    }

}