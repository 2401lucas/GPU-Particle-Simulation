//
// Created by 2401Lucas on 2025-11-04.
//

#ifndef GPU_PARTICLE_SIM_D3D12COMMANDLIST_H
#define GPU_PARTICLE_SIM_D3D12COMMANDLIST_H

#include "D3D12BindlessDescriptorManager.h"
#include "Rendering/RHI/CommandList.h"
#include "D3D12Common.h"

class D3D12CommandList : public CommandList {
public:
    D3D12CommandList() = default;
    ~D3D12CommandList() override = default;

    void Begin(BindlessDescriptorManager* bindlessManager) override;
    void End() override;

    void SetPipeline(Pipeline *pipeline) override;
    void SetViewport(const Viewport &viewport) override;
    void SetScissor(const Rect &scissor) override;
    void SetPrimitiveTopology(PrimitiveTopology topology) override;


    void SetVertexBuffer(Buffer *buffer, uint32_t slot) override;
    void SetVertexBuffers(Buffer *buffer, uint32_t slot) override;
    void SetIndexBuffer(Buffer *buffer) override;
    void SetConstantBuffer(Buffer *buffer, uint32_t slot, uint32_t offset) override;
    void SetTexture(Texture *texture, uint32_t slot) override;

    void Draw(uint32_t vertexCount, uint32_t startVertex) override;
    void DrawIndexed(uint32_t indexCount, uint32_t startIndex) override;
    void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) override;
    void DrawIndexedInstanced(uint32_t indexCount, uint32_t startIndex, uint32_t instanceCount, uint32_t startInstance) override;
    void Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) override;

    void ClearRenderTarget(Texture *texture, const float color[4]) override;
    void ClearDepthStencil(Texture *texture, float depth, uint8_t stencil) override;

    void CopyBuffer(Buffer *src, Buffer *dst, uint64_t size) override;
    void CopyTexture(Texture *src, Texture *dst) override;
    void CopyBufferToTexture(Buffer *src, Texture *dst) override;

    void TransitionTexture(Texture *texture, TextureUsage oldState, TextureUsage newState) override;
    void TransitionBuffer(Buffer *buffer, BufferUsage oldState, BufferUsage newState) override;

    void SetRenderTarget(Texture *renderTarget, Texture *depthStencil) override;
    void SetRenderTargets(Texture **renderTargets, uint32_t count, Texture *depthStencil) override;

    // D3D12-specific
    ID3D12GraphicsCommandList* GetNative() const { return m_cmdList.Get(); }

    void SetAllocator(ID3D12CommandAllocator* allocator) { m_allocator = allocator; }

    void BindBindlessDescriptorHeaps(D3D12BindlessDescriptorManager* manager);

    void SetBindlessResource(uint32_t resourceIndex, uint32_t rootParameterIndex);

private:
    friend class D3D12Device;
    friend class D3D12CommandQueue;

    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ID3D12CommandAllocator* m_allocator = nullptr; // NOT owned - queue owns it
    D3D12_COMMAND_LIST_TYPE m_commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    // Track current state
    ID3D12PipelineState* m_currentPSO = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY m_currentTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    bool m_isRecording = false;

    ID3D12RootSignature* m_currentRootSignature;

    // Helper functions
    static D3D12_RESOURCE_STATES TextureUsageToD3D12State(TextureUsage usage);
    static D3D12_RESOURCE_STATES BufferUsageToD3D12State(BufferUsage usage);
    static UINT GetBytesPerPixel(DXGI_FORMAT format);
};

#endif //GPU_PARTICLE_SIM_D3D12COMMANDLIST_H