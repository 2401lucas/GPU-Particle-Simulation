//
// Created by 2401Lucas on 2025-11-29.
//

#ifndef GPU_PARTICLE_SIM_SCENE_H
#define GPU_PARTICLE_SIM_SCENE_H

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>

#include "Resources/ResourceHandle.h"
#include "TransformSystem.h"

constexpr uint32_t INVALID_HANDLE = UINT32_MAX;

// Sparse Set for O(1) entity->component lookup
template<typename ComponentType>
class SparseSet {
public:
    // Add component for entity, returns index in dense array
    uint32_t Add(EntityHandle entity, const ComponentType& component) {
        uint32_t denseIndex = static_cast<uint32_t>(dense.size());

        // Grow sparse array if needed
        if (entity >= static_cast<EntityHandle>(sparse.size())) {
            sparse.resize(entity + 1, INVALID_HANDLE);
        }

        sparse[entity] = denseIndex;
        dense.push_back(component);
        denseToEntity.push_back(entity);

        return denseIndex;
    }

    // Remove component for entity (swap-and-pop)
    void Remove(EntityHandle entity) {
        if (!Has(entity)) return;

        uint32_t denseIndex = sparse[entity];
        uint32_t lastIndex = static_cast<uint32_t>(dense.size()) - 1;

        // Swap with last element
        if (denseIndex != lastIndex) {
            dense[denseIndex] = dense[lastIndex];
            EntityHandle movedEntity = denseToEntity[lastIndex];
            denseToEntity[denseIndex] = movedEntity;
            sparse[movedEntity] = denseIndex;
        }

        // Remove last element
        dense.pop_back();
        denseToEntity.pop_back();
        sparse[entity] = INVALID_HANDLE;
    }

    // Check if entity has component
    bool Has(EntityHandle entity) const {
        return entity >= 0 &&
               entity < static_cast<EntityHandle>(sparse.size()) &&
               sparse[entity] != INVALID_HANDLE;
    }

    // Get component for entity (assumes entity has component)
    ComponentType& Get(EntityHandle entity) {
        assert(Has(entity));
        return dense[sparse[entity]];
    }

    const ComponentType& Get(EntityHandle entity) const {
        assert(Has(entity));
        return dense[sparse[entity]];
    }

    // Get dense index for entity (useful for parallel arrays)
    uint32_t GetIndex(EntityHandle entity) const {
        assert(Has(entity));
        return sparse[entity];
    }

    // Direct access to dense array (for iteration)
    std::vector<ComponentType>& GetDense() { return dense; }
    const std::vector<ComponentType>& GetDense() const { return dense; }

    // Get entity at dense index
    EntityHandle GetEntity(uint32_t denseIndex) const {
        assert(denseIndex < denseToEntity.size());
        return denseToEntity[denseIndex];
    }

    size_t Size() const { return dense.size(); }
    void Clear() {
        dense.clear();
        denseToEntity.clear();
        sparse.clear();
    }

private:
    std::vector<ComponentType> dense;        // Packed component data
    std::vector<EntityHandle> denseToEntity;     // Dense index -> Entity ID
    std::vector<uint32_t> sparse;            // Entity ID -> Dense index (or INVALID_HANDLE)
};

class Scene {
public:
    Scene() {
        nextEntityID = 0;
    }

    // --- Entity Management ---
    EntityHandle CreateEntity() {
        EntityHandle entity = nextEntityID++;
        entities.push_back(entity);
        return entity;
    }

    void DestroyEntity(EntityHandle entity) {
        // Remove all components
        if (meshes.Has(entity)) meshes.Remove(entity);
        if (materials.Has(entity)) materials.Remove(entity);
        if (transforms.Has(entity)) {
            TransformHandle transformID = transforms.Get(entity);
            // Note: TransformSystem doesn't support removal yet,
            // you'd need to add RemoveTransform() method
            transforms.Remove(entity);
        }

        // Remove from entities list
        auto it = std::find(entities.begin(), entities.end(), entity);
        if (it != entities.end()) {
            entities.erase(it);
        }
    }

    // --- Component Management ---

    // Add mesh component
    void AddMesh(EntityHandle entity, MeshHandle mesh) {
        meshes.Add(entity, mesh);
    }

    // Add material component
    void AddMaterial(EntityHandle entity, MaterialHandle material) {
        materials.Add(entity, material);
    }

    // Add transform component
    TransformHandle AddTransform(EntityHandle entity,
                            const glm::vec3& pos = glm::vec3(0.0f),
                            const glm::quat& rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                            const glm::vec3& scale = glm::vec3(1.0f)) {
        TransformHandle transformID = transformSystem.CreateTransform(entity, pos, rot, scale);
        transforms.Add(entity, transformID);
        return transformID;
    }

    // --- Component Access ---

    bool HasMesh(EntityHandle entity) const { return meshes.Has(entity); }
    bool HasMaterial(EntityHandle entity) const { return materials.Has(entity); }
    bool HasTransform(EntityHandle entity) const { return transforms.Has(entity); }

    MeshHandle GetMesh(EntityHandle entity) const { return meshes.Get(entity); }
    MaterialHandle GetMaterial(EntityHandle entity) const { return materials.Get(entity); }
    TransformHandle GetTransform(EntityHandle entity) const { return transforms.Get(entity); }

    void SetMesh(EntityHandle entity, MeshHandle mesh) { meshes.Get(entity) = mesh; }
    void SetMaterial(EntityHandle entity, MaterialHandle material) { materials.Get(entity) = material; }

    // --- System Updates ---

    void Update() {
        // Update all transform matrices
        transformSystem.UpdateAllMatrices();
    }

    // --- Iteration Helpers ---

    // Iterate over all entities with mesh AND material AND transform
    template<typename Func>
    void ForEachRenderable(Func&& func) {
        // Iterate over smallest set for efficiency
        const auto& meshDense = meshes.GetDense();
        for (size_t i = 0; i < meshDense.size(); ++i) {
            EntityHandle entity = meshes.GetEntity(static_cast<uint32_t>(i));

            if (materials.Has(entity) && transforms.Has(entity)) {
                MeshHandle mesh = meshDense[i];
                MaterialHandle material = materials.Get(entity);
                TransformHandle transformID = transforms.Get(entity);

                func(entity, mesh, material, transformID);
            }
        }
    }

    // --- Accessors ---

    TransformSystem& GetTransformSystem() { return transformSystem; }
    const TransformSystem& GetTransformSystem() const { return transformSystem; }

    const std::vector<EntityHandle>& GetEntities() const { return entities; }
    size_t GetEntityCount() const { return entities.size(); }

private:
    // Entity tracking
    std::vector<EntityHandle> entities;
    EntityHandle nextEntityID;

    // Component storage (Sparse Sets for O(1) lookup)
    SparseSet<MeshHandle> meshes;
    SparseSet<MaterialHandle> materials;
    SparseSet<TransformHandle> transforms;  // EntityID -> TransformID mapping

    // Transform subsystem
    TransformSystem transformSystem;
};

#endif //GPU_PARTICLE_SIM_SCENE_H
