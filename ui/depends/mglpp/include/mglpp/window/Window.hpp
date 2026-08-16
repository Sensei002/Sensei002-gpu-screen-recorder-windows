#ifndef MGLPP_WINDOW_HPP
#define MGLPP_WINDOW_HPP

#include "../graphics/PrimitiveType.hpp"
#include "../graphics/Color.hpp"
#include "../system/vec.hpp"
#include "Keyboard.hpp"
#include "Mouse.hpp"
#include <stddef.h>
#include <string>
#include <functional>

extern "C" {
#include <mgl/window/window.h>
}

namespace mgl {
    typedef mgl_window_handle WindowHandle;

    class Event;
    class Drawable;
    class Shader;
    struct Vertex;

    struct View {
        vec2i position;
        vec2i size;
    };

    struct Scissor {
        vec2i position;
        vec2i size;
    };

    /*
        Return true to continue. |get_clipboard| returns false if this returns false.
        Note: |size| is the size of the current data, not the total data (if the callback only contains a part of the data).
    */
    using ClipboardCallback = std::function<bool(const unsigned char *data, size_t size, mgl_clipboard_type clipboard_type)>;

    class Window {
    public:
        /* Has to match mgl_window_create_params */
        struct CreateParams {
            mgl::vec2i position = {0, 0};
            mgl::vec2i size = {1280, 720};
            mgl::vec2i min_size; /* (0, 0) = no limit */
            mgl::vec2i max_size; /* (0, 0) = no limit */
            WindowHandle parent_window = 0; /* 0 = root window */
            bool hidden = false;
            bool override_redirect = false;
            bool support_alpha = false; /* support alpha for the window */
            bool hide_decorations = false; /* this is a hint, it may be ignored by the window manager */
            Color background_color = {0, 0, 0, 255};
            const char *class_name = nullptr;
            mgl_window_type window_type = MGL_WINDOW_TYPE_NORMAL;
            WindowHandle transient_for_window = 0; /* 0 = none */
            mgl_graphics_api graphics_api = MGL_GRAPHICS_API_EGL;
            bool request_depth_buffer = false;
            bool request_stencil_buffer = false;
            /* Only used when window_type == MGL_WINDOW_TYPE_OVERLAY on Wayland. */
            mgl_layer_shell_options layer_shell_options = {};
        };

        Window();
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        ~Window();

        bool create(const char *title, CreateParams create_params);
        // Initialize this window from an existing window
        bool create(WindowHandle existing_window);

        bool poll_event(Event &event);
        bool inject_x11_event(XEvent *xev, Event &event);

        void clear(Color color = Color(0, 0, 0, 255));
        void draw(Drawable &drawable, Shader *shader = nullptr);
        void draw(const Vertex *vertices, size_t vertex_count, PrimitiveType primitive_type, mgl::vec2f position = {0, 0}, Shader *shader = nullptr);
        void display();

        void set_visible(bool visible);
        bool is_open() const;
        bool has_focus() const;
        void close();
        void set_title(const char *title);
        // 0 = no fps limit, or limit fps to vsync if vsync is enabled
        void set_framerate_limit(unsigned int fps);
        void set_vsync_enabled(bool enabled);
        bool is_vsync_enabled() const;
        void set_fullscreen(bool fullscreen);
        bool is_fullscreen() const;
        // Enabling low latency may slightly increase cpu usage
        void set_low_latency(bool low_latency);
        bool is_low_latency_enabled() const;
        void set_key_repeat_enabled(bool enabled);
        void set_cursor_visible(bool visible);
        vec2i get_size() const;
        /* Note that window size in get_size doesn't update immediately as the change size request can be ignored by the window manager. A Resized event will be received if the resize occurred */
        void set_size(mgl::vec2i size);
        void set_position(mgl::vec2i position);
        /* if |minimum| is (0, 0) then there is no minimum limit, if |maximum| is (0, 0) then there is no maximum limit */
        void set_size_limits(mgl::vec2i minimum, mgl::vec2i maximum);
        vec2i get_mouse_position() const;
        
        /*
            This should be called every frame to retain the view.
            Make sure to set the view back to the previous view after rendering items
            by saving the previous view with |get_view| and then call
            |set_view| with that saved view.
        */
        void set_view(const View &new_view);
        View get_view();

        void set_scissor(const Scissor &scissor);
        Scissor get_scissor();

        bool is_key_pressed(Keyboard::Key key) const;
        bool is_mouse_button_pressed(Mouse::Button button) const;

        void set_clipboard(const std::string &str);
        bool get_clipboard(ClipboardCallback callback);
        std::string get_clipboard_string();

        WindowHandle get_system_handle() const;

        mgl_window* internal_window();
    private:
        mgl_window window;
        bool window_created = false;
    };
}

#endif /* MGLPP_WINDOW_HPP */
