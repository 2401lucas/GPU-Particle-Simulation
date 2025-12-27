# Name_In_Progress Engine

A modern DX12/Vulkan modern Renderer leveraging a GPU first approach to maximize performance.

### Design Philosophies
* Distinct Backend/Frontend
* Bindless
* GPU Focused
* Data Oriented Design

# Features

## Rendering Features

- Render API abstraction (DX12/Vulkan)
- Render Graph
- External Resource Manager
- GPU Oriented Renderer

## Engine Features

- Robust Window manager (GLFW)
- Input System w/ Rebindable inputs, Controller support & disk persistence.

## In Progress
### Personal Notes:

* Currently using an interlaced vbuf which is fine but maybe a deinterlaced vbuf would be better for my use case
* Need to fully incorporate bindless (Currently Rebind res-index buf region, should be part of a storage buffer and
  indexed)
* I would like to setup a custom ImGUI backend with my RHI.
* Before implementing too many features, the Vulkan RHI should be implemented to

* If this TODO is completed, I believe that it would be performant and feature complete enough to tout as a V1. I think
  post processing, PBR, Shadows and optimizing the Application->Renderer data flow would be the logical next steps to
  improve the project.


### TODO (In Progress )

* Sponza (Assimp Scene loading: This includes loading multiple mesh as well as material names, and using those materials
  names to load the actual materials)
* GPU Particles (GPU Compute)
* Fix Input
* UI (custom RHI backend?)
* Vulkan RHI backend

### TODO (Backlog)

* RenderPasses into classes?
* GPU Draw command generation
* Indirect Drawing
* Optimize Mesh loading (Assimp?)
* Culling(CPU+GPU)
* Tiled Lighting
* PBR
* Cascaded Shadow maps
* PBR
* IBL
* FXAA
* SSAO
* Shader Hot Reloading
* Wireframe Rendering
* Compute post processing
* CMAA
* Per object Rendering is handled by application, when implementing bindless maybe allow the renderer to have more
  control over the mesh and rendering. Also maybe SoA of model data from App->Renderer
* Current a ton of model info is sent in a uniform buffer, I think it would be better to upload to a storage buffer and
  index. This would increase the max model count from 256 to 16384 because in D3D12, uniform buffers are limited to
  65536 bytes and curently each model requires 256 bytes of data
* Read about how the GPU interacts with the PCIE bus.
* CPU performance seems pretty bad, could maybe be bad fences. Maybe a GPU stall somewhere. I know that CPU & CPU
  timings after Resizing are whack but even without that the performance is bad. I think that rebinding buffers all the
  time is also not helping. I would imagine bindless would help this performance a lot.

# Projects

## Particle Simulation