#pragma once

#include <deque>
#include <functional>
#include <glm/vec2.hpp>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Window/Window.h"

// Input source type
enum class InputSource { Keyboard, Mouse, Gamepad };

// Gamepad axis identifiers
enum class GamepadAxis {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    LeftTrigger = 4,
    RightTrigger = 5
};

// Gamepad button identifiers (matches GLFW_GAMEPAD_BUTTON_*)
enum class GamepadButton {
    A = 0, // GLFW_GAMEPAD_BUTTON_A (Cross on PlayStation)
    B = 1, // GLFW_GAMEPAD_BUTTON_B (Circle on PlayStation)
    X = 2, // GLFW_GAMEPAD_BUTTON_X (Square on PlayStation)
    Y = 3, // GLFW_GAMEPAD_BUTTON_Y (Triangle on PlayStation)
    LeftBumper = 4, // GLFW_GAMEPAD_BUTTON_LEFT_BUMPER
    RightBumper = 5, // GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER
    Back = 6, // GLFW_GAMEPAD_BUTTON_BACK
    Start = 7, // GLFW_GAMEPAD_BUTTON_START
    Guide = 8, // GLFW_GAMEPAD_BUTTON_GUIDE
    LeftThumb = 9, // GLFW_GAMEPAD_BUTTON_LEFT_THUMB
    RightThumb = 10, // GLFW_GAMEPAD_BUTTON_RIGHT_THUMB
    DPadUp = 11, // GLFW_GAMEPAD_BUTTON_DPAD_UP
    DPadRight = 12, // GLFW_GAMEPAD_BUTTON_DPAD_RIGHT
    DPadDown = 13, // GLFW_GAMEPAD_BUTTON_DPAD_DOWN
    DPadLeft = 14 // GLFW_GAMEPAD_BUTTON_DPAD_LEFT
};

// Input binding (can be key, mouse button, or gamepad button)
struct InputBinding {
    InputSource source = InputSource::Keyboard;
    int code = GLFW_KEY_UNKNOWN; // Key code, mouse button, or gamepad button
    int gamepadIndex = 0; // Which gamepad (0-3, for multiplayer)

    bool operator==(const InputBinding &other) const {
        return source == other.source && code == other.code &&
               gamepadIndex == other.gamepadIndex;
    }

    bool operator<(const InputBinding &other) const {
        if (source != other.source) return source < other.source;
        if (code != other.code) return code < other.code;
        return gamepadIndex < other.gamepadIndex;
    }
};

// Hash function for InputBinding
namespace std {
    template<>
    struct hash<InputBinding> {
        size_t operator()(const InputBinding &b) const {
            return hash<int>()(static_cast<int>(b.source)) ^
                   (hash<int>()(b.code) << 1) ^ (hash<int>()(b.gamepadIndex) << 2);
        }
    };
} // namespace std

// Action binding with multiple input sources
struct ActionBinding {
    std::string name;
    std::vector<InputBinding> bindings; // Support multiple bindings per action
    bool requiresModifiers = false;
    int requiredMods = 0; // GLFW_MOD_SHIFT, GLFW_MOD_CONTROL, etc.
};

// Axis binding (supports keyboard, mouse, and gamepad axes)
struct AxisBinding {
    std::string name;
    InputBinding negativeBinding;
    InputBinding positiveBinding;
    GamepadAxis gamepadAxis = GamepadAxis::LeftX;
    int gamepadIndex = 0;
    float scale = 1.0f;
    float deadzone = 0.15f; // Deadzone for analog axes
    bool isMouseAxis = false;
    bool isGamepadAxis = false;
};

// Button/key state for edge detection
struct ButtonState {
    bool down = false;
    bool prevDown = false;
    double lastChangeTime = 0.0;
    bool consumed = false; // For first press tracking
};

// Gamepad state
struct GamepadState {
    bool connected = false;
    std::string name;
    std::vector<float> axes; // 6 axes (left stick, right stick, triggers)
    std::vector<bool> buttons; // 15 buttons
    std::vector<bool> prevButtons;

    GamepadState() {
        axes.resize(6, 0.0f);
        buttons.resize(15, false);
        prevButtons.resize(15, false);
    }
};

// Event types for event queue
enum class InputEventType {
    ActionPressed,
    ActionReleased,
    AxisChanged,
    GamepadConnected,
    GamepadDisconnected,
    TextInput
};

// Input event structure
struct InputEvent {
    InputEventType type;
    std::string actionOrAxisName;
    float value = 0.0f;
    int gamepadIndex = -1;
    unsigned int codepoint = 0;
    double timestamp = 0.0;

    // Factory methods
    static InputEvent ActionPressed(const std::string &action, double time) {
        InputEvent e;
        e.type = InputEventType::ActionPressed;
        e.actionOrAxisName = action;
        e.timestamp = time;
        return e;
    }

    static InputEvent ActionReleased(const std::string &action, double time) {
        InputEvent e;
        e.type = InputEventType::ActionReleased;
        e.actionOrAxisName = action;
        e.timestamp = time;
        return e;
    }

    static InputEvent AxisChanged(const std::string &axis, float val,
                                  double time) {
        InputEvent e;
        e.type = InputEventType::AxisChanged;
        e.actionOrAxisName = axis;
        e.value = val;
        e.timestamp = time;
        return e;
    }

    static InputEvent GamepadConnected(int index, double time) {
        InputEvent e;
        e.type = InputEventType::GamepadConnected;
        e.gamepadIndex = index;
        e.timestamp = time;
        return e;
    }

    static InputEvent GamepadDisconnected(int index, double time) {
        InputEvent e;
        e.type = InputEventType::GamepadDisconnected;
        e.gamepadIndex = index;
        e.timestamp = time;
        return e;
    }

    static InputEvent TextInput(unsigned int codepoint, double time) {
        InputEvent e;
        e.type = InputEventType::TextInput;
        e.codepoint = codepoint;
        e.timestamp = time;
        return e;
    }
};

// The Input Manager acts as an interface into the Window.
// This means most functionallity required by applications only require the InputManager
class InputManager {
public:
    enum class CallbackMode {
        Immediate, // Fire callbacks immediately (fast but can deadlock if
        // InputManager is called from callback)
        Queued // Queue events and process later (safe, 1-frame delay)
    };

    explicit InputManager(Window *window);

    ~InputManager();

    // Must be called once per frame (after Window->PollAllEvents())
    void Update();

    // Process queued events and fire callbacks (only needed in Queued mode)
    void ProcessEvents();

    // --- Configuration ---
    void SetCallbackMode(CallbackMode mode) { m_callbackMode = mode; }
    CallbackMode GetCallbackMode() const { return m_callbackMode; }
    void SetMaxQueueSize(size_t size) { m_maxQueueSize = size; }

    // --- Action API ---
    void RegisterAction(const std::string &actionName,
                        const InputBinding &binding);

    void AddActionBinding(const std::string &actionName,
                          const InputBinding &binding);

    bool IsActionDown(const std::string &actionName) const;

    bool WasActionPressed(const std::string &actionName) const;

    bool WasActionReleased(const std::string &actionName) const;

    bool WasActionFirstPressed(const std::string &actionName);

    void RemapAction(const std::string &actionName,
                     const InputBinding &oldBinding,
                     const InputBinding &newBinding);

    void ClearActionBindings(const std::string &actionName);

    // --- Axis API ---
    void RegisterAxis(const std::string &axisName,
                      const InputBinding &negativeBinding,
                      const InputBinding &positiveBinding, float scale = 1.0f);

    void RegisterGamepadAxis(const std::string &axisName, GamepadAxis axis,
                             int gamepadIndex = 0, float scale = 1.0f,
                             float deadzone = 0.15f);

    float GetAxis(const std::string &axisName) const;

    glm::vec2 GetAxis2D(const std::string &axisNameX,
                        const std::string &axisNameY) const;

    // --- Mouse API ---
    glm::vec2 GetMousePosition() const;

    glm::vec2 GetMouseDelta() const;

    float GetMouseWheel() const;

    void SetMouseSensitivity(float sensitivity) {
        m_mouseSensitivity = sensitivity;
    }

    void SetCursorMode(int mode);

    void ResetMouseDelta(); // Call when changing cursor mode to prevent jumps

    // --- Gamepad API ---
    bool IsGamepadConnected(int gamepadIndex = 0) const;

    std::string GetGamepadName(int gamepadIndex = 0) const;

    int GetConnectedGamepadCount() const;

    bool IsGamepadButtonDown(GamepadButton button, int gamepadIndex = 0) const;

    bool WasGamepadButtonPressed(GamepadButton button,
                                 int gamepadIndex = 0) const;

    bool WasGamepadButtonReleased(GamepadButton button,
                                  int gamepadIndex = 0) const;

    float GetGamepadAxis(GamepadAxis axis, int gamepadIndex = 0) const;

    glm::vec2 GetGamepadLeftStick(int gamepadIndex = 0) const;

    glm::vec2 GetGamepadRightStick(int gamepadIndex = 0) const;

    void SetGamepadDeadzone(float deadzone) { m_defaultDeadzone = deadzone; }

    void SetGamepadVibration(int gamepadIndex, float leftMotor, float rightMotor);

    // --- Raw Input Query ---
    bool IsKeyDown(int key) const;

    bool IsMouseButtonDown(int button) const;

    // --- Persistence ---
    bool SaveBindings(const std::string &filename) const;

    bool LoadBindings(const std::string &filename);

    // --- Callbacks ---
    using ActionCallback = std::function<void(const std::string &actionName)>;
    using AxisCallback =
    std::function<void(const std::string &axisName, float value)>;
    using GamepadCallback = std::function<void(int gamepadIndex)>;
    using TextInputCallback = std::function<void(unsigned int codepoint)>;

    void SetActionPressedCallback(ActionCallback callback) {
        m_actionPressedCallback = std::move(callback);
    }

    void SetActionReleasedCallback(ActionCallback callback) {
        m_actionReleasedCallback = std::move(callback);
    }

    void SetAxisChangedCallback(AxisCallback callback) {
        m_axisChangedCallback = std::move(callback);
    }

    void SetGamepadConnectedCallback(GamepadCallback callback) {
        m_gamepadConnectedCallback = std::move(callback);
    }

    void SetGamepadDisconnectedCallback(GamepadCallback callback) {
        m_gamepadDisconnectedCallback = std::move(callback);
    }

    void SetTextInputCallback(TextInputCallback callback) {
        m_textInputCallback = std::move(callback);
    }

    // --- Utility ---
    double GetTime() const { return glfwGetTime(); }

    void ResetFirstPressFlags();

    // Debug
    size_t GetQueuedEventCount() const;

    void ClearEventQueue();

private:
    Window *m_window = nullptr;

    // Bindings
    std::map<std::string, ActionBinding> m_actionBindings;
    std::map<std::string, AxisBinding> m_axisBindings;

    // State tracking
    std::unordered_map<InputBinding, ButtonState> m_inputStates;
    std::vector<GamepadState> m_gamepadStates;

    // Mouse state
    glm::vec2 m_mousePos{0.0f};
    glm::vec2 m_prevMousePos{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    float m_mouseSensitivity = 1.0f;
    double m_wheelDelta = 0.0;

    // Settings
    float m_defaultDeadzone = 0.15f;
    CallbackMode m_callbackMode = CallbackMode::Queued;
    size_t m_maxQueueSize = 1000;

    // Event queue
    std::deque<InputEvent> m_eventQueue;
    mutable std::mutex m_queueMutex;

    // Callbacks
    ActionCallback m_actionPressedCallback;
    ActionCallback m_actionReleasedCallback;
    AxisCallback m_axisChangedCallback;
    GamepadCallback m_gamepadConnectedCallback;
    GamepadCallback m_gamepadDisconnectedCallback;
    TextInputCallback m_textInputCallback;

    // Thread safety
    mutable std::mutex m_mutex;

    // Internal helpers
    void UpdateGamepads();

    void UpdateButtonState(const InputBinding &binding, bool isDown);

    bool IsBindingDown(const InputBinding &binding) const;

    bool WasBindingPressed(const InputBinding &binding) const;

    bool WasBindingReleased(const InputBinding &binding) const;

    float ApplyDeadzone(float value, float deadzone) const;

    // Event queue helpers
    void PushEvent(InputEvent event);

    void TriggerActionPressed(const std::string &actionName);

    void TriggerActionReleased(const std::string &actionName);

    // GLFW callback handlers
    void OnKey(int key, int scancode, int action, int mods);

    void OnMouseButton(int button, int action, int mods);

    void OnCursorPos(double x, double y);

    void OnScroll(double xoffset, double yoffset);

    void OnCharInput(unsigned int codepoint);
};

// Helper functions for creating bindings
inline InputBinding KeyboardBinding(int key) {
    return InputBinding{InputSource::Keyboard, key, 0};
}

inline InputBinding MouseBinding(int button) {
    return InputBinding{InputSource::Mouse, button, 0};
}

inline InputBinding GamepadBinding(GamepadButton button, int gamepadIndex = 0) {
    return InputBinding{
        InputSource::Gamepad, static_cast<int>(button),
        gamepadIndex
    };
}
