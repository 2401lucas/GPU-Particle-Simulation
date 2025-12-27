# Usage
To interface with the engine, it requires a reference to the core Engine to get reference to the other core systems. Each application is responsible for providing a render list to the Renderer. Each application has no expectations other than providing the data in OnRender, which is automatically sorted/optimized by the renderer, and initializing the camera. This allows for a ton of flexibility in interfacing with the renderer. 

# TODO
Light System
