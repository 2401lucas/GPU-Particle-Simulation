//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_RENDERER_H
#define GPU_PARTICLE_SIM_RENDERER_H
#define MAX_OBJECTS_PER_FRAME 256

#include <memory>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Resources/ResourceHandle.h"
#include "Resources/ResourceManager.h"

#include "Core/Transform.h"
#include "Core/Camera.h"
#include "Core/TransformSystem.h"
#include "RenderGraph/RenderGraph.h"
#include "OS/Window/Window.h"
#include "RHI/Device.h"
#include "RHI/Buffer.h"
#include "RHI/CommandList.h"
#include "RHI/CommandQueue.h"
#include "RHI/Pipeline.h"

struct alignas(256) PerFrameData {
    glm::mat4 viewProjection;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 cameraPosition;

    float time;
    uint32_t frameIndex;
    float nearPlane;
    float farPlane;

    // Padding to 256-byte alignment
    uint32_t padding[9];
};

static_assert(sizeof(PerFrameData) == 256);

// This gets sorted in batching process for uploading
// Vert/Frag
struct alignas(16) GPUInstance {
    uint32_t meshID;
    uint32_t materialID;
    int32_t transformID;
    uint32_t padding;
};

static_assert(sizeof(GPUInstance) == 16);

// Vert/Frag/Compute
struct alignas(64) GPUTransform {
    glm::mat4 worldMatrix;
    // glm::mat4 normalMatrix; // Test if Bandwidth constrained or Compute Costrained
};

static_assert(sizeof(GPUTransform) == 64);

// Vert/Frag/Compute
struct alignas(32) GPUMeshData {
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t vertexOffset;

    // Bindless Data
    uint32_t parentVertexBufferID;

    // TODO: Culling Data
    uint32_t padding[4];
};

static_assert(sizeof(GPUMeshData) == 32);

// Vert/Frag/Compute
struct alignas(32) GPUMaterial {
    uint32_t materialFlags; // Use to specify material types
    uint32_t albedoTextureIndex; // Could use more generic names like Tex1... as PBR uses different naming conventions
    uint32_t normalTextureIndex;
    uint32_t metallicRoughnessIndex;
    uint32_t emissiveTextureIndex;

    uint32_t padding[3];
};

static_assert(sizeof(GPUMaterial) == 32);

/// <summary>
/// Information submitted by the application for rendering
/// </summary>
struct RenderInfo {
    MeshHandle mesh;
    MaterialHandle material;
    TransformHandle transform;

    // Rendering flags
    bool castsShadows = true;
    bool receiveShadows = true;
    bool isTransparent = false;

    // For sorting
    float distanceToCamera = 0.0f;
    uint32_t sortKey = 0;
};

/// <summary>
/// Batched render command for instanced rendering
/// </summary>
struct RenderBatch {
    uint32_t instanceID;
    uint32_t instanceCount = 0;
    bool castsShadows = true;
};

class Renderer {
public:
    explicit Renderer(Window *window, Device *device, ResourceManager *);

    ~Renderer();

    void Update(float deltaTime);

    Device *GetDevice() { return m_device; }

    void BeginFrame();

    void EndFrame();

    // Submission API (Called by Applications)

    void SetTransforms(const std::vector<glm::mat4> &transforms);

    /// <summary>
    /// Submit an object for rendering this frame
    /// </summary>
    void Submit(const RenderInfo &info);

    /// <summary>
    /// Submit multiple objects at once
    /// </summary>
    void Submit(const std::vector<RenderInfo> &infos);

    /// <summary>
    /// Set the active camera for this frame
    /// </summary>
    void SetCamera(Camera *camera);

    /// <summary>
    /// Set directional light
    /// </summary>
    void SetDirectionalLight(const glm::vec3 &direction, const glm::vec3 &color, float intensity);

    void EnableShadows(bool enable) { m_shadowsEnabled = enable; }

    void SetShadowMapSize(uint32_t size) { m_shadowMapSize = size; }

    void EnablePostProcessing(bool enable) { m_postProcessingEnabled = enable; }

    struct Statistics {
        uint32_t drawCalls = 0;
        uint32_t triangles = 0;
        uint32_t instancedDrawCalls = 0;
        uint32_t instanceCount = 0;
        float cpuFrameTime = 0.0f;
        float gpuFrameTime = 0.0f;
    };

    const Statistics &GetStatistics() const { return m_statistics; }

    void Resize();

private:
    // External References
    Window *m_window;
    ResourceManager *m_resourceManager;
    Device *m_device;
    Camera *m_camera;

    // Core Resources
    struct FrameResources {
        uint64_t fenceValue = 0;
        std::unique_ptr<Buffer> generalBuffer;
        std::unique_ptr<Buffer> transformBuffer;
        std::unique_ptr<Buffer> meshDataBuffer;
        std::unique_ptr<Buffer> materialBuffer;
        std::unique_ptr<Buffer> instanceBuffer;
    };

    FrameResources m_frameResources[FrameCount];
    uint64_t m_currentFenceValue = 0;

    std::unique_ptr<RenderGraph> m_renderGraph;
    std::unique_ptr<Swapchain> m_swapchain;

    TextureHandle m_defaultTexture;
    TextureHandle m_defaultNormalMap;
    TextureHandle m_defaultMetallicRoughness;

    // Command infrastructure
    std::unique_ptr<CommandQueue> m_graphicsQueue = nullptr;
    std::unique_ptr<CommandQueue> m_computeQueue = nullptr;
    std::unique_ptr<CommandQueue> m_transferQueue = nullptr;

    // Pipelines
    std::unique_ptr<Pipeline> m_mainPipeline;

    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_frameIndex = 0;
    uint32_t m_objectIDCounter = 0;

    // Submission Data
    std::vector<RenderInfo> m_submissions;
    std::vector<RenderBatch> m_batches;

    std::vector<glm::mat4> m_transforms;
    std::vector<GPUMaterial> m_materials;
    std::vector<GPUMeshData> m_meshData;
    std::vector<GPUInstance> m_instances;

    // Configuration
    bool m_shadowsEnabled = false;
    uint32_t m_shadowMapSize = 2048;
    bool m_postProcessingEnabled = false;

    // Internal Resources
    bool m_isFrameStarted = false;
    Statistics m_statistics;
    float m_totalTime = 0.0f;

    // Frame resource management
    void CreateFrameResources();

    FrameResources &GetCurrentFrameResources() { return m_frameResources[m_frameIndex]; }

    // Submission processing
    void ProcessSubmissions();

    void SortSubmissions();

    void BatchSubmissions();

    void CalculateSortKeys();

    // RenderGraph setup
    void BuildRenderGraph();

    // TODO: Move
    // Rendering functions (passed to RenderGraph)
    void RenderShadows(RenderPassContext &ctx);

    void RenderMain(RenderPassContext &ctx);

    void RenderParticles(RenderPassContext &ctx);

    void RenderPostProcess(RenderPassContext &ctx);

    void RenderUI(RenderPassContext &ctx);

    // Helpers
    void UpdatePerFrameData();

    void UpdatePerDrawGroupData();

    void WaitForGPU();
};

#endif //GPU_PARTICLE_SIM_RENDERER_H
