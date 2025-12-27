//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_MESHLOADER_H
#define GPU_PARTICLE_SIM_MESHLOADER_H

#include <cstdint>
#include <vector>
#include <string>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

using VPosition = glm::vec3;
using VNormal = glm::vec3;
using VTexCoord = glm::vec2;
using VTangent = glm::vec3;
using VIndex = uint32_t;

struct SubMesh {
    uint32_t indexOffset;
    uint32_t vertexOffset;
    uint32_t indexCount;
    uint32_t materialIndex;
};

struct MaterialData {
    std::string name;
    std::string albedo;
    std::string normal;
    std::string metallicRough;
    std::string emissive;
};

struct MeshData {
    std::vector<VPosition> positions;
    std::vector<VNormal> normals;
    std::vector<VTexCoord> texCoords;
    std::vector<VTangent> tangents;
    std::vector<VIndex> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<MaterialData> materials;
    std::string path;

    // Bounding box
    float boundsMin[3] = {0, 0, 0};
    float boundsMax[3] = {0, 0, 0};
};

class MeshLoader {
public:
    // TODO: INVESTIGATE
    ///<summary>
    /// Currently loads submesh into parent mesh, creating one large Mesh per load. This could(maybe should?) be changed to load each submesh as own objects
    ///</summary>
    static MeshData LoadFromFile(const std::string &path,
                                 bool loadMaterial);
};


#endif //GPU_PARTICLE_SIM_MESHLOADER_H
