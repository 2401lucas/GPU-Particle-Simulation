//
// Created by 2401Lucas on 2025-11-30.
//

#ifndef GPU_PARTICLE_SIM_TRANSFORMSYSTEM_H
#define GPU_PARTICLE_SIM_TRANSFORMSYSTEM_H

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <numeric>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

using TransformHandle = int32_t;
using EntityHandle = int32_t;

constexpr TransformHandle INVALID_TRANSFORM = -1;

class TransformSystem {
public:
    // --- Raw Transform Data (SoA) ---
    std::vector<glm::vec3> positions;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;

    // --- Computed Matrices ---
    std::vector<glm::mat4> localMatrices; // TRS -> Matrix
    std::vector<glm::mat4> worldMatrices; // For GPU upload

    // --- Hierarchy (Linked List in Array) ---
    std::vector<TransformHandle> parents;
    std::vector<TransformHandle> firstChild;
    std::vector<TransformHandle> nextSibling;

    // --- State Management ---
    std::vector<uint8_t> dirty; // Needs update flag
    std::vector<uint8_t> alive;
    // --- Mapping ---
    std::vector<EntityHandle> entityOwner; // TransformID -> EntityID
    std::unordered_map<EntityHandle, TransformHandle> entityToTransform; // EntityID -> TransformID

    // --- Sorted Update Order ---
    std::vector<TransformHandle> updateOrder; // Sorted by hierarchy depth
    bool needsResort = false;

    std::vector<TransformHandle> emptyHandles;

    // --- API ---
    TransformHandle CreateTransform(EntityHandle entity,
                                    const glm::vec3 &pos = glm::vec3(0.0f),
                                    const glm::quat &rot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                    const glm::vec3 &scale = glm::vec3(1.0f)) {
        TransformHandle id;

        if (emptyHandles.size() != 0) {
            id = emptyHandles.back();
            emptyHandles.pop_back();
            alive[id] = 1;
        } else {
            id = static_cast<TransformHandle>(positions.size());

            positions.push_back(pos);
            rotations.push_back(rot);
            scales.push_back(scale);

            localMatrices.push_back(glm::mat4(1.0f));
            worldMatrices.push_back(glm::mat4(1.0f));

            parents.push_back(INVALID_TRANSFORM);
            firstChild.push_back(INVALID_TRANSFORM);
            nextSibling.push_back(INVALID_TRANSFORM);

            dirty.push_back(1);
            alive.push_back(1);
            entityOwner.push_back(entity);
        }

        entityToTransform[entity] = id;

        UpdateLocalMatrix(id);
        needsResort = true;

        return id;
    }

    void RemoveTransform(TransformHandle id) {
        if (id < 0 || id >= alive.size() || !alive[id]) return;

        alive[id] = 0;
        emptyHandles.push_back(id);

        // Remove from parent/child relationships
        if (parents[id] != INVALID_TRANSFORM) {
            RemoveFromParentChildList(id);
        }
        RemoveAllChildren(id);

        // Remove from entity mapping
        EntityHandle entity = entityOwner[id];
        entityToTransform.erase(entity);

        needsResort = true;
    }

    // Orphan child for now
    void RemoveAllChildren(TransformHandle id) {
        TransformHandle child = firstChild[id];
        while (child != INVALID_TRANSFORM) {
            TransformHandle nextChild = nextSibling[child];

            // Orphan this child (make it a root)
            parents[child] = INVALID_TRANSFORM;
            nextSibling[child] = INVALID_TRANSFORM;
            MarkDirtyWithChildren(child); // Recalculate world matrices

            child = nextChild;
        }
        firstChild[id] = INVALID_TRANSFORM;
    }

    void SetPosition(TransformHandle id, const glm::vec3 &pos) {
        positions[id] = pos;
        MarkDirty(id);
    }

    void SetRotation(TransformHandle id, const glm::quat &rot) {
        rotations[id] = rot;
        MarkDirty(id);
    }

    void SetScale(TransformHandle id, const glm::vec3 &scale) {
        scales[id] = scale;
        MarkDirty(id);
    }

    void SetParent(TransformHandle child, TransformHandle parent) {
        // Remove from old parent's child list
        if (parents[child] != INVALID_TRANSFORM) {
            RemoveFromParentChildList(child);
        }

        // Add to new parent's child list
        if (parent != INVALID_TRANSFORM) {
            nextSibling[child] = firstChild[parent];
            firstChild[parent] = child;
        }

        parents[child] = parent;
        MarkDirtyWithChildren(child);
        needsResort = true;
    }

    void UpdateAllMatrices() {
        // Resort if hierarchy changed
        if (needsResort) {
            SortByHierarchyDepth();
            needsResort = false;
        }

        // Update local matrices (only dirty ones) - first linear pass
        for (size_t i = 0; i < dirty.size(); ++i) {
            if (alive[i] && dirty[i]) {
                UpdateLocalMatrix(i);
            }
        }

        // Update world matrices in sorted order - second linear pass
        // This is perfectly cache-friendly and has no recursion overhead
        for (TransformHandle id: updateOrder) {
            if (dirty[id]) {
                if (parents[id] == INVALID_TRANSFORM) {
                    worldMatrices[id] = localMatrices[id];
                } else {
                    // Parent is guaranteed to be already updated (lower depth in sorted order)
                    worldMatrices[id] = worldMatrices[parents[id]] * localMatrices[id];
                }
                dirty[id] = 0;
            }
        }
    }

    const glm::mat4 *GetWorldMatrixData() const {
        return worldMatrices.data();
    }

    size_t GetTransformCount() const {
        return worldMatrices.size();
    }

    TransformHandle GetTransformID(EntityHandle entity) const {
        auto it = entityToTransform.find(entity);
        return (it != entityToTransform.end()) ? it->second : INVALID_TRANSFORM;
    }

private:
    void UpdateLocalMatrix(TransformHandle id) {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), positions[id]);
        glm::mat4 R = glm::mat4_cast(rotations[id]);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scales[id]);
        localMatrices[id] = T * R * S;
    }

    void MarkDirty(TransformHandle id) {
        dirty[id] = 1;

        // Mark all descendants dirty using iterative traversal
        MarkDirtyWithChildren(id);
    }

    void MarkDirtyWithChildren(TransformHandle id) {
        // Use a simple stack for iterative traversal
        std::vector<TransformHandle> stack;
        stack.push_back(id);

        while (!stack.empty()) {
            TransformHandle current = stack.back();
            stack.pop_back();

            dirty[current] = 1;

            // Add all children to stack
            TransformHandle child = firstChild[current];
            while (child != INVALID_TRANSFORM) {
                stack.push_back(child);
                child = nextSibling[child];
            }
        }
    }

    void RemoveFromParentChildList(TransformHandle child) {
        TransformHandle parent = parents[child];
        if (parent == INVALID_TRANSFORM) return;

        // Find and remove from linked list
        if (firstChild[parent] == child) {
            firstChild[parent] = nextSibling[child];
        } else {
            TransformHandle prev = firstChild[parent];
            while (prev != INVALID_TRANSFORM && nextSibling[prev] != child) {
                prev = nextSibling[prev];
            }
            if (prev != INVALID_TRANSFORM) {
                nextSibling[prev] = nextSibling[child];
            }
        }
        nextSibling[child] = INVALID_TRANSFORM;
    }

    void SortByHierarchyDepth() {
        if (positions.empty()) return;

        // Build depth values iteratively
        std::vector<uint32_t> depths(positions.size(), 0);

        // Iteratively compute depths until convergence
        // This handles arbitrary hierarchy structures
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 0; i < positions.size(); ++i) {
                if (parents[i] != INVALID_TRANSFORM) {
                    uint32_t newDepth = depths[parents[i]] + 1;
                    if (newDepth != depths[i]) {
                        depths[i] = newDepth;
                        changed = true;
                    }
                }
            }
        }

        // Create sorted index array
        updateOrder.resize(positions.size());
        std::iota(updateOrder.begin(), updateOrder.end(), 0);

        // Sort by depth (parents before children)
        std::sort(updateOrder.begin(), updateOrder.end(),
                  [&depths](TransformHandle a, TransformHandle b) {
                      return depths[a] < depths[b];
                  });
    }
};

#endif //GPU_PARTICLE_SIM_TRANSFORMSYSTEM_H
