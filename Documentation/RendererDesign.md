# Renderer Design

## Main Pass

##### Initial Setup

* Clear Render & Depth Target
* Set Render & Depth Target
* Set Viewport & Scissor

##### Per Vertex/Index Buffer

* DCG Generation?
* Bind Pipeline & Topology? // Uber Shader?
* Bind Vertex/Index buffers
* GetFrame Resources, bind
* ForEach batch in this Vertex/Index buffer
* Draw(Set draw data in array with instance ID (Requires sorting beforehand))

EVERYTHING IS DESIGNED TO BE DYNAMIC

Mark objects as Dirty when core information is changed, IE DirtyTransform, Material, Mesh
Do not store as variable, but send obj ID's to be processed directly

Models are assigned a permanent TransformID to a slot in the buffer that remains static
Resource Manager is maybe doing too much, also how about that material handle system, I think it is doing too much



## Renderer Resources
### RHI Resources
#### Internal Only
* Swapchain
* Command Queues (Graphic, Compute & Transfer)

#### Externally influenced(influenced on a per-program basis)
* Pipeline(s)
* Per Frame Resources(fence, perFrameBuffer/perObjBuffer(outdated))

#### RenderAPI + other data from App->Renderer
* Transforms
* Lights
* Pipeline Settings

#### Renderer Managed Resources
* RenderGraph
* Settings
* Statistics


### How to handle compute submissions?

## STAGES
#### Initialization:


