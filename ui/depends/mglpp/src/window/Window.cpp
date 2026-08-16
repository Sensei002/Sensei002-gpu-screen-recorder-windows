#include "../../include/mglpp/window/Window.hpp"
#include "../../include/mglpp/window/Event.hpp"
#include "../../include/mglpp/graphics/Drawable.hpp"
#include "../../include/mglpp/graphics/Shader.hpp"

extern "C" {
#include <mgl/graphics/vertex.h>
#include <mgl/window/event.h>
#include <mgl/mgl.h>
}

namespace mgl {
    static bool clipboard_callback_interface(const unsigned char *data, size_t size, mgl_clipboard_type clipboard_type, void *userdata) {
        ClipboardCallback *clipboard_callback = (ClipboardCallback*)userdata;
        return (*clipboard_callback)(data, size, clipboard_type);
    }

    Window::Window() {
        
    }

    Window::~Window() {
        if(window_created)
            mgl_window_deinit(&window);
    }

    bool Window::create(const char *title, CreateParams create_params) {
        if(window_created)
            return false;

        if(mgl_window_create(&window, title, (const mgl_window_create_params*)&create_params) == 0) {
            window_created = true;
            return true;
        }
        return false;
    }

    bool Window::create(WindowHandle existing_window) {
        if(window_created)
            return false;

        if(mgl_window_init_from_existing_window(&window, existing_window) == 0) {
            window_created = true;
            return true;
        }
        return false;
    }

    bool Window::poll_event(Event &event) {
        return mgl_window_poll_event(&window, (mgl_event*)&event);
    }

    bool Window::inject_x11_event(XEvent *xev, Event &event) {
        return mgl_window_inject_x11_event(&window, xev, (mgl_event*)&event);
    }

    void Window::clear(Color color) {
        mgl_window_clear(&window, mgl_color{color.r, color.g, color.b, color.a});
    }

    void Window::draw(Drawable &drawable, Shader *shader) {
        // TODO: Make the opengl context active for this thread and window, if it already isn't
        if(shader)
            Shader::use(shader);
        drawable.draw(*this);
        if(shader)
            Shader::use(nullptr);
    }
    
    void Window::draw(const Vertex *vertices, size_t vertex_count, PrimitiveType primitive_type, mgl::vec2f position, Shader *shader) {
        // TODO: Make the opengl context active for this thread and window, if it already isn't
        if(shader)
            Shader::use(shader);

        mgl_context *context = mgl_get_context();
        mgl_vertices_draw(context, (const mgl_vertex*)vertices, vertex_count, (mgl_primitive_type)primitive_type, { position.x, position.y });

        if(shader)
            Shader::use(nullptr);
    }

    void Window::display() {
        mgl_window_display(&window);
    }

    void Window::set_visible(bool visible) {
        mgl_window_set_visible(&window, visible);
    }

    bool Window::is_open() const {
        return mgl_window_is_open(&window);
    }

    bool Window::has_focus() const {
        return mgl_window_has_focus(&window);
    }

    void Window::close() {
        mgl_window_close(&window);
    }

    void Window::set_title(const char *title) {
        mgl_window_set_title(&window, title);
    }

    void Window::set_framerate_limit(unsigned int fps) {
        mgl_window_set_framerate_limit(&window, fps);
    }

    void Window::set_vsync_enabled(bool enabled) {
        mgl_window_set_vsync_enabled(&window, enabled);
    }

    bool Window::is_vsync_enabled() const {
        return mgl_window_is_vsync_enabled(&window);
    }

    void Window::set_fullscreen(bool fullscreen) {
        mgl_window_set_fullscreen(&window, fullscreen);
    }

    bool Window::is_fullscreen() const {
        return mgl_window_is_fullscreen(&window);
    }

    void Window::set_low_latency(bool low_latency) {
        mgl_window_set_low_latency(&window, low_latency);
    }

    bool Window::is_low_latency_enabled() const {
        return mgl_window_is_low_latency_enabled(&window);
    }

    void Window::set_key_repeat_enabled(bool enabled) {
        mgl_window_set_key_repeat_enabled(&window, enabled);
    }

    void Window::set_cursor_visible(bool visible) {
        mgl_window_set_cursor_visible(&window, visible);
    }

    vec2i Window::get_size() const {
        return { window.size.x, window.size.y };
    }

    void Window::set_size(mgl::vec2i size) {
        mgl_window_set_size(&window, { size.x, size.y });
    }

    void Window::set_position(mgl::vec2i position) {
        mgl_window_set_position(&window, { position.x, position.y });
    }

    void Window::set_size_limits(mgl::vec2i minimum, mgl::vec2i maximum) {
        mgl_window_set_size_limits(&window, { minimum.x, minimum.y }, { maximum.x, maximum.y });
    }

    vec2i Window::get_mouse_position() const {
        return { window.cursor_position.x, window.cursor_position.y };
    }

    void Window::set_view(const View &new_view) {
        mgl_window_set_view(&window, (mgl_view*)&new_view);
    }

    View Window::get_view() {
        View view;
        mgl_window_get_view(&window, (mgl_view*)&view);
        return view;
    }

    void Window::set_scissor(const Scissor &scissor) {
        mgl_window_set_scissor(&window, (mgl_scissor*)&scissor);
    }

    Scissor Window::get_scissor() {
        Scissor scissor;
        mgl_window_get_scissor(&window, (mgl_scissor*)&scissor);
        return scissor;
    }

    bool Window::is_key_pressed(Keyboard::Key key) const {
        return mgl_window_is_key_pressed(&window, (mgl_key)key);
    }

    bool Window::is_mouse_button_pressed(Mouse::Button button) const {
        return mgl_window_is_mouse_button_pressed(&window, (mgl_mouse_button)button);
    }

    void Window::set_clipboard(const std::string &str) {
        mgl_window_set_clipboard(&window, str.c_str(), str.size());
    }

    bool Window::get_clipboard(ClipboardCallback callback) {
        return mgl_window_get_clipboard(&window, clipboard_callback_interface, &callback, MGL_CLIPBOARD_TYPE_ALL);
    }

    std::string Window::get_clipboard_string() {
        std::string result;
        char *str = nullptr;
        size_t size = 0;
        if(mgl_window_get_clipboard_string(&window, &str, &size)) {
            result.assign(str, size);
            free(str);
        }
        return result;
    }

    WindowHandle Window::get_system_handle() const {
        return mgl_window_get_system_handle(&window);
    }

    mgl_window* Window::internal_window() {
        return &window;
    }
}