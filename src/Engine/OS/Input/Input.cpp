#include "Input.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

// Static registry for InputManager instances (to handle scroll/char callbacks)
namespace {
    std::unordered_map<GLFWwindow *, InputManager *> g_InputManagers;
    std::mutex g_RegistryMutex;

    // RAII helper for exception-safe registration
    struct RegistryGuard {
        GLFWwindow *window;
        InputManager *manager;
        bool registered = false;

        RegistryGuard(GLFWwindow *w, InputManager *m) : window(w), manager(m) {
            std::lock_guard<std::mutex> lock(g_RegistryMutex);
            g_InputManagers[window] = manager;
            registered = true;
        }

        ~RegistryGuard() {
            if (registered) {
                std::lock_guard<std::mutex> lock(g_RegistryMutex);
                g_InputManagers.erase(window);
            }
        }

        void release() { registered = false; }
    };
} // namespace

InputManager::InputManager(Window *window) : m_window(window) {
    if (!m_window) {
        throw std::runtime_error("InputManager requires a valid Window pointer");
    }

    // Initialize mouse position
    double mx, my;
    glfwGetCursorPos(m_window->GetHandle(), &mx, &my);
    m_mousePos = glm::vec2(static_cast<float>(mx), static_cast<float>(my));
    m_prevMousePos = m_mousePos;

    // Initialize gamepad states (up to 4 gamepads)
    m_gamepadStates.resize(4);

    // Exception-safe registration
    RegistryGuard guard(m_window->GetHandle(), this);

    // Register callbacks via Window's callback system
    m_window->SetKeyCallback([this](int key, int scancode, int action, int mods) {
        OnKey(key, scancode, action, mods);
    });

    m_window->SetMouseButtonCallback([this](int button, int action, int mods) {
        OnMouseButton(button, action, mods);
    });

    m_window->SetCursorPosCallback(
        [this](double x, double y) { OnCursorPos(x, y); });

    // Set scroll and char callbacks directly
    glfwSetScrollCallback(m_window->GetHandle(),
                          [](GLFWwindow *wnd, double x, double y) {
                              std::lock_guard<std::mutex> lock(g_RegistryMutex);
                              auto it = g_InputManagers.find(wnd);
                              if (it != g_InputManagers.end()) {
                                  it->second->OnScroll(x, y);
                              }
                          });

    glfwSetCharCallback(m_window->GetHandle(),
                        [](GLFWwindow *wnd, unsigned int codepoint) {
                            std::lock_guard<std::mutex> lock(g_RegistryMutex);
                            auto it = g_InputManagers.find(wnd);
                            if (it != g_InputManagers.end()) {
                                it->second->OnCharInput(codepoint);
                            }
                        });

    guard.release(); // Successfully constructed, keep registration
}

InputManager::~InputManager() {
    // Unregister from global map
    {
        std::lock_guard<std::mutex> lock(g_RegistryMutex);
        g_InputManagers.erase(m_window->GetHandle());
    }

    // Clear Window callbacks
    if (m_window && m_window->GetHandle()) {
        m_window->SetKeyCallback(nullptr);
        m_window->SetMouseButtonCallback(nullptr);
        m_window->SetCursorPosCallback(nullptr);
        glfwSetScrollCallback(m_window->GetHandle(), nullptr);
        glfwSetCharCallback(m_window->GetHandle(), nullptr);
    }
}

void InputManager::Update() {
    if (!m_window) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    UpdateGamepads();

    // Mouse position is updated via onCursorPos callback
    // Store previous position for next frame's delta calculation
    m_mouseDelta = (m_prevMousePos - m_mousePos) * m_mouseSensitivity;
    m_prevMousePos = m_mousePos;

    // Reset wheel delta after frame
    m_wheelDelta = 0.0;
}

void InputManager::UpdateGamepads() {
    double now = GetTime();

    for (int i = 0; i < 4; ++i) {
        auto &state = m_gamepadStates[i];
        bool wasConnected = state.connected;

        // Check connection
        int present = glfwJoystickPresent(GLFW_JOYSTICK_1 + i);
        state.connected = (present == GLFW_TRUE);

        // Fire connection/disconnection events
        if (state.connected && !wasConnected) {
            PushEvent(InputEvent::GamepadConnected(i, now));
        } else if (!state.connected && wasConnected) {
            PushEvent(InputEvent::GamepadDisconnected(i, now));
        }

        if (!state.connected) {
            state.axes.assign(6, 0.0f);
            state.buttons.assign(15, false);
            state.prevButtons.assign(15, false);
            continue;
        }

        // Get gamepad name
        const char *name = glfwGetJoystickName(GLFW_JOYSTICK_1 + i);
        if (name) {
            state.name = std::string(name);
        }

        // Check if gamepad supports standard mapping
        int isGamepad = glfwJoystickIsGamepad(GLFW_JOYSTICK_1 + i);
        if (isGamepad != GLFW_TRUE) continue;

        // Get gamepad state
        GLFWgamepadstate glfwState;
        if (glfwGetGamepadState(GLFW_JOYSTICK_1 + i, &glfwState) == GLFW_TRUE) {
            // Update axes
            state.axes[static_cast<int>(GamepadAxis::LeftX)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
            state.axes[static_cast<int>(GamepadAxis::LeftY)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
            state.axes[static_cast<int>(GamepadAxis::RightX)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
            state.axes[static_cast<int>(GamepadAxis::RightY)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
            state.axes[static_cast<int>(GamepadAxis::LeftTrigger)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER];
            state.axes[static_cast<int>(GamepadAxis::RightTrigger)] =
                    glfwState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER];

            // Update buttons
            state.prevButtons = state.buttons;
            for (int btn = 0; btn < 15; ++btn) {
                bool nowDown = (glfwState.buttons[btn] == GLFW_PRESS);
                bool wasDown = state.buttons[btn];
                state.buttons[btn] = nowDown;

                InputBinding binding{InputSource::Gamepad, btn, i};
                UpdateButtonState(binding, nowDown);

                // Trigger action events for this button
                if (nowDown && !wasDown) {
                    // Button was just pressed
                    for (const auto &[actionName, actionBinding]: m_actionBindings) {
                        for (const auto &b: actionBinding.bindings) {
                            if (b == binding) {
                                TriggerActionPressed(actionName);
                                break;
                            }
                        }
                    }
                } else if (!nowDown && wasDown) {
                    // Button was just released
                    for (const auto &[actionName, actionBinding]: m_actionBindings) {
                        for (const auto &b: actionBinding.bindings) {
                            if (b == binding) {
                                TriggerActionReleased(actionName);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void InputManager::ProcessEvents() {
    // Move events out of queue (minimal lock time)
    std::deque<InputEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        events = std::move(m_eventQueue);
        m_eventQueue.clear();
    }

    // Process events WITHOUT holding any locks (safe for callbacks to call
    // InputManager methods)
    for (const auto &event: events) {
        switch (event.type) {
            case InputEventType::ActionPressed:
                if (m_actionPressedCallback) {
                    m_actionPressedCallback(event.actionOrAxisName);
                }
                break;

            case InputEventType::ActionReleased:
                if (m_actionReleasedCallback) {
                    m_actionReleasedCallback(event.actionOrAxisName);
                }
                break;

            case InputEventType::AxisChanged:
                if (m_axisChangedCallback) {
                    m_axisChangedCallback(event.actionOrAxisName, event.value);
                }
                break;

            case InputEventType::GamepadConnected:
                if (m_gamepadConnectedCallback) {
                    m_gamepadConnectedCallback(event.gamepadIndex);
                }
                break;

            case InputEventType::GamepadDisconnected:
                if (m_gamepadDisconnectedCallback) {
                    m_gamepadDisconnectedCallback(event.gamepadIndex);
                }
                break;

            case InputEventType::TextInput:
                if (m_textInputCallback) {
                    m_textInputCallback(event.codepoint);
                }
                break;
        }
    }
}

void InputManager::PushEvent(InputEvent event) {
    std::lock_guard<std::mutex> lock(m_queueMutex);

    // Prevent queue overflow
    if (m_eventQueue.size() >= m_maxQueueSize) {
        // Drop oldest event
        m_eventQueue.pop_front();
    }

    m_eventQueue.push_back(std::move(event));
}

void InputManager::TriggerActionPressed(const std::string &actionName) {
    if (m_callbackMode == CallbackMode::Immediate) {
        if (m_actionPressedCallback) {
            m_actionPressedCallback(actionName);
        }
    } else {
        PushEvent(InputEvent::ActionPressed(actionName, GetTime()));
    }
}

void InputManager::TriggerActionReleased(const std::string &actionName) {
    if (m_callbackMode == CallbackMode::Immediate) {
        if (m_actionReleasedCallback) {
            m_actionReleasedCallback(actionName);
        }
    } else {
        PushEvent(InputEvent::ActionReleased(actionName, GetTime()));
    }
}

void InputManager::UpdateButtonState(const InputBinding &binding, bool isDown) {
    auto &state = m_inputStates[binding];
    state.prevDown = state.down;

    if (isDown != state.down) {
        state.down = isDown;
        state.lastChangeTime = GetTime();
        state.consumed = false;
    }
}

bool InputManager::IsBindingDown(const InputBinding &binding) const {
    switch (binding.source) {
        case InputSource::Keyboard:
            return glfwGetKey(m_window->GetHandle(), binding.code) == GLFW_PRESS;

        case InputSource::Mouse:
            return glfwGetMouseButton(m_window->GetHandle(), binding.code) ==
                   GLFW_PRESS;

        case InputSource::Gamepad:
            if (binding.gamepadIndex < 0 || binding.gamepadIndex >= 4) return false;
            {
                const auto &state = m_gamepadStates[binding.gamepadIndex];
                if (!state.connected || binding.code < 0 || binding.code >= 15)
                    return false;
                return state.buttons[binding.code];
            }

        default:
            return false;
    }
}

bool InputManager::WasBindingPressed(const InputBinding &binding) const {
    auto it = m_inputStates.find(binding);
    if (it == m_inputStates.end()) return false;

    const auto &state = it->second;
    return !state.prevDown && state.down;
}

bool InputManager::WasBindingReleased(const InputBinding &binding) const {
    auto it = m_inputStates.find(binding);
    if (it == m_inputStates.end()) return false;

    const auto &state = it->second;
    return state.prevDown && !state.down;
}

float InputManager::ApplyDeadzone(float value, float deadzone) const {
    if (std::abs(value) < deadzone) return 0.0f;

    // Rescale from [deadzone, 1.0] to [0.0, 1.0]
    float sign = (value > 0.0f) ? 1.0f : -1.0f;
    float magnitude = std::abs(value);
    return sign * ((magnitude - deadzone) / (1.0f - deadzone));
}

// --- Action API ---

void InputManager::RegisterAction(const std::string &actionName,
                                  const InputBinding &binding) {
    std::lock_guard<std::mutex> lock(m_mutex);

    ActionBinding action;
    action.name = actionName;
    action.bindings.push_back(binding);
    m_actionBindings[actionName] = action;

    m_inputStates[binding] = ButtonState{};
}

void InputManager::AddActionBinding(const std::string &actionName,
                                    const InputBinding &binding) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it != m_actionBindings.end()) {
        it->second.bindings.push_back(binding);
        m_inputStates[binding] = ButtonState{};
    }
}

bool InputManager::IsActionDown(const std::string &actionName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    const auto &action = it->second;

    // Check modifier requirements
    if (action.requiresModifiers) {
        // Query current modifier state
        int currentMods = 0;
        if (glfwGetKey(m_window->GetHandle(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(m_window->GetHandle(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
            currentMods |= GLFW_MOD_SHIFT;
        }
        if (glfwGetKey(m_window->GetHandle(), GLFW_KEY_LEFT_CONTROL) ==
            GLFW_PRESS ||
            glfwGetKey(m_window->GetHandle(), GLFW_KEY_RIGHT_CONTROL) ==
            GLFW_PRESS) {
            currentMods |= GLFW_MOD_CONTROL;
        }
        if (glfwGetKey(m_window->GetHandle(), GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
            glfwGetKey(m_window->GetHandle(), GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
            currentMods |= GLFW_MOD_ALT;
        }
        if (glfwGetKey(m_window->GetHandle(), GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(m_window->GetHandle(), GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS) {
            currentMods |= GLFW_MOD_SUPER;
        }

        // Check if required modifiers match
        if ((currentMods & action.requiredMods) != action.requiredMods) {
            return false;
        }
    }

    for (const auto &binding: action.bindings) {
        if (IsBindingDown(binding)) {
            return true;
        }
    }

    return false;
}

bool InputManager::WasActionPressed(const std::string &actionName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    const auto &action = it->second;
    for (const auto &binding: action.bindings) {
        if (WasBindingPressed(binding)) {
            return true;
        }
    }

    return false;
}

bool InputManager::WasActionReleased(const std::string &actionName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    const auto &action = it->second;
    for (const auto &binding: action.bindings) {
        if (WasBindingReleased(binding)) {
            return true;
        }
    }

    return false;
}

bool InputManager::WasActionFirstPressed(const std::string &actionName) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return false;

    const auto &action = it->second;
    for (const auto &binding: action.bindings) {
        auto stateIt = m_inputStates.find(binding);
        if (stateIt != m_inputStates.end()) {
            auto &state = stateIt->second;
            if (!state.prevDown && state.down && !state.consumed) {
                state.consumed = true;
                return true;
            }
        }
    }

    return false;
}

void InputManager::RemapAction(const std::string &actionName,
                               const InputBinding &oldBinding,
                               const InputBinding &newBinding) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actionBindings.find(actionName);
    if (it == m_actionBindings.end()) return;

    auto &bindings = it->second.bindings;
    auto bindIt = std::find(bindings.begin(), bindings.end(), oldBinding);
    if (bindIt != bindings.end()) {
        *bindIt = newBinding;
        m_inputStates[newBinding] = ButtonState{};
    }
}

void InputManager::ClearActionBindings(const std::string &actionName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_actionBindings.erase(actionName);
}

// --- Axis API ---

void InputManager::RegisterAxis(const std::string &axisName,
                                const InputBinding &negativeBinding,
                                const InputBinding &positiveBinding,
                                float scale) {
    std::lock_guard<std::mutex> lock(m_mutex);

    AxisBinding axis;
    axis.name = axisName;
    axis.negativeBinding = negativeBinding;
    axis.positiveBinding = positiveBinding;
    axis.scale = scale;
    axis.isMouseAxis = false;
    axis.isGamepadAxis = false;

    m_axisBindings[axisName] = axis;

    m_inputStates[negativeBinding] = ButtonState{};
    m_inputStates[positiveBinding] = ButtonState{};
}

void InputManager::RegisterGamepadAxis(const std::string &axisName,
                                       GamepadAxis axis, int gamepadIndex,
                                       float scale, float deadzone) {
    std::lock_guard<std::mutex> lock(m_mutex);

    AxisBinding axisBinding;
    axisBinding.name = axisName;
    axisBinding.gamepadAxis = axis;
    axisBinding.gamepadIndex = gamepadIndex;
    axisBinding.scale = scale;
    axisBinding.deadzone = deadzone;
    axisBinding.isGamepadAxis = true;
    axisBinding.isMouseAxis = false;

    m_axisBindings[axisName] = axisBinding;
}

// MouseX & MouseY is always available
float InputManager::GetAxis(const std::string &axisName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_axisBindings.find(axisName);
    if (it == m_axisBindings.end()) return 0.0f;

    const auto &axis = it->second;

    // Handle gamepad axis
    if (axis.isGamepadAxis) {
        if (axis.gamepadIndex < 0 || axis.gamepadIndex >= 4) return 0.0f;
        const auto &state = m_gamepadStates[axis.gamepadIndex];
        if (!state.connected) return 0.0f;

        int axisIndex = static_cast<int>(axis.gamepadAxis);
        if (axisIndex < 0 || axisIndex >= 6) return 0.0f;

        float value = state.axes[axisIndex];
        value = ApplyDeadzone(value, axis.deadzone);
        return value * axis.scale;
    }

    // Handle mouse axis
    if (axis.isMouseAxis) {
        if (axisName == "MouseX") return m_mouseDelta.x * axis.scale;
        if (axisName == "MouseY") return m_mouseDelta.y * axis.scale;
        return 0.0f;
    }

    // Handle digital axis (keyboard/mouse buttons)
    float value = 0.0f;
    if (IsBindingDown(axis.negativeBinding)) value -= 1.0f;
    if (IsBindingDown(axis.positiveBinding)) value += 1.0f;

    return value * axis.scale;
}

glm::vec2 InputManager::GetAxis2D(const std::string &axisNameX,
                                  const std::string &axisNameY) const {
    return glm::vec2(GetAxis(axisNameX), GetAxis(axisNameY));
}

// --- Gamepad API ---

bool InputManager::IsGamepadConnected(int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return false;
    return m_gamepadStates[gamepadIndex].connected;
}

std::string InputManager::GetGamepadName(int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return "";
    return m_gamepadStates[gamepadIndex].name;
}

int InputManager::GetConnectedGamepadCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (const auto &state: m_gamepadStates) {
        if (state.connected) ++count;
    }
    return count;
}

bool InputManager::IsGamepadButtonDown(GamepadButton button,
                                       int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return false;

    const auto &state = m_gamepadStates[gamepadIndex];
    if (!state.connected) return false;

    int btnIndex = static_cast<int>(button);
    if (btnIndex < 0 || btnIndex >= 15) return false;

    return state.buttons[btnIndex];
}

bool InputManager::WasGamepadButtonPressed(GamepadButton button,
                                           int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return false;

    const auto &state = m_gamepadStates[gamepadIndex];
    if (!state.connected) return false;

    int btnIndex = static_cast<int>(button);
    if (btnIndex < 0 || btnIndex >= 15) return false;

    return state.buttons[btnIndex] && !state.prevButtons[btnIndex];
}

bool InputManager::WasGamepadButtonReleased(GamepadButton button,
                                            int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return false;

    const auto &state = m_gamepadStates[gamepadIndex];
    if (!state.connected) return false;

    int btnIndex = static_cast<int>(button);
    if (btnIndex < 0 || btnIndex >= 15) return false;

    return !state.buttons[btnIndex] && state.prevButtons[btnIndex];
}

float InputManager::GetGamepadAxis(GamepadAxis axis, int gamepadIndex) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (gamepadIndex < 0 || gamepadIndex >= 4) return 0.0f;

    const auto &state = m_gamepadStates[gamepadIndex];
    if (!state.connected) return 0.0f;

    int axisIndex = static_cast<int>(axis);
    if (axisIndex < 0 || axisIndex >= 6) return 0.0f;

    float value = state.axes[axisIndex];
    return ApplyDeadzone(value, m_defaultDeadzone);
}

glm::vec2 InputManager::GetGamepadLeftStick(int gamepadIndex) const {
    return glm::vec2(GetGamepadAxis(GamepadAxis::LeftX, gamepadIndex),
                     GetGamepadAxis(GamepadAxis::LeftY, gamepadIndex));
}

glm::vec2 InputManager::GetGamepadRightStick(int gamepadIndex) const {
    return glm::vec2(GetGamepadAxis(GamepadAxis::RightX, gamepadIndex),
                     GetGamepadAxis(GamepadAxis::RightY, gamepadIndex));
}

void InputManager::SetGamepadVibration(int gamepadIndex, float leftMotor,
                                       float rightMotor) {
    // Note: GLFW doesn't support rumble/vibration natively
    // TODO: use a platform-specific API
}

// --- Mouse API ---

glm::vec2 InputManager::GetMousePosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mousePos;
}

glm::vec2 InputManager::GetMouseDelta() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mouseDelta;
}

float InputManager::GetMouseWheel() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<float>(m_wheelDelta);
}

void InputManager::SetCursorMode(int mode) {
    if (m_window) {
        glfwSetInputMode(m_window->GetHandle(), GLFW_CURSOR, mode);
        ResetMouseDelta(); // Prevent huge delta on next frame
    }
}

void InputManager::ResetMouseDelta() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Get current position and reset delta
    double mx, my;
    glfwGetCursorPos(m_window->GetHandle(), &mx, &my);
    m_mousePos = glm::vec2(static_cast<float>(mx), static_cast<float>(my));
    m_prevMousePos = m_mousePos;
    m_mouseDelta = glm::vec2(0.0f);
}

// --- Raw Input ---

bool InputManager::IsKeyDown(int key) const {
    if (!m_window) return false;
    return glfwGetKey(m_window->GetHandle(), key) == GLFW_PRESS;
}

bool InputManager::IsMouseButtonDown(int button) const {
    if (!m_window) return false;
    return glfwGetMouseButton(m_window->GetHandle(), button) == GLFW_PRESS;
}

// --- Persistence ---

bool InputManager::SaveBindings(const std::string &filename) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    nlohmann::json j;

    // Save actions
    for (const auto &[name, action]: m_actionBindings) {
        nlohmann::json actionJson;
        nlohmann::json bindingsArray = nlohmann::json::array();

        for (const auto &binding: action.bindings) {
            nlohmann::json bindingJson;
            bindingJson["source"] = static_cast<int>(binding.source);
            bindingJson["code"] = binding.code;
            bindingJson["gamepadIndex"] = binding.gamepadIndex;
            bindingsArray.push_back(bindingJson);
        }

        actionJson["bindings"] = bindingsArray;
        actionJson["requiresModifiers"] = action.requiresModifiers;
        actionJson["requiredMods"] = action.requiredMods;
        j["actions"][name] = actionJson;
    }

    // Save axes
    for (const auto &[name, axis]: m_axisBindings) {
        nlohmann::json axisJson;
        axisJson["negativeSource"] = static_cast<int>(axis.negativeBinding.source);
        axisJson["negativeCode"] = axis.negativeBinding.code;
        axisJson["positiveSource"] = static_cast<int>(axis.positiveBinding.source);
        axisJson["positiveCode"] = axis.positiveBinding.code;
        axisJson["scale"] = axis.scale;
        axisJson["deadzone"] = axis.deadzone;
        axisJson["isMouseAxis"] = axis.isMouseAxis;
        axisJson["isGamepadAxis"] = axis.isGamepadAxis;

        if (axis.isGamepadAxis) {
            axisJson["gamepadAxis"] = static_cast<int>(axis.gamepadAxis);
            axisJson["gamepadIndex"] = axis.gamepadIndex;
        }

        j["axes"][name] = axisJson;
    }

    std::ofstream ofs(filename);
    if (!ofs) return false;

    ofs << j.dump(2);
    return true;
}

bool InputManager::LoadBindings(const std::string &filename) {
    std::ifstream ifs(filename);
    if (!ifs) return false;

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception &e) {
        std::cerr << "Failed to parse input bindings: " << e.what() << "\n";
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Load actions
    if (j.contains("actions")) {
        for (auto &[name, actionData]: j["actions"].items()) {
            ActionBinding action;
            action.name = name;
            action.requiresModifiers = actionData.value("requiresModifiers", false);
            action.requiredMods = actionData.value("requiredMods", 0);

            if (actionData.contains("bindings")) {
                for (const auto &bindingData: actionData["bindings"]) {
                    InputBinding binding;
                    binding.source =
                            static_cast<InputSource>(bindingData.value("source", 0));
                    binding.code = bindingData.value("code", GLFW_KEY_UNKNOWN);
                    binding.gamepadIndex = bindingData.value("gamepadIndex", 0);
                    action.bindings.push_back(binding);
                    m_inputStates[binding] = ButtonState{};
                }
            }

            m_actionBindings[action.name] = action;
        }
    }

    // Load axes
    if (j.contains("axes")) {
        for (auto &[name, axisData]: j["axes"].items()) {
            AxisBinding axis;
            axis.name = name;
            axis.negativeBinding.source =
                    static_cast<InputSource>(axisData.value("negativeSource", 0));
            axis.negativeBinding.code =
                    axisData.value("negativeCode", GLFW_KEY_UNKNOWN);
            axis.positiveBinding.source =
                    static_cast<InputSource>(axisData.value("positiveSource", 0));
            axis.positiveBinding.code =
                    axisData.value("positiveCode", GLFW_KEY_UNKNOWN);
            axis.scale = axisData.value("scale", 1.0f);
            axis.deadzone = axisData.value("deadzone", 0.15f);
            axis.isMouseAxis = axisData.value("isMouseAxis", false);
            axis.isGamepadAxis = axisData.value("isGamepadAxis", false);

            if (axis.isGamepadAxis) {
                axis.gamepadAxis =
                        static_cast<GamepadAxis>(axisData.value("gamepadAxis", 0));
                axis.gamepadIndex = axisData.value("gamepadIndex", 0);
            }

            m_axisBindings[axis.name] = axis;
            m_inputStates[axis.negativeBinding] = ButtonState{};
            m_inputStates[axis.positiveBinding] = ButtonState{};
        }
    }

    return true;
}

void InputManager::ResetFirstPressFlags() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &[binding, state]: m_inputStates) {
        state.consumed = false;
    }
}

size_t InputManager::GetQueuedEventCount() const {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_eventQueue.size();
}

void InputManager::ClearEventQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_eventQueue.clear();
}

// --- Callback Handlers ---

void InputManager::OnKey(int key, int scancode, int action, int mods) {
    InputBinding binding = KeyboardBinding(key);

    bool isDown = (action == GLFW_PRESS || action == GLFW_REPEAT);

    // Update state
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        UpdateButtonState(binding, isDown);
    }

    // Trigger action events
    if (action == GLFW_PRESS) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &[actionName, actionBinding]: m_actionBindings) {
            for (const auto &b: actionBinding.bindings) {
                if (b == binding) {
                    TriggerActionPressed(actionName);
                    break;
                }
            }
        }
    } else if (action == GLFW_RELEASE) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &[actionName, actionBinding]: m_actionBindings) {
            for (const auto &b: actionBinding.bindings) {
                if (b == binding) {
                    TriggerActionReleased(actionName);
                    break;
                }
            }
        }
    }
}

void InputManager::OnMouseButton(int button, int action, int mods) {
    InputBinding binding = MouseBinding(button);

    bool isDown = (action == GLFW_PRESS);

    // Update state
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        UpdateButtonState(binding, isDown);
    }

    // Trigger action events
    if (action == GLFW_PRESS) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &[actionName, actionBinding]: m_actionBindings) {
            for (const auto &b: actionBinding.bindings) {
                if (b == binding) {
                    TriggerActionPressed(actionName);
                    break;
                }
            }
        }
    } else if (action == GLFW_RELEASE) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &[actionName, actionBinding]: m_actionBindings) {
            for (const auto &b: actionBinding.bindings) {
                if (b == binding) {
                    TriggerActionReleased(actionName);
                    break;
                }
            }
        }
    }
}

// this could be called multiple times per frame ,and the delta is accumulated
// which could cause a jittery input in the right scenario
void InputManager::OnCursorPos(double x, double y) {
    std::lock_guard<std::mutex> lock(m_mutex);

    glm::vec2 newPos(static_cast<float>(x), static_cast<float>(y));
    // m_MouseDelta = (newPos - m_MousePos) * m_MouseSensitivity;
    m_mousePos = newPos;
}

void InputManager::OnScroll(double xoffset, double yoffset) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wheelDelta += yoffset;
}

void InputManager::OnCharInput(unsigned int codepoint) {
    if (m_callbackMode == CallbackMode::Immediate) {
        if (m_textInputCallback) {
            m_textInputCallback(codepoint);
        }
    } else {
        PushEvent(InputEvent::TextInput(codepoint, GetTime()));
    }
}