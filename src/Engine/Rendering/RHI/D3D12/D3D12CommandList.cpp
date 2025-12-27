//
// Created by 2401Lucas on 2025-11-04.
//

#include "D3D12CommandList.h"
#include "D3D12Buffer.h"
#include "D3D12Texture.h"
#include "D3D12Pipeline.h"
#include <stdexcept>

void D3D12CommandList::Begin(BindlessDescriptorManager *bindlessManager) {
    if (m_isRecording) {
        throw std::runtime_error("CommandList::Begin() called while already recording");
    }

    if (!m_allocator) {
        throw std::runtime_error("CommandList has no allocator assigned");
    }

    if (!m_cmdList) {
        throw std::runtime_error("CommandList not initialized");
    }

    DX_CHECK(m_cmdList->Reset(m_allocator, nullptr));
    m_isRecording = true;

    if (bindlessManager) {
        BindBindlessDescriptorHeaps((D3D12BindlessDescriptorManager *) bindlessManager);
    }
}

void D3D12CommandList::End() {
    if (!m_isRecording) {
        throw std::runtime_error("CommandList::End() called without Begin()");
    }

    DX_CHECK(m_cmdList->Close());
    m_isRecording = false;
}

void D3D12CommandList::SetPipeline(Pipeline *pipeline) {
    if (!pipeline || !m_isRecording) return;

    D3D12Pipeline *p = static_cast<D3D12Pipeline *>(pipeline);
    m_cmdList->SetPipelineState(p->pso.Get());

    // Set root signature
    if (p->rootSignature) {
        if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
            m_cmdList->SetComputeRootSignature(p->rootSignature.Get());
        } else {
            m_cmdList->SetGraphicsRootSignature(p->rootSignature.Get());
        }
    }

    m_currentPSO = p->pso.Get();
}

void D3D12CommandList::SetViewport(const Viewport &viewport) {
    if (!m_isRecording) return;

    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = viewport.x;
    vp.TopLeftY = viewport.y;
    vp.Width = viewport.width;
    vp.Height = viewport.height;
    vp.MinDepth = viewport.minDepth;
    vp.MaxDepth = viewport.maxDepth;

    m_cmdList->RSSetViewports(1, &vp);
}

void D3D12CommandList::SetScissor(const Rect &scissor) {
    if (!m_isRecording) return;

    D3D12_RECT sc = {};
    sc.left = static_cast<LONG>(scissor.left);
    sc.top = static_cast<LONG>(scissor.top);
    sc.right = static_cast<LONG>(scissor.right);
    sc.bottom = static_cast<LONG>(scissor.bottom);

    m_cmdList->RSSetScissorRects(1, &sc);
}

void D3D12CommandList::SetPrimitiveTopology(PrimitiveTopology topology) {
    if (!m_isRecording) return;

    D3D12_PRIMITIVE_TOPOLOGY t = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

    switch (topology) {
        case PrimitiveTopology::TriangleList:
            t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case PrimitiveTopology::TriangleStrip:
            t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        case PrimitiveTopology::LineList:
            t = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            break;
        case PrimitiveTopology::LineStrip:
            t = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            break;
        case PrimitiveTopology::PointList:
            t = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
        default:
            t = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
    }

    m_cmdList->IASetPrimitiveTopology(t);
    m_currentTopology = t;
}

void D3D12CommandList::SetVertexBuffer(Buffer *buffer, uint32_t slot) {
    if (!buffer || !m_isRecording) return;

    D3D12Buffer *vb = static_cast<D3D12Buffer *>(buffer);

    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = vb->resource->GetGPUVirtualAddress();
    vbv.SizeInBytes = static_cast<UINT>(vb->size);
    vbv.StrideInBytes = vb->stride;

    m_cmdList->IASetVertexBuffers(slot, 1, &vbv);
}

void D3D12CommandList::SetVertexBuffers(Buffer *buffer, uint32_t slot) {
}

void D3D12CommandList::SetIndexBuffer(Buffer *buffer) {
    if (!buffer || !m_isRecording) return;

    D3D12Buffer *ib = static_cast<D3D12Buffer *>(buffer);

    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = ib->resource->GetGPUVirtualAddress();
    ibv.SizeInBytes = static_cast<UINT>(ib->size);
    ibv.Format = (ib->stride == 2) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;

    m_cmdList->IASetIndexBuffer(&ibv);
}

void D3D12CommandList::SetConstantBuffer(Buffer *buffer, uint32_t slot, uint32_t offset) {
    if (!buffer || !m_isRecording) return;

    D3D12Buffer *cb = static_cast<D3D12Buffer *>(buffer);

    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = cb->resource->GetGPUVirtualAddress() + offset;
    // Set as root CBV (assumes root signature layout matches)
    if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
        m_cmdList->SetComputeRootConstantBufferView(slot, gpuAddress);
    } else {
        m_cmdList->SetGraphicsRootConstantBufferView(slot, gpuAddress);
    }
}

void D3D12CommandList::SetTexture(Texture *texture, uint32_t slot) {
    if (!texture || !m_isRecording) return;

    D3D12Texture *tex = static_cast<D3D12Texture *>(texture);

    // For descriptor tables to work, you need:
    // 1. Descriptor heaps bound via SetDescriptorHeaps()
    // 2. Root signature with descriptor table parameters
    // 3. Valid GPU descriptor handles

    // This is a simplified implementation assuming descriptor table at slot
    if (tex->srvHandle.IsValid()) {
        if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
            // m_cmdList->SetComputeRootDescriptorTable(slot, tex->srvHandle.index);
        } else {
            // m_cmdList->SetGraphicsRootDescriptorTable(slot, tex->srvHandle);
        }
    }
}

void D3D12CommandList::Draw(uint32_t vertexCount, uint32_t startVertex) {
    if (!m_isRecording) return;
    m_cmdList->DrawInstanced(vertexCount, 1, startVertex, 0);
}

void D3D12CommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndex) {
    if (!m_isRecording) return;
    m_cmdList->DrawIndexedInstanced(indexCount, 1, startIndex, 0, 0);
}

void D3D12CommandList::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) {
    if (!m_isRecording) return;
    m_cmdList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}

void D3D12CommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t startIndex, uint32_t instanceCount) {
    if (!m_isRecording) return;
    m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, 0, 0);
}

void D3D12CommandList::Dispatch(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ) {
    if (!m_isRecording) return;
    m_cmdList->Dispatch(groupsX, groupsY, groupsZ);
}

void D3D12CommandList::ClearRenderTarget(Texture *texture, const float color[4]) {
    if (!texture || !m_isRecording) return;

    D3D12Texture *rt = static_cast<D3D12Texture *>(texture);

    if (rt->rtvHandle.ptr != 0) {
        m_cmdList->ClearRenderTargetView(rt->rtvHandle, color, 0, nullptr);
    }
}

void D3D12CommandList::ClearDepthStencil(Texture *texture, float depth, uint8_t stencil) {
    if (!texture || !m_isRecording) return;

    D3D12Texture *ds = static_cast<D3D12Texture *>(texture);

    if (ds->dsvHandle.ptr != 0) {
        D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
        m_cmdList->ClearDepthStencilView(ds->dsvHandle, clearFlags, depth, stencil, 0, nullptr);
    }
}

void D3D12CommandList::TransitionTexture(Texture *texture, TextureUsage oldState, TextureUsage newState) {
    if (!texture || !m_isRecording) return;
    if (oldState == newState) return; // No transition needed

    D3D12Texture *tex = static_cast<D3D12Texture *>(texture);

    D3D12_RESOURCE_STATES oldD3DState = TextureUsageToD3D12State(oldState);
    D3D12_RESOURCE_STATES newD3DState = TextureUsageToD3D12State(newState);

    if (oldD3DState != newD3DState) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = tex->resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = oldD3DState;
        barrier.Transition.StateAfter = newD3DState;

        m_cmdList->ResourceBarrier(1, &barrier);
    }
}

void D3D12CommandList::TransitionBuffer(Buffer *buffer, BufferUsage oldState, BufferUsage newState) {
    if (!buffer || !m_isRecording) return;
    if (oldState == newState) return;

    D3D12Buffer *buf = static_cast<D3D12Buffer *>(buffer);

    D3D12_RESOURCE_STATES oldD3DState = BufferUsageToD3D12State(oldState);
    D3D12_RESOURCE_STATES newD3DState = BufferUsageToD3D12State(newState);

    if (oldD3DState != newD3DState) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = buf->resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = oldD3DState;
        barrier.Transition.StateAfter = newD3DState;

        m_cmdList->ResourceBarrier(1, &barrier);
    }
}

void D3D12CommandList::SetRenderTarget(Texture *renderTarget, Texture *depthStencil) {
    if (!m_isRecording) return;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE *dsvHandle = nullptr;

    if (renderTarget) {
        D3D12Texture *rt = static_cast<D3D12Texture *>(renderTarget);
        rtvHandle = rt->rtvHandle;
    }

    if (depthStencil) {
        D3D12Texture *ds = static_cast<D3D12Texture *>(depthStencil);
        dsvHandle = &ds->dsvHandle;
    }

    UINT numRTVs = renderTarget ? 1 : 0;
    m_cmdList->OMSetRenderTargets(numRTVs, renderTarget ? &rtvHandle : nullptr, FALSE, dsvHandle);
}

void D3D12CommandList::SetRenderTargets(Texture **renderTargets, uint32_t count, Texture *depthStencil) {
    if (!m_isRecording) return;

    if (count > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) {
        count = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE *dsvHandle = nullptr;

    for (uint32_t i = 0; i < count; ++i) {
        if (renderTargets[i]) {
            D3D12Texture *rt = static_cast<D3D12Texture *>(renderTargets[i]);
            rtvHandles[i] = rt->rtvHandle;
        }
    }

    if (depthStencil) {
        D3D12Texture *ds = static_cast<D3D12Texture *>(depthStencil);
        dsvHandle = &ds->dsvHandle;
    }

    m_cmdList->OMSetRenderTargets(count, count > 0 ? rtvHandles : nullptr, FALSE, dsvHandle);
}

void D3D12CommandList::BindBindlessDescriptorHeaps(
    D3D12BindlessDescriptorManager *manager) {
    if (!m_isRecording || !manager) return;
    m_currentRootSignature = manager->GetRootSignature();
    // STEP 1: Set root signature FIRST!
    if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
        m_cmdList->SetComputeRootSignature(m_currentRootSignature);
    } else {
        m_cmdList->SetGraphicsRootSignature(m_currentRootSignature);
    }


    // STEP 2: Bind descriptor heaps (only 2 heaps - this is the D3D12 limit!)
    ID3D12DescriptorHeap *heaps[] = {
        manager->GetResourceHeap(), // Single CBV_SRV_UAV heap
        manager->GetSamplerHeap() // Sampler heap
    };
    m_cmdList->SetDescriptorHeaps(2, heaps);

    // STEP 3: Set descriptor tables
    // SRV table points to start of heap (index 0)
    // UAV table points to offset UAV_HEAP_START in same heap
    // Sampler table points to sampler heap start

    D3D12_GPU_DESCRIPTOR_HANDLE srvHeapStart = manager->GetSRVHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE uavHeapStart = manager->GetUAVHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart = manager->GetSamplerHeapStart();

    if (m_commandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE) {
        m_cmdList->SetComputeRootDescriptorTable(0, srvHeapStart); // SRVs at indices 0+
        m_cmdList->SetComputeRootDescriptorTable(1, uavHeapStart); // UAVs at indices 0+ (offset in heap)
        m_cmdList->SetComputeRootDescriptorTable(2, samplerHeapStart); // Samplers
    } else {
        m_cmdList->SetGraphicsRootDescriptorTable(0, srvHeapStart); // SRVs at indices 0+
        m_cmdList->SetGraphicsRootDescriptorTable(1, uavHeapStart); // UAVs at indices 0+ (offset in heap)
        m_cmdList->SetGraphicsRootDescriptorTable(2, samplerHeapStart); // Samplers
    }
}

void D3D12CommandList::SetBindlessResource(uint32_t resourceIndex, uint32_t rootParameterIndex) {
    // TODO: Implement
    throw std::exception("SetBindlessResource not implemented");
}

void D3D12CommandList::CopyBuffer(Buffer *src, Buffer *dst, uint64_t size) {
    if (!src || !dst || !m_isRecording) return;

    D3D12Buffer *srcBuf = static_cast<D3D12Buffer *>(src);
    D3D12Buffer *dstBuf = static_cast<D3D12Buffer *>(dst);

    uint64_t copySize = size;
    if (copySize == 0 || copySize > srcBuf->size) {
        copySize = srcBuf->size;
    }
    if (copySize > dstBuf->size) {
        copySize = dstBuf->size;
    }

    m_cmdList->CopyBufferRegion(dstBuf->resource.Get(), 0, srcBuf->resource.Get(), 0, copySize);
}

void D3D12CommandList::CopyTexture(Texture *src, Texture *dst) {
    if (!src || !dst || !m_isRecording) return;

    D3D12Texture *srcTex = static_cast<D3D12Texture *>(src);
    D3D12Texture *dstTex = static_cast<D3D12Texture *>(dst);

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = srcTex->resource.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLocation.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = dstTex->resource.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    m_cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
}

void D3D12CommandList::CopyBufferToTexture(Buffer *src, Texture *dst) {
    if (!src || !dst || !m_isRecording) return;

    D3D12Buffer *srcBuf = static_cast<D3D12Buffer *>(src);
    D3D12Texture *dstTex = static_cast<D3D12Texture *>(dst);

    D3D12_RESOURCE_DESC texDesc = dstTex->resource->GetDesc();

    // Calculate proper footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    footprint.Offset = 0;
    footprint.Footprint.Format = texDesc.Format;
    footprint.Footprint.Width = static_cast<UINT>(texDesc.Width);
    footprint.Footprint.Height = texDesc.Height;
    footprint.Footprint.Depth = 1;

    UINT bytesPerPixel = GetBytesPerPixel(texDesc.Format);
    UINT rowPitch = footprint.Footprint.Width * bytesPerPixel;
    rowPitch = (rowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
               ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    footprint.Footprint.RowPitch = rowPitch;

    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = srcBuf->resource.Get();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = dstTex->resource.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    m_cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
}

// Helper functions

D3D12_RESOURCE_STATES D3D12CommandList::TextureUsageToD3D12State(TextureUsage usage) {
    switch (usage) {
        case TextureUsage::ShaderResource:
            // Use PIXEL_SHADER_RESOURCE for pixel shaders, NON_PIXEL for others
            // For simplicity, use common read state
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case TextureUsage::RenderTarget:
            return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case TextureUsage::DepthStencil:
            return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case TextureUsage::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case TextureUsage::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case TextureUsage::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        case TextureUsage::Present:
            return D3D12_RESOURCE_STATE_PRESENT;
        default:
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

D3D12_RESOURCE_STATES D3D12CommandList::BufferUsageToD3D12State(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Vertex:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case BufferUsage::Index:
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case BufferUsage::Uniform:
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case BufferUsage::Storage:
        case BufferUsage::UnorderedAccess:
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case BufferUsage::CopySource:
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case BufferUsage::CopyDest:
            return D3D12_RESOURCE_STATE_COPY_DEST;
        default:
            return D3D12_RESOURCE_STATE_COMMON;
    }
}

UINT D3D12CommandList::GetBytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R32_SINT:
            return 4;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            return 8;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return 16;
        case DXGI_FORMAT_R32G32B32_FLOAT:
            return 12;
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R16G16_UNORM:
            return 4;
        case DXGI_FORMAT_R8_UNORM:
            return 1;
        default:
            return 4; // Default fallback
    }
}
