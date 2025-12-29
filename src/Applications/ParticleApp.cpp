//
// Created by 2401Lucas on 2025-10-30.
//

#include "ParticleApp.h"

void ParticleApp::OnInitialize(Engine &engine) {
    auto &events = engine.GetEvents();
    events.EmitQueued(Events::APP_INITIALIZED);

    auto &resources = engine.GetResourceManager();
    auto meshes = resources.LoadMesh("assets/Cube/cube.obj");
    auto materialHandle = resources.LoadMaterial({"assets/Cube/Cube"});

    demoCubeL = scene.CreateEntity();
    scene.AddMesh(demoCubeL, meshes[0]);
    scene.AddMaterial(demoCubeL, materialHandle);
    scene.AddTransform(demoCubeL, {0, 0, 0});

    demoCubeR = scene.CreateEntity();
    scene.AddMesh(demoCubeR, meshes[0]);
    scene.AddMaterial(demoCubeR, materialHandle);
    scene.AddTransform(demoCubeR, {3, 0, 3});
    // Transform test
    // auto rm = scene.AddTransform(demoCubeR, {3, 0, 3});
    // auto& t = scene.GetTransformSystem();
    // auto&pos = t.positions[rm];

    auto &renderer = engine.GetRenderer();
    m_camera = std::make_unique<Camera>(Transform({0, 0, 5}, {0, 0, 0}, {1, 1, 1}), (1280.f / 720.f), 60, 0.0001f,
                                        500.0f);

    renderer.SetCamera(m_camera.get());

    auto &input = engine.GetInput();
    input.RegisterAxis("Y Axis", KeyboardBinding(GLFW_KEY_W), KeyboardBinding(GLFW_KEY_S));
    input.RegisterAxis("X Axis", KeyboardBinding(GLFW_KEY_D), KeyboardBinding(GLFW_KEY_A));
}

void ParticleApp::Update(Engine &engine, float deltaTime) {
    auto &input = engine.GetInput();
    if (input.IsKeyDown(GLFW_KEY_ESCAPE)) {
        engine.RequestExit();
    }

    auto xMov = input.GetAxis("X Axis");
    auto yMov = input.GetAxis("Y Axis");
    auto mouse = input.GetMouseDelta();

    auto &cameraTranform = m_camera->GetTransform();
    glm::vec3 forward = cameraTranform.Front();
    forward.y = 0.0f;
    forward = glm::normalize(forward);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 moveDir = (forward * yMov) + (right * xMov);
    // if (glm::length(moveDir) > 0.0f)
    //     moveDir = glm::normalize(moveDir);

    cameraTranform.AddToPosition(moveDir * m_speed * deltaTime);
    cameraTranform.AddToRotation({-mouse.y * deltaTime, mouse.x * deltaTime, 0});

    if (input.IsKeyDown(GLFW_KEY_E)) {
        cameraTranform.AddToRotation({0, 7 * deltaTime, 0});
    }
}

void ParticleApp::OnRender(Engine &engine) {
    scene.Update();

    auto &renderer = engine.GetRenderer();
    renderer.BeginFrame();
    renderer.SetTransforms(scene.GetTransformSystem().worldMatrices);

    std::vector<RenderInfo> renderInfos;
    renderInfos.reserve(scene.GetEntityCount());

    scene.ForEachRenderable([&](EntityHandle entity, MeshHandle mesh,
                                MaterialHandle mat, TransformHandle transform) {
        RenderInfo info{
            .mesh = mesh,
            .material = mat,
            .transform = transform,
            .castsShadows = true,
            .receiveShadows = true,
        };
        renderInfos.push_back(info);
    });
    renderer.Submit(renderInfos);
    renderer.EndFrame();
}

void ParticleApp::OnShutdown(Engine &engine) {
    auto &events = engine.GetEvents();
    events.EmitQueued(Events::APP_SHUTDOWN);
}

void ParticleApp::OnResize(Engine &engine, int width, int height) {
    m_camera->SetAspectRatio(static_cast<float>(width) / height);
}

void ParticleApp::OnFocusChanged(Engine &engine, bool hasFocus) {
}
