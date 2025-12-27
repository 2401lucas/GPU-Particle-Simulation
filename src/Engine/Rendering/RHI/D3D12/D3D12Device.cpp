//
// Created by 2401Lucas on 2025-10-31.
//

#include "D3D12Device.h"
#include <d3dcompiler.h>
#include <fstream>
#include <algorithm>
#include <sstream>

#include "D3D12CommandQueue.h"

ComPtr<IDXGIAdapter4> SelectAdapter(ComPtr<IDXGIFactory6> factory, uint32_t preferredIndex) {
    ComPtr<IDXGIAdapter4> chosenAdapter;
    ComPtr<IDXGIAdapter1> adapter1;
    UINT index = 0;

    for (UINT i = 0; SUCCEEDED(
             factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1))); ++
         i) {
        ComPtr<IDXGIAdapter4> adapter4;
        DX_CHECK(adapter1.As(&adapter4));

        DXGI_ADAPTER_DESC3 desc;
        adapter4->GetDesc3(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(adapter4.Get(), D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), nullptr))) {
            if (index == preferredIndex) {
                chosenAdapter = adapter4;
                break;
            }
            ++index;
            if (!chosenAdapter) {
                chosenAdapter = adapter4;
            }
        }
    }

    if (!chosenAdapter) {
        ComPtr<IDXGIAdapter4> defaultAdapter;
        ComPtr<IDXGIAdapter1> ad1;
        if (SUCCEEDED(factory->EnumAdapters1(0, &ad1))) {
            DX_CHECK(ad1.As(&defaultAdapter));
            chosenAdapter = defaultAdapter;
        }
    }

    return chosenAdapter;
}

// ===== DescriptorHeapAllocator Implementation =====

DescriptorHeapAllocator::DescriptorHeapAllocator(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                 uint32_t numDescriptors)
    : m_descriptorSize(device->GetDescriptorHandleIncrementSize(type))
      , m_numDescriptors(numDescriptors)
      , m_currentOffset(0) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = type;
    desc.NumDescriptors = numDescriptors;
    desc.Flags = (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
                     ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                     : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    DX_CHECK(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap)));
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::AllocateCPU() {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t offset;
    if (!m_freeList.empty()) {
        offset = m_freeList.front();
        m_freeList.pop();
    } else {
        if (m_currentOffset >= m_numDescriptors) {
            throw std::runtime_error("Descriptor heap exhausted");
        }
        offset = m_currentOffset++;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += offset * m_descriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeapAllocator::AllocateGPU() {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint32_t offset;
    if (!m_freeList.empty()) {
        offset = m_freeList.front();
        m_freeList.pop();
    } else {
        if (m_currentOffset >= m_numDescriptors) {
            throw std::runtime_error("Descriptor heap exhausted");
        }
        offset = m_currentOffset++;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += offset * m_descriptorSize;
    return handle;
}

void DescriptorHeapAllocator::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    std::lock_guard<std::mutex> lock(m_mutex);

    D3D12_CPU_DESCRIPTOR_HANDLE start = m_heap->GetCPUDescriptorHandleForHeapStart();
    uint32_t offset = static_cast<uint32_t>((handle.ptr - start.ptr) / m_descriptorSize);
    m_freeList.push(offset);
}

// ===== CommandAllocatorPool Implementation =====

CommandAllocatorPool::CommandAllocatorPool(ID3D12Device *device, D3D12_COMMAND_LIST_TYPE type)
    : m_device(device), m_type(type) {
}

ComPtr<ID3D12CommandAllocator> CommandAllocatorPool::RequestAllocator(uint64_t completedFenceValue) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if any allocators are ready for reuse
    while (!m_allocatorQueue.empty() && m_allocatorQueue.front().fenceValue <= completedFenceValue) {
        ComPtr<ID3D12CommandAllocator> allocator = m_allocatorQueue.front().allocator;
        m_allocatorQueue.pop();
        DX_CHECK(allocator->Reset());
        return allocator;
    }

    // Create new allocator
    ComPtr<ID3D12CommandAllocator> allocator;
    DX_CHECK(m_device->CreateCommandAllocator(m_type, IID_PPV_ARGS(&allocator)));
    return allocator;
}

void CommandAllocatorPool::DiscardAllocator(uint64_t fenceValue, ComPtr<ID3D12CommandAllocator> allocator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocatorQueue.push({fenceValue, allocator});
}

// ===== ResourceStateTracker Implementation =====

void ResourceStateTracker::TrackResource(ID3D12Resource *resource, D3D12_RESOURCE_STATES initialState) {
    m_resourceStates[resource] = {initialState, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES};
}

void ResourceStateTracker::TransitionResource(ID3D12GraphicsCommandList *cmdList, ID3D12Resource *resource,
                                              D3D12_RESOURCE_STATES newState, uint32_t subresource) {
    auto it = m_resourceStates.find(resource);
    if (it == m_resourceStates.end()) {
        // Resource not tracked, assume common state
        TrackResource(resource, D3D12_RESOURCE_STATE_COMMON);
        it = m_resourceStates.find(resource);
    }

    D3D12_RESOURCE_STATES oldState = it->second.state;

    if (oldState != newState) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = subresource;
        barrier.Transition.StateBefore = oldState;
        barrier.Transition.StateAfter = newState;

        m_pendingBarriers.push_back(barrier);
        it->second.state = newState;
    }
}

void ResourceStateTracker::FlushBarriers(ID3D12GraphicsCommandList *cmdList) {
    if (!m_pendingBarriers.empty()) {
        cmdList->ResourceBarrier(static_cast<UINT>(m_pendingBarriers.size()), m_pendingBarriers.data());
        m_pendingBarriers.clear();
    }
}

void ResourceStateTracker::Reset() {
    m_pendingBarriers.clear();
}

// ===== UploadBufferAllocator Implementation =====

UploadBufferAllocator::UploadBufferAllocator(ID3D12Device *device, size_t pageSize)
    : m_device(device), m_pageSize(pageSize), m_currentPage(0) {
}

UploadBufferAllocator::Allocation UploadBufferAllocator::Allocate(size_t size, size_t alignment) {
    // Align the size
    size_t alignedSize = (size + alignment - 1) & ~(alignment - 1);

    // Check if we need a new page
    if (m_pages.empty() || m_pages[m_currentPage].offset + alignedSize > m_pages[m_currentPage].pageSize) {
        // Try to find an existing page with enough space
        bool foundPage = false;
        for (size_t i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].offset + alignedSize <= m_pages[i].pageSize) {
                m_currentPage = i;
                foundPage = true;
                break;
            }
        }

        if (!foundPage) {
            // Create new page
            UploadPage page;
            page.pageSize = std::max(m_pageSize, alignedSize);
            page.offset = 0;

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
            CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(page.pageSize);

            DX_CHECK(m_device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&page.resource)));

            DX_CHECK(page.resource->Map(0, nullptr, &page.cpuAddress));
            page.gpuAddress = page.resource->GetGPUVirtualAddress();

            m_currentPage = m_pages.size();
            m_pages.push_back(page);
        }
    }

    UploadPage &page = m_pages[m_currentPage];

    // Align the offset
    size_t alignedOffset = (page.offset + alignment - 1) & ~(alignment - 1);

    Allocation allocation;
    allocation.cpuAddress = static_cast<uint8_t *>(page.cpuAddress) + alignedOffset;
    allocation.gpuAddress = page.gpuAddress + alignedOffset;
    allocation.resource = page.resource.Get();
    allocation.offset = alignedOffset;

    page.offset = alignedOffset + size;

    return allocation;
}

void UploadBufferAllocator::Reset() {
    for (auto &page: m_pages) {
        page.offset = 0;
    }
    m_currentPage = 0;
}

// ===== D3D12Device Implementation =====

D3D12Device::D3D12Device(const DeviceCreateInfo &info) {
    OutputDebugStringA("===== Initializing D3D12 =====\n");

    UINT flags = 0u;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugInterface;
    if (info.enableDebugLayer) {
        flags |= DXGI_CREATE_FACTORY_DEBUG;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface)))) {
            debugInterface->EnableDebugLayer();
            if (info.enableGPUValidation) {
                ComPtr<ID3D12Debug1> debug1;
                if (SUCCEEDED(debugInterface.As(&debug1))) {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                }
            }
        }
    }
#endif

    ComPtr<IDXGIFactory6> factory;
    DX_CHECK(CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory)));

    OutputDebugStringA("=== D3D12 Device Selection ===\n");
    m_adapter = SelectAdapter(factory, 1);
    if (!m_adapter) {
        throw std::runtime_error("No suitable DXGI adapter found.");
    }
    DXGI_ADAPTER_DESC desc;
    m_adapter->GetDesc(&desc);
    std::wstringstream ss;

    ss << L"DXGI_ADAPTER_DESC\n";
    ss << L"Description: " << desc.Description << L"\n";
    ss << L"DedicatedVideoMemory:   " << (desc.DedicatedVideoMemory / (1024 * 1024)) << L" MB\n";
    ss << L"DedicatedSystemMemory:  " << (desc.DedicatedSystemMemory / (1024 * 1024)) << L" MB\n";
    ss << L"SharedSystemMemory:     " << (desc.SharedSystemMemory / (1024 * 1024)) << L" MB\n";
    ss << L"=========================\n";

    // Convert to narrow string for OutputDebugStringA
    std::wstring ws = ss.str();
    std::string msg(ws.begin(), ws.end());
    OutputDebugStringA(msg.c_str());


    DX_CHECK(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device)));

    CheckBindlessSupport();

#if defined(_DEBUG)
    if (info.enableDebugLayer) {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }
#endif

    InitializeSynchronization();
    InitializeDescriptorHeaps();
    InitializeCommandAllocatorPools();

    m_bindlessManager = std::make_unique<D3D12BindlessDescriptorManager>(m_device.Get());
    m_bindlessRootSignature = CreateBindlessRootSignature();
    m_bindlessManager->m_rootSignature = m_bindlessRootSignature.Get();

    CommandQueueCreateInfo commandQueueCreateInfo{
        .type = QueueType::Graphics,
        .debugName = "Internal Transfer",
    };
    m_uploadCommandQueue = std::unique_ptr<D3D12CommandQueue>(
        (D3D12CommandQueue *) CreateCommandQueue(commandQueueCreateInfo));
    m_uploadCommandList = std::unique_ptr<
        D3D12CommandList>((D3D12CommandList *) CreateCommandList(QueueType::Graphics));

    // Initialize upload allocator
    m_uploadAllocator = std::make_unique<UploadBufferAllocator>(m_device.Get());
}

D3D12Device::~D3D12Device() {
    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
    }
}

void D3D12Device::InitializeSynchronization() {
    DX_CHECK(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        throw std::runtime_error("Failed to create fence event.");
    }
}

void D3D12Device::InitializeDescriptorHeaps() {
    m_rtvHeap = std::make_unique<DescriptorHeapAllocator>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 256);
    m_dsvHeap = std::make_unique<DescriptorHeapAllocator>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256);
    m_cbvSrvUavHeap = std::make_unique<DescriptorHeapAllocator>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                                4096);
    m_samplerHeap = std::make_unique<DescriptorHeapAllocator>(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 256);
}

void D3D12Device::InitializeCommandAllocatorPools() {
    m_directAllocatorPool = std::make_unique<CommandAllocatorPool>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT);
    m_computeAllocatorPool = std::make_unique<CommandAllocatorPool>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    m_copyAllocatorPool = std::make_unique<CommandAllocatorPool>(m_device.Get(), D3D12_COMMAND_LIST_TYPE_COPY);
}

void D3D12Device::WaitForFenceValue(UINT64 value) {
    if (m_fence->GetCompletedValue() < value) {
        DX_CHECK(m_fence->SetEventOnCompletion(value, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}


/*
 * Root Signature Layout (Tier 2 Compatible):
 *
 * Root Parameter 0: Descriptor Table - Large SRV range (t0-t999)
 * Root Parameter 1: Descriptor Table - Large UAV range (u0-u99)
 * Root Parameter 2: Descriptor Table - Sampler range (s0-s15)
 * Root Parameter 3: Root Constants (64 bytes)
 * Root Parameter 4: Root CBV for per-frame data (b0)
 * Root Parameter 5: Root CBV for per-object data (b1)
 */
ComPtr<ID3D12RootSignature> D3D12Device::CreateBindlessRootSignature() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

    bool supportsBindlessTier2 = (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2);

    if (!supportsBindlessTier2) {
        throw std::exception(
            "WARNING: Device doesn't support Tier 2 resource binding. Using traditional descriptors.\n");
        return CreateFallbackRootSignature();
    }


    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];

    ranges[0].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
        MAX_BINDLESS_SRVS,
        0,
        0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );

    ranges[1].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
        MAX_BINDLESS_UAVS,
        0,
        0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );

    ranges[2].Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
        16, // s0-s15
        0,
        0,
        D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE
    );

    CD3DX12_ROOT_PARAMETER1 rootParams[6];

    // 0: Large SRV table
    rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);

    // 1: Large UAV table
    rootParams[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);

    // 2: Sampler table
    rootParams[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

    // 3: Push constants (64 bytes)
    rootParams[3].InitAsConstants(16, 2, 0, D3D12_SHADER_VISIBILITY_ALL);

    // 4: Per-frame CBV
    rootParams[4].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

    // 5: Per-object CBV
    rootParams[5].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;

    D3D12_ROOT_SIGNATURE_FLAGS flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    rsDesc.Init_1_1(
        _countof(rootParams),
        rootParams,
        0,
        nullptr,
        flags
    );

    ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &serialized, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA("Bindless root signature serialization failed:\n");
            OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to serialize bindless root signature");
    }

    ComPtr<ID3D12RootSignature> rootSig;
    DX_CHECK(m_device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSig)
    ));

    rootSig->SetName(L"Bindless Root Signature");

    return rootSig;
}

void D3D12Device::WaitIdle() {
    // Externally synced for now
}

CommandList *D3D12Device::CreateCommandList(QueueType queueType) {
    auto cmdList = std::make_unique<D3D12CommandList>();

    D3D12_COMMAND_LIST_TYPE d3d12Type = GetD3D12CommandListType(queueType);
    cmdList->m_commandListType = d3d12Type;

    // Creates a temporary allocator for initial creation
    // Real allocator is set by the D3D12CommandQueue
    ComPtr<ID3D12CommandAllocator> tempAllocator;
    DX_CHECK(m_device->CreateCommandAllocator(d3d12Type, IID_PPV_ARGS(&tempAllocator)));

    DX_CHECK(m_device->CreateCommandList(
        0,
        d3d12Type,
        tempAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList->m_cmdList)
    ));
    // Close immediately as it starts recording upon creation
    cmdList->m_cmdList->Close();

    const wchar_t *typeName = GetQueueTypeName(queueType);
    std::wstring name = std::wstring(typeName) + L" CommandList";
    cmdList->m_cmdList->SetName(name.c_str());

    return cmdList.release();
}

Swapchain *D3D12Device::CreateSwapchain(void *windowHandle, CommandQueue *queue, uint32_t width, uint32_t height) {
    if (!windowHandle)
        return nullptr;
    auto m_swapchain = std::make_unique<D3D12Swapchain>();
    m_swapchain->m_device = m_device;

    ComPtr<IDXGIFactory4> factory;
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
    if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory))))
        return nullptr;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {
        .Width = width,
        .Height = height,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = FrameCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING,

    };

    auto q = (D3D12CommandQueue *) queue;
    ComPtr<IDXGISwapChain1> swapChain1;
    DX_CHECK(factory->CreateSwapChainForHwnd(
        q->m_commandQueue.Get(), // Your D3D12 command queue
        static_cast<HWND>(windowHandle), // HWND from your window system
        &scDesc,
        nullptr,
        nullptr,
        &swapChain1));

    DX_CHECK(swapChain1.As(&m_swapchain->m_swapchain));

    // RTV Heap creation
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = FrameCount,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };

    m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_swapchain->m_rtvHeap));

    m_swapchain->m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create RTVs for each backbuffer
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_swapchain->m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < FrameCount; ++i) {
        DX_CHECK(m_swapchain->m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_swapchain->m_backBuffers[i])));

        m_device->CreateRenderTargetView(m_swapchain->m_backBuffers[i].Get(), nullptr, rtvHandle);
        m_swapchain->m_backBufferTextureWrappers[i] = std::make_unique<D3D12Texture>();
        m_swapchain->m_backBufferTextureWrappers[i]->rtvHandle = rtvHandle;
        m_swapchain->m_backBufferTextureWrappers[i]->resource = m_swapchain->m_backBuffers[i];
        rtvHandle.Offset(1, m_swapchain->m_rtvDescriptorSize);
    }
    return m_swapchain.release();
}

CommandQueue *D3D12Device::CreateCommandQueue(const CommandQueueCreateInfo &createInfo) {
    auto queue = std::make_unique<D3D12CommandQueue>();

    try {
        // Store queue type
        queue->m_type = createInfo.type;
        queue->m_d3d12Type = GetD3D12CommandListType(createInfo.type);

        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {
            .Type = queue->m_d3d12Type,
            .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
            .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
            .NodeMask = 0,
        };

        DX_CHECK(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue->m_commandQueue)));

        // Set debug name
        std::wstring debugName;
        if (createInfo.debugName) {
            debugName = std::wstring(createInfo.debugName, createInfo.debugName + strlen(createInfo.debugName));
        } else {
            debugName = std::wstring(GetQueueTypeName(createInfo.type)) + L" Queue";
        }
        DX_CHECK(queue->m_commandQueue->SetName(debugName.c_str()));

        // Create per-frame command allocators
        queue->m_allocators.resize(FrameCount);
        queue->m_fenceValues.resize(FrameCount, 0);

        for (uint32_t i = 0; i < FrameCount; ++i) {
            DX_CHECK(m_device->CreateCommandAllocator(
                queue->m_d3d12Type,
                IID_PPV_ARGS(&queue->m_allocators[i])
            ));

            // Set debug name for allocator
            std::wstring allocatorName = debugName + L" Allocator [Frame " + std::to_wstring(i) + L"]";
            DX_CHECK(queue->m_allocators[i]->SetName(allocatorName.c_str()));
        }

        // Create fence for GPU synchronization
        DX_CHECK(m_device->CreateFence(
            0,
            D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&queue->m_fence)
        ));

        std::wstring fenceName = debugName + L" Fence";
        DX_CHECK(queue->m_fence->SetName(fenceName.c_str()));

        // Create fence event
        queue->m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (queue->m_fenceEvent == nullptr) {
            DX_CHECK(HRESULT_FROM_WIN32(GetLastError()));
        }
    } catch (const std::exception &e) {
        // Cleanup on failure
        if (queue->m_fenceEvent) {
            CloseHandle(queue->m_fenceEvent);
            queue->m_fenceEvent = nullptr;
        }
        throw;
    }

    return queue.release();
}

Buffer *D3D12Device::CreateBuffer(const BufferCreateInfo &desc) {
    auto buffer = std::make_unique<D3D12Buffer>();
    buffer->size = desc.size;
    buffer->usage = desc.usage;
    buffer->stride = desc.stride;

    D3D12_RESOURCE_DESC resourceDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = desc.size,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc = {
            .Count = 1,
            .Quality = 0,
        },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    // Determine heap type and initial state based on usage
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_STATES initialState = BufferUsageToResourceState(desc.usage);

    if (desc.memoryType == MemoryType::Upload) {
        heapType = D3D12_HEAP_TYPE_UPLOAD;
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    } else if (desc.memoryType == MemoryType::Readback) {
        heapType = D3D12_HEAP_TYPE_READBACK;
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    if (desc.usage == BufferUsage::UnorderedAccess) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    CD3DX12_HEAP_PROPERTIES heapProps(heapType);
    DX_CHECK(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&buffer->resource)));

    m_stateTracker.TrackResource(buffer->resource.Get(), initialState);

    // Cache GPU address
    buffer->gpuAddress = buffer->resource->GetGPUVirtualAddress();

    // Create bindless descriptors based on usage
    if (desc.usage == BufferUsage::Storage) {
        // Structured buffer for reading (SRV)
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = static_cast<UINT>(desc.size / desc.stride),
                .StructureByteStride = desc.stride,
                .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
            },
        };
        buffer->srvHandle = m_bindlessManager->AllocateSRV(buffer->resource.Get(), &srvDesc);
        buffer->stride = desc.stride;
    }

    if (desc.usage == BufferUsage::UnorderedAccess) {
        // RW Structured buffer (UAV)
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer = {
                .FirstElement = 0,
                .NumElements = static_cast<UINT>(desc.size / desc.stride),
                .StructureByteStride = desc.stride,
                .CounterOffsetInBytes = 0,
                .Flags = D3D12_BUFFER_UAV_FLAG_NONE,
            }
        };
        buffer->uavHandle = m_bindlessManager->AllocateUAV(buffer->resource.Get(), &uavDesc);
        buffer->stride = desc.stride;
    }

    if (desc.usage == BufferUsage::Uniform) {
        // Constant buffer view
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
            .BufferLocation = buffer->gpuAddress,
            .SizeInBytes = static_cast<UINT>((desc.size + 255) & ~255), // Align to 256 bytes
        };
        buffer->cbvHandle = m_bindlessManager->AllocateCBV(&cbvDesc);
    }

    if (desc.usage == BufferUsage::Vertex) {
        buffer->vertexBufferView = {
            .BufferLocation = buffer->gpuAddress,
            .SizeInBytes = static_cast<UINT>(buffer->size),
            .StrideInBytes = desc.stride,
        };
    }

    return buffer.release();
}

Texture *D3D12Device::CreateTexture(const TextureCreateInfo &desc) {
    D3D12Texture *texture = new D3D12Texture();
    texture->width = desc.width;
    texture->height = desc.height;
    texture->format = desc.format;
    texture->usage = desc.usage;

    D3D12_RESOURCE_DESC resourceDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = desc.width,
        .Height = desc.height,
        .DepthOrArraySize = 1,
        .MipLevels = desc.mipLevels,
        .Format = TextureFormatToDxgiFormat(desc.format),
        .SampleDesc = {
            .Count = 1,
            .Quality = 0,
        },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_NONE,
    };
    D3D12_RESOURCE_STATES initialState = TextureUsageToResourceState(desc.usage);

    if (desc.usage == TextureUsage::RenderTarget) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    } else if (desc.usage == TextureUsage::DepthStencil) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    } else if (desc.usage == TextureUsage::UnorderedAccess) {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE *clearValue = nullptr;
    D3D12_CLEAR_VALUE clearValueData = {};

    if (desc.usage == TextureUsage::RenderTarget) {
        clearValueData.Format = resourceDesc.Format;
        clearValueData.Color[0] = 0.0f;
        clearValueData.Color[1] = 0.0f;
        clearValueData.Color[2] = 0.0f;
        clearValueData.Color[3] = 1.0f;
        clearValue = &clearValueData;
    } else if (desc.usage == TextureUsage::DepthStencil) {
        clearValueData.Format = resourceDesc.Format;
        clearValueData.DepthStencil.Depth = 1.0f;
        clearValueData.DepthStencil.Stencil = 0;
        clearValue = &clearValueData;
    }

    DX_CHECK(m_device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        clearValue,
        IID_PPV_ARGS(&texture->resource)));

    m_stateTracker.TrackResource(texture->resource.Get(), initialState);

    // Create views
    // RTV and DSV remain non-bindless (they must be CPU descriptors)
    if (desc.usage == TextureUsage::RenderTarget) {
        texture->rtvHandle = m_rtvHeap->AllocateCPU();
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {
            .Format = resourceDesc.Format,
            .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
            .Texture2D = {.MipSlice = 0},
        };
        m_device->CreateRenderTargetView(texture->resource.Get(), &rtvDesc, texture->rtvHandle);
    }

    if (desc.usage == TextureUsage::DepthStencil) {
        texture->dsvHandle = m_dsvHeap->AllocateCPU();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {
            .Format = resourceDesc.Format,
            .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
            .Flags = D3D12_DSV_FLAG_NONE,
            .Texture2D = {.MipSlice = 0},
        };
        m_device->CreateDepthStencilView(texture->resource.Get(), &dsvDesc, texture->dsvHandle);
    }

    // Create bindless SRV for shader access
    if (desc.usage == TextureUsage::ShaderResource || desc.usage == TextureUsage::RenderTarget) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
            .Format = resourceDesc.Format,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = {
                .MostDetailedMip = 0,
                .MipLevels = desc.mipLevels,
            },
        };
        texture->srvHandle = m_bindlessManager->AllocateSRV(texture->resource.Get(), &srvDesc);
    }

    // Create bindless UAV for RW access
    if (desc.usage == TextureUsage::UnorderedAccess) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
            .Format = resourceDesc.Format,
            .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
            .Texture2D = {.MipSlice = 0},
        };
        texture->uavHandle = m_bindlessManager->AllocateUAV(texture->resource.Get(), &uavDesc);
    }


    return texture;
}

void D3D12Device::DestroyBuffer(Buffer *buffer) {
    if (buffer) {
        D3D12Buffer *d3d12Buf = static_cast<D3D12Buffer *>(buffer);
        if (d3d12Buf->srvHandle.IsValid()) {
            m_bindlessManager->Free(d3d12Buf->srvHandle);
        }
        if (d3d12Buf->uavHandle.IsValid()) {
            m_bindlessManager->Free(d3d12Buf->uavHandle);
        }
        if (d3d12Buf->cbvHandle.IsValid()) {
            m_bindlessManager->Free(d3d12Buf->cbvHandle);
        }
    }
    delete buffer;
}

void D3D12Device::DestroyTexture(Texture *texture) {
    if (texture) {
        D3D12Texture *d3d12Tex = static_cast<D3D12Texture *>(texture);

        if (d3d12Tex->rtvHandle.ptr != 0) {
            m_rtvHeap->Free(d3d12Tex->rtvHandle);
        }
        if (d3d12Tex->dsvHandle.ptr != 0) {
            m_dsvHeap->Free(d3d12Tex->dsvHandle);
        }
        if (d3d12Tex->srvHandle.IsValid()) {
            m_bindlessManager->Free(d3d12Tex->srvHandle);
        }
        if (d3d12Tex->uavHandle.IsValid()) {
            m_bindlessManager->Free(d3d12Tex->uavHandle);
        }
    }
    delete texture;
}

void D3D12Device::DestroyPipeline(Pipeline *pipeline) {
    delete pipeline;
}

bool D3D12Device::SupportsRayTracing() const {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)))) {
        return options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
    }
    return false;
}

bool D3D12Device::SupportsMeshShaders() const {
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
        return options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    }
    return false;
}

uint64_t D3D12Device::GetVideoMemoryBudget() const {
    DXGI_QUERY_VIDEO_MEMORY_INFO memInfo = {};
    if (SUCCEEDED(m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
        return memInfo.Budget;
    }
    return 0;
}

ComPtr<ID3D12RootSignature> D3D12Device::CreateDefaultGraphicsRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4, 0); // b0-b3
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0); // t0-t7
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0); // u0-u3

    CD3DX12_ROOT_PARAMETER1 rootParams[4];
    rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL); // CBVs
    rootParams[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL); // SRVs
    rootParams[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL); // UAVs
    rootParams[3].InitAsConstants(16, 4); // 64 bytes of root constants

    CD3DX12_STATIC_SAMPLER_DESC samplers[2];
    samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_POINT);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init_1_1(_countof(rootParams), rootParams, _countof(samplers), samplers,
                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &serialized, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA("Root signature serialization failed:\n");
            OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    ComPtr<ID3D12RootSignature> rootSig;
    DX_CHECK(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSig)));

    return rootSig;
}

ComPtr<ID3D12RootSignature> D3D12Device::CreateDefaultComputeRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 4, 0); // b0-b3
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0); // t0-t7
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0); // u0-u7

    CD3DX12_ROOT_PARAMETER1 rootParams[4];
    rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL); // CBVs
    rootParams[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL); // SRVs
    rootParams[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL); // UAVs
    rootParams[3].InitAsConstants(16, 0); // 64 bytes of root constants

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init_1_1(_countof(rootParams), rootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &serialized, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA("Root signature serialization failed:\n");
            OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to serialize root signature");
    }

    ComPtr<ID3D12RootSignature> rootSig;
    DX_CHECK(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSig)));

    return rootSig;
}

/*
 * Fallback Root Signature Layout (Compatible with Tier 1):
 *
 * Root Parameter 0: Descriptor Table - SRVs (t0-t99)
 * Root Parameter 1: Descriptor Table - UAVs (u0-u7)
 * Root Parameter 2: Descriptor Table - Samplers (s0-s7)
 * Root Parameter 3: Root Constants (64 bytes)
 * Root Parameter 4: Root CBV (b0)
 * Root Parameter 5: Root CBV (b1)
 */
ComPtr<ID3D12RootSignature> D3D12Device::CreateFallbackRootSignature() {
    CD3DX12_DESCRIPTOR_RANGE1 ranges[3];

    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 100, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 8, 0);
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 8, 0);

    CD3DX12_ROOT_PARAMETER1 rootParams[6];
    rootParams[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    rootParams[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    rootParams[2].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);
    rootParams[3].InitAsConstants(16, 2, 0, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[4].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    rootParams[5].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
    rsDesc.Init_1_1(
        _countof(rootParams),
        rootParams,
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    );

    ComPtr<ID3DBlob> serialized, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rsDesc, &serialized, &error);

    if (FAILED(hr)) {
        if (error) {
            OutputDebugStringA("Fallback root signature serialization failed:\n");
            OutputDebugStringA(static_cast<const char *>(error->GetBufferPointer()));
        }
        throw std::runtime_error("Failed to serialize fallback root signature");
    }

    ComPtr<ID3D12RootSignature> rootSig;
    DX_CHECK(m_device->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(&rootSig)
    ));

    rootSig->SetName(L"Fallback Root Signature");

    OutputDebugStringA("Using fallback root signature (limited to 100 textures)\n");

    return rootSig;
}

void D3D12Device::CheckBindlessSupport() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));

    OutputDebugStringA("=== D3D12 Feature Support ===\n");

    switch (options.ResourceBindingTier) {
        case D3D12_RESOURCE_BINDING_TIER_1:
            OutputDebugStringA("Resource Binding Tier: 1 (Limited)\n");
            OutputDebugStringA("  - Max descriptors per table: 16 (SRV/UAV/CBV), 16 (Samplers)\n");
            break;
        case D3D12_RESOURCE_BINDING_TIER_2:
            OutputDebugStringA("Resource Binding Tier: 2 (Good for bindless)\n");
            OutputDebugStringA("  - Max descriptors per table: 1,000,000 (SRV/UAV/CBV)\n");
            break;
        case D3D12_RESOURCE_BINDING_TIER_3:
            OutputDebugStringA("Resource Binding Tier: 3 (Full bindless support)\n");
            OutputDebugStringA("  - Unlimited descriptors, full bindless\n");
            break;
    }

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {D3D_SHADER_MODEL_6_0};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))) {
        char msg[256];
        sprintf_s(msg, "Shader Model: %d.%d\n",
                  (shaderModel.HighestShaderModel >> 4),
                  (shaderModel.HighestShaderModel & 0xF));
        OutputDebugStringA(msg);
    }

    OutputDebugStringA("============================\n");
}

Pipeline *D3D12Device::CreatePipeline(const PipelineCreateInfo &pipelineCreateInfo) {
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    inputLayout.reserve(pipelineCreateInfo.vertexAttributes.size());

    for (uint32_t i = 0; i < pipelineCreateInfo.vertexAttributes.size(); ++i) {
        const auto &a = pipelineCreateInfo.vertexAttributes[i];
        D3D12_INPUT_ELEMENT_DESC desc = {
            .SemanticName = a.semantic,
            .SemanticIndex = a.index,
            .Format = TextureFormatToDxgiFormat(a.format),
            .InputSlot = a.binding,
            .AlignedByteOffset = a.offset,
            .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, //TODO: Support Per Instance
            .InstanceDataStepRate = 0,
        };
        inputLayout.push_back(desc);
    }

    D3D12_RASTERIZER_DESC rastDesc = {
        .FillMode = pipelineCreateInfo.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID,
        .CullMode = (pipelineCreateInfo.cullMode == CullMode::None)
                        ? D3D12_CULL_MODE_NONE
                        : (pipelineCreateInfo.cullMode == CullMode::Front)
                              ? D3D12_CULL_MODE_FRONT
                              : D3D12_CULL_MODE_BACK,
        .FrontCounterClockwise = FALSE,
        .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
        .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        .DepthClipEnable = TRUE,
        .MultisampleEnable = pipelineCreateInfo.sampleCount > 1,
        .AntialiasedLineEnable = FALSE,
        .ForcedSampleCount = 0,
        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
    };
    D3D12_BLEND_DESC blendDesc = {
        .AlphaToCoverageEnable = FALSE,
        .IndependentBlendEnable = (pipelineCreateInfo.renderTargetCount > 1) ? TRUE : FALSE,
    };

    for (auto &att: blendDesc.RenderTarget) {
        att.BlendEnable = pipelineCreateInfo.blendMode != BlendMode::None;
        att.SrcBlend = D3D12_BLEND_ONE;
        att.DestBlend = D3D12_BLEND_ZERO;
        att.BlendOp = D3D12_BLEND_OP_ADD;
        att.SrcBlendAlpha = D3D12_BLEND_ONE;
        att.DestBlendAlpha = D3D12_BLEND_ZERO;
        att.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        att.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_DEPTH_STENCIL_DESC dsDesc = {
        .DepthEnable = pipelineCreateInfo.depthTestEnable,
        .DepthWriteMask = pipelineCreateInfo.depthWriteEnable
                              ? D3D12_DEPTH_WRITE_MASK_ALL
                              : D3D12_DEPTH_WRITE_MASK_ZERO,
        .DepthFunc = (pipelineCreateInfo.depthFunc == CompareFunc::Less)
                         ? D3D12_COMPARISON_FUNC_LESS
                         : (pipelineCreateInfo.depthFunc == CompareFunc::LessEqual)
                               ? D3D12_COMPARISON_FUNC_LESS_EQUAL
                               : (pipelineCreateInfo.depthFunc == CompareFunc::Greater)
                                     ? D3D12_COMPARISON_FUNC_GREATER
                                     : D3D12_COMPARISON_FUNC_ALWAYS,
        .StencilEnable = FALSE,
    };
    auto vsBlob = GetShaderBlob(pipelineCreateInfo.vertexShader);
    auto psBlob = GetShaderBlob(pipelineCreateInfo.pixelShader);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
        .pRootSignature = m_bindlessRootSignature.Get(),
        .VS = {vsBlob ? vsBlob->GetBufferPointer() : nullptr, vsBlob ? vsBlob->GetBufferSize() : 0},
        .PS = {psBlob ? psBlob->GetBufferPointer() : nullptr, psBlob ? psBlob->GetBufferSize() : 0},
        .BlendState = blendDesc,
        .SampleMask = UINT_MAX,
        .RasterizerState = rastDesc,
        .DepthStencilState = dsDesc,
        .InputLayout = {
            .pInputElementDescs = inputLayout.data(),
            .NumElements = static_cast<UINT>(inputLayout.size()),
        },
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = pipelineCreateInfo.renderTargetCount,
        .DSVFormat = TextureFormatToDxgiFormat(pipelineCreateInfo.depthStencilFormat),
        .SampleDesc = {
            .Count = pipelineCreateInfo.sampleCount > 0 ? pipelineCreateInfo.sampleCount : 1,
            .Quality = 0,
        },
    };
    for (UINT i = 0; i < pipelineCreateInfo.renderTargetCount && i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        psoDesc.RTVFormats[i] = TextureFormatToDxgiFormat(pipelineCreateInfo.renderTargetFormats[i]);

    auto pipeline = std::make_unique<D3D12Pipeline>();
    DX_CHECK(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipeline->pso)));

    return pipeline.release();
}

void D3D12Device::UploadBufferData(Buffer *buffer, const void *data, size_t size) {
    if (!buffer || !data || size == 0) return;

    D3D12Buffer *d3d12Buffer = static_cast<D3D12Buffer *>(buffer);

    // Allocate from upload buffer
    auto allocation = m_uploadAllocator->Allocate(size, 256);
    memcpy(allocation.cpuAddress, data, size);

    uint64_t completedValue = m_uploadCommandQueue->GetCompletedFenceValue();
    ComPtr<ID3D12CommandAllocator> allocator = m_directAllocatorPool->RequestAllocator(completedValue);

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    DX_CHECK(m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList)
    ));

    // Transition buffer to copy dest if needed
    m_stateTracker.TransitionResource(cmdList.Get(), d3d12Buffer->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    m_stateTracker.FlushBarriers(cmdList.Get());

    // Copy data
    cmdList->CopyBufferRegion(d3d12Buffer->resource.Get(), 0, allocation.resource, allocation.offset, size);

    // Transition back to target state
    D3D12_RESOURCE_STATES targetState = BufferUsageToResourceState(d3d12Buffer->usage);
    m_stateTracker.TransitionResource(cmdList.Get(), d3d12Buffer->resource.Get(), targetState);
    m_stateTracker.FlushBarriers(cmdList.Get());

    DX_CHECK(cmdList->Close());
    ID3D12CommandList *lists[] = {cmdList.Get()};
    m_uploadCommandQueue->m_commandQueue->ExecuteCommandLists(1, lists);

    m_uploadCommandQueue->Signal(completedValue);

    // Store for later synchronization
    m_pendingUploads.push({completedValue, allocator});
}

void D3D12Device::UploadTextureData(Texture *texture, const void *data, size_t size) {
    if (!texture || !data || size == 0) return;

    D3D12Texture *d3d12Texture = static_cast<D3D12Texture *>(texture);

    // Get resource description
    D3D12_RESOURCE_DESC desc = d3d12Texture->resource->GetDesc();

    // Calculate layout info
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;

    m_device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &numRows, &rowSizeInBytes, &totalBytes);

    // Allocate from upload buffer
    auto allocation = m_uploadAllocator->Allocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    // Copy row by row
    const uint8_t *srcData = static_cast<const uint8_t *>(data);
    uint8_t *dstData = static_cast<uint8_t *>(allocation.cpuAddress);

    for (UINT row = 0; row < numRows; ++row) {
        memcpy(dstData + row * layout.Footprint.RowPitch,
               srcData + row * rowSizeInBytes,
               rowSizeInBytes);
    }

    uint64_t completedValue = m_uploadCommandQueue->GetCompletedFenceValue();
    ComPtr<ID3D12CommandAllocator> allocator = m_directAllocatorPool->RequestAllocator(completedValue);

    ComPtr<ID3D12GraphicsCommandList> cmdList;
    DX_CHECK(m_device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(),
        nullptr,
        IID_PPV_ARGS(&cmdList)
    ));


    // Transition texture to copy dest
    m_stateTracker.TransitionResource(cmdList.Get(), d3d12Texture->resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    m_stateTracker.FlushBarriers(cmdList.Get());

    // Copy texture data
    D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
    srcLocation.pResource = allocation.resource;
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint = layout;
    srcLocation.PlacedFootprint.Offset = allocation.offset;

    D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
    dstLocation.pResource = d3d12Texture->resource.Get();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = 0;

    cmdList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

    // Transition texture to target state
    D3D12_RESOURCE_STATES targetState = TextureUsageToResourceState(d3d12Texture->usage);
    m_stateTracker.TransitionResource(cmdList.Get(), d3d12Texture->resource.Get(), targetState);
    m_stateTracker.FlushBarriers(cmdList.Get());

    DX_CHECK(cmdList->Close());
    ID3D12CommandList *lists[] = {cmdList.Get()};
    m_uploadCommandQueue->m_commandQueue->ExecuteCommandLists(1, lists);

    m_uploadCommandQueue->Signal(completedValue);

    // Store for later synchronization
    m_pendingUploads.push({completedValue, allocator});
}

void D3D12Device::FlushUploads() {
    if (m_pendingUploads.empty()) return;

    // Wait for all uploads to complete
    auto &last = m_pendingUploads.back();
    m_uploadCommandQueue->WaitForFence(last.fenceValue);

    // Return allocators to pool
    while (!m_pendingUploads.empty()) {
        auto &entry = m_pendingUploads.front();
        m_directAllocatorPool->DiscardAllocator(entry.fenceValue, entry.allocator);
        m_pendingUploads.pop();
    }

    // Reset upload allocator
    m_uploadAllocator->Reset();
}

ComPtr<ID3DBlob> D3D12Device::GetShaderBlob(const Shader &shader) {
    std::ifstream file(shader.filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not find Shader");;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

    ComPtr<ID3DBlob> shaderBlob, errorBlob;

    UINT compileFlags = 0;
#ifdef _DEBUG
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#else
    compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
#endif

    HRESULT hr = D3DCompile(
        source.data(),
        source.size(),
        (LPCSTR) shader.filepath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        shader.entry.c_str(),
        ShaderTargetToString(shader.stage).c_str(),
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA(static_cast<const char *>(errorBlob->GetBufferPointer()));
        }
        throw std::runtime_error("Shader compilation failed");;
    }

    return shaderBlob;
}

std::string D3D12Device::ShaderTargetToString(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return "vs_5_1";
        case ShaderStage::Pixel: return "ps_5_1";
        case ShaderStage::Compute: return "cs_5_1";
        case ShaderStage::Geometry: return "gs_5_1";
        case ShaderStage::Hull: return "hs_5_1";
        case ShaderStage::Domain: return "ds_5_1";
        default: return "";
    }
}

DXGI_FORMAT D3D12Device::TextureFormatToDxgiFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::RGBA32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case TextureFormat::RGB32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
        case TextureFormat::RG32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
        case TextureFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case TextureFormat::RGBA16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case TextureFormat::RG16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
        case TextureFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
        case TextureFormat::RGBA8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::Depth32: return DXGI_FORMAT_D32_FLOAT;
        case TextureFormat::Depth24Stencil8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

D3D12_RESOURCE_STATES D3D12Device::BufferUsageToResourceState(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::Vertex: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case BufferUsage::Index: return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        case BufferUsage::Uniform: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case BufferUsage::Storage: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case BufferUsage::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

D3D12_RESOURCE_STATES D3D12Device::TextureUsageToResourceState(TextureUsage usage) {
    switch (usage) {
        case TextureUsage::ShaderResource: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        case TextureUsage::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case TextureUsage::DepthStencil: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case TextureUsage::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        default: return D3D12_RESOURCE_STATE_COMMON;
    }
}

D3D12_COMMAND_LIST_TYPE D3D12Device::GetD3D12CommandListType(QueueType type) {
    switch (type) {
        case QueueType::Graphics:
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case QueueType::Compute:
            return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case QueueType::Transfer:
            return D3D12_COMMAND_LIST_TYPE_COPY;
        default:
            throw std::runtime_error("Unknown queue type");
    }
}

const wchar_t *D3D12Device::GetQueueTypeName(QueueType type) {
    switch (type) {
        case QueueType::Graphics: return L"Graphics";
        case QueueType::Compute: return L"Compute";
        case QueueType::Transfer: return L"Transfer";
        default: return L"Unknown";
    }
}
