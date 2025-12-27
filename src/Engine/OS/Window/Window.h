//
// Created by 2401Lucas on 2025-10-29.
//

#ifndef GPU_PARTICLE_SIM_WINDOW_H
#define GPU_PARTICLE_SIM_WINDOW_H

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Window";
    bool resizable = true;
    bool fullscreen = false;
    bool vsync = true;
};

/// Window class represents a GLFW window in the application.
/// Each window has its own context and can be used independently.
/// There is a global state that manages the initialization and termination of GLFW globally, only required once.
class Window {
public:
    // --- Global State ---
    static bool InitGLFW();

    static void TerminateGLFW();

    // --- Constructors ---
    explicit Window(const WindowConfig &config);

    ~Window();

    Window(Window &&other) = delete;

    Window &operator=(Window &&other) = delete;

    Window(const Window &) = delete;

    Window &operator=(const Window &) = delete;

    // --- Core API ---
    void MakeContextCurrent() const;

    static void ClearContext();

    bool ShouldClose() const;

    void Close();

    void SetTitle(const std::string &title);

    void SetVSync(bool enabled);

    bool IsVSync() const { return m_vSyncEnabled; }

    void SetSize(int width, int height);

    void SetPosition(int x, int y);

    void Show();

    void Hide();

    void Focus();

    void SetCursorMode(int mode);

    // --- Event Loop ---
    void ProcessEvents();

    // --- Callbacks ---
    void SetResizeCallback(std::function<void(int, int)> cb) {
        m_resizeCallback = std::move(cb);
    }

    void SetKeyCallback(std::function<void(int, int, int, int)> cb) {
        m_keyCallback = std::move(cb);
    }

    void SetMouseButtonCallback(std::function<void(int, int, int)> cb) {
        m_mouseButtonCallback = std::move(cb);
    }

    void SetCursorPosCallback(std::function<void(double, double)> cb) {
        m_cursorPosCallback = std::move(cb);
    }

    // --- Getters ---
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    float GetAspectRatio() const {
        return m_height > 0 ? static_cast<float>(m_width) / m_height : 0.0f;
    }

    GLFWwindow *GetHandle() const { return m_window; }
    HWND GetHwnd() const { return glfwGetWin32Window(m_window); }
    bool WasResized() const { return m_wasResized; }
    void ResetResizeFlag() { m_wasResized = false; }

private:
    GLFWwindow *m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_wasResized = false;
    bool m_vSyncEnabled = true;

    std::function<void(int, int)> m_resizeCallback;
    std::function<void(int, int, int, int)> m_keyCallback;
    std::function<void(int, int, int)> m_mouseButtonCallback;
    std::function<void(double, double)> m_cursorPosCallback;

    void Cleanup();

    static bool &GetGLFWInitFlag();

    // --- Static Callbacks ---
    static void ErrorCallback(int errorCode, const char *description);

    static void FramebufferResizeCallback(GLFWwindow *, int, int);

    static void KeyCallback(GLFWwindow *, int, int, int, int);

    static void MouseButtonCallback(GLFWwindow *, int, int, int);

    static void CursorPosCallback(GLFWwindow *, double, double);

    static Window *GetWindowInstance(GLFWwindow *);
};

#endif
