//
// Created by 2401Lucas on 2025-10-30.
//

#include "ResourceManager.h"

#include "TextureLoader.h"

ResourceManager::ResourceManager(Device *device, EventSystem *eventSystem) : m_device(device),
                                                                             m_eventSystem(eventSystem),
                                                                             m_gpuMemoryUsed(0) {
    m_gpuMemorySize = device->GetVideoMemoryBudget();

    CreateDefaults();
    CreateVertexBuffers();
}

ResourceManager::~ResourceManager() {
    m_meshPool.Clear();
}

ResourcePoolState ResourceManager::GetResourceState(MeshHandle handle) {
    return m_meshPool.GetState(handle);
}

ResourcePoolState ResourceManager::GetResourceState(TextureHandle handle) {
    return m_texturePool.GetState(handle);
}

bool ResourceManager::IsLoaded(MeshHandle handle) {
    return GetResourceState(handle) == ResourcePoolState::Loaded;
}

bool ResourceManager::IsLoaded(TextureHandle handle) {
    return GetResourceState(handle) == ResourcePoolState::Loaded;
}

std::vector<MeshHandle> ResourceManager::LoadMesh(const std::string &path, bool loadMaterial) {
    MeshHandle existingHandle = m_meshPool.FindByPath(path);

    if (existingHandle.IsValid()) {
        m_meshPool.AddRef(existingHandle);
        return {existingHandle};
    }

    try {
        std::vector<MeshHandle> handles;
        MeshData meshData = MeshLoader::LoadFromFile(path, loadMaterial);
        std::vector<MaterialHandle> newMaterials;
        for (auto &material: meshData.materials) {
            newMaterials.push_back(LoadMaterial(material));
        }

        // Build submesh draw command information
        // Global offset is the offset in the current GPU buffer, local offset is the offset into the new data being loaded into the GPU.
        // This allows for all mesh data to be uploaded together and using the sum of the global and local offsets.
        for (auto &submesh: meshData.subMeshes) {
            auto mesh = std::make_unique<Mesh>(submesh.indexCount, 1, m_currentIndexOffset + submesh.indexOffset,
                                               m_currentVertexOffset + submesh.vertexOffset, 0, 0,
                                               newMaterials[submesh.materialIndex]);
            handles.push_back(m_meshPool.Add(path, std::move(mesh)));
        }

        // TODO:
        //      - Manage buffer size to prevent overflow
        //      - Allow the creation of additional buffers if current buffers are full
        //      - Memory fragmentation
        //      - Static buffers that are only fully cleared
        // Performs a bulk upload of the vertex data of each loaded model
        m_device->UploadBufferData(m_vPositionBuf.get(), meshData.positions.data(),
                                   meshData.positions.size() * sizeof(VPosition));
        m_device->UploadBufferData(m_vNormalBuf.get(), meshData.normals.data(),
                                   meshData.normals.size() * sizeof(VNormal));
        m_device->UploadBufferData(m_vTexCoordBuf.get(), meshData.texCoords.data(),
                                   meshData.texCoords.size() * sizeof(VTexCoord));
        m_device->UploadBufferData(m_vTangentBuf.get(), meshData.tangents.data(),
                                   meshData.tangents.size() * sizeof(VTangent));

        m_currentIndexOffset += meshData.indices.size();
        m_currentVertexOffset += meshData.positions.size();

        return handles;
    } catch (const std::exception &e) {
        printf("Failed to load mesh: ");
        printf(path.c_str());
        printf(" - ");
        printf(e.what());
        printf("\n");
        return {};
    }
}

TextureHandle ResourceManager::LoadTexture(const std::string &path, bool forceTexture) {
    TextureHandle existingHandle = m_texturePool.FindByPath(path);

    if (existingHandle.IsValid()) {
        m_texturePool.AddRef(existingHandle);
        return existingHandle;
    }

    try {
        TextureData textureData = TextureLoader::LoadFromFile(path);
        TextureCreateInfo textureCI{
            .width = textureData.width,
            .height = textureData.height,
            .depth = textureData.depth,
            .mipLevels = static_cast<uint16_t>(textureData.mipLevels),
            .arraySize = 1,
            .format = textureData.format,
            .usage = TextureUsage::ShaderResource
        };
        std::unique_ptr<Texture> texture(m_device->CreateTexture(textureCI));
        m_device->UploadTextureData(texture.get(), textureData.data.data(),
                                    textureData.data.size() * sizeof(uint8_t));
        TextureHandle handle = m_texturePool.Add(path, std::move(texture));
        return handle;
    } catch (const std::exception &e) {
        printf("Failed to load texture: ");
        printf(path.c_str());
        printf(" - ");
        printf(e.what());
        printf("\n");
        if (forceTexture)
            return m_defaultTextureHandle;
        return {};
    }
}

// Texture Pool needs a flag to specify if it is loaded by a material, in which case they need to not be deleted based on last access time.
// TODO: Bindless Indices are influenced by defragmentation, how should this be handled?
// I think holding both Texture Handle & caching the bindless index is the solution, with it being updated when defragmenting...?
MaterialHandle ResourceManager::LoadMaterial(const MaterialData &mat) {
    MaterialHandle existingHandle = m_materialPool.FindByPath(mat.name);

    if (existingHandle.IsValid()) {
        m_materialPool.AddRef(existingHandle);
        return existingHandle;
    }

    std::unique_ptr<Material> material = std::make_unique<Material>();

    auto albedo = LoadTexture(mat.albedo, true);
    material->albedoTexture = albedo;
    material->albedoBindlessIndex = m_texturePool.Get(albedo)->GetBindlessIndex();

    if (!mat.normal.empty()) {
        auto normal = LoadTexture(mat.normal);
        material->normalTexture = normal;
        material->normalBindlessIndex = m_texturePool.Get(normal)->GetBindlessIndex();
    }
    if (!mat.metallicRough.empty()) {
        auto metallicRough = LoadTexture(mat.metallicRough);
        material->metallicRoughnessTexture = metallicRough;
        material->metallicRoughnessBindlessIndex = m_texturePool.Get(metallicRough)->GetBindlessIndex();
    }
    if (!mat.emissive.empty()) {
        auto emissive = LoadTexture(mat.emissive);
        material->emissiveTexture = emissive;
        material->emissiveBindlessIndex = m_texturePool.Get(emissive)->GetBindlessIndex();
    }

    MaterialHandle handle = m_materialPool.Add(mat.name, std::move(material));
    return handle;
}

PipelineHandle ResourceManager::LoadPipeline(const PipelineCreateInfo &info) {
    PipelineHandle existingHandle = m_pipelinePool.FindByPath(info.debugName);

    if (existingHandle.IsValid()) {
        m_pipelinePool.AddRef(existingHandle);
        return existingHandle;
    }

    try {
        std::unique_ptr<Pipeline> shader(m_device->CreatePipeline(info));
        PipelineHandle handle = m_pipelinePool.Add(info.debugName, std::move(shader));
        return handle;
    } catch (const std::exception &e) {
        printf("Failed to load Pipeline: ");
        printf(info.debugName);
        printf(" - ");
        printf(e.what());
        printf("\n");
        return PipelineHandle{};
    }
}

void ResourceManager::UnloadMesh(MeshHandle handle) {
    m_meshPool.Remove(handle);
}

void ResourceManager::UnloadTexture(TextureHandle handle) {
    m_texturePool.Remove(handle);
}

void ResourceManager::UnloadMaterial(MaterialHandle handle) {
    auto res = m_materialPool.Get(handle);
    UnloadTexture(res->albedoTexture);
    UnloadTexture(res->normalTexture);
    UnloadTexture(res->emissiveTexture);
    UnloadTexture(res->emissiveTexture);

    m_materialPool.Remove(handle);
}

void ResourceManager::UnloadPipeline(PipelineHandle handle) {
    m_pipelinePool.Remove(handle);
}

void ResourceManager::UnloadAllTextures() {
    m_texturePool.Clear();
}

void ResourceManager::UnloadAllMeshes() {
    m_meshPool.Clear();
}

Mesh *ResourceManager::GetMesh(MeshHandle handle) {
    return m_meshPool.Get(handle);
}

Texture *ResourceManager::GetTexture(TextureHandle handle) {
    return m_texturePool.Get(handle);
}

Material *ResourceManager::GetMaterial(MaterialHandle handle) {
    return m_materialPool.Get(handle);
}

Pipeline *ResourceManager::GetPipeline(PipelineHandle handle) {
    return m_pipelinePool.Get(handle);
}

void ResourceManager::ReloadPipeline(PipelineHandle handle) {
}

void ResourceManager::ReloadTexture(TextureHandle handle) {
}

void ResourceManager::EnableHotReload(bool enable) {
}

void ResourceManager::AddRef(MeshHandle handle) {
    m_meshPool.AddRef(handle);
}

void ResourceManager::Release(MeshHandle handle) {
    if (m_meshPool.Release(handle)) {
        // Ref count reached 0, unload
        UnloadMesh(handle);
    }
}

uint64_t ResourceManager::GetTotalGPUMemoryUsed() const {
    return m_gpuMemoryUsed;
}

uint64_t ResourceManager::GetGPUMemoryBudget() const {
    return m_gpuMemorySize;
}

void ResourceManager::TrimMemory() {
}

void ResourceManager::Update() {
}

void ResourceManager::CreateDefaults() {
    TextureData textureData = TextureLoader::CreateCheckerboard(512, 512);
    TextureCreateInfo textureCI{
        .width = textureData.width,
        .height = textureData.height,
        .depth = textureData.depth,
        .mipLevels = static_cast<uint16_t>(textureData.mipLevels),
        .arraySize = 1,
        .format = textureData.format,
        .usage = TextureUsage::ShaderResource
    };
    std::unique_ptr<Texture> texture(m_device->CreateTexture(textureCI));
    m_device->UploadTextureData(texture.get(), textureData.data.data(),
                                textureData.data.size() * sizeof(uint8_t));
    m_defaultTextureHandle = m_texturePool.Add("default", std::move(texture));


    std::unique_ptr<Material> material = std::make_unique<Material>();
    material->albedoTexture = m_defaultTextureHandle;
    material->albedoBindlessIndex = m_texturePool.Get(m_defaultTextureHandle)->GetBindlessIndex();
    m_defaultMaterialHandle = m_materialPool.Add("default", std::move(material));
}

//TODO: Separate Vertex&Index creation, support creating multiple vertex buffers when they run out of space
void ResourceManager::CreateVertexBuffers() {
    BufferCreateInfo vPositionsBufCI{
        .size = sizeof(VPosition) * MAX_VERTICES,
        .stride = sizeof(VPosition),
        .usage = BufferUsage::Vertex,
        .memoryType = MemoryType::GPU,
        .debugName = "Vertex Positions Buffer"
    };
    m_vPositionBuf = std::unique_ptr<Buffer>(m_device->CreateBuffer(vPositionsBufCI));

    BufferCreateInfo vNormalsBufCI{
        .size = sizeof(VNormal) * MAX_VERTICES,
        .stride = sizeof(VNormal),
        .usage = BufferUsage::Vertex,
        .memoryType = MemoryType::GPU,
        .debugName = "Vertex Normals Buffer"
    };
    m_vNormalBuf = std::unique_ptr<Buffer>(m_device->CreateBuffer(vNormalsBufCI));

    BufferCreateInfo vTexCoordsBufCI{
        .size = sizeof(VTexCoord) * MAX_VERTICES,
        .stride = sizeof(VTexCoord),
        .usage = BufferUsage::Vertex,
        .memoryType = MemoryType::GPU,
        .debugName = "Vertex TexCoord Buffer"
    };
    m_vTexCoordBuf = std::unique_ptr<Buffer>(m_device->CreateBuffer(vTexCoordsBufCI));

    BufferCreateInfo vTangentBufCI{
        .size = sizeof(VTangent) * MAX_VERTICES,
        .stride = sizeof(VTangent),
        .usage = BufferUsage::Vertex,
        .memoryType = MemoryType::GPU,
        .debugName = "Vertex Tangent Buffer"
    };
    m_vTangentBuf = std::unique_ptr<Buffer>(m_device->CreateBuffer(vTangentBufCI));

    BufferCreateInfo vIndexBufCI{
        .size = sizeof(VIndex) * MAX_INDICES,
        .stride = sizeof(VIndex),
        .usage = BufferUsage::Index,
        .memoryType = MemoryType::GPU,
        .debugName = "Index Buffer"
    };
    m_vIndexBuf = std::unique_ptr<Buffer>(m_device->CreateBuffer(vIndexBufCI));
}
