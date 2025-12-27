//
// Created by 2401Lucas on 2025-10-30.
//
#include "Renderer.h"
#include "RenderGraph/RenderGraph.h"
#include "RenderGraph/RenderPass.h"
#include "RHI/Device.h"
#include "../OS/Window/Window.h"
#include <algorithm>
#include <stdexcept>
#include <chrono>

Renderer::Renderer(Window *window, Device *device, ResourceManager *resourceManager)
    : m_window(window), m_device(device), m_resourceManager(resourceManager)
      , m_isFrameStarted(false) {
    m_width = m_window->GetWidth();
    m_height = m_window->GetHeight();

    CommandQueueCreateInfo graphicsQueueCI = {
        .type = QueueType::Graphics,
        .debugName = "Main Graphics Queue"
    };
    m_graphicsQueue = std::unique_ptr<CommandQueue>(m_device->CreateCommandQueue(graphicsQueueCI));

    m_swapchain = std::unique_ptr<Swapchain>(
        m_device->CreateSwapchain(window->GetHwnd(), m_graphicsQueue.get(),
                                  window->GetWidth(), window->GetHeight()));

    m_renderGraph = std::make_unique<RenderGraph>(m_device, m_graphicsQueue.get(), FrameCount);

    CreateFrameResources();

    PipelineCreateInfo pipelineCI{
        .vertexShader = {
            .filepath = "assets/shaders/shaders.hlsl",
            .entry = "VSMain",
            .stage = ShaderStage::Vertex
        },
        .pixelShader = {
            .filepath = "assets/shaders/shaders.hlsl",
            .entry = "PSMain",
            .stage = ShaderStage::Pixel
        },
        .vertexAttributes = {
            {"POSITION", 0, TextureFormat::RGB32_FLOAT, 0, 0},
            {"NORMAL", 0, TextureFormat::RGB32_FLOAT, 1, 0},
            {"TEXCOORD", 0, TextureFormat::RG32_FLOAT, 2, 0},
            {"TANGENT", 0, TextureFormat::RGB32_FLOAT, 3, 0},
        },
        .vertexBuffers = {
            {.binding = 0, .stride = sizeof(VPosition), .rate = InputRate::PerVertex},
            {.binding = 1, .stride = sizeof(VNormal), .rate = InputRate::PerVertex},
            {.binding = 2, .stride = sizeof(VTexCoord), .rate = InputRate::PerVertex},
            {.binding = 3, .stride = sizeof(VTangent), .rate = InputRate::PerVertex},
        },
        .cullMode = CullMode::Back,
        .wireframe = false,
        .sampleCount = 1,
        .topology = PrimitiveTopology::TriangleList,
        .depthTestEnable = true,
        .depthWriteEnable = true,
        .depthFunc = CompareFunc::Less,
        .blendMode = BlendMode::None,
        .renderTargetFormats = {TextureFormat::RGBA8_UNORM},
        .renderTargetCount = 1,
        .depthStencilFormat = TextureFormat::Depth32,
        .dynamicViewport = true,
        .dynamicScissor = true,
        .debugName = "MainPipeline",
    };

    m_mainPipeline = std::unique_ptr<Pipeline>(m_device->CreatePipeline(pipelineCI));

    m_defaultTexture = m_resourceManager->LoadTexture("assets/uv-test.png");

    //TODO: Sync buffer creation on device
    // Wait for Buffer to be created, this should be signaled by the device.
    // Maybe a work queue that clears once all operations are completed.
    WaitForGPU();
}

Renderer::~Renderer() {
    WaitForGPU();

    if (m_renderGraph) {
        m_renderGraph->Flush();
    }
}

void Renderer::CreateFrameResources() {
    for (uint32_t i = 0; i < FrameCount; ++i) {
        BufferCreateInfo perFrameBufferCI = {
            .size = sizeof(PerFrameData),
            .usage = BufferUsage::Uniform,
            .memoryType = MemoryType::Upload,
            .debugName = "generalBuffer_" + std::to_string(i),
        };
        m_frameResources[i].generalBuffer = std::unique_ptr<Buffer>(
            m_device->CreateBuffer(perFrameBufferCI));

        BufferCreateInfo transformBufferCI = {
            .size = sizeof(GPUTransform) * MAX_OBJECTS_PER_FRAME,
            // Support up to 256 objects per frame (limited by uniform max size of 65536 bytes)
            .usage = BufferUsage::Uniform,
            .memoryType = MemoryType::Upload,
            .debugName = "transformBuffer_" + std::to_string(i),
        };
        m_frameResources[i].transformBuffer = std::unique_ptr<Buffer>(
            m_device->CreateBuffer(transformBufferCI));

        BufferCreateInfo meshDataBufferCI = {
            .size = sizeof(MeshData) * MAX_OBJECTS_PER_FRAME,
            .usage = BufferUsage::Uniform,
            .memoryType = MemoryType::Upload,
            .debugName = "meshDataBuffer_" + std::to_string(i),
        };
        m_frameResources[i].meshDataBuffer = std::unique_ptr<Buffer>(
            m_device->CreateBuffer(meshDataBufferCI));

        BufferCreateInfo materialBufferCI = {
            .size = sizeof(Material) * MAX_OBJECTS_PER_FRAME,
            .usage = BufferUsage::Uniform,
            .memoryType = MemoryType::Upload,
            .debugName = "materialBuffer_" + std::to_string(i),
        };
        m_frameResources[i].materialBuffer = std::unique_ptr<Buffer>(
            m_device->CreateBuffer(materialBufferCI));

        BufferCreateInfo instanceBuffer = {
          .size = sizeof(InstanceData) * MAX_OBJECTS_PER_FRAME,
          .usage = BufferUsage::Uniform,
          .memoryType = MemoryType::Upload,
          .debugName = "instanceBuffer_" + std::to_string(i),
        };
        m_frameResources[i].instanceBuffer = std::unique_ptr<Buffer>(
            m_device->CreateBuffer(instanceBuffer));

        //  Persistent map
        m_frameResources[i].generalBuffer->Map();
        m_frameResources[i].transformBuffer->Map();
        m_frameResources[i].meshDataBuffer->Map();
        m_frameResources[i].materialBuffer->Map();
        m_frameResources[i].instanceBuffer->Map();
    }
}

void Renderer::Update(float deltaTime) {
    m_totalTime += deltaTime;
}

void Renderer::BeginFrame() {
    if (m_isFrameStarted) {
        throw std::runtime_error("BeginFrame called twice without EndFrame");
    }

    // Wait for the frame we're about to reuse
    uint32_t nextFrameIndex = (m_frameIndex + 1) % FrameCount;
    uint64_t fenceValueToWaitFor = m_frameResources[nextFrameIndex].fenceValue;
    if (fenceValueToWaitFor > 0) {
        m_graphicsQueue->WaitForFence(fenceValueToWaitFor);
    }

    m_graphicsQueue->BeginFrame(nextFrameIndex);
    m_renderGraph->NextFrame();
    m_frameIndex = nextFrameIndex;

    m_isFrameStarted = true;
    m_submissions.clear();
    m_batches.clear();
    m_statistics = Statistics{};
    m_objectIDCounter = 0;
}

void Renderer::EndFrame() {
    if (!m_isFrameStarted) {
        throw std::runtime_error("EndFrame called without BeginFrame");
    }

    // Update per-frame data before building render graph
    UpdatePerFrameData();
    ProcessSubmissions();
    BuildRenderGraph();
    CommandList *commandList = m_renderGraph->Execute();

    // Submit to queue with fence
    m_currentFenceValue++;
    m_graphicsQueue->Execute(commandList);
    m_graphicsQueue->Signal(m_currentFenceValue);

    // Track fence value for this frame
    m_frameResources[m_frameIndex].fenceValue = m_currentFenceValue;

    m_swapchain->Present(m_window->IsVSync());

    m_isFrameStarted = false;
}

void Renderer::SetTransforms(const std::vector<glm::mat4> &transforms) {

}

void Renderer::Submit(const RenderInfo &info) {
    m_submissions.push_back(info);
}

void Renderer::Submit(const std::vector<RenderInfo> &infos) {
    m_submissions.insert(m_submissions.end(), infos.begin(), infos.end());
}

void Renderer::SetCamera(Camera &camera) {
    m_camera = &camera;
}

void Renderer::SetDirectionalLight(const glm::vec3 &direction, const glm::vec3 &color, float intensity) {
    m_directionalLight.direction = glm::normalize(direction);
    m_directionalLight.color = color;
    m_directionalLight.intensity = intensity;
}

void Renderer::Resize() {
    WaitForGPU();
    m_renderGraph->Flush();
    m_width = m_window->GetWidth();
    m_height = m_window->GetHeight();
    m_camera->SetAspectRatio(m_window->GetAspectRatio());
    m_swapchain->Resize(m_width, m_height);
}

void Renderer::UpdatePerFrameData() {
    if (!m_camera) return;

    PerFrameData frameData = {};
    frameData.viewProjection = m_camera->GetPerspective() * m_camera->GetViewMatrix();
    frameData.cameraPosition = m_camera->GetTransform().GetPosition();
    frameData.time = m_totalTime;
    frameData.lightDirection = m_directionalLight.direction;
    frameData.lightIntensity = m_directionalLight.intensity;
    frameData.lightColor = m_directionalLight.color;
    frameData.frameIndex = m_frameIndex;

    auto &frameResources = GetCurrentFrameResources();
    void *mappedData = frameResources.generalBuffer->GetMappedPtr();
    if (mappedData) {
        memcpy(mappedData, &frameData, sizeof(PerFrameData));
    }



}

// Per object data is limited to 256 models because it is maxing out the size limit of 65536 bytes in a uniform buffer.
// This is because the object data is sending too much information. If I could only pass 1 index to access this information in a global storage buffer,
// this would increase the maximum number of models to 16384
// TODO: Implement solution based on above observation
void Renderer::UpdatePerObjectData(Transform &transform, Material *material, uint32_t objectID) {
    PerObjectData objectData = {};
    objectData.worldMatrix = transform.GetTransformMat();

    // Calculate normal matrix (inverse transpose of world matrix's 3x3 part)
    glm::mat3 normalMatrix3 = glm::transpose(glm::inverse(glm::mat3(objectData.worldMatrix)));
    objectData.normalMatrix = glm::mat4(normalMatrix3);

    // Get bindless texture indices from material
    if (material) {
        // Bindless indices are cached when material textures are set
        objectData.albedoTextureIndex = material->albedoBindlessIndex;
        objectData.normalTextureIndex = material->normalBindlessIndex;
        objectData.metallicRoughnessIndex = material->metallicRoughnessBindlessIndex;
        objectData.emissiveTextureIndex = material->emissiveBindlessIndex;

        // Material factors
        auto &properties = material->properties;
        objectData.albedoFactor = glm::vec4(properties.baseColor[0]);
        objectData.metallicFactor = 0;
        objectData.roughnessFactor = 0;
    } else {
        // Use default texture
        Texture *defaultTex = m_resourceManager->GetTexture(m_defaultTexture);
        objectData.albedoTextureIndex = defaultTex ? defaultTex->GetBindlessIndex() : 0;
        objectData.normalTextureIndex = 0;
        objectData.metallicRoughnessIndex = 0;
        objectData.emissiveTextureIndex = 0;
        objectData.albedoFactor = glm::vec4(1.0f);
        objectData.metallicFactor = 0.5f;
        objectData.roughnessFactor = 0.5f;
    }

    objectData.objectID = objectID;

    // Upload to current frame's per-object buffer
    auto &frameResources = GetCurrentFrameResources();
    char *mappedData = (char *) frameResources.perObjectBuffer->GetMappedPtr();
    if (mappedData) {
        memcpy(mappedData + objectID * sizeof(PerObjectData), &objectData, sizeof(PerObjectData));
    }
}

void Renderer::ProcessSubmissions() {
    if (m_submissions.empty()) {
        return;
    }

    CalculateSortKeys();
    SortSubmissions();
    BatchSubmissions();
}

void Renderer::CalculateSortKeys() {
    if (!m_camera) return;

    glm::vec3 cameraPos = m_camera->GetTransform().GetPosition();

    for (auto &submission: m_submissions) {
        // Calculate distance to camera
        glm::vec3 objectPos = submission.transform.GetPosition();
        float distance = glm::length(objectPos - cameraPos);
        submission.distanceToCamera = distance;

        // TODO: Create material ID's for sorting
        // Create sort key (material ID << 16 | depth)
        uint32_t materialID = 0; //submission.material ? /*submission.material->GetID()*/0 : 0;
        uint16_t depthKey = static_cast<uint16_t>(glm::clamp(distance * 10.0f, 0.0f, 65535.0f));

        if (submission.isTransparent) {
            // Transparent objects sort back-to-front
            submission.sortKey = (1u << 31) | (materialID << 16) | (0xFFFF - depthKey);
        } else {
            // Opaque objects sort front-to-back
            submission.sortKey = (0u << 31) | (materialID << 16) | depthKey;
        }
    }
}

void Renderer::SortSubmissions() {
    std::sort(m_submissions.begin(), m_submissions.end(),
              [](const RenderInfo &a, const RenderInfo &b) {
                  return a.sortKey < b.sortKey;
              });
}

// TODO: Batch instanced
void Renderer::BatchSubmissions() {
    m_batches.clear();

    if (m_submissions.empty()) {
        return;
    }

    for (size_t i = 0; i < m_submissions.size(); i++) {
        const auto &submission = m_submissions[i];
        auto currentBatch = RenderBatch{};
        currentBatch.mesh = m_resourceManager->GetMesh(submission.mesh);
        currentBatch.material = m_resourceManager->GetMaterial(submission.material);
        currentBatch.castsShadows = submission.castsShadows;
        currentBatch.transforms.push_back(submission.transform);
        m_batches.push_back(currentBatch);
    }

    m_statistics.drawCalls = static_cast<uint32_t>(m_submissions.size());
    m_statistics.instancedDrawCalls = static_cast<uint32_t>(m_batches.size());

    uint32_t totalInstances = 0;
    for (const auto &batch: m_batches) {
        totalInstances += static_cast<uint32_t>(batch.transforms.size());
    }
    m_statistics.instanceCount = totalInstances;
}

void Renderer::BuildRenderGraph() {
    m_renderGraph->Clear();

    // Register backbuffer as external resource
    Texture *backbuffer = m_swapchain->GetSwapchainBuffer(m_frameIndex);
    m_renderGraph->RegisterExternalTexture("Backbuffer", backbuffer, TextureUsage::Present);
    m_renderGraph->SetPresentTarget("Backbuffer");

    // Shadow pass (if enabled)
    if (m_shadowsEnabled && !m_batches.empty()) {
        // auto shadowPass = RenderPassBuilder("Shadow")
        //         .WriteTexture("ShadowMap", m_shadowMapSize, m_shadowMapSize,
        //                       RenderPassResource::Format::Depth32,
        //                       TextureUsage::DepthStencil)
        //         .Execute([this](RenderPassContext &ctx) {
        //             RenderShadows(ctx);
        //         })
        //         .Build();

        // m_renderGraph->AddPass(std::move(shadowPass));
    }

    // Main geometry pass
    auto mainPass = RenderPassBuilder("Main")
            // .ReadTexture("ShadowMap", TextureUsage::ShaderResource, PipelineStage::PixelShader)
            .WriteTexture("Backbuffer", m_width, m_height,
                          RenderPassResource::Format::RGBA16F,
                          TextureUsage::RenderTarget, PipelineStage::RenderTarget)
            .WriteTexture("SceneDepth", m_width, m_height,
                          RenderPassResource::Format::Depth32,
                          TextureUsage::DepthStencil, PipelineStage::DepthStencil)
            .Execute([this](RenderPassContext &ctx) {
                RenderMain(ctx);
            })
            .Build();

    m_renderGraph->AddPass(std::move(mainPass));

    // Post-process (if enabled)
    if (m_postProcessingEnabled) {
        // auto postPass = RenderPassBuilder("PostProcess")
        //         .ReadTexture("SceneColor", TextureUsage::ShaderResource)
        //         .WriteTexture("FinalColor", width, height,
        //                       RenderPassResource::Format::RGBA8,
        //                       TextureUsage::RenderTarget)
        //         .Execute([this](RenderPassContext &ctx) {
        //             RenderPostProcess(ctx);
        //         })
        //         .Build();

        // m_renderGraph->AddPass(std::move(postPass));
    }
}

void Renderer::RenderShadows(RenderPassContext &ctx) {
    // TODO: Implement shadow rendering
    for (const auto &batch: m_batches) {
        if (!batch.castsShadows) continue;

        if (batch.transforms.size() == 1) {
            // Single draw
        } else {
            // Instanced draw
        }
    }
}

void Renderer::RenderMain(RenderPassContext &ctx) {
    // Clear
    const float clearColor[4] = {0.1f, 0.1f, 0.15f, 1.0f};
    // auto renderTarget = ctx.GetTexture("Backbuffer");
    // auto depthTarget = ctx.GetTexture("SceneDepth");
    // Todo: Implement GetTexture in RenderPassContext
    auto renderTarget = ctx.outputTextures[0];
    auto depthTarget = ctx.outputTextures[1];

    ctx.commandList->ClearRenderTarget(renderTarget, clearColor);
    ctx.commandList->ClearDepthStencil(depthTarget, 1.0f, 0);
    ctx.commandList->SetRenderTarget(renderTarget, depthTarget);

    // Set viewport and scissor
    Viewport vp = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(m_window->GetWidth()),
        .height = static_cast<float>(m_window->GetHeight()),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    ctx.commandList->SetViewport(vp);

    Rect scissor = {
        .left = 0,
        .top = 0,
        .right = static_cast<int32_t>(m_window->GetWidth()),
        .bottom = static_cast<int32_t>(m_window->GetHeight())
    };
    ctx.commandList->SetScissor(scissor);

    // Set pipeline
    ctx.commandList->SetPipeline(m_mainPipeline.get());
    ctx.commandList->SetPrimitiveTopology(PrimitiveTopology::TriangleList);

    // Bind per-frame data (root parameter 4)
    auto &frameResources = GetCurrentFrameResources();
    ctx.commandList->SetConstantBuffer(frameResources.generalBuffer.get(), 4, 0);

    // Set vertex and index buffers
    auto buffers = m_resourceManager->GetRenderBuffers();
    ctx.commandList->SetVertexBuffers({buffers.position, buffers.normal, buffers.texCoord, buffers.tangent},
                                      {0, 1, 2, 3});
    ctx.commandList->SetIndexBuffer(buffers.index);

    // Render all batches
    for (auto &batch: m_batches) {
        if (!batch.mesh) continue;
        ctx.commandList->DrawIndexedInstanced(batch.mesh->indexCount, batch.mesh->firstIndex,
                                                         batch.mesh->instanceCount, batch.mesh->firstInstance);
    }
}

void Renderer::RenderParticles(RenderPassContext &ctx) {
    // TODO: Implement particle rendering
}

void Renderer::RenderPostProcess(RenderPassContext &ctx) {
    // TODO: Implement post-processing
}

void Renderer::RenderUI(RenderPassContext &ctx) {
    // TODO: Implement UI rendering
}

void Renderer::WaitForGPU() {
    if (m_graphicsQueue) {
        m_graphicsQueue->WaitIdle();
    }
}
