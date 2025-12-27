//
// Created by 2401Lucas on 2025-10-30.
//

#include "RenderGraph.h"
#include "RenderPass.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <queue>
#include <chrono>

RenderGraph::RenderGraph(Device *device, CommandQueue *commandQueue, uint32_t frameCount)
    : m_device(device), m_commandQueue(commandQueue), m_frameCount(frameCount) {
    if (!m_commandQueue) {
        throw std::runtime_error("RenderGraph: CommandQueue cannot be null");
    }

    QueueType queueType = m_commandQueue->GetType();

    m_commandLists.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        // Create command list for this queue type
        m_commandLists[i] = std::unique_ptr<CommandList>(m_device->CreateCommandList(queueType));

        // Assign the allocator from the queue to this command list
        m_commandQueue->AssignCommandList(m_commandLists[i].get(), i);
    }

    // Initialize per-frame resource tracking
    m_frameResources.resize(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i) {
        m_frameResources[i].frameIndex = i;
    }
}

RenderGraph::~RenderGraph() {
    Flush(); // Ensure GPU is done with all resources
    Clear();
}

void RenderGraph::AddPass(std::unique_ptr<RenderPass> pass) {
    if (!pass) {
        throw std::runtime_error("Cannot add null pass to RenderGraph");
    }

    m_passes.push_back(std::move(pass));
}

void RenderGraph::Clear() {
    m_passes.clear();
    m_compiledPasses.clear();
    m_dependencies.clear();
}

CommandList *RenderGraph::Execute() {
    auto startTime = std::chrono::high_resolution_clock::now();

    CommandList *commandList = m_commandLists[m_currentFrameIndex].get();
    commandList->Begin(m_device->GetBindlessManager());

    m_statistics.barrierCount = 0;

    BuildDependencyGraph();

    TopologicalSort();

    AllocateResources();

    CalculateResourceLifetimes();

    if (m_resourceAliasing) {
        AliasResources();
    }

    auto compileTime = std::chrono::high_resolution_clock::now();
    m_statistics.compileTime = std::chrono::duration<float, std::milli>(
        compileTime - startTime).count();

    for (size_t i = 0; i < m_compiledPasses.size(); i++) {
        const auto &compiledPass = m_compiledPasses[i];

        if (!compiledPass.pass->IsEnabled()) {
            continue;
        }

        if (m_autoBarriers) {
            InsertBarriers(static_cast<uint32_t>(i));
        }

        ExecutePass(compiledPass);
    }

    if (!m_presentTarget.empty()) {
        auto it = m_externalResources.find(m_presentTarget);
        if (it != m_externalResources.end() && it->second.type == ExternalResource::Type::Texture) {
            TransitionExternalResource(m_presentTarget, (uint32_t) TextureUsage::Present);
        }
    }

    commandList->End();

    auto executeTime = std::chrono::high_resolution_clock::now();
    m_statistics.executeTime = std::chrono::duration<float, std::milli>(
        executeTime - compileTime).count();

    UpdateStatistics();
#if defined(DEBUG_RENDERGRAPH)
    LogRenderGraph();
#endif

    return commandList;
}

void RenderGraph::NextFrame() {
    m_currentFrameIndex = (m_currentFrameIndex + 1) % m_frameCount;

    auto &currentFrame = m_frameResources[m_currentFrameIndex];
    for (auto &[name, resource]: currentFrame.resources) {
        resource.canBeDestroyed = true;
    }

    CleanupOldResources();
}

void RenderGraph::Flush() {
    for (auto &frameRes: m_frameResources) {
        for (auto &[name, resource]: frameRes.resources) {
            if (resource.texture) {
                m_device->DestroyTexture(resource.texture);
                resource.texture = nullptr;
            }
            if (resource.buffer) {
                m_device->DestroyBuffer(resource.buffer);
                resource.buffer = nullptr;
            }
        }
        frameRes.resources.clear();
    }
}

void RenderGraph::BuildDependencyGraph() {
    m_dependencies.clear();

    // Build map of resource producers
    std::unordered_map<std::string, RenderPass *> producers;

    for (auto &pass: m_passes) {
        for (const auto &output: pass->GetOutputs()) {
            producers[output.name] = pass.get();
        }
    }

    // Find dependencies
    for (auto &pass: m_passes) {
        for (const auto &input: pass->GetInputs()) {
            auto it = producers.find(input.name);
            if (it != producers.end()) {
                // Found a dependency
                PassDependency dep;
                dep.producer = it->second;
                dep.consumer = pass.get();
                dep.resourceName = input.name;
                m_dependencies.push_back(dep);
            }
            // If not found, assume it's an external resource
        }
    }
}

void RenderGraph::TopologicalSort() {
    m_compiledPasses.clear();

    if (m_passes.empty()) {
        return;
    }

    // Build adjacency list and in-degree count
    std::unordered_map<RenderPass *, std::vector<RenderPass *> > adjacencyList;
    std::unordered_map<RenderPass *, int> inDegree;

    // Initialize
    for (auto &pass: m_passes) {
        inDegree[pass.get()] = 0;
        adjacencyList[pass.get()] = {};
    }

    // Build graph
    for (const auto &dep: m_dependencies) {
        adjacencyList[dep.producer].push_back(dep.consumer);
        inDegree[dep.consumer]++;
    }

    // Kahn's algorithm for topological sort
    std::queue<RenderPass *> queue;

    // Add all nodes with no incoming edges
    for (auto &pass: m_passes) {
        if (inDegree[pass.get()] == 0) {
            queue.push(pass.get());
        }
    }

    uint32_t index = 0;
    while (!queue.empty()) {
        RenderPass *current = queue.front();
        queue.pop();

        // Add to sorted list
        CompiledPass compiled;
        compiled.pass = current;
        compiled.index = index++;
        m_compiledPasses.push_back(compiled);

        // Reduce in-degree for neighbors
        for (RenderPass *neighbor: adjacencyList[current]) {
            inDegree[neighbor]--;
            if (inDegree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    // Check for cycles
    if (m_compiledPasses.size() != m_passes.size()) {
        throw std::runtime_error("RenderGraph contains circular dependencies!");
    }
}

void RenderGraph::CleanupOldResources() {
    // Resources can be destroyed if they haven't been used in m_frameCount frames
    for (auto &frameRes: m_frameResources) {
        std::vector<std::string> toRemove;

        for (auto &[name, resource]: frameRes.resources) {
            uint32_t framesSinceUse = m_currentFrameIndex >= resource.lastUsedFrame
                                          ? m_currentFrameIndex - resource.lastUsedFrame
                                          : (m_frameCount - resource.lastUsedFrame) + m_currentFrameIndex;

            if (framesSinceUse >= m_frameCount && resource.canBeDestroyed) {
                if (resource.texture) {
                    m_device->DestroyTexture(resource.texture);
                    resource.texture = nullptr;
                }
                if (resource.buffer) {
                    m_device->DestroyBuffer(resource.buffer);
                    resource.buffer = nullptr;
                }
                toRemove.push_back(name);
            }
        }

        for (const auto &name: toRemove) {
            frameRes.resources.erase(name);
        }
    }
}


void RenderGraph::AllocateResources() {
    auto &currentFrame = m_frameResources[m_currentFrameIndex];

    for (auto &compiled: m_compiledPasses) {
        RenderPass *pass = compiled.pass;

        // Allocate output resources
        for (const auto &output: pass->GetOutputs()) {
            if (IsExternalResource(output.name)) {
                compiled.outputResourceNames.push_back(output.name);
                continue;
            }

            GetOrCreateResource(output.name, output);
            compiled.outputResourceNames.push_back(output.name);
        }

        // Track input resources
        for (const auto &input: pass->GetInputs()) {
            compiled.inputResourceNames.push_back(input.name);
        }
    }
}

Texture *RenderGraph::CreateTransientTexture(const RenderPassResource &desc) {
    TextureCreateInfo textureCI{
        .width = desc.width,
        .height = desc.height,
        .depth = 1,
        .mipLevels = 1,
        .arraySize = 1,
        .usage = (TextureUsage) desc.stateFlag
    };

    // Map format
    switch (desc.format) {
        case RenderPassResource::Format::RGBA8:
            textureCI.format = TextureFormat::RGBA8_UNORM;
            break;
        case RenderPassResource::Format::RGBA16F:
            textureCI.format = TextureFormat::RGBA16_FLOAT;
            break;
        case RenderPassResource::Format::RGBA32F:
            textureCI.format = TextureFormat::RGBA32_FLOAT;
            break;
        case RenderPassResource::Format::Depth32:
            textureCI.format = TextureFormat::Depth32;
            break;
        default:
            break;
    }

    return m_device->CreateTexture(textureCI);
}

Buffer *RenderGraph::CreateTransientBuffer(const RenderPassResource &desc) {
    BufferCreateInfo bufferInfo;
    bufferInfo.size = desc.size;
    bufferInfo.usage = BufferUsage::Storage;
    bufferInfo.memoryType = MemoryType::GPU;

    return m_device->CreateBuffer(bufferInfo);
}

void RenderGraph::CalculateResourceLifetimes() {
    // Reset lifetimes
    for (auto &[name, resource]: m_frameResources[m_currentFrameIndex].resources) {
        resource.firstUse = UINT32_MAX;
        resource.lastUse = 0;
    }

    // Calculate first and last use for each resource
    for (const auto &compiled: m_compiledPasses) {
        uint32_t passIndex = compiled.index;

        // Check inputs
        for (const auto &input: compiled.pass->GetInputs()) {
            auto it = m_frameResources[m_currentFrameIndex].resources.find(input.name);
            if (it != m_frameResources[m_currentFrameIndex].resources.end()) {
                it->second.firstUse = std::min(it->second.firstUse, passIndex);
                it->second.lastUse = std::max(it->second.lastUse, passIndex);
            }
        }

        // Check outputs
        for (const auto &output: compiled.pass->GetOutputs()) {
            auto it = m_frameResources[m_currentFrameIndex].resources.find(output.name);
            if (it != m_frameResources[m_currentFrameIndex].resources.end()) {
                it->second.firstUse = std::min(it->second.firstUse, passIndex);
                it->second.lastUse = std::max(it->second.lastUse, passIndex);
            }
        }
    }
}

// TODO: Implement
void RenderGraph::AliasResources() {
    // Find resources with non-overlapping lifetimes that could share memory
}

void RenderGraph::InsertBarriers(uint32_t passIndex) {
    CommandList *commandList = m_commandLists[m_currentFrameIndex].get();
    const auto &compiled = m_compiledPasses[passIndex];

    // Handle inputs
    for (const auto &input: compiled.pass->GetInputs()) {
        // Check if it's an external resource
        auto extIt = m_externalResources.find(input.name);
        if (extIt != m_externalResources.end()) {
            TransitionExternalResource(input.name, input.stateFlag);
            continue;
        }

        // Check transient resources
        TransientResource *resource = GetCurrentFrameResource(input.name);
        if (!resource) {
            continue;
        }

        if (resource->currentStateFlag != input.stateFlag) {
            if (resource->type == TransientResource::Type::Texture) {
                commandList->TransitionTexture(
                    resource->texture,
                    (TextureUsage) resource->currentStateFlag,
                    (TextureUsage) input.stateFlag
                );
            } else {
                commandList->TransitionBuffer(
                    resource->buffer,
                    (BufferUsage) resource->currentStateFlag,
                    (BufferUsage) input.stateFlag
                );
            }

            resource->currentStateFlag = input.stateFlag;
            m_statistics.barrierCount++;
        }
    }

    // Handle outputs
    for (const auto &output: compiled.pass->GetOutputs()) {
        // Check external resources
        auto extIt = m_externalResources.find(output.name);
        if (extIt != m_externalResources.end()) {
            TransitionExternalResource(output.name, output.stateFlag);
            continue;
        }

        TransientResource *resource = GetCurrentFrameResource(output.name);
        if (!resource) {
            continue;
        }

        if (resource->currentStateFlag != output.stateFlag) {
            if (resource->type == TransientResource::Type::Texture) {
                commandList->TransitionTexture(
                    resource->texture,
                    (TextureUsage) resource->currentStateFlag,
                    (TextureUsage) output.stateFlag
                );
            } else {
                commandList->TransitionBuffer(
                    resource->buffer,
                    (BufferUsage) resource->currentStateFlag,
                    (BufferUsage) output.stateFlag
                );
            }

            resource->currentStateFlag = output.stateFlag;
            m_statistics.barrierCount++;
        }
    }
}

void RenderGraph::TransitionExternalResource(const std::string &name, uint32_t newState) {
    auto it = m_externalResources.find(name);
    if (it == m_externalResources.end()) {
        return;
    }

    auto &resource = it->second;
    if (resource.currentStateFlag == newState) {
        return;
    }

    CommandList *commandList = m_commandLists[m_currentFrameIndex].get();

    if (resource.type == ExternalResource::Type::Texture) {
        commandList->TransitionTexture(
            resource.texture,
            (TextureUsage) resource.currentStateFlag,
            (TextureUsage) newState
        );
    } else {
        commandList->TransitionBuffer(
            resource.buffer,
            (BufferUsage) resource.currentStateFlag,
            (BufferUsage) newState
        );
    }

    resource.currentStateFlag = newState;
    m_statistics.barrierCount++;
}

void RenderGraph::ExecutePass(const CompiledPass &compiledPass) {
    RenderPassContext context = BuildPassContext(compiledPass);

    compiledPass.pass->Execute(context);
}

RenderPassContext RenderGraph::BuildPassContext(const CompiledPass &compiledPass) {
    RenderPassContext context;
    context.commandList = m_commandLists[m_currentFrameIndex].get();
    context.frameIndex = m_currentFrameIndex;
    context.deltaTime = 0.016f; // Would get from timer

    // Populate input textures and buffers
    for (const auto &inputName: compiledPass.inputResourceNames) {
        // Try to find as texture first
        Texture *tex = GetTexture(inputName);
        if (tex) {
            context.inputTextures.push_back(tex);
        }

        // Try to find as buffer
        Buffer *buf = GetBuffer(inputName);
        if (buf) {
            context.inputBuffers.push_back(buf);
        }
    }

    // Populate output textures and buffers
    for (const auto &outputName: compiledPass.outputResourceNames) {
        Texture *tex = GetTexture(outputName);
        if (tex) {
            context.outputTextures.push_back(tex);
        }

        Buffer *buf = GetBuffer(outputName);
        if (buf) {
            context.outputBuffers.push_back(buf);
        }
    }

    return context;
}

void RenderGraph::RegisterExternalTexture(const std::string &name, Texture *texture,
                                          TextureUsage initialState) {
    ExternalResource resource;
    resource.texture = texture;
    resource.type = ExternalResource::Type::Texture;
    resource.initialStateFlag = (uint32_t) initialState;
    resource.currentStateFlag = (uint32_t) initialState;
    resource.isPresentTarget = false;

    m_externalResources[name] = resource;
}

void RenderGraph::RegisterExternalBuffer(const std::string &name, Buffer *buffer,
                                         BufferUsage initialState) {
    ExternalResource resource;
    resource.buffer = buffer;
    resource.type = ExternalResource::Type::Buffer;
    resource.initialStateFlag = (uint32_t) initialState;
    resource.currentStateFlag = (uint32_t) initialState;
    resource.isPresentTarget = false;

    m_externalResources[name] = resource;
}

void RenderGraph::SetPresentTarget(const std::string &name) {
    m_presentTarget = name;

    auto it = m_externalResources.find(name);
    if (it != m_externalResources.end()) {
        it->second.isPresentTarget = true;
    }
}

bool RenderGraph::IsExternalResource(const std::string &name) const {
    return m_externalResources.find(name) != m_externalResources.end();
}


RenderGraph::TransientResource *RenderGraph::GetOrCreateResource(const std::string &name,
                                                                 const RenderPassResource &desc) {
    auto &currentFrame = m_frameResources[m_currentFrameIndex];
    auto it = currentFrame.resources.find(name);

    if (it != currentFrame.resources.end()) {
        return &it->second;
    }

    // Create new transient resource for this frame
    TransientResource resource;
    resource.name = name;
    resource.type = desc.type == RenderPassResource::Type::Texture
                        ? TransientResource::Type::Texture
                        : TransientResource::Type::Buffer;
    resource.lastUsedFrame = m_currentFrameIndex;
    resource.canBeDestroyed = false; // Will be set true after frame is fully executed
    resource.initialStateFlag = desc.stateFlag;
    resource.currentStateFlag = desc.stateFlag;

    if (resource.type == TransientResource::Type::Texture) {
        resource.texture = CreateTransientTexture(desc);
        resource.width = desc.width;
        resource.height = desc.height;
    } else {
        resource.buffer = CreateTransientBuffer(desc);
        resource.size = desc.size;
    }

    currentFrame.resources[name] = resource;
    return &currentFrame.resources[name];
}

RenderGraph::TransientResource *RenderGraph::GetCurrentFrameResource(const std::string &name) {
    auto &currentFrame = m_frameResources[m_currentFrameIndex];
    auto it = currentFrame.resources.find(name);

    if (it != currentFrame.resources.end()) {
        it->second.lastUsedFrame = m_currentFrameIndex;
        return &it->second;
    }

    return nullptr;
}

Texture *RenderGraph::GetTexture(const std::string &name) {
    // Check external first
    auto extIt = m_externalResources.find(name);
    if (extIt != m_externalResources.end() &&
        extIt->second.type == ExternalResource::Type::Texture) {
        return extIt->second.texture;
    }

    // Check current frame transients
    TransientResource *resource = GetCurrentFrameResource(name);
    if (resource && resource->type == TransientResource::Type::Texture) {
        return resource->texture;
    }

    return nullptr;
}

Buffer *RenderGraph::GetBuffer(const std::string &name) {
    // Check external first
    auto extIt = m_externalResources.find(name);
    if (extIt != m_externalResources.end() &&
        extIt->second.type == ExternalResource::Type::Buffer) {
        return extIt->second.buffer;
    }

    // Check current frame transients
    TransientResource *resource = GetCurrentFrameResource(name);
    if (resource && resource->type == TransientResource::Type::Buffer) {
        return resource->buffer;
    }

    return nullptr;
}

void RenderGraph::UpdateStatistics() {
    m_statistics.passCount = static_cast<uint32_t>(m_compiledPasses.size());
    m_statistics.transientResourceCount = static_cast<uint32_t>(m_frameResources[m_currentFrameIndex].resources.size());

    uint64_t memoryUsed = 0;
    for (const auto &[name, resource]: m_frameResources[m_currentFrameIndex].resources) {
        if (resource.type == TransientResource::Type::Texture) {
            // Estimate texture memory (simplified)
            memoryUsed += static_cast<uint64_t>(resource.width) * resource.height * 4;
        } else {
            memoryUsed += resource.size;
        }
    }
    m_statistics.transientMemoryUsed = memoryUsed;
}

void RenderGraph::LogRenderGraph() {
    printf("\n===== RenderGraph =====\n");

    char msg[512];
    sprintf_s(msg, "Passes: %u\n", m_statistics.passCount);
    printf(msg);

    sprintf_s(msg, "Dependencies: %zu\n", m_dependencies.size());
    printf(msg);

    sprintf_s(msg, "Transient Resources: %u\n", m_statistics.transientResourceCount);
    printf(msg);

    sprintf_s(msg, "Memory Used: %.2f MB\n",
              m_statistics.transientMemoryUsed / (1024.0f * 1024.0f));
    printf(msg);

    sprintf_s(msg, "Compile Time: %.2f ms\n", m_statistics.compileTime);
    printf(msg);

    printf("\nPass Execution Order:\n");
    for (const auto &compiled: m_compiledPasses) {
        sprintf_s(msg, "  %u: %s%s\n",
                  compiled.index,
                  compiled.pass->GetName().c_str(),
                  compiled.pass->IsEnabled() ? "" : " (disabled)");
        printf(msg);
    }

    printf("\nResource Lifetimes:\n");
    for (const auto &[name, resource]: m_frameResources[m_currentFrameIndex].resources) {
        sprintf_s(msg, "  %s: [%u, %u]\n",
                  name.c_str(), resource.firstUse, resource.lastUse);
        printf(msg);
    }

    printf("========================\n\n");
}
