# Renderer Hardware Interface (RHI) — Engine Architecture Overview

The Renderer Hardware Interface is the foundational GPU abstraction layer of the engine. It provides a low level, explicit, and API agnostic interface over modern graphics APIs (currently supporting Vulkan and DX12), enabling high performance rendering systems while preserving portability. The RHI is designed around the needs of GPU driven rendering, bindless resource access, and data oriented scene processing. It exposes predictable primitives that higher level systems build upon.


## Core Design Pillars
### 1. Explicit GPU Control

The RHI implementation avoids abstraction by following a factory pattern. All operations are fully explicit except for a few helper functions. The backend is focused on being deterministic.


### 2. Data Oriented Architecture

The RHI favors POD types, handles, contiguous allocations, and cache friendly data flow:
* No polymorphic resource classes
* No virtual functions
* No per object heap allocations

Resources are represented as lightweight structs wrapping native GPU handles and simple metadata, allowing the renderer to operate on large arrays of handles or offsets instead of scattered objects.


### 3. Backend Symmetry (Vulkan first Design)

The abstraction mirrors Vulkan’s model and maps D3D12 onto it. This provides:
* Unified concepts of queues, command lists, and fences
* Identical resource state tracking across APIs
* Nearly identical upload and memory models
* Minimal branching in upper layers

This symmetry dramatically reduces backend specific code and simplifies future expansion.


### 4. Zero Intrusion Philosophy

The RHI does not impose a rendering pattern. It does not:
* Manage render passes 
* Manage frame graphs
* Own materials or scene data
* Enforce resource lifetimes

Instead, it acts as a toolbox of predictable GPU primitives. All scheduling, ordering, and logic remain in the renderer layer.


### 5. Built for GPU Driven Rendering

The RHI is optimized for modern techniques:
* Descriptor indexing / bindless resources
* GPU driven culling and sorting
* Compute generated indirect draw calls (vkCmdDrawIndirect, D3D12ExecuteIndirect)
* Large, unified GPU buffers (mesh, material, transform, instance data)
* Streaming, hot loading, and per frame dynamic data updates (todo)

Every design choice is made with these workflows in mind.


## Integration with Higher Level Systems
### 1. Render Graph

The render graph builds on top of the RHI by:
* Performing pass scheduling
* Inserting barriers and transitions using RHI primitives
* Assembling command lists
* Tracking lifetime and usage of transient resources

The RHI’s explicit state transitions make dependency resolution straightforward and deterministic.


### 2. GPU Driven Culling & Rendering

The RHI exposes all functionality required for GPU driven rendering:
* Compute dispatch
* Storage buffers
* UAV writes
* Indirect draw buffer support
* Synchronization primitives

The renderer's culling pipeline can:
* Read global instance/mesh buffers
* Write indirect draw commands
* Execute them without CPU involvement


### 3. Asset Streaming & Hot Reloading (TODO)

The upload system supports:
* Asynchronous loading
* Streaming meshes into suballocated mega buffers
* Texture mip streaming

Upload operations have deterministic lifetimes tied to fences, making streaming safe and predictable.


### Why This RHI Design Works Well
* High performance, low overhead architecture  with no per object allocations, thin wrappers, and zero hidden work.
* Modern rendering technique compatibility, Designed specifically for bindless shading, GPU culling, mesh streaming, and indirect drawing pipelines.
* API portability without “lowest common denominator” constraints . Vulkan first model with D3D12 mapped onto it ensures feature parity without sacrificing explicitness.
* Clean layering & extensibility  The RHI is foundational without leaking into higher level systems. Render graph, materials, scene, and culling all cleanly stack above it.


## TODO LIST
* Texture Streaming
* Mesh streaming
* Async Loads
* Defragmentation
* Hot Reloading