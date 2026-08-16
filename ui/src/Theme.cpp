#include "../include/Theme.hpp"
#include "../include/Config.hpp"
#include "../include/GsrInfo.hpp"

#include <mglpp/graphics/Text.hpp>
#include <cmath>
#include <assert.h>

namespace gsr {
    static Theme *theme = nullptr;
    static ColorTheme *color_theme = nullptr;

    static mgl::Color gpu_vendor_to_color(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN:  return mgl::Color(221, 0, 49);
            case GpuVendor::AMD:      return mgl::Color(221, 0, 49);
            case GpuVendor::INTEL:    return mgl::Color(8, 109, 183);
            case GpuVendor::NVIDIA:   return mgl::Color(118, 185, 0);
            case GpuVendor::BROADCOM: return mgl::Color(221, 0, 49);
            case GpuVendor::APPLE:    return mgl::Color(221, 0, 49);
        }
        return mgl::Color(221, 0, 49);
    }

    static mgl::Color color_name_to_color(const std::string &color_name) {
        GpuVendor vendor = GpuVendor::UNKNOWN;
        if(color_name == "amd")
            vendor = GpuVendor::AMD;
        else if(color_name == "intel")
            vendor = GpuVendor::INTEL;
        else if(color_name == "nvidia")
            vendor = GpuVendor::NVIDIA;
        else if(color_name == "broadcom")
            vendor = GpuVendor::BROADCOM;
        else if(color_name == "apple")
            vendor = GpuVendor::APPLE;
        return gpu_vendor_to_color(vendor);
    }

    bool Theme::set_window_size(mgl::vec2i window_size) {
        if(std::abs(window_size.x - window_width) < 0.1f && std::abs(window_size.y - window_height) < 0.1f)
            return true;

        window_width = window_size.x;
        window_height = window_size.y;

        std::string default_font_name = mgl::Text::get_default_font_name();
        if(default_font_name.empty())
            default_font_name = "Sans";

        theme->title_font_desc = default_font_name + std::string(" Bold ") + std::to_string(std::round(std::max(16.0f, window_size.y * 0.019f)/1.8));
        theme->top_bar_font_desc = default_font_name + std::string(" Bold ") + std::to_string(std::round(std::max(23.0f, window_size.y * 0.03f)/1.8));
        theme->body_font_desc = default_font_name + std::string(" ") + std::to_string(std::round(std::max(13.0f, window_size.y * 0.015f)/1.8));
        theme->camera_setup_font_desc = default_font_name + " 14";

        return true;
    }

    bool init_theme(const std::string &resources_path) {
        if(theme)
            return true;

        theme = new Theme();

        if(!theme->combobox_arrow_texture.load_from_file((resources_path + "images/combobox_arrow.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->settings_texture.load_from_file((resources_path + "images/settings.png").c_str()))
            goto error;

        if(!theme->settings_small_texture.load_from_file((resources_path + "images/settings_small.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->settings_extra_small_texture.load_from_file((resources_path + "images/settings_extra_small.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->folder_texture.load_from_file((resources_path + "images/folder.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->up_arrow_texture.load_from_file((resources_path + "images/up_arrow.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->replay_button_texture.load_from_file((resources_path + "images/replay.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->record_button_texture.load_from_file((resources_path + "images/record.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->stream_button_texture.load_from_file((resources_path + "images/stream.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->close_texture.load_from_file((resources_path + "images/cross.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->logo_texture.load_from_file((resources_path + "images/gpu_screen_recorder_logo.png").c_str()))
            goto error;

        if(!theme->checkbox_circle_texture.load_from_file((resources_path + "images/checkbox_circle.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->checkbox_background_texture.load_from_file((resources_path + "images/checkbox_background.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->play_texture.load_from_file((resources_path + "images/play.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->stop_texture.load_from_file((resources_path + "images/stop.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->pause_texture.load_from_file((resources_path + "images/pause.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->save_texture.load_from_file((resources_path + "images/save.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->screenshot_texture.load_from_file((resources_path + "images/screenshot.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->trash_texture.load_from_file((resources_path + "images/trash.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->masked_texture.load_from_file((resources_path + "images/masked.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->unmasked_texture.load_from_file((resources_path + "images/unmasked.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->warning_texture.load_from_file((resources_path + "images/warning.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->question_mark_texture.load_from_file((resources_path + "images/question_mark.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->info_texture.load_from_file((resources_path + "images/info.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_home_texture.load_from_file((resources_path + "images/ps4_home.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_options_texture.load_from_file((resources_path + "images/ps4_options.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_dpad_up_texture.load_from_file((resources_path + "images/ps4_dpad_up.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_dpad_down_texture.load_from_file((resources_path + "images/ps4_dpad_down.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;
        
        if(!theme->ps4_dpad_left_texture.load_from_file((resources_path + "images/ps4_dpad_left.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_dpad_right_texture.load_from_file((resources_path + "images/ps4_dpad_right.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_cross_texture.load_from_file((resources_path + "images/ps4_cross.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        if(!theme->ps4_triangle_texture.load_from_file((resources_path + "images/ps4_triangle.png").c_str(), mgl::Texture::LoadOptions{false, false, MGL_TEXTURE_SCALE_LINEAR_MIPMAP}))
            goto error;

        return true;

        error:
        deinit_theme();
        return false;
    }

    void deinit_theme() {
        if(theme) {
            delete theme;
            theme = nullptr;
        }
    }

    Theme& get_theme() {
        assert(theme);
        return *theme;
    }

    bool init_color_theme(const Config &config, const GsrInfo &gsr_info) {
        if(color_theme)
            return true;

        color_theme = new ColorTheme();

        if(config.main_config.tint_color.empty())
            color_theme->tint_color = gpu_vendor_to_color(gsr_info.gpu_info.vendor);
        else
            color_theme->tint_color = color_name_to_color(config.main_config.tint_color);

        return true;
    }

    void deinit_color_theme() {
        if(color_theme) {
            delete color_theme;
            color_theme = nullptr;
        }
    }

    ColorTheme& get_color_theme() {
        assert(color_theme);
        return *color_theme;
    }
}