//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_MESH_H
#define GPU_PARTICLE_SIM_MESH_H

#include <cstdint>
#include "ResourceHandle.h"

/// <summary>
/// Wrapper containing GPU bufferID, index and vertex offsets and counts for rendering
/// </summary>
/// <remarks>
/// Design Choice: Manually calculate index offsets in Shader rather than baking them.\n
/// Reason: This is done because if we defragment the buffer the offsets would no longer be accurate, which would require rebaking the indices.
/// </remarks>
struct Mesh {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
    uint32_t firstInstance;

    // Resource Bindings
    uint32_t parentBufferID;
    MaterialHandle materialHandle;
};


#endif //GPU_PARTICLE_SIM_MESH_H
