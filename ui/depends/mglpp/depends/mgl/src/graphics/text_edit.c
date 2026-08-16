#include "../../include/mgl/graphics/text_edit.h"
#include "../../include/mgl/window/window.h"
#include "../../include/mgl/window/event.h"
#include "../../include/mgl/window/key.h"
#include "../../include/mgl/window/mouse_button.h"
#include "../../include/mgl/mgl.h"

#include <pango/pangoft2.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

/* Returns the text fed to the layout (display text when masked, real otherwise). */
static const char* layout_text(const mgl_text_edit *editor) {
    return (editor->masked && editor->display_buffer)
        ? (const char*)editor->display_buffer->data
        : (const char*)editor->buffer->data;
}

static int layout_len(const mgl_text_edit *editor) {
    return (editor->masked && editor->display_buffer)
        ? (int)editor->display_buffer->len - 1
        : (int)editor->buffer->len - 1;
}

static int mask_byte_len(const mgl_text_edit *editor) {
    char buf[8];
    int n = g_unichar_to_utf8((gunichar)editor->mask_char, buf);
    return n > 0 ? n : 1;
}

/* Convert a real-buffer byte position to the equivalent layout-buffer byte position. */
static int real_to_layout_byte(const mgl_text_edit *editor, int real_byte) {
    if (!editor->masked)
        return real_byte;
    const char *text = (const char*)editor->buffer->data;
    int codepoints = (int)g_utf8_pointer_to_offset(text, text + real_byte);
    return codepoints * mask_byte_len(editor);
}

/* Convert a layout-buffer byte position to the equivalent real-buffer byte position. */
static int layout_to_real_byte(const mgl_text_edit *editor, int layout_byte) {
    if (!editor->masked)
        return layout_byte;
    int codepoints = layout_byte / mask_byte_len(editor);
    const char *text = (const char*)editor->buffer->data;
    const char *target = g_utf8_offset_to_pointer(text, codepoints);
    return (int)(target - text);
}

/* Overwrites |buf| with |data| (|len| bytes) plus a trailing '\0' not counted in len. */
static void buffer_set_bytes(GArray *buf, const char *data, int len) {
    g_array_set_size(buf, 0);
    g_array_append_vals(buf, data, (guint)len);
    char zero = '\0';
    g_array_append_val(buf, zero);
}

/* Rebuild the display buffer from the real buffer. */
static void rebuild_display_buffer(mgl_text_edit *editor) {
    if (!editor->display_buffer)
        editor->display_buffer = g_array_new(FALSE, TRUE, 1);

    char mask_utf8[8];
    int mask_len = g_unichar_to_utf8((gunichar)editor->mask_char, mask_utf8);
    if (mask_len <= 0) {
        mask_utf8[0] = '*';
        mask_len = 1;
    }

    const char *text = (const char*)editor->buffer->data;
    const int real_len = (int)editor->buffer->len - 1;
    const long codepoints = g_utf8_strlen(text, real_len);
    const guint total = (guint)(codepoints * mask_len);

    g_array_set_size(editor->display_buffer, total + 1);
    char *dst = (char*)editor->display_buffer->data;
    for (long i = 0; i < codepoints; i++)
        memcpy(dst + i * mask_len, mask_utf8, (size_t)mask_len);
    dst[total] = '\0';
}

static int utf8_next(const char *text, int byte_pos, int byte_len) {
    if (byte_pos >= byte_len)
        return byte_len;
    const char *next = g_utf8_find_next_char(text + byte_pos, text + byte_len);
    return next ? (int)(next - text) : byte_len;
}

static int utf8_prev(const char *text, int byte_pos) {
    if (byte_pos <= 0)
        return 0;
    const char *prev = g_utf8_find_prev_char(text, text + byte_pos);
    return prev ? (int)(prev - text) : 0;
}

static int is_space_at(const char *text, int byte_pos, int byte_len) {
    if (byte_pos < 0 || byte_pos >= byte_len)
        return 0;
    return g_unichar_isspace(g_utf8_get_char(text + byte_pos));
}

/* If there's a non-empty selection, writes ordered bounds to *lo, *hi and returns true. */
static bool get_ordered_selection(const mgl_text_edit *editor, int *lo, int *hi) {
    if (editor->sel_byte < 0 || editor->sel_byte == editor->caret_byte)
        return false;
    *lo = MIN(editor->caret_byte, editor->sel_byte);
    *hi = MAX(editor->caret_byte, editor->sel_byte);
    return true;
}

/* Shared prologue for caret movement: clears or anchors the selection. */
static void begin_selection_move(mgl_text_edit *editor, bool extend_selection) {
    if (!extend_selection)
        editor->sel_byte = -1;
    else if (editor->sel_byte < 0)
        editor->sel_byte = editor->caret_byte;
}

/* Shared epilogue after a buffer mutation: rebuild the display buffer if masked,
 * move the caret to |real_pos|, clear transient state, and mark the layout dirty. */
static void commit_buffer_edit(mgl_text_edit *editor, int real_pos) {
    if (editor->masked)
        rebuild_display_buffer(editor);
    editor->caret_byte = real_to_layout_byte(editor, real_pos);
    editor->prefer_prev_line = false;
    editor->sel_byte = -1;
    editor->goal_x_pu = -1;
    editor->dirty = true;
}

/* If there's a selection, delete it from the real buffer and return the new
 * real caret position. Otherwise return the current caret's real position.
 * Does not mark the editor dirty or rebuild the display buffer. */
static int consume_selection(mgl_text_edit *editor) {
    int lo, hi;
    if (get_ordered_selection(editor, &lo, &hi)) {
        int real_lo = layout_to_real_byte(editor, lo);
        int real_hi = layout_to_real_byte(editor, hi);
        g_array_remove_range(editor->buffer, (guint)real_lo, (guint)(real_hi - real_lo));
        editor->sel_byte = -1;
        return real_lo;
    }
    return layout_to_real_byte(editor, editor->caret_byte);
}

/* Copies digits (and an optional leading '-') from |src| to |dst|, terminated and capped
 * at |dst_size|. Returns bytes written (excluding the terminator). */
static int filter_number_chars(const char *src, size_t src_len, bool allow_negative,
                               char *dst, size_t dst_size) {
    if (dst_size == 0)
        return 0;
    size_t out = 0;
    bool has_minus = false;
    for (size_t i = 0; i < src_len && out + 1 < dst_size; i++) {
        char c = src[i];
        if (c == '-' && !has_minus && out == 0 && allow_negative) {
            dst[out++] = c;
            has_minus = true;
        } else if (c >= '0' && c <= '9') {
            dst[out++] = c;
        }
    }
    dst[out] = '\0';
    return (int)out;
}

/* Advance an iter to |target_line| (no-op if already there). */
static void layout_iter_seek_line(PangoLayoutIter *iter, int target_line) {
    for (int li = 0; li < target_line; li++) {
        if (!pango_layout_iter_next_line(iter))
            break;
    }
}

/* Filters |text| to integer characters and clamps to [min_val, max_val].
 * Writes result to |out_buf| (size 32). Returns resulting length. */
static int filter_and_clamp_number(const char *text, int min_val, int max_val, char out_buf[32]) {
    int out = filter_number_chars(text, strlen(text), min_val < 0, out_buf, 32);
    if (out > 0 && !(out == 1 && out_buf[0] == '-')) {
        char *end = NULL;
        long value = strtol(out_buf, &end, 10);
        if (end != out_buf) {
            value = CLAMP(value, min_val, max_val);
            out = snprintf(out_buf, 32, "%ld", value);
        }
    }
    return out;
}

int mgl_text_edit_len(const mgl_text_edit *editor) {
    return (int)editor->buffer->len - 1;
}

const char* mgl_text_edit_text(const mgl_text_edit *editor) {
    return editor->buffer->data;
}

bool mgl_text_edit_handle_event(mgl_text_edit *editor, const mgl_event *event) {
    switch(event->type) {
        case MGL_EVENT_TEXT_ENTERED: {
            if(!editor->focused)
                break;
            if(event->text.codepoint >= 32 && event->text.codepoint != 127) {
                if(editor->number_mode) {
                    gunichar cp = event->text.codepoint;
                    bool is_digit = (cp >= '0' && cp <= '9');
                    bool is_minus = (cp == '-');
                    if(!is_digit && !is_minus)
                        break;
                    if(is_minus) {
                        if(editor->num_min >= 0)
                            break;
                        /* '-' only valid at position 0 with no existing '-' */
                        int sel_lo, sel_hi;
                        bool has_selection = get_ordered_selection(editor, &sel_lo, &sel_hi);
                        int insert_pos = has_selection ? sel_lo : editor->caret_byte;
                        const char *text = layout_text(editor);
                        int text_len = layout_len(editor);
                        bool existing_minus = (text_len > 0 && text[0] == '-');
                        bool selection_covers_start = (has_selection && insert_pos == 0);
                        if(insert_pos != 0 || (existing_minus && !selection_covers_start))
                            break;
                    }
                }
                editor->caret_blink_time_seconds = mgl_clock_get_elapsed_time_seconds(&editor->caret_timer);
                mgl_text_edit_insert(editor, event->text.str, -1);
            }
            return true;
        }
        case MGL_EVENT_KEY_PRESSED: {
            if(!editor->focused)
                break;
            editor->caret_blink_time_seconds = mgl_clock_get_elapsed_time_seconds(&editor->caret_timer);
            switch(event->key.code) {
                case MGL_KEY_UP: {
                    mgl_text_edit_move_vertical(editor, -1, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_DOWN: {
                    mgl_text_edit_move_vertical(editor, 1, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_LEFT: {
                    if(event->key.key_states.control)
                        mgl_text_edit_move_word_left(editor, event->key.key_states.shift);
                    else
                        mgl_text_edit_move_left(editor, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_RIGHT: {
                    if(event->key.key_states.control)
                        mgl_text_edit_move_word_right(editor, event->key.key_states.shift);
                    else
                        mgl_text_edit_move_right(editor, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_HOME: {
                    mgl_text_edit_move_home(editor, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_END: {
                    mgl_text_edit_move_end(editor, event->key.key_states.shift);
                    break;
                }
                case MGL_KEY_BACKSPACE: {
                    if(event->key.key_states.control)
                        mgl_text_edit_delete_word(editor, -1);
                    else
                        mgl_text_edit_delete(editor, -1, 1);
                    break;
                }
                case MGL_KEY_DELETE: {
                    if(event->key.key_states.control)
                        mgl_text_edit_delete_word(editor, 1);
                    else
                        mgl_text_edit_delete(editor, 1, 1);
                    break;
                }
                case MGL_KEY_ENTER:
                case MGL_KEY_NUMPAD_ENTER:{
                    mgl_text_edit_insert(editor, "\n", 1);
                    break;
                }
                case MGL_KEY_A: {
                    if(event->key.key_states.control) {
                        editor->sel_byte = 0;
                        editor->caret_byte = layout_len(editor);
                        editor->prefer_prev_line = false;
                        editor->goal_x_pu = -1;
                    }
                    break;
                }
                case MGL_KEY_C: {
                    if(event->key.key_states.control) {
                        int lo, hi;
                        if(get_ordered_selection(editor, &lo, &hi))
                            mgl_window_set_clipboard(mgl_get_context()->current_window, (const char*)editor->buffer->data + lo, hi - lo);
                    }
                    break;
                }
                case MGL_KEY_V: {
                    if(event->key.key_states.control) {
                        char *clipboard_text = NULL;
                        size_t clipboard_text_size = 0;
                        if(mgl_window_get_clipboard_string(mgl_get_context()->current_window, &clipboard_text, &clipboard_text_size)) {
                            if(editor->number_mode) {
                                char filtered[32];
                                int out = filter_number_chars(clipboard_text, clipboard_text_size, editor->num_min < 0, filtered, sizeof(filtered));
                                mgl_text_edit_insert(editor, filtered, out);
                            } else {
                                mgl_text_edit_insert(editor, clipboard_text, clipboard_text_size);
                            }
                            free(clipboard_text);
                        }
                    }
                    break;
                }
            }
            return true;
        }
        case MGL_EVENT_MOUSE_BUTTON_PRESSED: {
            if(event->mouse_button.button == MGL_BUTTON_LEFT) {
                const int rel_x = event->mouse_button.x - editor->position.x;
                const int rel_y = event->mouse_button.y - editor->position.y;

                const mgl_vec2i size = mgl_text_edit_get_size(editor, true);
                if(rel_x >= 0 && rel_y >= 0 && rel_x <= size.x && rel_y <= size.y) {
                    mgl_text_edit_set_focused(editor, true);
                    if(event->mouse_button.key_states.shift) {
                        mgl_text_edit_set_caret_from_mouse(editor, rel_x, rel_y, true);
                    } else {
                        mgl_text_edit_set_caret_from_mouse(editor, rel_x, rel_y, false);
                        editor->sel_byte = editor->caret_byte;
                    }
                    editor->mouse_dragging = true;
                    return true;
                } else {
                    mgl_text_edit_set_focused(editor, false);
                }
            }
            break;
        }
        case MGL_EVENT_MOUSE_BUTTON_RELEASED: {
            if(event->mouse_button.button == MGL_BUTTON_LEFT) {
                editor->mouse_dragging = false;
                if(editor->sel_byte == editor->caret_byte) {
                    editor->sel_byte = -1;
                    return true;
                }
            }
            break;
        }
        case MGL_EVENT_MOUSE_MOVED: {
            if(editor->mouse_dragging) {
                const int rel_x = event->mouse_move.x - editor->position.x;
                const int rel_y = event->mouse_move.y - editor->position.y;
                mgl_text_edit_set_caret_from_mouse(editor, rel_x, rel_y, true);
                editor->caret_blink_time_seconds = mgl_clock_get_elapsed_time_seconds(&editor->caret_timer);
                return true;
            }
            break;
        }
    }
    return false;
}

void mgl_text_edit_sync(mgl_text_edit *editor) {
    if (!editor->dirty)
        return;
    pango_layout_set_text(editor->layout, layout_text(editor), layout_len(editor));
    editor->dirty = false;
}

void mgl_text_edit_set_single_paragraph_mode(mgl_text_edit *editor, bool setting) {
    editor->single_paragraph_mode = setting;
    pango_layout_set_single_paragraph_mode(editor->layout, setting);
    if (editor->wrap_width > 0) {
        if (setting) {
            pango_layout_set_width(editor->layout, -1);
        } else {
            int inner = editor->wrap_width - editor->margin_left - editor->margin_right;
            if (inner < 1) inner = 1;
            pango_layout_set_width(editor->layout, inner * PANGO_SCALE);
        }
    }
    editor->scroll_x = 0;
    editor->caret_byte = 0;
    editor->prefer_prev_line = false;
    editor->sel_byte = -1;
    editor->goal_x_pu = -1;
}

void mgl_text_edit_set_number_mode(mgl_text_edit *editor, bool enabled, int min_val, int max_val) {
    editor->number_mode = enabled;
    editor->num_min = min_val;
    editor->num_max = max_val;
}

void mgl_text_edit_clamp_number(mgl_text_edit *editor) {
    if(!editor->number_mode)
        return;
    const char *text = mgl_text_edit_text(editor);
    int len = mgl_text_edit_len(editor);
    if(len == 0 || (len == 1 && text[0] == '-'))
        return;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if(end == text)
        return;
    value = CLAMP(value, editor->num_min, editor->num_max);
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", value);
    if(strcmp(buf, text) != 0)
        mgl_text_edit_set_text(editor, buf);
}

void mgl_text_edit_set_mask_char(mgl_text_edit *editor, uint32_t mask_char) {
    if (mask_char == 0)
        mask_char = '*';
    if (editor->mask_char == mask_char)
        return;
    editor->mask_char = mask_char;
    if (editor->masked) {
        int real_caret = layout_to_real_byte(editor, editor->caret_byte);
        int real_sel = editor->sel_byte >= 0
                     ? layout_to_real_byte(editor, editor->sel_byte) : -1;
        rebuild_display_buffer(editor);
        editor->caret_byte = real_to_layout_byte(editor, real_caret);
        editor->sel_byte = real_sel >= 0 ? real_to_layout_byte(editor, real_sel) : -1;
        editor->dirty = true;
    }
}

void mgl_text_edit_set_masked(mgl_text_edit *editor, bool masked) {
    if (editor->masked == masked)
        return;

    /* Capture caret/sel in real coordinates so we can re-project after the toggle. */
    int real_caret = layout_to_real_byte(editor, editor->caret_byte);
    int real_sel = editor->sel_byte >= 0
                 ? layout_to_real_byte(editor, editor->sel_byte) : -1;

    editor->masked = masked;
    if (masked)
        rebuild_display_buffer(editor);

    editor->caret_byte = real_to_layout_byte(editor, real_caret);
    editor->sel_byte = real_sel >= 0 ? real_to_layout_byte(editor, real_sel) : -1;
    editor->prefer_prev_line = false;
    editor->goal_x_pu = -1;
    editor->scroll_x = 0;
    editor->dirty = true;
}

bool mgl_text_edit_is_masked(const mgl_text_edit *editor) {
    return editor->masked;
}

/* Set the caret to the end of the given visual line.
 * For space-wrapped lines: caret goes at the first trailing whitespace.
 * For non-whitespace wraps: caret goes at next line's start byte with
 * prefer_prev_line=1 so it renders at the trailing edge of the last char. */
static void mgl_text_edit_set_line_end(mgl_text_edit *editor, int line_index) {
    int text_len = layout_len(editor);
    int num_lines = pango_layout_get_line_count(editor->layout);

    if (line_index >= num_lines - 1) {
        editor->caret_byte = text_len;
        editor->prefer_prev_line = false;
        return;
    }

    PangoLayoutLine *line = pango_layout_get_line_readonly(editor->layout, line_index);
    int line_end = line->start_index + line->length;
    if (line_end > text_len)
        line_end = text_len;

    const char *text = layout_text(editor);
    int visible_end = line_end;
    while (visible_end > line->start_index &&
           is_space_at(text, utf8_prev(text, visible_end), text_len))
        visible_end = utf8_prev(text, visible_end);

    if (visible_end < line_end) {
        editor->caret_byte = visible_end;
        editor->prefer_prev_line = false;
    } else {
        editor->caret_byte = line_end;
        editor->prefer_prev_line = true;
    }
}

/* Resolve Pango's trailing value after cursor movement.
 * Advances to the next cluster boundary.  If that crosses a soft-wrap
 * boundary on a non-whitespace character, sets prefer_prev_line. */
static void mgl_text_edit_resolve_trailing(mgl_text_edit *editor, int trailing) {
    if (trailing <= 0)
        return;

    const char *text = layout_text(editor);
    int text_len = layout_len(editor);
    int current = editor->caret_byte;
    int next = utf8_next(text, current, text_len);

    mgl_text_edit_sync(editor);

    int line_current = 0;
    int line_next = 0;
    pango_layout_index_to_line_x(editor->layout, current, TRUE,
                                 &line_current, NULL);
    if (next < text_len)
        pango_layout_index_to_line_x(editor->layout, next, FALSE,
                                     &line_next, NULL);
    else
        line_next = line_current;

    editor->caret_byte = next;
    if (line_current != line_next && !is_space_at(text, current, text_len))
        editor->prefer_prev_line = true;
}

/* Get the visual line index for the current caret position.
 * When prefer_prev_line is set, looks up the previous character
 * with trailing=TRUE to find the previous line. */
static int mgl_text_edit_resolve_line_idx(mgl_text_edit *editor) {
    int line_index = 0;
    if (editor->prefer_prev_line && editor->caret_byte > 0) {
        int prev = utf8_prev(layout_text(editor), editor->caret_byte);
        pango_layout_index_to_line_x(editor->layout, prev, TRUE,
                                     &line_index, NULL);
    } else {
        pango_layout_index_to_line_x(editor->layout, editor->caret_byte,
                                     FALSE, &line_index, NULL);
    }
    return line_index;
}

void mgl_text_edit_init(mgl_text_edit *editor, const char *font_str, int wrap_width) {
    mgl_text_renderer_init();

    memset(editor, 0, sizeof(*editor));

    editor->buffer = g_array_new(FALSE, TRUE, 1);
    char zero = '\0';
    g_array_append_val(editor->buffer, zero);

    editor->font_desc = pango_font_description_from_string(font_str);
    editor->wrap_width = wrap_width;

    editor->layout = pango_layout_new(mgl_get_text_renderer()->context);
    pango_layout_set_font_description(editor->layout, editor->font_desc);
    if (wrap_width > 0) {
        pango_layout_set_width(editor->layout, wrap_width * PANGO_SCALE);
        pango_layout_set_wrap(editor->layout, PANGO_WRAP_WORD_CHAR);
    }
    pango_layout_set_text(editor->layout, "", -1);

    editor->sel_byte   = -1;
    editor->goal_x_pu  = -1;

    mgl_clock_init(&editor->caret_timer);
    editor->caret_blink_time_seconds = 0.0;

    editor->position.x = 0;
    editor->position.y = 0;
    editor->mouse_dragging = false;
    editor->focused = false;

    editor->number_mode = false;
    editor->num_min = 0;
    editor->num_max = 0;

    editor->mask_char = '*';
    editor->masked = false;
    editor->display_buffer = NULL;

    editor->margin_left = 0;
    editor->margin_top  = 0;
    editor->margin_right  = 0;
    editor->margin_bottom = 0;
}

void mgl_text_edit_deinit(mgl_text_edit *editor) {
    if (editor->layout)
        g_object_unref(editor->layout);
    if (editor->font_desc)
        pango_font_description_free(editor->font_desc);
    if (editor->buffer)
        g_array_free(editor->buffer, TRUE);
    if (editor->display_buffer)
        g_array_free(editor->display_buffer, TRUE);
    memset(editor, 0, sizeof(*editor));

    //mgl_text_renderer_deinit();
}

void mgl_text_edit_set_text(mgl_text_edit *editor, const char *text) {
    char filtered[32];
    int len;
    if(editor->number_mode) {
        len = filter_and_clamp_number(text, editor->num_min, editor->num_max, filtered);
        text = filtered;
    } else {
        len = (int)strlen(text);
    }

    buffer_set_bytes(editor->buffer, text, len);
    commit_buffer_edit(editor, len);
}

void mgl_text_edit_insert(mgl_text_edit *editor, const char *str, int len) {
    len = len >= 0 ? len : (int)strlen(str);
    if (len == 0)
        return;

    int real_caret = consume_selection(editor);
    g_array_insert_vals(editor->buffer, (guint)real_caret, str, (guint)len);
    commit_buffer_edit(editor, real_caret + len);
}

void mgl_text_edit_delete(mgl_text_edit *editor, int direction, int count) {
    int lo, hi;
    if (get_ordered_selection(editor, &lo, &hi)) {
        int real_lo = layout_to_real_byte(editor, lo);
        int real_hi = layout_to_real_byte(editor, hi);
        g_array_remove_range(editor->buffer, (guint)real_lo, (guint)(real_hi - real_lo));
        commit_buffer_edit(editor, real_lo);
        return;
    }

    int real_caret = layout_to_real_byte(editor, editor->caret_byte);
    int real_len = (int)editor->buffer->len - 1;
    const char *real = (const char*)editor->buffer->data;

    if (direction < 0) {
        if (real_caret <= 0)
            return;
        int start = real_caret;
        for (int i = 0; i < count; i++)
            start = utf8_prev(real, start);
        g_array_remove_range(editor->buffer, (guint)start, (guint)(real_caret - start));
        real_caret = start;
    } else {
        if (real_caret >= real_len)
            return;
        int end = real_caret;
        for (int i = 0; i < count && end < real_len; i++)
            end = utf8_next(real, end, real_len);
        g_array_remove_range(editor->buffer, (guint)real_caret, (guint)(end - real_caret));
    }

    commit_buffer_edit(editor, real_caret);
}

mgl_range mgl_text_edit_get_selection(const mgl_text_edit *editor) {
    int lo, hi;
    if (!get_ordered_selection(editor, &lo, &hi))
        return (mgl_range){ 0, 0 };
    int real_lo = layout_to_real_byte(editor, lo);
    int real_hi = layout_to_real_byte(editor, hi);
    return (mgl_range){ real_lo, real_hi - real_lo };
}

void mgl_text_edit_move_left(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    if (editor->caret_byte <= 0)
        return;

    int new_index = 0;
    int new_trailing = 0;
    pango_layout_move_cursor_visually(editor->layout, TRUE,
                                     editor->caret_byte, 0, -1,
                                     &new_index, &new_trailing);

    editor->caret_byte = CLAMP(new_index, 0, layout_len(editor));
    editor->prefer_prev_line = false;
    mgl_text_edit_resolve_trailing(editor, new_trailing);
    editor->goal_x_pu = -1;
}

void mgl_text_edit_move_right(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    if (editor->caret_byte >= layout_len(editor))
        return;

    /* If at a wrap boundary showing as end-of-prev-line,
     * just reveal the actual position on the next line. */
    if (editor->prefer_prev_line) {
        editor->prefer_prev_line = false;
        editor->goal_x_pu = -1;
        return;
    }

    int new_index = 0;
    int new_trailing = 0;
    pango_layout_move_cursor_visually(editor->layout, TRUE,
                                     editor->caret_byte, 0, +1,
                                     &new_index, &new_trailing);

    editor->caret_byte = CLAMP(new_index, 0, layout_len(editor));
    editor->prefer_prev_line = false;
    mgl_text_edit_resolve_trailing(editor, new_trailing);
    editor->goal_x_pu = -1;
}

void mgl_text_edit_move_word_left(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    if (editor->caret_byte <= 0)
        return;

    gint num_attrs = 0;
    const PangoLogAttr *attrs = pango_layout_get_log_attrs_readonly(editor->layout, &num_attrs);

    const char *text = layout_text(editor);
    int char_index = (int)g_utf8_pointer_to_offset(text, text + editor->caret_byte);
    char_index--;
    while (char_index > 0 && !attrs[char_index].is_word_start)
        char_index--;

    editor->caret_byte = (int)(g_utf8_offset_to_pointer(text, char_index) - text);
    editor->prefer_prev_line = false;
    editor->goal_x_pu = -1;
}

void mgl_text_edit_move_word_right(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    int text_len = layout_len(editor);
    if (editor->caret_byte >= text_len)
        return;

    gint num_attrs = 0;
    const PangoLogAttr *attrs = pango_layout_get_log_attrs_readonly(editor->layout, &num_attrs);

    const char *text = layout_text(editor);
    int start_pos = editor->caret_byte;
    int char_index = (int)g_utf8_pointer_to_offset(text, text + start_pos);

    char_index++;
    while (char_index < num_attrs && !attrs[char_index].is_word_end)
        char_index++;
    if (char_index >= num_attrs)
        char_index = num_attrs - 1;

    int target_pos = (int)(g_utf8_offset_to_pointer(text, char_index) - text);
    if (target_pos > text_len)
        target_pos = text_len;

    int start_line = 0;
    pango_layout_index_to_line_x(editor->layout, start_pos, FALSE, &start_line, NULL);

    /* If the word scan crossed a visual line boundary, stop at the line end
     * (unless the caret is already at the line end). */
    bool already_at_end = editor->prefer_prev_line;
    if (!already_at_end) {
        PangoLayoutLine *sline = pango_layout_get_line_readonly(editor->layout, start_line);
        int sline_end = MIN(sline->start_index + sline->length, text_len);

        int sline_visible_end = sline_end;
        while (sline_visible_end > sline->start_index &&
               is_space_at(text, utf8_prev(text, sline_visible_end), text_len))
            sline_visible_end = utf8_prev(text, sline_visible_end);

        if (start_pos >= sline_visible_end)
            already_at_end = true;
    }

    int end_line;
    if (target_pos < text_len)
        pango_layout_index_to_line_x(editor->layout, target_pos, FALSE, &end_line, NULL);
    else
        end_line = pango_layout_get_line_count(editor->layout) - 1;

    if (end_line != start_line && !already_at_end) {
        mgl_text_edit_set_line_end(editor, start_line);
    } else {
        editor->caret_byte = target_pos;
        editor->prefer_prev_line = false;
    }
    editor->goal_x_pu = -1;
}

void mgl_text_edit_delete_word(mgl_text_edit *editor, int direction) {
    if (editor->sel_byte >= 0 && editor->sel_byte != editor->caret_byte) {
        mgl_text_edit_delete(editor, direction, 1);
        return;
    }

    mgl_text_edit_sync(editor);

    const char *text = layout_text(editor);
    int text_len = layout_len(editor);

    gint num_attrs = 0;
    const PangoLogAttr *attrs = pango_layout_get_log_attrs_readonly(editor->layout, &num_attrs);
    int char_index = (int)g_utf8_pointer_to_offset(text, text + editor->caret_byte);

    int target;
    if (direction < 0) {
        target = char_index - 1;
        while (target > 0 && !attrs[target].is_word_start)
            target--;
        target = MAX(target, 0);
    } else {
        target = char_index + 1;
        while (target < num_attrs && !attrs[target].is_word_end)
            target++;
        target = MIN(target, num_attrs - 1);
    }

    int target_pos = CLAMP((int)(g_utf8_offset_to_pointer(text, target) - text), 0, text_len);
    int lo_layout = MIN(target_pos, editor->caret_byte);
    int hi_layout = MAX(target_pos, editor->caret_byte);
    if (lo_layout == hi_layout)
        return;

    int real_lo = layout_to_real_byte(editor, lo_layout);
    int real_hi = layout_to_real_byte(editor, hi_layout);
    g_array_remove_range(editor->buffer, (guint)real_lo, (guint)(real_hi - real_lo));
    commit_buffer_edit(editor, real_lo);
}

void mgl_text_edit_move_vertical(mgl_text_edit *editor, int direction, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    /* Capture goal X on first vertical move */
    if (editor->goal_x_pu < 0) {
        if (editor->prefer_prev_line && editor->caret_byte > 0) {
            int prev = utf8_prev(layout_text(editor), editor->caret_byte);
            int dummy_line = 0;
            pango_layout_index_to_line_x(editor->layout, prev, TRUE,
                                         &dummy_line, &editor->goal_x_pu);
        } else {
            PangoRectangle strong;
            pango_layout_get_cursor_pos(editor->layout, editor->caret_byte, &strong, NULL);
            editor->goal_x_pu = strong.x;
        }
    }

    int current_line = mgl_text_edit_resolve_line_idx(editor);
    int target_line = current_line + direction;
    int num_lines = pango_layout_get_line_count(editor->layout);

    if (target_line < 0 || target_line >= num_lines) {
        editor->caret_byte = (direction < 0) ? 0 : layout_len(editor);
        editor->prefer_prev_line = false;
        return;
    }

    /* Find Y midpoint of the target line */
    PangoLayoutIter *iter = pango_layout_get_iter(editor->layout);
    layout_iter_seek_line(iter, target_line);
    PangoRectangle line_rect;
    pango_layout_iter_get_line_extents(iter, NULL, &line_rect);
    pango_layout_iter_free(iter);
    int target_y = line_rect.y + line_rect.height / 2;

    int new_index = 0;
    int new_trailing = 0;
    pango_layout_xy_to_index(editor->layout, editor->goal_x_pu, target_y,
                             &new_index, &new_trailing);

    editor->caret_byte = CLAMP(new_index, 0, layout_len(editor));
    editor->prefer_prev_line = false;
    mgl_text_edit_resolve_trailing(editor, new_trailing);
    /* Do NOT reset goal_x_pu — it persists across vertical moves */
}

void mgl_text_edit_move_home(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    int line_index = mgl_text_edit_resolve_line_idx(editor);
    PangoLayoutLine *line = pango_layout_get_line_readonly(editor->layout, line_index);
    editor->caret_byte = line->start_index;
    editor->prefer_prev_line = false;
    editor->goal_x_pu = -1;
}

void mgl_text_edit_move_end(mgl_text_edit *editor, bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    mgl_text_edit_set_line_end(editor, mgl_text_edit_resolve_line_idx(editor));
    editor->goal_x_pu = -1;
}

void mgl_text_edit_set_caret_from_mouse(mgl_text_edit *editor,
                                        int mouse_x, int mouse_y,
                                        bool extend_selection) {
    mgl_text_edit_sync(editor);
    begin_selection_move(editor, extend_selection);

    const int scroll_offset = (editor->single_paragraph_mode && editor->wrap_width > 0)
                            ? editor->scroll_x : 0;
    int inner_x = mouse_x - editor->margin_left;
    int inner_y = mouse_y - editor->margin_top;
    if (inner_x < 0) inner_x = 0;
    if (inner_y < 0) inner_y = 0;
    int pango_x = (inner_x + scroll_offset) * PANGO_SCALE;
    int pango_y = inner_y * PANGO_SCALE;

    int byte_index = 0;
    int trailing = 0;
    pango_layout_xy_to_index(editor->layout, pango_x, pango_y, &byte_index, &trailing);

    int text_len = layout_len(editor);
    byte_index = CLAMP(byte_index, 0, text_len);

    /* Pango never returns trailing=1 at the end of wrapped lines.
     * Detect clicks past the glyph's trailing edge and go to line end. */
    if (!trailing && byte_index < text_len) {
        int line_index = 0;
        int trailing_x = 0;
        pango_layout_index_to_line_x(editor->layout, byte_index, TRUE, &line_index, &trailing_x);
        int num_lines = pango_layout_get_line_count(editor->layout);
        if (pango_x >= trailing_x && line_index < num_lines - 1) {
            mgl_text_edit_set_line_end(editor, line_index);
            editor->goal_x_pu = -1;
            return;
        }
    }

    editor->caret_byte = byte_index;
    editor->prefer_prev_line = false;
    mgl_text_edit_resolve_trailing(editor, trailing);
    editor->goal_x_pu = -1;
}

typedef struct {
    int x, y, width, height;
} mgl_caret_rect;

static mgl_caret_rect mgl_text_edit_get_caret_rect(mgl_text_edit *editor) {
    mgl_text_edit_sync(editor);

    mgl_caret_rect rect = { 0, 0, 2, 0 };

    if (editor->prefer_prev_line && editor->caret_byte > 0) {
        /* Caret is at a soft-wrap boundary but should render at the
         * trailing edge of the last character on the previous line. */
        int prev_byte = utf8_prev(layout_text(editor), editor->caret_byte);
        int line_index = 0;
        int x_pu = 0;
        pango_layout_index_to_line_x(editor->layout, prev_byte, TRUE,
                                     &line_index, &x_pu);

        PangoLayoutIter *iter = pango_layout_get_iter(editor->layout);
        layout_iter_seek_line(iter, line_index);
        PangoRectangle line_rect;
        pango_layout_iter_get_line_extents(iter, NULL, &line_rect);
        rect.x = PANGO_PIXELS(x_pu);
        rect.y = PANGO_PIXELS(line_rect.y);
        rect.height = PANGO_PIXELS(line_rect.height);
        pango_layout_iter_free(iter);
    } else {
        PangoRectangle strong;
        pango_layout_get_cursor_pos(editor->layout, editor->caret_byte,
                                    &strong, NULL);
        rect.x = PANGO_PIXELS(strong.x);
        rect.y = PANGO_PIXELS(strong.y);
        rect.height = PANGO_PIXELS(strong.height);
    }

    return rect;
}

static void mgl_text_edit_render_selection(mgl_text_edit *editor,
                                           float origin_x, float origin_y,
                                           mgl_color color) {
    mgl_context *context = mgl_get_context();

    int sel_lo, sel_hi;
    if (!get_ordered_selection(editor, &sel_lo, &sel_hi))
        return;
    mgl_text_edit_sync(editor);

    PangoLayoutIter *iter = pango_layout_get_iter(editor->layout);
    do {
        PangoLayoutLine *line = pango_layout_iter_get_line_readonly(iter);
        int line_start = line->start_index;
        int line_end   = line_start + line->length;

        if (line_end <= sel_lo || line_start >= sel_hi)
            continue;

        int range_lo = MAX(sel_lo, line_start);
        int range_hi = MIN(sel_hi, line_end);

        int *x_ranges = NULL;
        int  x_range_count = 0;
        pango_layout_line_get_x_ranges(line, range_lo, range_hi,
                                       &x_ranges, &x_range_count);

        PangoRectangle line_rect;
        pango_layout_iter_get_line_extents(iter, NULL, &line_rect);

        context->gl.glDisable(GL_TEXTURE_2D);
        context->gl.glColor4ub(color.r, color.g, color.b, color.a);
        for (int i = 0; i < x_range_count; i++) {
            float sel_x = origin_x + (float)PANGO_PIXELS(x_ranges[i * 2]);
            float sel_x2 = origin_x + (float)PANGO_PIXELS(x_ranges[i * 2 + 1]);
            float sel_y = origin_y + (float)PANGO_PIXELS(line_rect.y);
            float sel_h = (float)PANGO_PIXELS(line_rect.height);

            context->gl.glBegin(GL_QUADS);
            context->gl.glVertex3f(sel_x,  sel_y, 0.0f);
            context->gl.glVertex3f(sel_x2, sel_y, 0.0f);
            context->gl.glVertex3f(sel_x2, sel_y + sel_h, 0.0f);
            context->gl.glVertex3f(sel_x,  sel_y + sel_h, 0.0f);
            context->gl.glEnd();
        }
        context->gl.glEnable(GL_TEXTURE_2D);
        g_free(x_ranges);
    } while (pango_layout_iter_next_line(iter));

    pango_layout_iter_free(iter);
}

static void mgl_text_edit_render_caret(mgl_text_edit *editor,
                                       float origin_x, float origin_y,
                                       mgl_color color) {
    mgl_context *context = mgl_get_context();
    mgl_caret_rect caret = mgl_text_edit_get_caret_rect(editor);

    context->gl.glDisable(GL_TEXTURE_2D);
    context->gl.glColor4ub(color.r, color.g, color.b, color.a);
    context->gl.glBegin(GL_QUADS);
    context->gl.glVertex3f(origin_x + caret.x,               origin_y + caret.y, 0.0f);
    context->gl.glVertex3f(origin_x + caret.x + caret.width, origin_y + caret.y, 0.0f);
    context->gl.glVertex3f(origin_x + caret.x + caret.width, origin_y + caret.y + caret.height, 0.0f);
    context->gl.glVertex3f(origin_x + caret.x,               origin_y + caret.y + caret.height, 0.0f);
    context->gl.glEnd();
    context->gl.glEnable(GL_TEXTURE_2D);
}

bool mgl_text_edit_is_focused(const mgl_text_edit *editor) {
    return editor->focused;
}

void mgl_text_edit_set_focused(mgl_text_edit *editor, bool focused) {
    editor->focused = focused;
    if (focused) {
        editor->caret_blink_time_seconds = mgl_clock_get_elapsed_time_seconds(&editor->caret_timer);
        return;
    }

    editor->sel_byte = -1;
    editor->mouse_dragging = false;
    mgl_text_edit_clamp_number(editor);
    if (editor->single_paragraph_mode && editor->wrap_width > 0) {
        const mgl_vec2i text_size = mgl_text_edit_get_size(editor, false);
        if (text_size.x <= editor->wrap_width)
            editor->scroll_x = 0;
    }
}

static mgl_scissor scissor_get_sub_area(const mgl_scissor *parent, const mgl_scissor *child) {
    const int x1 = child->position.x > parent->position.x ? child->position.x : parent->position.x;
    const int y1 = child->position.y > parent->position.y ? child->position.y : parent->position.y;
    const int x2_child  = child->position.x  + child->size.x;
    const int y2_child  = child->position.y  + child->size.y;
    const int x2_parent = parent->position.x + parent->size.x;
    const int y2_parent = parent->position.y + parent->size.y;
    const int x2 = x2_child < x2_parent ? x2_child : x2_parent;
    const int y2 = y2_child < y2_parent ? y2_child : y2_parent;
    return (mgl_scissor){
        .position = { x1, y1 },
        .size     = { x2 - x1 > 0 ? x2 - x1 : 0, y2 - y1 > 0 ? y2 - y1 : 0 }
    };
}

static void mgl_text_edit_update_scroll(mgl_text_edit *editor) {
    if (!editor->single_paragraph_mode || editor->wrap_width <= 0)
        return;
    const int visible_width = editor->wrap_width - editor->margin_left - editor->margin_right;
    mgl_caret_rect caret = mgl_text_edit_get_caret_rect(editor);
    if (caret.x < editor->scroll_x)
        editor->scroll_x = caret.x;
    else if (caret.x + caret.width > editor->scroll_x + visible_width)
        editor->scroll_x = caret.x + caret.width - visible_width;
    if (editor->scroll_x < 0)
        editor->scroll_x = 0;
}

void mgl_text_edit_draw(mgl_text_edit *editor, mgl_color color) {
    mgl_text_edit_sync(editor);
    mgl_text_edit_update_scroll(editor);
    mgl_text_renderer *text_renderer = mgl_get_text_renderer();

    const bool scrollable = editor->single_paragraph_mode && editor->wrap_width > 0;
    const float origin_x = editor->position.x + (float)editor->margin_left - (scrollable ? (float)editor->scroll_x : 0.0f);
    const float origin_y = editor->position.y + (float)editor->margin_top;

    mgl_window *window = mgl_get_context()->current_window;
    mgl_scissor saved_scissor;
    if (scrollable) {
        mgl_window_get_scissor(window, &saved_scissor);
        PangoRectangle logical_rect;
        pango_layout_get_pixel_extents(editor->layout, NULL, &logical_rect);
        const int inner_width = editor->wrap_width - editor->margin_left - editor->margin_right;
        const mgl_scissor child = {
            .position = { (int)editor->position.x + editor->margin_left, (int)editor->position.y + editor->margin_top },
            .size     = { inner_width > 0 ? inner_width : 0, logical_rect.height }
        };
        const mgl_scissor clipped = scissor_get_sub_area(&saved_scissor, &child);
        mgl_window_set_scissor(window, &clipped);
    }

    mgl_text_edit_render_selection(editor, origin_x, origin_y, (mgl_color){63, 89, 153, 178 * (color.a / 255)});
    mgl_text_renderer_render_layout(text_renderer, editor->layout, origin_x, origin_y);
    mgl_text_renderer_flush_and_draw(text_renderer, color);

    /* Blinking caret (500ms on / 500ms off), only when focused */
    if(editor->focused) {
        const int64_t caret_elapsed_time_ms = (mgl_clock_get_elapsed_time_seconds(&editor->caret_timer) - editor->caret_blink_time_seconds) * 1000.0;
        if (caret_elapsed_time_ms % 1000 < 500)
            mgl_text_edit_render_caret(editor, origin_x, origin_y, (mgl_color){229, 229, 255, color.a});
    }

    if (scrollable)
        mgl_window_set_scissor(window, &saved_scissor);
}

mgl_vec2i mgl_text_edit_get_size(mgl_text_edit *editor, bool use_wrap_width) {
    mgl_text_edit_sync(editor);
    PangoRectangle logical_rect;
    pango_layout_get_pixel_extents(editor->layout, NULL, &logical_rect);
    const int width = (use_wrap_width && editor->wrap_width) > 0 ? editor->wrap_width : (logical_rect.width + editor->margin_left + editor->margin_right);
    return (mgl_vec2i){ width, logical_rect.height + editor->margin_top + editor->margin_bottom };
}

void mgl_text_edit_set_margins(mgl_text_edit *editor, int left, int top, int right, int bottom) {
    editor->margin_left   = left;
    editor->margin_top    = top;
    editor->margin_right  = right;
    editor->margin_bottom = bottom;
    if (editor->wrap_width > 0 && !editor->single_paragraph_mode) {
        int inner = editor->wrap_width - left - right;
        if (inner < 1) inner = 1;
        pango_layout_set_width(editor->layout, inner * PANGO_SCALE);
    }
}

void mgl_text_edit_set_position(mgl_text_edit *editor, mgl_vec2f position) {
    editor->position = position;
}

mgl_vec2f mgl_text_edit_get_position(mgl_text_edit *editor) {
    return editor->position;
}
