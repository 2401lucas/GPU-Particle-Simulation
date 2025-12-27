# Render Graph

A Render Graph (or Frame Graph) is responsible for orchestrating rendering work each frame including managing resources,
determining required GPU memory barriers, and organizing command lists. My implementation uses a fluent API that allows
each render pass to be described declaratively, making the pipeline easy to read and modify.


### Per-Frame Workflow

Each frame, the Renderer collects all data needed for rendering and then constructs the Render Graph from scratch. This
design provides full runtime flexibility: passes, resources, and dependencies can be added or removed dynamically
without requiring static pipelines or precompiled command sequences.


### Registering External Resources

Before defining passes, the Renderer registers any external resources the graph will use, most commonly the
swapchain back buffer. External resources can be annotated with roles such as Present Target, allowing the graph to automatically
handle layout transitions at the end of the frame.


### Building Render Passes

Render passes are defined using a RenderPassBuilder. For each pass, the builder specifies:

* Resources read/written
* An execution callback, which receives a RenderPassContext that exposes the resolved GPU resource views for that pass

Once defined, the pass is submitted to the graph and incorporated into the dependency system.


### Graph Compilation

After all passes are registered, the Render Graph compiles and executes. During compilation, it performs several key
operations:


### Dependency Graph Construction

Determines pass order based on read/write relationships.


### Topological Sorting

Produces a valid execution sequence that respects dependencies.


### Resource Lifetime Analysis

Determines when each resource begins and ends its usage window.


### Resource Allocation & Aliasing

Reuses memory across passes whose lifetimes don’t overlap.


### Automatic Barrier Insertion

Generates all required GPU transitions based on how resources are used.

After compilation, the graph dispatches each pass in sequence, resulting in a clean, deterministic rendering pipeline
with minimal manual synchronization.


## Future Optimizations
 There are two main areas I plan to improve upon.


### Smarter Resource Aliasing

More advanced aliasing strategies could further reduce memory usage, especially for transient render targets.


### Incremental Graph Rebuilds

Currently, the graph is rebuilt every frame, and even a trivial graph takes ~1 ms to construct. Only rebuilding when
scene structure or render settings change would reduce CPU overhead and improve scalability.