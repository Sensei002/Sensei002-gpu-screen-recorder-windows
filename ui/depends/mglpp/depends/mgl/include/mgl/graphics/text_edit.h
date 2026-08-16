#ifndef MGL_TEXT_EDIT_H
#define MGL_TEXT_EDIT_H

#include "../system/clock.h"
#include "text.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct _GArray GArray;
typedef struct _PangoFontDescription PangoFontDescription;

typedef struct mgl_event mgl_event;

/* |mgl_text_edit| expects utf8 strings */

typedef struct {
    int start;
    int length;
} mgl_range;

typedef struct {
    GArray               *buffer;          /* real text (utf8), trailing '\0' not in len */
    GArray               *display_buffer;  /* masked text fed to the layout when |masked| is true */
    PangoLayout          *layout;
    PangoFontDescription *font_desc;
    int                   wrap_width;

    int  caret_byte;        /* byte offset into the layout text (display when masked, real otherwise) */
    bool prefer_prev_line;  /* render caret at end of previous visual line */
    int  sel_byte;          /* selection anchor (-1 = none), in same coordinates as caret_byte */
    int  goal_x_pu;         /* sticky X for vertical movement (-1 = reset) */
    bool dirty;             /* buffer changed, layout needs re-sync */
    int  scroll_x;          /* horizontal scroll offset in pixels (single paragraph mode only) */
    bool single_paragraph_mode;

    mgl_clock caret_timer;
    double caret_blink_time_seconds;

    mgl_vec2f position;
    bool mouse_dragging;
    bool focused;

    bool number_mode;
    int  num_min;
    int  num_max;

    uint32_t mask_char;     /* code point used to mask each character; default '*' */
    bool     masked;        /* when true, the layout shows mask_char repeated per codepoint */

    int margin_left, margin_top, margin_right, margin_bottom;
} mgl_text_edit;

void mgl_text_edit_init(mgl_text_edit *editor, const char *font_str, int wrap_width);
void mgl_text_edit_deinit(mgl_text_edit *editor);

int mgl_text_edit_len(const mgl_text_edit *editor);
const char* mgl_text_edit_text(const mgl_text_edit *editor);

/* Returns true if the event was handled */
bool mgl_text_edit_handle_event(mgl_text_edit *editor, const mgl_event *event);
void mgl_text_edit_sync(mgl_text_edit *editor);

/* If the text edit should be displayed on a single line, for example for username input */
void mgl_text_edit_set_single_paragraph_mode(mgl_text_edit *editor, bool setting);

/* Restrict input to integers clamped to [min_val, max_val]. Clamp is applied on focus loss. */
void mgl_text_edit_set_number_mode(mgl_text_edit *editor, bool enabled, int min_val, int max_val);
/* Clamp current text to the configured number range. No-op if number_mode is off. */
void mgl_text_edit_clamp_number(mgl_text_edit *editor);

/* Set the code point used to mask each character when masking is enabled. Default is '*'. */
void mgl_text_edit_set_mask_char(mgl_text_edit *editor, uint32_t mask_char);
/* Toggle whether the displayed text is masked (for password input).
 * When enabled, the visible text is the mask character repeated per codepoint.
 * The real text is preserved and accessible via mgl_text_edit_text() and
 * mgl_text_edit_get_selection(). Editing operations apply to the real text. */
void mgl_text_edit_set_masked(mgl_text_edit *editor, bool masked);
bool mgl_text_edit_is_masked(const mgl_text_edit *editor);

void mgl_text_edit_set_text(mgl_text_edit *editor, const char *text);
/* If len is -1 then the length of str is calculated automatically */
void mgl_text_edit_insert(mgl_text_edit *editor, const char *str, int len);
void mgl_text_edit_delete(mgl_text_edit *editor, int direction, int count);
/* Returns byte index start and length of the selected text. Returns 0, 0 if no text is selected */
mgl_range mgl_text_edit_get_selection(const mgl_text_edit *editor);

void mgl_text_edit_move_left(mgl_text_edit *editor, bool extend_selection);
void mgl_text_edit_move_right(mgl_text_edit *editor, bool extend_selection);
void mgl_text_edit_move_word_left(mgl_text_edit *editor, bool extend_selection);
void mgl_text_edit_move_word_right(mgl_text_edit *editor, bool extend_selection);

void mgl_text_edit_delete_word(mgl_text_edit *editor, int direction);

void mgl_text_edit_move_vertical(mgl_text_edit *editor, int direction, bool extend_selection);
void mgl_text_edit_move_home(mgl_text_edit *editor, bool extend_selection);
void mgl_text_edit_move_end(mgl_text_edit *editor, bool extend_selection);

void mgl_text_edit_set_caret_from_mouse(mgl_text_edit *editor,
                                        int mouse_x, int mouse_y,
                                        bool extend_selection);

bool mgl_text_edit_is_focused(const mgl_text_edit *editor);
/* Set the focused state. On focus gain the caret blink timer is reset.
 * On focus loss the selection is cleared, the value is clamped when in number mode,
 * and horizontal scroll is reset when the text fits the wrap width. */
void mgl_text_edit_set_focused(mgl_text_edit *editor, bool focused);

void mgl_text_edit_draw(mgl_text_edit *editor, mgl_color color);

mgl_vec2i mgl_text_edit_get_size(mgl_text_edit *editor, bool use_wrap_width);
void mgl_text_edit_set_position(mgl_text_edit *editor, mgl_vec2f position);
mgl_vec2f mgl_text_edit_get_position(mgl_text_edit *editor);

/* Set inner margins. Rendering is inset by these amounts; widget bounds and click area are unaffected. */
void mgl_text_edit_set_margins(mgl_text_edit *editor, int left, int top, int right, int bottom);

#endif /* MGL_TEXT_EDIT_H */
