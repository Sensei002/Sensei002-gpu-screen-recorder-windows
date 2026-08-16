#pragma once

#include <mglpp/system/MemoryMappedFile.hpp>
#include <mglpp/graphics/Color.hpp>
#include <mglpp/graphics/Texture.hpp>

#include <string>

namespace gsr {
    struct Config;
    struct GsrInfo;

    struct Theme {
        Theme() = default;
        Theme(const Theme&) = delete;
        Theme& operator=(const Theme&) = delete;

        float window_width = 0.0f;
        float window_height = 0.0f;

        std::string body_font_desc;
        std::string title_font_desc;
        std::string top_bar_font_desc;
        std::string camera_setup_font_desc;

        mgl::Texture combobox_arrow_texture;
        mgl::Texture settings_texture;
        mgl::Texture settings_small_texture;
        mgl::Texture settings_extra_small_texture;
        mgl::Texture folder_texture;
        mgl::Texture up_arrow_texture;
        mgl::Texture replay_button_texture;
        mgl::Texture record_button_texture;
        mgl::Texture stream_button_texture;
        mgl::Texture close_texture;
        mgl::Texture logo_texture;
        mgl::Texture checkbox_circle_texture;
        mgl::Texture checkbox_background_texture;
        mgl::Texture play_texture;
        mgl::Texture stop_texture;
        mgl::Texture pause_texture;
        mgl::Texture save_texture;
        mgl::Texture screenshot_texture;
        mgl::Texture trash_texture;
        mgl::Texture masked_texture;
        mgl::Texture unmasked_texture;
        mgl::Texture warning_texture;
        mgl::Texture info_texture;
        mgl::Texture question_mark_texture;

        mgl::Texture ps4_home_texture;
        mgl::Texture ps4_options_texture;
        mgl::Texture ps4_dpad_up_texture;
        mgl::Texture ps4_dpad_down_texture;
        mgl::Texture ps4_dpad_left_texture;
        mgl::Texture ps4_dpad_right_texture;
        mgl::Texture ps4_cross_texture;
        mgl::Texture ps4_triangle_texture;

        double double_click_timeout_seconds = 0.4;

        // Reloads fonts
        bool set_window_size(mgl::vec2i window_size);
    };

    bool init_theme(const std::string &resources_path);
    void deinit_theme();
    Theme& get_theme();

    struct ColorTheme {
        mgl::Color tint_color = mgl::Color(118, 185, 0);
        mgl::Color page_bg_color = mgl::Color(38, 43, 47);
        mgl::Color text_color = mgl::Color(255, 255, 255);
    };

    bool init_color_theme(const Config &config, const GsrInfo &gsr_info);
    void deinit_color_theme();
    ColorTheme& get_color_theme();
}