//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_RESOURCEMANAGER_H
#define GPU_PARTICLE_SIM_RESOURCEMANAGER_H
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <glm/mat4x4.hpp>

#include "Material.h"
#include "Mesh.h"
#include "MeshLoader.h"
#include "ResourceHandle.h"
#include "Core/EventSystem.h"
#include "Rendering/RHI/Texture.h"
#include "Rendering/RHI/Device.h"

constexpr uint32_t MAX_VERTICES = 1000000;
constexpr uint32_t MAX_INDICES = 1000000;

enum ResourcePoolState {
    Unloaded, // Resource not loaded
    Loading, // Currently loading (async)
    Loaded, // Fully loaded and ready to use
    Failed // Loading failed
};

template<typename ResourceType, typename HandleType>
class ResourcePool {
public:
    struct ResourceEntry {
        std::string path;
        std::unique_ptr<ResourceType> resource;
        ResourcePoolState state = ResourcePoolState::Unloaded;
        uint32_t refCount = 0;
        uint32_t generation = 0;
        std::chrono::steady_clock::time_point lastAccessTime;
        uint64_t memorySize = 0;
    };

    HandleType Add(const std::string &path, std::unique_ptr<ResourceType> resource) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        HandleType handle{
            .id = m_nextId++,
            .generation = 0
        };

        ResourceEntry entry{
            .path = path,
            .resource = std::move(resource),
            .state = ResourcePoolState::Loaded,
            .refCount = 1,
            .generation = 0,
            .lastAccessTime = std::chrono::steady_clock::now()
        };

        m_resources[handle.id] = std::move(entry);
        m_pathToId[path] = handle.id;

        return handle;
    }

    HandleType CreatePlaceholder(const std::string &path) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        HandleType handle{
            .id = m_nextId++,
            .generation = 0
        };

        ResourceEntry entry{
            .path = path,
            .resource = nullptr,
            .state = ResourcePoolState::Loading,
            .refCount = 1,
            .generation = 0,
            .lastAccessTime = std::chrono::steady_clock::now()
        };

        m_resources[handle.id] = std::move(entry);
        m_pathToId[path] = handle.id;

        return handle;
    }

    void UpdatePlaceholder(HandleType handle, std::unique_ptr<ResourceType> resource) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it != m_resources.end() && it->second.generation == handle.generation) {
            it->second.resource = std::move(resource);
            it->second.state = ResourcePoolState::Loaded;
            it->second.lastAccessTime = std::chrono::steady_clock::now();
        }
    }

    ResourceType *Get(HandleType handle) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it == m_resources.end() || it->second.generation != handle.generation || it->second.state !=
            ResourcePoolState::Loaded) { return nullptr; }

        it->second.lastAccessTime = std::chrono::steady_clock::now();
        return it->second.resource.get();
    }

    ResourcePoolState GetState(HandleType handle) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it == m_resources.end()) return ResourcePoolState::Unloaded;
        if (it->second.generation != handle.generation) return ResourcePoolState::Unloaded;

        return it->second.state;
    }

    void Remove(HandleType handle) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it == m_resources.end()) { return; }
        m_pathToId.erase(it->second.path);
        m_resources.erase(it);
    }

    HandleType FindByPath(const std::string &path) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_pathToId.find(path);
        if (it != m_pathToId.end()) {
            auto resIt = m_resources.find(it->second);
            if (resIt != m_resources.end()) {
                HandleType handle{
                    .id = it->second,
                    .generation = resIt->second.generation
                };
            }
        }
        return HandleType{};
    }

    std::string GetPath(HandleType handle) const {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it != m_resources.end() && it->second.generation == handle.generation) {
            return it->second.path;
        }
        return "";
    }

    void AddRef(HandleType handle) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it != m_resources.end() && it->second.generation == handle.generation) {
            it->second.refCount++;
        }
    }

    bool Release(HandleType handle) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_resources.find(handle.id);
        if (it != m_resources.end() && it->second.generation == handle.generation) {
            if (it->second.refCount > 0) {
                it->second.refCount--;
                return it->second.refCount == 0;
            }
        }
        return false;
    }

    uint64_t GetTotalMemory() const {
        const std::lock_guard<std::mutex> lock(m_mutex);

        uint64_t total = 0;
        for (const auto &[id, entry]: m_resources) {
            if (entry.state == ResourcePoolState::Loaded) {
                total += entry.memorySize;
            }
        }
        return total;
    }

    std::vector<HandleType> GetLRUResources(uint64_t targetMemory) {
        const std::lock_guard<std::mutex> lock(m_mutex);

        // Collect all loaded resources with timestamps
        std::vector<std::pair<std::chrono::steady_clock::time_point, HandleType> > candidates;
        for (const auto &[id, entry]: m_resources) {
            if (entry.state == ResourcePoolState::Loaded && entry.refCount == 0) {
                HandleType handle;
                handle.id = id;
                handle.generation = entry.generation;
                candidates.push_back({entry.lastAccessTime, handle});
            }
        }

        // Sort by access time (oldest first)
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto &a, const auto &b) {
                      return a.first < b.first;
                  });

        // Collect handles until we've freed enough memory
        std::vector<HandleType> result;
        uint64_t freedMemory = 0;
        for (const auto &[time, handle]: candidates) {
            auto it = m_resources.find(handle.id);
            if (it != m_resources.end()) {
                result.push_back(handle);
                freedMemory += it->second.memorySize;

                if (freedMemory >= targetMemory) {
                    break;
                }
            }
        }

        return result;
    }

    void Clear() {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_resources.clear();
        m_pathToId.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, ResourceEntry> m_resources;
    std::unordered_map<std::string, uint64_t> m_pathToId;
    uint64_t m_nextId = 1;
};

//TODO Asset Streaming
class ResourceManager {
public:
    explicit ResourceManager(Device *device, EventSystem *eventSystem);

    ~ResourceManager();

    ResourceManager(const ResourceManager &) = delete;

    ResourceManager &operator=(const ResourceManager &) = delete;

    // Resource State Queries
    ResourcePoolState GetResourceState(MeshHandle);

    ResourcePoolState GetResourceState(TextureHandle);

    bool IsLoaded(MeshHandle);

    bool IsLoaded(TextureHandle);

    // Resource Creation

    ///<param name="loadMaterial"> Automatically loads a meshes associated material if one exists</param>
    std::vector<MeshHandle> LoadMesh(const std::string &path, bool loadMaterial = true);

    // Force Texture requires the returned texture handle to be valid by returning a default texture if no texture is found
    TextureHandle LoadTexture(const std::string &path, bool forceTexture = false);

    MaterialHandle LoadMaterial(const MaterialData &mat);

    PipelineHandle LoadPipeline(const PipelineCreateInfo &info);

    // Resource Deletion
    void UnloadMesh(MeshHandle);

    void UnloadTexture(TextureHandle);

    void UnloadMaterial(MaterialHandle);

    void UnloadPipeline(PipelineHandle);


    void UnloadAllMeshes();

    void UnloadAllTextures();

    // Resource Access
    Mesh *GetMesh(MeshHandle);

    Texture *GetTexture(TextureHandle);

    Material *GetMaterial(MaterialHandle);

    Pipeline *GetPipeline(PipelineHandle);

    // Hot Reloads
    void ReloadPipeline(PipelineHandle);

    void ReloadTexture(TextureHandle);

    void EnableHotReload(bool enable);


    // Reference Counting
    void AddRef(MeshHandle);

    void Release(MeshHandle);

    // Memory Management
    uint64_t GetTotalGPUMemoryUsed() const;

    uint64_t GetGPUMemoryBudget() const;

    void TrimMemory();

    void Update();

    struct MeshRenderBuffers {
        uint32_t numBuffers;
        Buffer *position;
        Buffer *normal;
        Buffer *texCoord;
        Buffer *tangent;
        Buffer *index;
    };

    MeshRenderBuffers GetRenderBuffers() {
        return {
            1,
            m_vPositionBuf.get(),
            m_vNormalBuf.get(),
            m_vTexCoordBuf.get(),
            m_vTangentBuf.get(),
            m_vIndexBuf.get()
        };
    }

private:
    Device *m_device;
    EventSystem *m_eventSystem;

    EventHandle m__onTransformUpdated;

    // Resource Pools
    ResourcePool<Mesh, MeshHandle> m_meshPool;
    ResourcePool<Texture, TextureHandle> m_texturePool;
    ResourcePool<Material, MaterialHandle> m_materialPool;
    ResourcePool<Pipeline, PipelineHandle> m_pipelinePool;

    TextureHandle m_defaultTextureHandle;

    uint32_t m_currentVertexOffset = 0;
    uint32_t m_currentIndexOffset = 0;

    std::unique_ptr<Buffer> m_vPositionBuf;
    std::unique_ptr<Buffer> m_vNormalBuf;
    std::unique_ptr<Buffer> m_vTexCoordBuf;
    std::unique_ptr<Buffer> m_vTangentBuf;
    std::unique_ptr<Buffer> m_vIndexBuf;

    // Hot reloading
    bool hotReloadEnabled = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps;

    // Memory
    uint64_t m_gpuMemorySize = 0;
    uint64_t m_gpuMemoryUsed = 0;

    void CreateDefaultTexture();

    void CreateVertexBuffers();
};


#endif //GPU_PARTICLE_SIM_RESOURCEMANAGER_H
