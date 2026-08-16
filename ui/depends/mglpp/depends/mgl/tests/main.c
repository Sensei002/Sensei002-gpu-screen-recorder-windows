#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mgl/mgl.h>
#include <mgl/window/window.h>
#include <mgl/window/event.h>
#include <mgl/graphics/texture.h>
#include <mgl/graphics/rectangle.h>
#include <mgl/graphics/sprite.h>
#include <mgl/graphics/text.h>
#include <mgl/graphics/text_edit.h>
#include <mgl/graphics/vertex_buffer.h>
#include <mgl/graphics/shader.h>
#include <mgl/system/clock.h>
#include <mgl/system/fileutils.h>
#include <mgl/system/utf8.h>

#define require_equals(a, b) do { if((a) != (b)) { fprintf(stderr, "Assert failed on line %d: %s == %s\n", __LINE__, #a, #b); abort(); } } while(0)
#define require_not_equals(a, b) do { if((a) == (b)) { fprintf(stderr, "Assert failed on line %d: %s != %s\n", __LINE__, #a, #b); abort(); } } while(0)

typedef struct {
    mgl_texture *texture;
    mgl_vertex_buffer *vertex_buffer1;
    mgl_shader_program *shader_program;
    mgl_clock clock;
    mgl_text_edit *te;
    int fps;
    int fps_counter;
} Userdata;

static void draw(mgl_window *window, void *userdata) {
    mgl_context *context = mgl_get_context();
    Userdata *u = userdata;

    mgl_shader_program_set_uniform_vec2f(u->shader_program, "resolution", (mgl_vec2f){ u->texture->width, u->texture->height });

    static float rot = 0.0f;
    rot += 5.0f;

    mgl_sprite sprite;
    mgl_sprite_init(&sprite, u->texture);
    mgl_sprite_set_position(&sprite, (mgl_vec2f){ 500.0f - 10.0f + u->texture->width * 0.5f * 2.0f, 500.0f + u->texture->height * 0.5f * 2.0f });
    mgl_sprite_set_color(&sprite, (mgl_color){255, 255, 255, 128});
    mgl_sprite_set_rotation(&sprite, rot);
    mgl_sprite_set_origin(&sprite, (mgl_vec2f){ u->texture->width * 0.5f, u->texture->height * 0.5f });
    sprite.scale = (mgl_vec2f){ 2.0f, 2.0f };
    mgl_shader_program_use(u->shader_program);
    mgl_sprite_draw(context, &sprite);
    mgl_shader_program_use(NULL);

    ++u->fps_counter;
    if(mgl_clock_get_elapsed_time_seconds(&u->clock) >= 1.0) {
        mgl_clock_restart(&u->clock);
        u->fps = u->fps_counter;
        u->fps_counter = 0;
        fprintf(stderr, "fps: %d\n", u->fps);
    }

    const char *multilingual_text = "iiiiiiiiiiiiii Hello world! 😘 1234, 饕餮  로 초성, 중성, 종성을 東京都と周辺7県で首都圏を構成"
        "【システマ流】誰でも出来る「不眠.ストレス」に打ち勝つ呼吸法（東京都・埼玉県・千葉県・神奈川県）の総人口は約 千葉県・神奈川日本の民間研究所が年に発表した「世界の都市総合力ランキング」では、ロンド"
        "ンとニューヨークに次ぐ世界3位と評価された年に首都圏整備法の施行に伴い廃止された。このように首都建設法の廃止により区部の東部には、隅田川、荒川、江戸川、中川などの河口部に沖積平野が広がっ日本国内におけ"
        "る気候区分では23区〜多摩東部および伊豆諸島は太平洋側気候、多摩西部などは中央高地式気候に属する。小笠原諸島は南日本気候である。特徴としては、四季の変化が明瞭であり、天気が日によって変化しやすい。夏季は"
        "高温・多雨となり、冬季は晴れて乾燥する東京都区部 - 気象庁露場のあった大手町付近の観測による最低気温が最も高くなることも珍しくなかった。しかし、夏場の最高気温自体はそれほど高くもない。一方、内陸寄りにあ"
        "る練馬区のアメダス観測[注 7][22]地域では冬日は珍しくなく、新宿区や渋谷区などの都心部でも冬日の観測はよく見られる。また、気象観測所のある千代田区内におい足立区、荒川区、板橋区、江戸川区、大田区、葛飾区"
        "北区、江東区、品川区、渋谷区、新宿区、杉並区、墨田区、世田谷区、台東区、中央区、千代田区、豊島区、中野区、練馬区、文京区、港区、目黒区昭島市、あきる野市、稲城市、青梅市、清瀬市、国立市、小金井市、国分寺市"
        "小平市、狛江市、立川市、多摩市、調布市、西東京市、八王子市、羽村市、東久留米市、東村山市、東大和市、日野市、府中市、福生市、町田市、三鷹市、武蔵野市、武蔵村山市";

    mgl_text text_small;
    mgl_text_init(&text_small, multilingual_text, -1, "Adwaita Sans Bold 20");
    mgl_text_set_wrap_width(&text_small, 500);
    mgl_text_set_max_rows(&text_small, 10);
    mgl_text_set_position(&text_small, (mgl_vec2f){50.0f, 50.0f});
    mgl_text_draw(&text_small);
    mgl_text_deinit(&text_small);

    mgl_text text_large;
    mgl_text_init(&text_large, multilingual_text, -1, "Adwaita Sans 32");
    mgl_text_set_wrap_width(&text_large, 500);
    mgl_text_set_max_rows(&text_large, 10);
    mgl_text_set_position(&text_large, (mgl_vec2f){50.0f, 500.0f});
    mgl_text_draw(&text_large);
    mgl_text_deinit(&text_large);

    mgl_text_edit_set_position(u->te, (mgl_vec2f){800.0f, 340.0f});
    const mgl_vec2i editor_size = mgl_text_edit_get_size(u->te, true);

    mgl_rectangle editor_background_rect = {
        .position = mgl_text_edit_get_position(u->te),
        .size = { editor_size.x, editor_size.y },
        .color = { 255, 0, 0, 255 },
    };
    mgl_rectangle_draw(context, &editor_background_rect);
    mgl_text_edit_draw(u->te, (mgl_color){234, 234, 244, 255});

    const mgl_font_atlas *font_atlas = mgl_text_renderer_get_atlas(mgl_get_text_renderer());

    const mgl_texture atlas_texture = {
        .id = font_atlas->texture,
        .width = font_atlas->width,
        .height = font_atlas->height,
        .format = MGL_TEXTURE_FORMAT_ALPHA,
        .max_width = 4096,
        .max_height = 4096,
        .pixel_coordinates = true,
        .scale_type = MGL_TEXTURE_SCALE_NEAREST,
        .owned = false,
    };

    mgl_rectangle rect = {
        .position = { window->cursor_position.x, window->cursor_position.y },
        .size = { font_atlas->width, font_atlas->height },
        .color = { 255, 255, 255, 255 },
    };
    mgl_rectangle_draw(context, &rect);

    mgl_vertex vertices1[4] = {
        (mgl_vertex){
            .position = {0.0f, 0.0f},
            .texcoords = {0.0f, 0.0f},
            .color = {255, 0, 0, 100}
        },
        (mgl_vertex){
            .position = {font_atlas->width, 0.0f},
            .texcoords = {font_atlas->width, 0.0f},
            .color = {0, 255, 0, 100},
        },
        (mgl_vertex){
            .position = {font_atlas->width, font_atlas->height},
            .texcoords = {font_atlas->width, font_atlas->height},
            .color = {0, 0, 255, 100},
        },
        (mgl_vertex){
            .position = {0.0f, font_atlas->height},
            .texcoords = {0.0f, font_atlas->height},
            .color = {255, 0, 255, 100}
        }
    };

    mgl_vertex_buffer_update(u->vertex_buffer1, vertices1, 4, MGL_PRIMITIVE_QUADS, MGL_USAGE_STREAM);
    mgl_vertex_buffer_set_position(u->vertex_buffer1, (mgl_vec2f){ window->cursor_position.x, window->cursor_position.y });
    mgl_vertex_buffer_draw(context, u->vertex_buffer1, &atlas_texture);
}

static void test_utf8(void) {
    uint32_t codepoint = 0;
    size_t codepoint_length = 0;

    require_equals(mgl_utf8_decode("a", 1, &codepoint, &codepoint_length), true);
    require_equals(codepoint, 0x61);
    require_equals(codepoint_length, 1);

    require_equals(mgl_utf8_decode("á", 2, &codepoint, &codepoint_length), true);
    require_equals(codepoint, 0xE1);
    require_equals(codepoint_length, 2);

    require_equals(mgl_utf8_decode("‡", 3, &codepoint, &codepoint_length), true);
    require_equals(codepoint, 0x2021);
    require_equals(codepoint_length, 3);

    require_equals(mgl_utf8_decode("𒀀", 4, &codepoint, &codepoint_length), true);
    require_equals(codepoint, 0x12000);
    require_equals(codepoint_length, 4);

    require_equals(mgl_utf8_get_start_of_codepoint("abc", strlen("abc"), 0), 0);
    require_equals(mgl_utf8_get_start_of_codepoint("abc", strlen("abc"), 2), 2);
    require_equals(mgl_utf8_get_start_of_codepoint("abö", strlen("abö"), 4), 2);

    require_equals(mgl_utf8_get_character_count("", 0), 0);
    require_equals(mgl_utf8_get_character_count("abc", strlen("abc")), 3);
    require_equals(mgl_utf8_get_character_count("abc", 2), 2);
    require_equals(mgl_utf8_get_character_count("aöc", strlen("aöc")), 3);
    require_equals(mgl_utf8_get_character_count("‡edöx", strlen("‡edöx")), 5);
    require_equals(mgl_utf8_get_character_count("a𒀀b", strlen("a𒀀b")), 3);

    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){}, 0), 0);
    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){'a', 'b', 'c'}, 3), strlen("abc"));
    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){'a', 'b'}, 2), 2);
    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){'a', 0xF6, 'c'}, 3), strlen("aöc"));
    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){0x2021, 'e', 'd', 0xF6, 'x'}, 5), strlen("‡edöx"));
    require_equals(mgl_utf32_get_utf8_count((uint32_t[]){'a', 0x12000, 'b'}, 3), strlen("a𒀀b"));

    require_equals(mgl_utf8_index_to_byte_index("", 0, 3), 0);
    require_equals(mgl_utf8_index_to_byte_index("abc", strlen("abc"), 0), 0);
    require_equals(mgl_utf8_index_to_byte_index("abc", strlen("abc"), 2), 2);
    require_equals(mgl_utf8_index_to_byte_index("aöc", strlen("aöc"), 2), 3);
    require_equals(mgl_utf8_index_to_byte_index("‡edöx", strlen("‡edöx"), 2), 4);
    require_equals(mgl_utf8_index_to_byte_index("a𒀀b", strlen("a𒀀b"), 2), 5);

    require_equals(mgl_byte_index_to_utf8_index("", 0, 3), 0);
    require_equals(mgl_byte_index_to_utf8_index("abc", strlen("abc"), 0), 0);
    require_equals(mgl_byte_index_to_utf8_index("abc", strlen("abc"), strlen("ab")), 2);
    require_equals(mgl_byte_index_to_utf8_index("aöc", strlen("aöc"), strlen("aö")), 2);
    require_equals(mgl_byte_index_to_utf8_index("‡edöx", strlen("‡edöx"), strlen("‡e")), 2);
    require_equals(mgl_byte_index_to_utf8_index("a𒀀b", strlen("a𒀀b"), strlen("a𒀀")), 2);

    {
        const size_t utf32_buffer_size = 32;
        uint32_t utf32_buffer[32];

        require_equals(mgl_utf8_to_utf32("", 0, utf32_buffer, utf32_buffer_size), 0);

        require_equals(mgl_utf8_to_utf32("abc", strlen("abc"), utf32_buffer, utf32_buffer_size), 3);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){'a', 'b', 'c'}, 3), 0);

        require_equals(mgl_utf8_to_utf32("abc", strlen("ab"), utf32_buffer, utf32_buffer_size), 2);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){'a', 'b'}, 2), 0);

        require_equals(mgl_utf8_to_utf32("aöc", strlen("aöc"), utf32_buffer, utf32_buffer_size), 3);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){'a', 0xF6, 'c'}, 3), 0);

        require_equals(mgl_utf8_to_utf32("‡edöx", strlen("‡edöx"), utf32_buffer, utf32_buffer_size), 5);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){0x2021, 'e', 'd', 0xF6, 'x'}, 5), 0);

        require_equals(mgl_utf8_to_utf32("a𒀀b", strlen("a𒀀b"), utf32_buffer, utf32_buffer_size), 3);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){'a', 0x12000, 'b'}, 3), 0);

        require_equals(mgl_utf8_to_utf32("a𒀀b", strlen("a𒀀b"), utf32_buffer, 2), 2);
        require_equals(memcmp(utf32_buffer, (uint32_t[]){'a', 0x12000}, 2), 0);
    }

    {
        const size_t utf8_buffer_size = 32;
        unsigned char utf8_buffer[32];

        require_equals(mgl_utf32_to_utf8((uint32_t[]){}, 0, utf8_buffer, utf8_buffer_size), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){'a', 'b', 'c'}, 3, utf8_buffer, utf8_buffer_size), strlen("abc"));
        require_equals(memcmp(utf8_buffer, "abc", strlen("abc")), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){'a', 'b'}, 2, utf8_buffer, utf8_buffer_size), strlen("ab"));
        require_equals(memcmp(utf8_buffer, "ab", strlen("ab")), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){'a', 0xF6, 'c'}, 3, utf8_buffer, utf8_buffer_size), strlen("aöc"));
        require_equals(memcmp(utf8_buffer, "aöc", strlen("aöc")), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){0x2021, 'e', 'd', 0xF6, 'x'}, 5, utf8_buffer, utf8_buffer_size), strlen("‡edöx"));
        require_equals(memcmp(utf8_buffer, "‡edöx", strlen("‡edöx")), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){'a', 0x12000, 'b'}, 3, utf8_buffer, utf8_buffer_size), strlen("a𒀀b"));
        require_equals(memcmp(utf8_buffer, "a𒀀b", strlen("a𒀀b")), 0);

        require_equals(mgl_utf32_to_utf8((uint32_t[]){'a', 0x12000}, 2, utf8_buffer, 4), 1);
        require_equals(memcmp(utf8_buffer, "a", strlen("a")), 0);
    }
}

int main(void) {
    test_utf8();

    if(mgl_init(MGL_WINDOW_SYSTEM_NATIVE) != 0)
        return 1;

    mgl_texture texture;
    mgl_vertex_buffer vertex_buffer1;
    mgl_shader_program shader_program;

    Userdata userdata;
    userdata.texture = &texture;
    userdata.vertex_buffer1 = &vertex_buffer1;
    userdata.shader_program = &shader_program;
    userdata.fps = 0;
    userdata.fps_counter = 0;
    mgl_clock_init(&userdata.clock);

    mgl_window window;
    if(mgl_window_create(&window, "mgl", &(mgl_window_create_params){ .size = {1280, 720}, .min_size = { 1000, 1000 }, .graphics_api = MGL_GRAPHICS_API_EGL }) != 0)
        return 1;

    if(mgl_texture_init(&texture) != 0)
        return 1;

    if(mgl_texture_load_from_file(&texture, "tests/X11.jpg", &(mgl_texture_load_options){ .compressed = false, .pixel_coordinates = false, .scale_type = MGL_TEXTURE_SCALE_LINEAR }) != 0)
        return 1;

    if(mgl_shader_program_init(&shader_program) != 0)
        return 1;

    if(mgl_shader_program_add_shader_from_file(&shader_program, "tests/circle_mask.glsl", MGL_SHADER_FRAGMENT) != 0)
        return 1;

    if(mgl_shader_program_finalize(&shader_program) != 0)
        return 1;

    mgl_vertex_buffer_init(&vertex_buffer1);

    char default_font_name[128];
    mgl_text_get_default_font_name(default_font_name, sizeof(default_font_name));
    if(default_font_name[0] == '\0')
        snprintf(default_font_name, sizeof(default_font_name), "Sans");

    char font[128];
    snprintf(font, sizeof(font), "%s 16", default_font_name);

    mgl_text_edit editor;
    mgl_text_edit_init(&editor, font, 500.0f);
    mgl_text_edit_set_margins(&editor, 20, 20, 20, 20);
    mgl_text_edit_set_text(&editor,
        "Click here and type! This is a fully editable text field with "
        "word wrapping, caret navigation (arrows, Home, End), "
        "mouse click positioning, selection (Shift+arrows / Shift+click), "
        "and Backspace/Delete.");
    userdata.te = &editor;

    mgl_event event;
    while(mgl_window_is_open(&window)) {
        while(mgl_window_poll_event(&window, &event)) {
            mgl_text_edit_handle_event(&editor, &event);

            switch(event.type) {
                case MGL_EVENT_TEXT_ENTERED: {
                    if(event.text.codepoint >= 32 && event.text.codepoint != 127) {
                        fprintf(stderr, "text event, codepoint: %u, str: %s\n", event.text.codepoint, event.text.str);
                    }
                    break;
                }
                case MGL_EVENT_KEY_PRESSED: {
                    switch(event.key.code) {
                        case MGL_KEY_F: {
                            if(event.key.key_states.control) {
                                mgl_window_set_fullscreen(&window, !mgl_window_is_fullscreen(&window));
                            }
                            break;
                        }
                        case MGL_KEY_X: {
                            if(event.key.key_states.control) {
                                bool enable_vsync = !mgl_window_is_vsync_enabled(&window);
                                mgl_window_set_vsync_enabled(&window, enable_vsync);
                                fprintf(stderr, "vsync %s\n", enable_vsync ? "enabled" : "disabled");
                            }
                            break;
                        }
                    }

                    fprintf(stderr, "key press event, code: %u\n", event.key.code);
                    break;
                }
                case MGL_EVENT_KEY_RELEASED: {
                    fprintf(stderr, "key release event, code: %u\n", event.key.code);
                    break;
                }
                case MGL_EVENT_MONITOR_PROPERTY_CHANGED: {
                    fprintf(stderr, "monitor property changed\n");
                    break;
                }
                case MGL_EVENT_MONITOR_CONNECTED: {
                    fprintf(stderr, "monitor connected\n");
                    break;
                }
                case MGL_EVENT_MONITOR_DISCONNECTED: {
                    fprintf(stderr, "monitor disconnected\n");
                    break;
                }
            }
        }

        mgl_window_clear(&window, (mgl_color){0, 0, 0, 255});
        draw(&window, &userdata);
        mgl_window_display(&window);
    }

    mgl_text_edit_deinit(&editor);
    mgl_vertex_buffer_deinit(&vertex_buffer1);
    mgl_shader_program_deinit(&shader_program);
    mgl_texture_unload(&texture);
    mgl_window_deinit(&window);
    mgl_deinit();
    return 0;
}
