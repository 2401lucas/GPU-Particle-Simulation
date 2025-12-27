//
// Created by 2401Lucas on 2025-10-31.
//

#ifndef GPU_PARTICLE_SIM_DEVICE_H
#define GPU_PARTICLE_SIM_DEVICE_H

#include <memory>
#include <cstdint>

#include "BindlessDescriptorManager.h"
#include "Swapchain.h"
#include "CommandQueue.h"

struct DeviceCreateInfo {
    bool enableDebugLayer = false;
    bool enableGPUValidation = false;
    uint32_t preferredAdapterIndex = 0;
};

/// <summary>
/// Device represents the GPU and is the factory for all RHI objects.
/// This is the main entry point for the RHI.
/// </summary>
class Device {
public:
    static std::unique_ptr<Device> Create(const DeviceCreateInfo &desc);

    virtual ~Device() = default;

    // Creation Factory

    [[nodiscard]] virtual CommandQueue *CreateCommandQueue(const CommandQueueCreateInfo &createInfo) = 0;

    [[nodiscard]] virtual CommandList *CreateCommandList(QueueType) = 0;

    [[nodiscard]] virtual Swapchain *CreateSwapchain(void *windowHandle, CommandQueue *queue, uint32_t width, uint32_t height) = 0;

    [[nodiscard]] virtual Buffer *CreateBuffer(const BufferCreateInfo &desc) = 0;

    [[nodiscard]] virtual Texture *CreateTexture(const TextureCreateInfo &desc) = 0;

    [[nodiscard]] virtual Pipeline *CreatePipeline(const PipelineCreateInfo &desc) = 0;

    // Resource Management

    virtual void UploadBufferData(Buffer *buffer, const void *data, size_t size) = 0;

    virtual void UploadTextureData(Texture *texture, const void *data, size_t size) = 0;

    virtual void FlushUploads() = 0;

    virtual void DestroyBuffer(Buffer *buffer) = 0;

    virtual void DestroyTexture(Texture *texture) = 0;

    virtual void DestroyPipeline(Pipeline *pipeline) = 0;

    // Device Queries

    virtual bool SupportsRayTracing() const = 0;

    virtual bool SupportsMeshShaders() const = 0;

    virtual uint64_t GetVideoMemoryBudget() const = 0;

    virtual BindlessDescriptorManager *GetBindlessManager() const = 0;

    // Sync

    virtual void WaitIdle() = 0;
};


#endif //GPU_PARTICLE_SIM_DEVICE_H
