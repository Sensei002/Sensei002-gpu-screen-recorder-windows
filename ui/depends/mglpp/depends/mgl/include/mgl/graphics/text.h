#ifndef MGL_TEXT_H
#define MGL_TEXT_H

#include "../system/vec.h"
#include "color.h"
#include "font_atlas.h"
#include "quad_batch.h"
#include <stddef.h>

/* |mgl_text| expects utf8 strings */

typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoContext PangoContext;
typedef struct _PangoLayout  PangoLayout;

typedef struct {
    int byte_index;
    int codepoint_index;
    mgl_vec2f pos;
} mgl_index_codepoint_pair;

typedef struct {
    PangoFontMap  *font_map;
    PangoContext  *context;
    mgl_font_atlas atlas;
    mgl_quad_batch batch;
} mgl_text_renderer;

typedef struct {
    PangoLayout *layout;
    int text_len;
    mgl_vec2f position;
    mgl_color color;
} mgl_text;

void mgl_text_renderer_init(void);
void mgl_text_renderer_deinit(void);

mgl_text_renderer* mgl_get_text_renderer(void);
const mgl_font_atlas* mgl_text_renderer_get_atlas(mgl_text_renderer *self);
void mgl_text_renderer_flush_and_draw(mgl_text_renderer *self, mgl_color color);
void mgl_text_renderer_render_layout(mgl_text_renderer *self, PangoLayout *layout, float origin_x, float origin_y);

/* Set text_len to -1 to automatically calculate the length of the text. The text color is white by default */
void mgl_text_init(mgl_text *self, const char *text, int text_len, const char *font_desc);
void mgl_text_deinit(mgl_text *self);
/* The destination is expected to not be initialized */
void mgl_text_copy(const mgl_text *self, mgl_text *destination);
int mgl_text_get_font_size_from_font_description(const char *font_desc);
bool mgl_text_get_default_font_name(char *font_name_buffer, size_t len);

void mgl_text_set_string(mgl_text *self, const char *text, int text_len);
const char* mgl_text_get_string(const mgl_text *self, int *length);

int mgl_text_get_font_size(const mgl_text *self);

/* Set the max width of the text. Text further than this is wrapped at words/characters.
 * Set to 0 to disable text wrapping */
void mgl_text_set_wrap_width(mgl_text *self, int wrap_width);
/* Set the max rows to show for the text.
 * Ellipsis will be shown at the end of the text instead of further rows. Set to 0 to disable max rows */
void mgl_text_set_max_rows(mgl_text *self, int max_rows);

void mgl_text_set_position(mgl_text *self, mgl_vec2f position);
mgl_vec2f mgl_text_get_position(const mgl_text *self);

void mgl_text_set_color(mgl_text *self, mgl_color color);
mgl_color mgl_text_get_color(const mgl_text *self);

mgl_vec2i mgl_text_get_size(const mgl_text *self);

mgl_vec2f mgl_text_find_character_pos(const mgl_text *self, int index);
mgl_index_codepoint_pair mgl_text_find_closest_caret_index_by_position(const mgl_text *self, mgl_vec2f position);

void mgl_text_draw(mgl_text *self);

#endif /* MGL_TEXT_H */
