# RHI Design
## Design Philosophy

The design philosophy of the RHI is to provide a high level abstraction over the underlying graphics API. The RHI follows a feature set that both underlying APIs support. The RHI uses an inheritance-based architecture where abstract base classes define the interface and platform-specific implementations provide the concrete functionality. It follows a factory pattern where the Device class is responsible for creating and destroying all API resources through factory methods that return unique pointers to the created resources. This approach provides type safety, clear ownership semantics, and a clean abstraction boundary between the RHI layer and the underlying graphics API.
## Implementation Details
### Device Creation

When creating a Device, the DeviceCreateInfo struct is used to specify the desired configuration. This can include enabling the debug layer, enabling GPU validation, and enabling other device specific features. The Device class itself follows the inheritance pattern, with a base Device class and platform-specific implementations (e.g., D3D12Device, VulkanDevice).
### Resource Creation

When creating the various other resources, such as CommandQueues, CommandLists, Swapchains, Buffers, Textures or Pipelines, the Device class has factory methods that create the appropriate platform-specific implementation and return a unique pointer to the base class interface. For example, Device::CreateTexture() returns a std::unique_ptr<Texture>, which internally points to a D3D12Texture or VulkanTexture depending on the backend. This allows the application to manage the lifetime of the resource through standard C++ RAII patterns, ensuring that resources are properly destroyed when they are no longer needed. The inheritance approach provides type safety and allows the compiler to catch API misuse at compile time.

### Resource Interactions

Resource interactions, such as binding resources to pipelines, updating buffer data, or submitting command lists to queues, are managed through the CommandList class. The CommandList class follows the same inheritance pattern as other RHI resources, with virtual methods defining the interface for common operations. These commands include managing pipeline states, resource bindings, render target attachments, and other resource interactions. The virtual dispatch overhead is negligible compared to the cost of actual GPU operations, and the type-safe interface prevents common usage errors.
