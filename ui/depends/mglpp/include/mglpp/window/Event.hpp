#ifndef MGLPP_EVENT_HPP
#define MGLPP_EVENT_HPP

#include "Keyboard.hpp"
#include "Mouse.hpp"
#include <stdint.h>

extern "C" {
#include <mgl/window/event.h>
}

/* These have to match the same structures in mgl event */

namespace mgl {
    class Event {
    public:
        struct SizeEvent {
            int width;
            int height;
        };

        struct TextEvent {
            uint32_t codepoint;
            int size;
            char str[5]; /* utf8, null terminated */
        };

        struct KeyEvent {
            Keyboard::Key code;
            mgl_key_states key_states;
        };

        struct MouseButtonEvent {
            Mouse::Button button;
            int x; /* mouse position relative to left of the window */
            int y; /* mouse position relative to top of the window */
            mgl_key_states key_states;
        };

        struct MouseWheelScrollEvent {
            int delta; /* positive = up, negative = down */
            int x; /* mouse position relative to left of the window */
            int y; /* mouse position relative to left of the window */
            mgl_key_states key_states;
        };

        struct MouseMoveEvent {
            int x; /* relative to left of the window */
            int y; /* relative to the top of the window */
            mgl_key_states key_states;
        };

        struct MonitorGenericEvent {
            const char *name; /* This name may only be valid until the next time |mgl_window_poll_event| is called */
            int id;
            int x;
            int y;
            int width;
            int height;
            int refresh_rate; /* 0 if unknown */
        };

        using MonitorConnectedEvent = MonitorGenericEvent;
        using MonitorPropertyChangedEvent = MonitorGenericEvent;

        struct MonitorDisconnectedEvent {
            int id;
        };

        enum class MappingChangedType : int {
            MODIFIER,
            KEYBOARD,
            POINTER
        };

        struct MappingChangedEvent {
            MappingChangedType type;
        };

        enum Type : int {
            Unknown,
            Closed, /* Window closed */
            Resized, /* Window resized */
            LostFocus,
            GainedFocus,
            TextEntered,
            KeyPressed,
            KeyReleased,
            MouseButtonPressed,
            MouseButtonReleased,
            MouseWheelScrolled,
            MouseMoved,
            MonitorConnected,
            MonitorDisconnected,
            MonitorPropertyChanged,
            MappingChanged
        };

        Type type;

        union {
            SizeEvent size;
            TextEvent text;
            KeyEvent key;
            MouseButtonEvent mouse_button;
            MouseWheelScrollEvent mouse_wheel_scroll;
            MouseMoveEvent mouse_move;
            MonitorConnectedEvent monitor_connected;
            MonitorDisconnectedEvent monitor_disconnected;
            MonitorPropertyChangedEvent monitor_property_changed;
            MappingChangedEvent mapping_changed;
        };
    };
}

#endif /* MGLPP_EVENT_HPP */
