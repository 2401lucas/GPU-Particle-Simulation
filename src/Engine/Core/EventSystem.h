//
// Created by 2401Lucas on 2025-10-30.
//

#ifndef GPU_PARTICLE_SIM_EVENTSYSTEM_H
#define GPU_PARTICLE_SIM_EVENTSYSTEM_H

#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <any>
#include <stdexcept>

/// <summary>
/// Event data container that can hold arbitrary data.
/// Can pass parameters with events.
/// </summary>
struct EventData {
    std::unordered_map<std::string, std::any> data;

    // Helper methods for setting/getting typed data
    template<typename T>
    void Set(const std::string &key, const T &value) {
        data[key] = value;
    }

    template<typename T>
    T Get(const std::string &key) const {
        auto it = data.find(key);
        if (it != data.end()) {
            return std::any_cast<T>(it->second);
        }
        throw std::runtime_error("Key not found in event data: " + key);
    }

    template<typename T>
    T GetOr(const std::string &key, const T &defaultValue) const {
        auto it = data.find(key);
        if (it != data.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    bool Has(const std::string &key) const {
        return data.find(key) != data.end();
    }
};

using EventCallback = std::function<void(const EventData &)>;

/// <summary>
/// Handle returned when subscribing to events.
/// Used to unsubscribe from events.
/// </summary>
struct EventHandle {
    uint64_t id;
    bool IsValid() const { return id != 0; }
};

/// <summary>
/// Event system for decoupling communication between systems.
/// Allows systems to emit events without knowing who's listening.
/// </summary>
class EventSystem {
public:
    EventSystem();

    ~EventSystem();

    /// <summary>
    /// Subscribe to an event type with a callback.
    /// The callback will be invoked when the event is emitted.
    /// </summary>
    /// <param name="eventType">Name of the event to listen for</param>
    /// <param name="callback">Function to call when event occurs</param>
    /// <returns>Handle to the subscription (use to unsubscribe)</returns>
    EventHandle Subscribe(const std::string &eventType, EventCallback callback);

    /// <summary>
    /// Unsubscribe from an event using the handle.
    /// </summary>
    /// <param name="handle">Handle returned from Subscribe</param>
    void Unsubscribe(EventHandle handle);

    /// <summary>
    /// Unsubscribe all listeners for a specific event type.
    /// </summary>
    /// <param name="eventType">Event type to clear listeners for</param>
    void UnsubscribeAll(const std::string &eventType);

    /// <summary>
    /// Emit an event immediately (synchronous).
    /// All subscribed callbacks are invoked immediately.
    /// </summary>
    /// <param name="eventType">Name of the event</param>
    /// <param name="data">Data to pass to listeners</param>
    void Emit(const std::string &eventType, const EventData &data = EventData{});

    /// <summary>
    /// Queue an event to be processed later.
    /// Use ProcessQueued() to dispatch queued events.
    /// </summary>
    /// <param name="eventType">Name of the event</param>
    /// <param name="data">Data to pass to listeners</param>
    void EmitQueued(const std::string &eventType, const EventData &data = EventData{});

    /// <summary>
    /// Process all queued events.
    /// Call this once per frame at an appropriate time.
    /// </summary>
    void ProcessQueued();

    /// <summary>
    /// Clear all event subscriptions and queued events.
    /// </summary>
    void Clear();

    /// <summary>
    /// Get the number of listeners for a specific event type.
    /// </summary>
    size_t GetListenerCount(const std::string &eventType) const;

private:
    struct EventListener {
        uint64_t id;
        EventCallback callback;
    };

    struct QueuedEvent {
        std::string type;
        EventData data;
    };

    std::unordered_map<std::string, std::vector<EventListener> > m_listeners;
    std::vector<QueuedEvent> m_queuedEvents;
    uint64_t m_nextHandleId = 1;
};

namespace Events {
    // Application lifecycle
    constexpr const char *APP_INITIALIZED = "app_initialized";
    constexpr const char *APP_SHUTDOWN = "app_shutdown";
    constexpr const char *APP_FOCUS_CHANGED = "app_focus_changed";
    constexpr const char *APP_RESIZED = "app_resized";

    // Rendering
    constexpr const char *FRAME_BEGIN = "frame_begin";
    constexpr const char *FRAME_END = "frame_end";
    constexpr const char *EFFECT_SPAWN = "effect_spawn";

    // Gameplay
    constexpr const char *ENTITY_SPAWNED = "entity_spawned";
    constexpr const char *ENTITY_DESTROYED = "entity_destroyed";
    constexpr const char *COLLISION_OCCURRED = "collision_occurred";

    constexpr const char *TRANSFORM_UPDATED = "transform_updated";
}

#endif //GPU_PARTICLE_SIM_EVENTSYSTEM_H
