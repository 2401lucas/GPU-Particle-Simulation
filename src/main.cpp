#include "Engine/Core/Engine.h"
#include "Engine/OS/Window/Window.h"
#include "Applications/ParticleApp.h"

int main() {
    Window::InitGLFW();

    {
        auto app = std::make_unique<ParticleApp>();

        //TODO: Console Argument parsing for paramaters
        Engine engine;

        engine.Initialize(std::move(app), 1280, 720);

        engine.Run();
    }

    Window::TerminateGLFW();
    return 0;
}
