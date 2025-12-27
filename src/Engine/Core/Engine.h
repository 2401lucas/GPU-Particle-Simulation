//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_ENGINE_H
#define GPU_PARTICLE_SIM_ENGINE_H
#include <memory>

#include "Application.h"
#include "../Rendering/Renderer.h"
#include "../OS/Input/Input.h"
#include "../OS/Window/Window.h"
#include "../Resources/ResourceManager.h"

class Application;

class Engine {
public:
    Engine() : m_deltaTime(0), m_totalTime(0) {
    }

    ~Engine() {
        m_renderer.reset(); // Delete First
    }

    void Initialize(std::unique_ptr<Application>, const int width, const int height);

    void Run();

    Renderer &GetRenderer() { return *m_renderer; }
    InputManager &GetInput() { return *m_input; }
    EventSystem &GetEvents() { return *m_event; }
    ResourceManager &GetResourceManager() { return *m_resources; }

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_totalTime; }

    void RequestExit() { m_window->Close(); }

private:
    std::unique_ptr<Application> m_application;

    std::unique_ptr<Window> m_window;
    std::unique_ptr<Device> m_device;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<ResourceManager> m_resources;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<EventSystem> m_event;

    float m_deltaTime;
    float m_totalTime;
};


#endif //GPU_PARTICLE_SIM_ENGINE_H
