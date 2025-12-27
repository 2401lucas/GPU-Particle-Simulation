#include "Window.h"

#include <iostream>

bool& Window::GetGLFWInitFlag() {
  static bool initialized = false;
  return initialized;
}

bool Window::InitGLFW() {
  if (GetGLFWInitFlag()) return true;
  glfwSetErrorCallback(ErrorCallback);
  if (!glfwInit()) {
    std::cerr << "GLFW: Initialization failed.\n";
    return false;
  }
  GetGLFWInitFlag() = true;
  return true;
}

void Window::TerminateGLFW() {
  if (GetGLFWInitFlag()) {
    glfwTerminate();
    GetGLFWInitFlag() = false;
  }
}

Window::Window(const WindowConfig& config) {
  if (!GetGLFWInitFlag())
    throw std::runtime_error("GLFW not initialized. Call Window::InitGLFW().");

  glfwWindowHint(GLFW_RESIZABLE, config.resizable);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

  GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
  m_window = glfwCreateWindow(config.width, config.height, config.title.c_str(),
                              monitor, nullptr);

  if (!m_window) throw std::runtime_error("Failed to create GLFW window.");

  m_width = config.width;
  m_height = config.height;
  m_vSyncEnabled = config.vsync;

  glfwSetWindowUserPointer(m_window, this);
  glfwSetFramebufferSizeCallback(m_window, FramebufferResizeCallback);
  glfwSetKeyCallback(m_window, KeyCallback);
  glfwSetMouseButtonCallback(m_window, MouseButtonCallback);
  glfwSetCursorPosCallback(m_window, CursorPosCallback);

  MakeContextCurrent();
  SetVSync(config.vsync);
}

Window::~Window() { Cleanup(); }

void Window::Cleanup() {
  if (!m_window) return;

  ClearContext();
  glfwDestroyWindow(m_window);
  m_window = nullptr;
}

// --- Core API ---
void Window::MakeContextCurrent() const { glfwMakeContextCurrent(m_window); }

void Window::ClearContext() { glfwMakeContextCurrent(nullptr); }

bool Window::ShouldClose() const {
  return m_window && glfwWindowShouldClose(m_window);
}

void Window::Close() {
  if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE);
}

void Window::SetTitle(const std::string& title) {
  if (m_window) glfwSetWindowTitle(m_window, title.c_str());
}

void Window::SetVSync(bool enabled) {
  if (m_window) {
    MakeContextCurrent();
    glfwSwapInterval(enabled ? 1 : 0);
    m_vSyncEnabled = enabled;
  }
}

void Window::SetSize(int width, int height) {
  if (m_window) {
    glfwSetWindowSize(m_window, width, height);
    m_width = width;
    m_height = height;
  }
}

void Window::SetPosition(int x, int y) {
  if (m_window) glfwSetWindowPos(m_window, x, y);
}

void Window::Show() {
  if (m_window) glfwShowWindow(m_window);
}
void Window::Hide() {
  if (m_window) glfwHideWindow(m_window);
}
void Window::Focus() {
  if (m_window) glfwFocusWindow(m_window);
}

void Window::ProcessEvents() {
  if (m_window) glfwPollEvents();
}

void Window::SetCursorMode(int mode) {
  if (m_window) glfwSetInputMode(m_window, GLFW_CURSOR, mode);
}

void Window::ErrorCallback(int errorCode, const char* description) {
  std::cerr << "GLFW Error [" << errorCode << "]: " << description << "\n";
}

void Window::FramebufferResizeCallback(GLFWwindow* window, int width,
                                       int height) {
  if (auto* self = GetWindowInstance(window)) {
    self->m_width = width;
    self->m_height = height;
    self->m_wasResized = true;
    if (self->m_resizeCallback) self->m_resizeCallback(width, height);
  }
}

void Window::KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                         int mods) {
  if (auto* self = GetWindowInstance(window); self && self->m_keyCallback)
    self->m_keyCallback(key, scancode, action, mods);
}

void Window::MouseButtonCallback(GLFWwindow* window, int button, int action,
                                 int mods) {
  if (auto* self = GetWindowInstance(window);
      self && self->m_mouseButtonCallback)
    self->m_mouseButtonCallback(button, action, mods);
}

void Window::CursorPosCallback(GLFWwindow* window, double x, double y) {
  if (auto* self = GetWindowInstance(window); self && self->m_cursorPosCallback)
    self->m_cursorPosCallback(x, y);
}

Window* Window::GetWindowInstance(GLFWwindow* window) {
  return static_cast<Window*>(glfwGetWindowUserPointer(window));
}
