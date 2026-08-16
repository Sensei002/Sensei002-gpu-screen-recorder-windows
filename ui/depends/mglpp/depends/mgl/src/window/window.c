#include "../../include/mgl/window/window.h"
#include "../../include/mgl/window/x11.h"
#ifndef MGL_NO_TEXT
#include "../../include/mgl/graphics/text.h"
#endif
#ifdef MGL_WAYLAND
#include "../../include/mgl/window/wayland.h"
#endif
#ifdef _WIN32
#include "../../include/mgl/window/win32.h"
#endif
#include "../../include/mgl/mgl.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int mgl_window_init(mgl_window *self, const char *title, const mgl_window_create_params *params, mgl_window_handle existing_window) {
    memset(self, 0, sizeof(*self));
    self->vsync_enabled = true;
    self->key_repeat_enabled = true;
    mgl_clock_init(&self->frame_timer);

    mgl_context *context = mgl_get_context();
    switch(context->window_system) {
        case MGL_WINDOW_SYSTEM_NATIVE:
            assert(false);
            break;
        case MGL_WINDOW_SYSTEM_X11:
#ifndef _WIN32
            return mgl_window_x11_init(self, title, params, existing_window) ? 0 : -1;
#else
            fprintf(stderr, "mgl error: mgl_window_init: init called with MGL_WINDOW_SYSTEM_X11, but mgl was built without X11 support\n");
            return -1;
#endif
        case MGL_WINDOW_SYSTEM_WAYLAND: {
#ifdef MGL_WAYLAND
            return mgl_window_wayland_init(self, title, params, existing_window) ? 0 : -1;
#else
            fprintf(stderr, "mgl error: mgl_window_init: init called with MGL_WINDOW_SYSTEM_WAYLAND, but mgl was built without Wayland support\n");
            return -1;
#endif
            break;
        }
        case MGL_WINDOW_SYSTEM_WIN32: {
#ifdef _WIN32
            return mgl_window_win32_init(self, title, params, existing_window) ? 0 : -1;
#else
            fprintf(stderr, "mgl error: mgl_window_init: init called with MGL_WINDOW_SYSTEM_WIN32, but mgl was built without Windows support\n");
            return -1;
#endif
            break;
        }
    }
    return -1;
}

int mgl_window_create(mgl_window *self, const char *title, const mgl_window_create_params *params) {
    return mgl_window_init(self, title, params, 0);
}

int mgl_window_init_from_existing_window(mgl_window *self, mgl_window_handle existing_window) {
    return mgl_window_init(self, "", NULL, existing_window);
}

void mgl_window_deinit(mgl_window *self) {
    if(self->deinit) {
#ifndef MGL_NO_TEXT
        mgl_text_renderer_deinit();
#endif
        self->deinit(self);
    }
}

void mgl_window_clear(mgl_window *self, mgl_color color) {
    (void)self;
    mgl_context *context = mgl_get_context();
    context->gl.glClearColor((float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f, (float)color.a / 255.0f);
    context->gl.glClear(GL_COLOR_BUFFER_BIT);
}

bool mgl_window_poll_event(mgl_window *self, mgl_event *event) {
    return self->poll_event(self, event);
}

bool mgl_window_inject_x11_event(mgl_window *self, XEvent *xev, mgl_event *event) {
    if(self->inject_x11_event)
        return self->inject_x11_event(self, xev, event);
    else
        return false;
}

void mgl_window_display(mgl_window *self) {
    mgl_context *context = mgl_get_context();
    self->swap_buffers(self);
    if(self->low_latency) {
        context->gl.glFlush();
        context->gl.glFinish();
    }

    if(self->frame_time_limit > 0.000001) {
        double time_left_to_sleep = self->frame_time_limit - mgl_clock_get_elapsed_time_seconds(&self->frame_timer);
        if(time_left_to_sleep > 0.000001) {
#ifdef _WIN32
            Sleep((DWORD)(time_left_to_sleep * 1000.0));
#else
            usleep(time_left_to_sleep * 1000000.0);
#endif
        }
        mgl_clock_restart(&self->frame_timer);
    } else if(self->vsync_enabled && self->frame_time_limit_monitor > 0.000001) {
        double time_left_to_sleep = self->frame_time_limit_monitor - mgl_clock_get_elapsed_time_seconds(&self->frame_timer);
        if(time_left_to_sleep > 0.000001) {
#ifdef _WIN32
            Sleep((DWORD)(time_left_to_sleep * 1000.0));
#else
            usleep(time_left_to_sleep * 1000000.0);
#endif
        }
        mgl_clock_restart(&self->frame_timer);
    }
}

void mgl_window_set_view(mgl_window *self, mgl_view *new_view) {
    mgl_context *context = mgl_get_context();
    self->view = *new_view;
    context->gl.glViewport(new_view->position.x, self->size.y - new_view->size.y - new_view->position.y, new_view->size.x, new_view->size.y);
    context->gl.glMatrixMode(GL_PROJECTION);
    context->gl.glLoadIdentity();
    context->gl.glOrtho(0.0, new_view->size.x, new_view->size.y, 0.0, 0.0, 1.0);
    context->gl.glMatrixMode(GL_MODELVIEW);
    context->gl.glLoadIdentity();
}

void mgl_window_get_view(mgl_window *self, mgl_view *view) {
    *view = self->view;
}

void mgl_window_set_scissor(mgl_window *self, const mgl_scissor *new_scissor) {
    mgl_context *context = mgl_get_context();
    self->scissor = *new_scissor;
    context->gl.glScissor(self->scissor.position.x, self->size.y - self->scissor.position.y - self->scissor.size.y, self->scissor.size.x, self->scissor.size.y);
}

void mgl_window_get_scissor(mgl_window *self, mgl_scissor *scissor) {
    *scissor = self->scissor;
}

void mgl_window_set_visible(mgl_window *self, bool visible) {
    self->set_visible(self, visible);
}

bool mgl_window_is_open(const mgl_window *self) {
    return self->open;
}

bool mgl_window_has_focus(const mgl_window *self) {
    return self->focused;
}

/* TODO: Track keys with events instead, but somehow handle window focus lost */
bool mgl_window_is_key_pressed(const mgl_window *self, mgl_key key) {
    if(key < 0 || key >= __MGL_NUM_KEYS__)
        return false;
    
    return self->is_key_pressed(self, key);
}

/* TODO: Track keys with events instead, but somehow handle window focus lost */
bool mgl_window_is_mouse_button_pressed(const mgl_window *self, mgl_mouse_button button) {
    if(button < 0 || button >= __MGL_NUM_MOUSE_BUTTONS__)
        return false;

    return self->is_mouse_button_pressed(self, button);
}

mgl_window_handle mgl_window_get_system_handle(const mgl_window *self) {
    return self->get_system_handle(self);
}

void mgl_window_close(mgl_window *self) {
    self->close(self);
}

void mgl_window_set_title(mgl_window *self, const char *title) {
    self->set_title(self, title);
}

void mgl_window_set_cursor_visible(mgl_window *self, bool visible) {
    self->set_cursor_visible(self, visible);
}

void mgl_window_set_framerate_limit(mgl_window *self, int fps) {
    if(fps <= 0)
        self->frame_time_limit = 0.0;
    else
        self->frame_time_limit = 1.0 / (double)fps;
}

void mgl_window_set_vsync_enabled(mgl_window *self, bool enabled) {
    self->set_vsync_enabled(self, enabled);
}

bool mgl_window_is_vsync_enabled(const mgl_window *self) {
    return self->is_vsync_enabled(self);
}

void mgl_window_set_fullscreen(mgl_window *self, bool fullscreen) {
    self->set_fullscreen(self, fullscreen);
}

bool mgl_window_is_fullscreen(const mgl_window *self) {
    return self->is_fullscreen(self);
}

void mgl_window_set_low_latency(mgl_window *self, bool low_latency) {
    self->low_latency = low_latency;
}

bool mgl_window_is_low_latency_enabled(const mgl_window *self) {
    return self->low_latency;
}

void mgl_window_set_position(mgl_window *self, mgl_vec2i position) {
    self->set_position(self, position);
}

void mgl_window_set_size(mgl_window *self, mgl_vec2i size) {
    self->set_size(self, size);
}

void mgl_window_set_size_limits(mgl_window *self, mgl_vec2i minimum, mgl_vec2i maximum) {
    self->set_size_limits(self, minimum, maximum);
}

void mgl_window_set_clipboard(mgl_window *self, const char *str, size_t size) {
    self->set_clipboard(self, str, size);
}

bool mgl_window_get_clipboard(mgl_window *self, mgl_clipboard_callback callback, void *userdata, uint32_t clipboard_types) {
    return self->get_clipboard(self, callback, userdata, clipboard_types);
}

typedef struct {
    char **str;
    size_t *size;
} ClipboardStringCallbackData;

static bool clipboard_copy_string_callback(const unsigned char *data, size_t size, mgl_clipboard_type clipboard_type, void *userdata) {
    ClipboardStringCallbackData *callback_data = userdata;
    if(clipboard_type != MGL_CLIPBOARD_TYPE_STRING) {
        free(*callback_data->str);
        *callback_data->str = NULL;
        *callback_data->size = 0;
        return false;
    }

    char *new_data = realloc(*callback_data->str, *callback_data->size + size);
    if(!new_data) {
        free(*callback_data->str);
        *callback_data->str = NULL;
        *callback_data->size = 0;
        return false;
    }

    memcpy(new_data + *callback_data->size, data, size);

    *callback_data->str = new_data;
    *callback_data->size += size;
    return true;
}

bool mgl_window_get_clipboard_string(mgl_window *self, char **str, size_t *size) {
    assert(str);
    assert(size);

    *str = NULL;
    *size = 0;

    ClipboardStringCallbackData callback_data;
    callback_data.str = str;
    callback_data.size = size;
    const bool success = mgl_window_get_clipboard(self, clipboard_copy_string_callback, &callback_data, MGL_CLIPBOARD_TYPE_STRING);
    if(!success) {
        free(*str);
        *str = NULL;
        *size = 0;
    }
    return success;
}

void mgl_window_set_key_repeat_enabled(mgl_window *self, bool enabled) {
    self->set_key_repeat_enabled(self, enabled);
}

void mgl_window_flush(mgl_window *self) {
    self->flush(self);
}

void* mgl_window_get_egl_display(mgl_window *self) {
    return self->get_egl_display(self);
}

void* mgl_window_get_egl_context(mgl_window *self) {
    return self->get_egl_context(self);
}

void mgl_window_for_each_active_monitor_output(mgl_window *self, mgl_active_monitor_callback callback, void *userdata) {
    self->for_each_active_monitor_output(self, callback, userdata);
}
