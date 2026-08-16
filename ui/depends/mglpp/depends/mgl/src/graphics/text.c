#include "../../include/mgl/graphics/text.h"
#include "../../include/mgl/mgl.h"

#include <pango/pangoft2.h>
#include <glib.h>
#include <gio/gio.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#define SCREEN_DPI 96.0

static mgl_text_renderer renderer;
static bool text_renderer_initialized = false;
static bool font_atlas_initialized = false;

void mgl_text_renderer_init(void) {
    if(text_renderer_initialized)
        return;
    text_renderer_initialized = true;

    renderer.font_map = pango_ft2_font_map_new();
    pango_ft2_font_map_set_resolution(PANGO_FT2_FONT_MAP(renderer.font_map), SCREEN_DPI, SCREEN_DPI);
    renderer.context = pango_font_map_create_context(renderer.font_map);
    mgl_batch_init(&renderer.batch);
}

void mgl_text_renderer_deinit(void) {
    if(!text_renderer_initialized)
        return;
    text_renderer_initialized = false;

    if(font_atlas_initialized) {
        font_atlas_initialized = false;
        mgl_font_atlas_deinit(&renderer.atlas);
    }

    mgl_batch_deinit(&renderer.batch);
    g_object_unref(renderer.context);
    g_object_unref(renderer.font_map);
}

static void mgl_text_renderer_font_atlas_init_once(void) {
    mgl_text_renderer_init();
    if(!font_atlas_initialized) {
        font_atlas_initialized = true;
        mgl_font_atlas_init(&renderer.atlas);
    }
}

mgl_text_renderer* mgl_get_text_renderer(void) {
    assert(text_renderer_initialized);
    return &renderer;
}

const mgl_font_atlas* mgl_text_renderer_get_atlas(mgl_text_renderer *self) {
    return &self->atlas;
}

void mgl_text_renderer_flush_and_draw(mgl_text_renderer *self, mgl_color color) {
    mgl_context *context = mgl_get_context();

    mgl_font_atlas_flush(&self->atlas);
    context->gl.glColor4ub(color.r, color.g, color.b, color.a);
    context->gl.glBindTexture(GL_TEXTURE_2D, self->atlas.texture);
    context->gl.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    mgl_batch_draw(&self->batch);
    context->gl.glBindTexture(GL_TEXTURE_2D, 0);
    mgl_batch_clear(&self->batch);
}

void mgl_text_renderer_render_layout(mgl_text_renderer *self, PangoLayout *layout, float origin_x, float origin_y) {
    mgl_text_renderer_font_atlas_init_once();

    PangoLayoutIter *iter = pango_layout_get_iter(layout);

    do {
        PangoLayoutRun *run = pango_layout_iter_get_run(iter);
        if (!run)
            continue;

        PangoFont        *font    = run->item->analysis.font;
        PangoGlyphString *glyphs  = run->glyphs;
        int               baseline = pango_layout_iter_get_baseline(iter);

        PangoRectangle run_rect;
        pango_layout_iter_get_run_extents(iter, NULL, &run_rect);
        int pen_x = run_rect.x;

        for (int gi = 0; gi < glyphs->num_glyphs; gi++) {
            PangoGlyphInfo *info = &glyphs->glyphs[gi];

            if (info->glyph & PANGO_GLYPH_UNKNOWN_FLAG) {
                pen_x += info->geometry.width;
                continue;
            }

            const mgl_glyph_entry *entry = mgl_font_atlas_get(&self->atlas, font, info->glyph);
            if (!entry || !entry->width || !entry->height) {
                pen_x += info->geometry.width;
                continue;
            }

            float gx = origin_x
                + (float)(pen_x + info->geometry.x_offset) / (float)PANGO_SCALE
                + (float)entry->bearing_x;
            float gy = origin_y
                + (float)(baseline + info->geometry.y_offset) / (float)PANGO_SCALE
                - (float)entry->bearing_y;

            mgl_batch_push_quad(&self->batch, lroundf(gx), lroundf(gy),
                            lroundf(entry->width), lroundf(entry->height),
                            entry->u0, entry->v0, entry->u1, entry->v1);

            pen_x += info->geometry.width;
        }
    } while (pango_layout_iter_next_run(iter));

    pango_layout_iter_free(iter);
}

/* Set len to -1 to automatically calculate the length of the text */
void mgl_text_init(mgl_text *self, const char *text, int text_len, const char *font_desc) {
    mgl_text_renderer_init();

    self->layout = pango_layout_new(renderer.context);
    self->text_len = text_len >= 0 ? text_len : strlen(text);
    PangoFontDescription *desc = pango_font_description_from_string(font_desc);
    pango_layout_set_font_description(self->layout, desc);
    pango_font_description_free(desc);
    pango_layout_set_text(self->layout, text, self->text_len);

    self->position = (mgl_vec2f){0.0f, 0.0f};
    self->color = (mgl_color){255, 255, 255, 255};
}

void mgl_text_deinit(mgl_text *self) {
    if(self->layout) {
        g_object_unref(self->layout);
        self->layout = NULL;
    }

    //mgl_text_renderer_deinit();
}

void mgl_text_copy(const mgl_text *self, mgl_text *destination) {
    destination->layout = pango_layout_copy(self->layout);
    destination->text_len = self->text_len;
    destination->position = self->position;
    destination->color = self->color;
}

int mgl_text_get_font_size_from_font_description(const char *font_desc) {
    PangoFontDescription *desc = pango_font_description_from_string(font_desc);
    if(desc) {
        const int size = PANGO_PIXELS(pango_font_description_get_size(desc));
        pango_font_description_free(desc);
        return size;
    } else {
        return 0;
    }
}

static char* scanr(char *p, int c, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        const size_t index = size - i - 1;
        if(p[index] == c)
            return &p[index];
    }
    return NULL;
}

static void font_name_remove_size(char *font) {
    size_t len = strlen(font);
    char *last_space_p = scanr(font, ' ', len);
    if(last_space_p) {
        *last_space_p = '\0';
        len = last_space_p - font;
    }

    for(; len > 0; --len) {
        if(font[len - 1] != ' ')
            break;
        font[len - 1] = '\0';
    }
}

bool mgl_text_get_default_font_name(char *font_name_buffer, size_t len) {
    if(len == 0)
        return false;
    font_name_buffer[0] = '\0';

#ifdef _WIN32
    /* No GSettings/GNOME on Windows; the Windows UI default is Segoe UI.
       The UI's Theme.cpp falls back to this when no font is configured. */
    const char *default_font = "Segoe UI";
    if(len > strlen(default_font)) {
        strcpy(font_name_buffer, default_font);
        return true;
    }
    return false;
#else
    GSettingsSchemaSource *schema_source = g_settings_schema_source_get_default();
    if(!schema_source)
        return false;

    gchar **non_relocatable = NULL;
    g_settings_schema_source_list_schemas(schema_source, FALSE, &non_relocatable, NULL);

    bool has_gnome_desktop_interface = false;
    if(non_relocatable) {
        for (size_t i = 0; non_relocatable[i] != NULL; i++) {
            if(g_strcmp0(non_relocatable[i], "org.gnome.desktop.interface") == 0) {
                has_gnome_desktop_interface = true;
                break;
            }
        }
    }

    g_strfreev(non_relocatable);

    if(!has_gnome_desktop_interface)
        return false;

    GSettings *settings = g_settings_new("org.gnome.desktop.interface");
    if(!settings)
        return false;

    gchar *font = g_settings_get_string(settings, "font-name");
    if(font) {
        font_name_remove_size(font);
        snprintf(font_name_buffer, len, "%s", font);
        g_free(font);
    }

    g_object_unref(settings);
    return true;
#endif
}

/* Set text_len to -1 to automatically calculate the length of the text */
void mgl_text_set_string(mgl_text *self, const char *text, int text_len) {
    self->text_len = text_len >= 0 ? text_len : strlen(text);
    pango_layout_set_text(self->layout, text, self->text_len);
}

const char* mgl_text_get_string(const mgl_text *self, int *length) {
    if(length)
        *length = self->text_len;
    return pango_layout_get_text(self->layout);
}

int mgl_text_get_font_size(const mgl_text *self) {
    const PangoFontDescription *desc = pango_layout_get_font_description(self->layout);
    if(desc)
        return PANGO_PIXELS(pango_font_description_get_size(desc));
    else
        return 0;
}

void mgl_text_set_wrap_width(mgl_text *self, int wrap_width) {
    if (wrap_width > 0) {
        pango_layout_set_width(self->layout, wrap_width * PANGO_SCALE);
        pango_layout_set_wrap(self->layout, PANGO_WRAP_WORD_CHAR);
    } else {
        pango_layout_set_width(self->layout, -1);
    }
}

void mgl_text_set_max_rows(mgl_text *self, int max_rows) {
    if (max_rows > 0) {
        pango_layout_set_height(self->layout, -max_rows);
        pango_layout_set_ellipsize(self->layout, PANGO_ELLIPSIZE_END);
    } else {
        pango_layout_set_height(self->layout, -1);
        pango_layout_set_ellipsize(self->layout, PANGO_ELLIPSIZE_NONE);
    }
}

void mgl_text_set_position(mgl_text *self, mgl_vec2f position) {
    self->position = position;
}

mgl_vec2f mgl_text_get_position(const mgl_text *self) {
    return self->position;
}

void mgl_text_set_color(mgl_text *self, mgl_color color) {
    self->color = color;
}

mgl_color mgl_text_get_color(const mgl_text *self) {
    return self->color;
}

mgl_vec2i mgl_text_get_size(const mgl_text *self) {
    PangoRectangle logical_rect;
    pango_layout_get_pixel_extents(self->layout, NULL, &logical_rect);
    return (mgl_vec2i){ logical_rect.width, logical_rect.height };
}

mgl_vec2f mgl_text_find_character_pos(const mgl_text *self, int index) {
    PangoRectangle pos = {0, 0, 0, 0};
    pango_layout_index_to_pos(self->layout, index, &pos);
    return (mgl_vec2f){ self->position.x + pos.x, self->position.y + pos.y };
}

mgl_index_codepoint_pair mgl_text_find_closest_caret_index_by_position(const mgl_text *self, mgl_vec2f position) {
    int byte_index = 0;
    int trailing = 0;
    pango_layout_xy_to_index(self->layout, self->position.x - position.y, self->position.y - position.y, &byte_index, &trailing);

    const char *str = pango_layout_get_text(self->layout);
    return (mgl_index_codepoint_pair) {
        .byte_index = byte_index,
        .codepoint_index = g_utf8_pointer_to_offset(str, str + byte_index),
        .pos = position,
    };
}

void mgl_text_draw(mgl_text *self) {
    mgl_text_renderer_render_layout(&renderer, self->layout, self->position.x, self->position.y);
    mgl_text_renderer_flush_and_draw(&renderer, self->color);
}
