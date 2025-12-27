//
// Created by 2401Lucas on 2025-11-04.
//

#include "D3D12Swapchain.h"

SwapchainPresentResult D3D12Swapchain::Present(bool vsync) {
    UINT flags = 0;
    if (!vsync) flags |= DXGI_PRESENT_ALLOW_TEARING;
    m_swapchain->Present(vsync ? 1 : 0, flags);
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
    return SwapchainPresentResult::Success;
}

void D3D12Swapchain::Resize(uint32_t width, uint32_t height) {
    // Release old backbuffers
    for (UINT i = 0; i < FrameCount; ++i) {
        m_backBuffers[i].Reset();
        m_backBufferTextureWrappers[i]->resource.Reset();
    }

    m_swapchain->ResizeBuffers(FrameCount, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    // Recreate RTVs
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < FrameCount; ++i) {
        m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtvHandle);
        m_backBufferTextureWrappers[i]->resource = m_backBuffers[i];
        m_backBufferTextureWrappers[i]->rtvHandle = rtvHandle;
        rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
    m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
}

const TextureFormat D3D12Swapchain::GetColorFormat() const {
    return TextureFormat::RGBA8_UNORM;
}

const uint32_t D3D12Swapchain::GetImageCount() const {
    return FrameCount;
}

Texture *D3D12Swapchain::GetSwapchainBuffer(uint32_t frameIndex) const {
    return m_backBufferTextureWrappers[m_frameIndex].get();
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12Swapchain::GetCurrentRTV() const {
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    handle.Offset(m_frameIndex, m_rtvDescriptorSize);
    return handle;
}
