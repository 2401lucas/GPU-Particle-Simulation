//
// Created by 2401Lucas on 2025-10-30.
//

#include "Engine.h"

#include "../Rendering/Renderer.h"

void Engine::Initialize(std::unique_ptr<Application> app, const int width, const int height) {
    m_application = std::move(app);
    WindowConfig config{
        .width = width,
        .height = height,
        .title = m_application->GetWindowTitle(),
        .fullscreen = m_application->GetStartFullscreen(),
        .vsync = m_application->GetVSyncEnabled(),
    };
    m_window = std::make_unique<Window>(config);

    DeviceCreateInfo deviceInfo{.enableDebugLayer = true, .enableGPUValidation = true, .preferredAdapterIndex = 0};
    m_device = Device::Create(deviceInfo);

    m_event = std::make_unique<EventSystem>();

    m_resources = std::make_unique<ResourceManager>(m_device.get(), m_event.get());
    m_renderer = std::make_unique<Renderer>(m_window.get(), m_device.get(), m_resources.get());

    m_input = std::make_unique<InputManager>(m_window.get());
    m_input->SetCallbackMode(InputManager::CallbackMode::Queued);

    m_application->OnInitialize(*this);
}

void Engine::Run() {
    auto lastTime = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> now;

    while (!m_window->ShouldClose()) {
        // Time management
        now = std::chrono::steady_clock::now();
        m_deltaTime = std::chrono::duration<float>(now - lastTime).count();;
        lastTime = now;

        if (m_window->WasResized()) {
            m_renderer->Resize();
        }

        // Input
        m_window->ProcessEvents();
        m_input->Update();
        
        // Update systems and game logic
        m_application->Update(*this, m_deltaTime);
        // m_physics->Update(m_deltaTime);
        // m_audio->Update(m_deltaTime);

        // Process events
        m_event->ProcessQueued();

        // Rendering: Application submits, Renderer executes
        m_application->OnRender(*this); // Fills submission queue
        m_resources->Update();
    }
}
