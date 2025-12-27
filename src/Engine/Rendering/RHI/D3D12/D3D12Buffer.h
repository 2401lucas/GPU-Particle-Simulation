//
// Created by 2401Lucas on 2025-11-04.
//

#ifndef GPU_PARTICLE_SIM_D3D12BUFFER_H
#define GPU_PARTICLE_SIM_D3D12BUFFER_H

#include "../Buffer.h"
#include "D3D12Common.h"
#include "D3D12BindlessDescriptorManager.h"

struct D3D12Buffer : public Buffer {
    ComPtr<ID3D12Resource> resource;
    size_t size = 0;
    uint32_t stride = 0;
    BufferUsage usage = BufferUsage::Vertex;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    void *mappedData = nullptr;

    // Bindless handles
    BindlessHandle srvHandle; // For R structured buffers
    BindlessHandle uavHandle; // For RW structured buffers
    BindlessHandle cbvHandle; // For constant buffers

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    
    D3D12Buffer() = default;

    ~D3D12Buffer() override {
        if (mappedData && resource) {
            resource->Unmap(0, nullptr);
            mappedData = nullptr;
        }
    }

    void *GetMappedPtr() const override {
        return mappedData;
    }

    uint64_t GetSize() const override {
        return size;
    }

    uint64_t GetGPUAddress() const override {
        return gpuAddress;
    }

    // TODO: Add ability for readback
    void *Map() override {
        if (!mappedData && resource) {
            D3D12_RANGE readRange = {0, 0}; //  The buffer will not be read on CPU
            if (SUCCEEDED(resource->Map(0, &readRange, &mappedData))) {
                return mappedData;
            }
        }
        return mappedData;
    }

    // Unmap buffer
    void Unmap() override {
        if (mappedData && resource) {
            D3D12_RANGE writtenRange = {0, size}; // Assume the entire buffer was written
            resource->Unmap(0, &writtenRange);
            mappedData = nullptr;
        }
    }

    // Get GPU virtual address (cached for performance)
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() {
        if (gpuAddress == 0 && resource) {
            gpuAddress = resource->GetGPUVirtualAddress();
        }
        return gpuAddress;
    }

    uint32_t GetBindlessIndex() const override {
        // Return the appropriate bindless index based on usage
        if (srvHandle.IsValid()) return srvHandle.index;
        if (uavHandle.IsValid()) return uavHandle.index;
        if (cbvHandle.IsValid()) return cbvHandle.index;
        return INVALID_DESCRIPTOR_INDEX;
    }
};

#endif //GPU_PARTICLE_SIM_D3D12BUFFER_H
