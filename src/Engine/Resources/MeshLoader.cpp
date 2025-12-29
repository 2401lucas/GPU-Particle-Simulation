//
// Created by 2401Lucas on 2025-10-30.
//

#include "MeshLoader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cfloat>
#include <cmath>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// This only supports models with triangle primitives
MeshData MeshLoader::LoadFromFile(const std::string &path,
                                  bool loadMaterial) {
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(path, aiProcess_Triangulate |
                                                   aiProcess_JoinIdenticalVertices |
                                                   aiProcess_FlipUVs);

    if (scene == nullptr) {
        throw std::runtime_error(importer.GetErrorString());
    }

#ifdef DEBUG_MODELS
    std::cout << "Filepath Name: " << filepath.c_str() << std::endl;
    std::cout << "Model Name: " << scene->mName.C_Str() << std::endl;
    std::cout << "Mesh Count: " << scene->mNumMeshes << std::endl;
    std::cout << "Texture Count: " << scene->mNumTextures << std::endl;
    std::cout << "Materials Count: " << scene->mNumMaterials << std::endl;
#endif

    MeshData newMesh;
    newMesh.path = path;

    uint32_t vertexCount = 0, facesCount = 0;
    for (uint32_t meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx) {
        vertexCount += scene->mMeshes[meshIdx]->mNumVertices;
        facesCount += scene->mMeshes[meshIdx]->mNumFaces;
    }
    newMesh.positions.resize(vertexCount);
    newMesh.normals.resize(vertexCount);
    newMesh.texCoords.resize(vertexCount);
    newMesh.indices.reserve(facesCount * 3);

    uint32_t localIndexOffset = 0;
    uint32_t localVertexOffset = 0;
    for (uint32_t meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx) {
        auto &mesh = scene->mMeshes[meshIdx];

        SubMesh newSubmesh{};
        newSubmesh.materialIndex = mesh->mMaterialIndex;
        newSubmesh.indexOffset = localIndexOffset;
        newSubmesh.vertexOffset = localVertexOffset;

        if (mesh->HasPositions()) {
            memcpy(&newMesh.positions[localVertexOffset], mesh->mVertices, sizeof(VPosition) * mesh->mNumVertices);
        }
        if (mesh->HasNormals()) {
            memcpy(&newMesh.normals[localVertexOffset], mesh->mNormals, sizeof(VNormal) * mesh->mNumVertices);
        }
        if (mesh->HasTextureCoords(0)) {
            memcpy(&newMesh.texCoords[localVertexOffset], mesh->mTextureCoords[0],
                   sizeof(VTexCoord) * mesh->mNumVertices);
        }
        if (mesh->HasTangentsAndBitangents()) {
            // TODO
        }
        if (mesh->HasFaces()) {
            for (uint32_t i = 0; i < mesh->mNumFaces; ++i) {
                newMesh.indices.push_back(mesh->mFaces[i].mIndices[0] + newSubmesh.vertexOffset);
                newMesh.indices.push_back(mesh->mFaces[i].mIndices[1] + newSubmesh.vertexOffset);
                newMesh.indices.push_back(mesh->mFaces[i].mIndices[2] + newSubmesh.vertexOffset);
            }
            newSubmesh.indexCount = newMesh.indices.size() - localIndexOffset;
        }

        newMesh.subMeshes.push_back(newSubmesh);
        localVertexOffset += mesh->mNumVertices;
        localIndexOffset += mesh->mNumFaces * 3;
    }

    if (!loadMaterial) return newMesh;

    for (uint32_t i = 0; i < scene->mNumMaterials; i++) {
        auto &material = scene->mMaterials[i];
        aiString filepath;
        MaterialData newMaterial;
        newMaterial.name = material->GetName().C_Str();

        if (material->GetTextureCount(aiTextureType_BASE_COLOR)) {
            material->GetTexture(aiTextureType_BASE_COLOR, 0, &filepath);
            newMaterial.albedo = filepath.C_Str();
        }
        if (material->GetTextureCount(aiTextureType_NORMAL_CAMERA)) {
            material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &filepath);
            newMaterial.normal = filepath.C_Str();
        }
        if (material->GetTextureCount(aiTextureType_EMISSION_COLOR)) {
            material->GetTexture(aiTextureType_EMISSION_COLOR, 0, &filepath);
            newMaterial.emissive = filepath.C_Str();
        }
        if (material->GetTextureCount(aiTextureType_METALNESS)) {
            material->GetTexture(aiTextureType_METALNESS, 0, &filepath);
            newMaterial.metallicRough = filepath.C_Str();
        }

        newMesh.materials.push_back(newMaterial);
    }
    return newMesh;
}
