//
// Created by 2401Lucas on 2025-10-31.
//

#ifndef GPU_PARTICLE_SIM_COMMANDLIST_H
#define GPU_PARTICLE_SIM_COMMANDLIST_H

#include <cstdint>

#include "Buffer.h"
#include "Pipeline.h"
#include "Texture.h"
#include "BindlessDescriptorManager.h"

struct Viewport {
    float x, y;
    float width, height;
    float minDepth, maxDepth;
};

struct Rect {
    int32_t left, top;
    int32_t right, bottom;
};

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void Begin() = 0;

    virtual void End() = 0;

    // Pipeline State
    virtual void SetPipeline(Pipeline *pipeline) = 0;

    virtual void SetViewport(const Viewport &viewport) = 0;

    virtual void SetScissor(const Rect &scissor) = 0;

    virtual void SetPrimitiveTopology(PrimitiveTopology topology) = 0;

    // Resource Binding
    virtual void SetVertexBuffer(Buffer *buffer, uint32_t slot) = 0;

    virtual void SetVertexBuffers(std::vector<Buffer *>buffer, std::vector<uint32_t> slot) = 0;

    virtual void SetIndexBuffer(Buffer *buffer) = 0;

    virtual void SetConstantBuffer(Buffer *buffer, uint32_t slot, uint32_t offset) = 0;

    virtual void SetTexture(Texture *texture, uint32_t slot) = 0;

    // Draw Commands
    virtual void Draw(uint32_t vertexCount, uint32_t startVertex = 0) = 0;

    virtual void DrawIndexed(uint32_t indexCount, uint32_t startIndex = 0) = 0;

    virtual void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) = 0;

    virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t startIndex, uint32_t instanceCount,
                                      uint32_t startInstance) = 0;

    virtual void DrawDCGBuffer(Buffer*);

    // Compute

    virtual void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) = 0;

    // Clear/Copy

    virtual void ClearRenderTarget(Texture *texture, const float color[4]) = 0;

    virtual void ClearDepthStencil(Texture *texture, float depth, uint8_t stencil) = 0;

    virtual void CopyBuffer(Buffer *src, Buffer *dst, uint64_t size) = 0;

    virtual void CopyTexture(Texture *src, Texture *dst) = 0;

    virtual void CopyBufferToTexture(Buffer *src, Texture *dst) = 0;

    // Resource Barriers
    virtual void TransitionTexture(Texture *texture,
                                   TextureUsage oldState,
                                   TextureUsage newState) = 0;

    virtual void TransitionBuffer(Buffer *buffer,
                                  BufferUsage oldState,
                                  BufferUsage newState) = 0;

    // Render Targets
    virtual void SetRenderTarget(Texture *renderTarget, Texture *depthStencil) = 0;

    virtual void SetRenderTargets(Texture **renderTargets, uint32_t count,
                                  Texture *depthStencil) = 0;
};

#endif //GPU_PARTICLE_SIM_COMMANDLIST_H
