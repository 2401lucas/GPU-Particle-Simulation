//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_MATERIAL_H
#define GPU_PARTICLE_SIM_MATERIAL_H


#include "ResourceHandle.h"

struct MaterialProperties {
    float baseColor[4] = {1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float emissive[3] = {0, 0, 0};
    float alphaCutoff = 0.5f;
};

struct Material {
    TextureHandle albedoTexture;
    TextureHandle normalTexture;
    TextureHandle metallicRoughnessTexture;
    TextureHandle emissiveTexture;
    MaterialProperties properties;

    // Cached bindless indices to reduce calls
    uint32_t albedoBindlessIndex = 0;
    uint32_t normalBindlessIndex = 0;
    uint32_t metallicRoughnessBindlessIndex = 0;
    uint32_t emissiveBindlessIndex = 0;

};


#endif //GPU_PARTICLE_SIM_MATERIAL_H
