#include "../../include/gui/GlobalSettingsPage.hpp"

#include "../../include/Overlay.hpp"
#include "../../include/Theme.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Translation.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/Image.hpp"
#include "../../include/gui/RadioButton.hpp"
#include "../../include/gui/ComboBox.hpp"
#include "../../include/gui/CheckBox.hpp"
#include "../../include/gui/LineSeparator.hpp"
#include "../../include/gui/CustomRendererWidget.hpp"

#include <assert.h>
#include <X11/Xlib.h>
extern "C" {
#include <mgl/mgl.h>
}
#include <mglpp/window/Window.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Text.hpp>

#ifndef GSR_UI_VERSION
#define GSR_UI_VERSION "Unknown"
#endif

#ifndef GSR_FLATPAK_VERSION
#define GSR_FLATPAK_VERSION "Unknown"
#endif

namespace gsr {
    static const char* gpu_vendor_to_color_name(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN:  return "amd";
            case GpuVendor::AMD:      return "amd";
            case GpuVendor::INTEL:    return "intel";
            case GpuVendor::NVIDIA:   return "nvidia";
            case GpuVendor::BROADCOM: return "broadcom";
            case GpuVendor::APPLE:    return "apple";
        }
        return "amd";
    }

    static const char* gpu_vendor_to_string(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN:  return "Unknown";
            case GpuVendor::AMD:      return "AMD";
            case GpuVendor::INTEL:    return "Intel";
            case GpuVendor::NVIDIA:   return "NVIDIA";
            case GpuVendor::BROADCOM: return "Broadcom";
            case GpuVendor::APPLE:    return "Apple";
        }
        return "unknown";
    }

    static uint32_t mgl_modifier_to_hotkey_modifier(mgl::Keyboard::Key modifier_key) {
        switch(modifier_key) {
            case mgl::Keyboard::LControl:  return HOTKEY_MOD_LCTRL;
            case mgl::Keyboard::LShift:    return HOTKEY_MOD_LSHIFT;
            case mgl::Keyboard::LAlt:      return HOTKEY_MOD_LALT;
            case mgl::Keyboard::LSystem:   return HOTKEY_MOD_LSUPER;
            case mgl::Keyboard::RControl:  return HOTKEY_MOD_RCTRL;
            case mgl::Keyboard::RShift:    return HOTKEY_MOD_RSHIFT;
            case mgl::Keyboard::RAlt:      return HOTKEY_MOD_RALT;
            case mgl::Keyboard::RSystem:   return HOTKEY_MOD_RSUPER;
            default:                       return 0;
        }
        return 0;
    }

    static bool key_is_alpha_numerical(mgl::Keyboard::Key key) {
        return key >= mgl::Keyboard::A && key <= mgl::Keyboard::Num9;
    }

    GlobalSettingsPage::GlobalSettingsPage(Overlay *overlay, const GsrInfo *gsr_info, Config &config, PageStack *page_stack) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        overlay(overlay),
        config(config),
        gsr_info(gsr_info),
        page_stack(page_stack)
    {
        auto content_page = std::make_unique<GsrPage>(TR("Global"), TR("Settings"));
        content_page->add_button(TR("Back"), "back", get_color_theme().page_bg_color);
        content_page->on_click = [page_stack](const std::string &id) {
            if(id == "back")
                page_stack->pop();
        };
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
        load();

        auto hotkey_overlay = std::make_unique<CustomRendererWidget>(get_size());
        hotkey_overlay->draw_handler = [this](mgl::Window &window, mgl::vec2f, mgl::vec2f) {
            Button *configure_hotkey_button = configure_hotkey_get_button_by_active_type();
            if(!configure_hotkey_button)
                return;

            mgl::Text title_text(TRF("Press a key combination to use for the hotkey: \"%s\"", hotkey_configure_action_name.c_str()), get_theme().title_font_desc.c_str());
            mgl::Text hotkey_text(configure_hotkey_button->get_text(), get_theme().top_bar_font_desc.c_str());
            mgl::Text description_text(TR("Alpha-numerical keys can't be used alone in hotkeys, they have to be used one or more of these keys: Alt, Ctrl, Shift and Super.\nPress Esc to cancel or Backspace to remove the hotkey."), get_theme().body_font_desc.c_str());
            const float text_max_width = std::max(title_text.get_bounds().size.x, std::max(hotkey_text.get_bounds().size.x, description_text.get_bounds().size.x));

            const float padding_horizontal = int(get_theme().window_height * 0.01f);
            const float padding_vertical = int(get_theme().window_height * 0.01f);

            const mgl::vec2f bg_size = mgl::vec2f(text_max_width + padding_horizontal*2.0f, get_theme().window_height * 0.13f).floor();
            mgl::Rectangle bg_rect(mgl::vec2f(get_theme().window_width*0.5f - bg_size.x*0.5f, get_theme().window_height*0.5f - bg_size.y*0.5f).floor(), bg_size);
            bg_rect.set_color(get_color_theme().page_bg_color);
            window.draw(bg_rect);

            const mgl::vec2f tint_size = mgl::vec2f(bg_size.x, 0.004f * get_theme().window_height).floor();
            mgl::Rectangle tint_rect(bg_rect.get_position() - mgl::vec2f(0.0f, tint_size.y), tint_size);
            tint_rect.set_color(get_color_theme().tint_color);
            window.draw(tint_rect);

            title_text.set_position(mgl::vec2f(bg_rect.get_position() + mgl::vec2f(bg_rect.get_size().x*0.5f - title_text.get_bounds().size.x*0.5f, padding_vertical)).floor());
            description_text.set_position(mgl::vec2f(bg_rect.get_position() + mgl::vec2f(bg_rect.get_size().x*0.5f - description_text.get_bounds().size.x*0.5f, bg_rect.get_size().y - description_text.get_bounds().size.y - padding_vertical)).floor());

            window.draw(title_text);

            const float title_text_bottom = title_text.get_position().y + title_text.get_bounds().size.y;
            hotkey_text.set_position(
                mgl::vec2f(
                    bg_rect.get_position().x + bg_rect.get_size().x*0.5f - hotkey_text.get_bounds().size.x*0.5f,
                    title_text_bottom + (description_text.get_position().y - title_text_bottom) * 0.5f - hotkey_text.get_bounds().size.y*0.5f
                ).floor());
            window.draw(hotkey_text);

            const float caret_padding_x = int(0.001f * get_theme().window_height);
            const mgl::vec2f caret_size = mgl::vec2f(std::max(2.0f, 0.002f * get_theme().window_height), hotkey_text.get_bounds().size.y).floor();
            mgl::Rectangle caret_rect(hotkey_text.get_position() + mgl::vec2f(hotkey_text.get_bounds().size.x + caret_padding_x, hotkey_text.get_bounds().size.y*0.5f - caret_size.y*0.5f).floor(), caret_size);
            window.draw(caret_rect);

            window.draw(description_text);
        };
        hotkey_overlay->set_visible(false);
        hotkey_overlay_ptr = hotkey_overlay.get();
        add_widget(std::move(hotkey_overlay));
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_appearance_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Accent color"), get_color_theme().text_color));
        auto tint_color_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        tint_color_radio_button_ptr = tint_color_radio_button.get();
        tint_color_radio_button->add_item(TR("Red"), "amd");
        tint_color_radio_button->add_item(TR("Green"), "nvidia");
        tint_color_radio_button->add_item(TR("Blue"), "intel");
        tint_color_radio_button->on_selection_changed = [](std::string_view, std::string_view id) {
            if(id == "amd")
                get_color_theme().tint_color = mgl::Color(221, 0, 49);
            else if(id == "nvidia")
                get_color_theme().tint_color = mgl::Color(118, 185, 0);
            else if(id == "intel")
                get_color_theme().tint_color = mgl::Color(8, 109, 183);
            return true;
        };
        list->add_widget(std::move(tint_color_radio_button));
        return std::make_unique<Subsection>(TR("Appearance"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_startup_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Start program on system startup?"), get_color_theme().text_color));
        auto startup_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        startup_radio_button_ptr = startup_radio_button.get();
        startup_radio_button->add_item(TR("Yes"), "start_on_system_startup");
        startup_radio_button->add_item(TR("No"), "dont_start_on_system_startup");
        startup_radio_button->on_selection_changed = [&](std::string_view, std::string_view id) {
            bool enable = false;
            if(id == "dont_start_on_system_startup")
                enable = false;
            else if(id == "start_on_system_startup")
                enable = true;
            else
                return false;

            const int exit_status = set_xdg_autostart(enable);
            if(on_startup_changed)
                on_startup_changed(enable, exit_status);
            return exit_status == 0;
        };
        list->add_widget(std::move(startup_radio_button));
        return std::make_unique<Subsection>(TR("Startup"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<RadioButton> GlobalSettingsPage::create_enable_keyboard_hotkeys_button() {
        auto enable_hotkeys_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::VERTICAL);
        enable_keyboard_hotkeys_radio_button_ptr = enable_hotkeys_radio_button.get();
        enable_hotkeys_radio_button->add_item(TR("Yes"), "enable_hotkeys");
        enable_hotkeys_radio_button->add_item(TR("Yes, but only grab virtual devices (supports some input remapping software)"), "enable_hotkeys_virtual_devices");
        enable_hotkeys_radio_button->add_item(TR("Yes, but don't grab devices (supports all input remapping software)"), "enable_hotkeys_no_grab");
        enable_hotkeys_radio_button->add_item(TR("No"), "disable_hotkeys");
        enable_hotkeys_radio_button->on_selection_changed = [&](std::string_view, std::string_view id) {
            if(on_keyboard_hotkey_changed)
                on_keyboard_hotkey_changed(id);
            return true;
        };
        return enable_hotkeys_radio_button;
    }

    std::unique_ptr<RadioButton> GlobalSettingsPage::create_enable_joystick_hotkeys_button() {
        auto enable_hotkeys_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        enable_joystick_hotkeys_radio_button_ptr = enable_hotkeys_radio_button.get();
        enable_hotkeys_radio_button->add_item(TR("Yes"), "enable_hotkeys");
        enable_hotkeys_radio_button->add_item(TR("No"), "disable_hotkeys");
        enable_hotkeys_radio_button->on_selection_changed = [&](std::string_view, std::string_view id) {
            if(on_joystick_hotkey_changed)
                on_joystick_hotkey_changed(id);
            return true;
        };
        return enable_hotkeys_radio_button;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_show_hide_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Show/hide UI:"), get_color_theme().text_color));
        auto show_hide_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        show_hide_button_ptr = show_hide_button.get();
        list->add_widget(std::move(show_hide_button));

        show_hide_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::SHOW_HIDE);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_replay_on_off_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Turn replay on/off:"), get_color_theme().text_color));
        auto turn_replay_on_off_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        turn_replay_on_off_button_ptr = turn_replay_on_off_button.get();
        list->add_widget(std::move(turn_replay_on_off_button));

        turn_replay_on_off_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::REPLAY_START_STOP);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_replay_save_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Save replay:"), get_color_theme().text_color));
        auto save_replay_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_replay_button_ptr = save_replay_button.get();
        list->add_widget(std::move(save_replay_button));

        save_replay_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::REPLAY_SAVE);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_replay_partial_save_1min_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Save 1 minute replay:"), get_color_theme().text_color));
        auto save_replay_1_min_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_replay_1_min_button_ptr = save_replay_1_min_button.get();
        list->add_widget(std::move(save_replay_1_min_button));

        save_replay_1_min_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::REPLAY_SAVE_1_MIN);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_replay_partial_save_10min_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Save 10 minute replay:"), get_color_theme().text_color));
        auto save_replay_10_min_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_replay_10_min_button_ptr = save_replay_10_min_button.get();
        list->add_widget(std::move(save_replay_10_min_button));

        save_replay_10_min_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::REPLAY_SAVE_10_MIN);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_record_start_stop_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Start/stop recording:"), get_color_theme().text_color));
        auto start_stop_recording_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        start_stop_recording_button_ptr = start_stop_recording_button.get();
        list->add_widget(std::move(start_stop_recording_button));

        start_stop_recording_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::RECORD_START_STOP);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_record_pause_unpause_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Pause/unpause recording:"), get_color_theme().text_color));
        auto pause_unpause_recording_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        pause_unpause_recording_button_ptr = pause_unpause_recording_button.get();
        list->add_widget(std::move(pause_unpause_recording_button));

        pause_unpause_recording_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::RECORD_PAUSE_UNPAUSE);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_record_hotkey_window_region_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Start/stop recording a region:"), get_color_theme().text_color));
        auto start_stop_recording_region_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        start_stop_recording_region_button_ptr = start_stop_recording_region_button.get();
        list->add_widget(std::move(start_stop_recording_region_button));

        start_stop_recording_region_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::RECORD_START_STOP_REGION);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_record_hotkey_window_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        char str[128];
        if(gsr_info->system_info.display_server == DisplayServer::X11)
            snprintf(str, sizeof(str), "%s", TR("Start/stop recording a window:"));
        else
            snprintf(str, sizeof(str), "%s", TR("Start/stop recording with desktop portal:"));

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));
        auto start_stop_recording_window_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        start_stop_recording_window_button_ptr = start_stop_recording_window_button.get();
        list->add_widget(std::move(start_stop_recording_window_button));

        start_stop_recording_window_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::RECORD_START_STOP_WINDOW);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_stream_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Start/stop streaming:"), get_color_theme().text_color));
        auto start_stop_streaming_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        start_stop_streaming_button_ptr = start_stop_streaming_button.get();
        list->add_widget(std::move(start_stop_streaming_button));

        start_stop_streaming_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::STREAM_START_STOP);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_screenshot_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Take a screenshot:"), get_color_theme().text_color));
        auto take_screenshot_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        take_screenshot_button_ptr = take_screenshot_button.get();
        list->add_widget(std::move(take_screenshot_button));

        take_screenshot_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::TAKE_SCREENSHOT);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_screenshot_region_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Take a screenshot of a region:"), get_color_theme().text_color));
        auto take_screenshot_region_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        take_screenshot_region_button_ptr = take_screenshot_region_button.get();
        list->add_widget(std::move(take_screenshot_region_button));

        take_screenshot_region_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::TAKE_SCREENSHOT_REGION);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_screenshot_window_hotkey_options() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        char str[128];
        if(gsr_info->system_info.display_server == DisplayServer::X11)
            snprintf(str, sizeof(str), "%s", TR("Take a screenshot of a window:"));
        else
            snprintf(str, sizeof(str), "%s", TR("Take a screenshot with desktop portal:"));

        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));
        auto take_screenshot_window_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        take_screenshot_window_button_ptr = take_screenshot_window_button.get();
        list->add_widget(std::move(take_screenshot_window_button));

        take_screenshot_window_button_ptr->on_click = [this] {
            configure_hotkey_start(ConfigureHotkeyType::TAKE_SCREENSHOT_WINDOW);
        };

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_hotkey_control_buttons() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        auto clear_hotkeys_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Clear hotkeys"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        clear_hotkeys_button->on_click = [this] {
            for_each_config_hotkey([&](ConfigHotkey *config_hotkey_item) {
                *config_hotkey_item = {mgl::Keyboard::Unknown, 0};
            });
            load_hotkeys();
            overlay->rebind_all_keyboard_hotkeys();
        };
        list->add_widget(std::move(clear_hotkeys_button));

        auto reset_hotkeys_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Reset hotkeys to default"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        reset_hotkeys_button->on_click = [this] {
            config.set_hotkeys_to_default();
            load_hotkeys();
            overlay->rebind_all_keyboard_hotkeys();
        };
        list->add_widget(std::move(reset_hotkeys_button));

        return list;
    }

    static std::unique_ptr<List> create_joystick_hotkey_text(mgl::Texture *image1, mgl::Texture *image2, float max_height, const char *suffix) {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Press"), get_color_theme().text_color));
        list->add_widget(std::make_unique<Image>(image1, mgl::vec2f{max_height, 1000.0f}, Image::ScaleBehavior::SCALE));
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("and"), get_color_theme().text_color));
        list->add_widget(std::make_unique<Image>(image2, mgl::vec2f{max_height, 1000.0f}, Image::ScaleBehavior::SCALE));
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), suffix, get_color_theme().text_color));
        return list;
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_keyboard_hotkey_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        List *list_ptr = list.get();
        auto subsection = std::make_unique<Subsection>(TR("Keyboard hotkeys"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));

        list_ptr->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Enable keyboard hotkeys?"), get_color_theme().text_color));
        list_ptr->add_widget(create_enable_keyboard_hotkeys_button());
        list_ptr->add_widget(std::make_unique<LineSeparator>(LineSeparator::Orientation::HORIZONTAL, subsection->get_inner_size().x));
        list_ptr->add_widget(create_show_hide_hotkey_options());
        list_ptr->add_widget(create_replay_on_off_hotkey_options());
        list_ptr->add_widget(create_replay_save_hotkey_options());
        list_ptr->add_widget(create_replay_partial_save_1min_hotkey_options());
        list_ptr->add_widget(create_replay_partial_save_10min_hotkey_options());
        list_ptr->add_widget(create_record_start_stop_hotkey_options());
        list_ptr->add_widget(create_record_pause_unpause_hotkey_options());
        list_ptr->add_widget(create_record_hotkey_window_region_options());
        list_ptr->add_widget(create_record_hotkey_window_options());
        list_ptr->add_widget(create_stream_hotkey_options());
        list_ptr->add_widget(create_screenshot_hotkey_options());
        list_ptr->add_widget(create_screenshot_region_hotkey_options());
        list_ptr->add_widget(create_screenshot_window_hotkey_options());
        list_ptr->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Press ESC to go back to the previous page/close the UI."), get_color_theme().text_color));
        list_ptr->add_widget(create_hotkey_control_buttons());
        return subsection;
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_controller_hotkey_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        List *list_ptr = list.get();
        auto subsection = std::make_unique<Subsection>(TR("Controller hotkeys"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));

        list_ptr->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Enable controller hotkeys?"), get_color_theme().text_color));
        list_ptr->add_widget(create_enable_joystick_hotkeys_button());
        list_ptr->add_widget(std::make_unique<LineSeparator>(LineSeparator::Orientation::HORIZONTAL, subsection->get_inner_size().x));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_options_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to show/hide the UI")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_dpad_up_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to take a screenshot")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_dpad_down_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to save a replay")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_dpad_left_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to start/stop recording")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_dpad_right_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to turn replay on/off")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_cross_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to save a 1 minute replay")));
        list_ptr->add_widget(create_joystick_hotkey_text(&get_theme().ps4_home_texture, &get_theme().ps4_triangle_texture, 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()), TR("to save a 10 minute replay")));
        return subsection;
    }

    std::unique_ptr<Button> GlobalSettingsPage::create_exit_program_button() {
        auto exit_program_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Exit program"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        exit_program_button->on_click = [&]() {
            if(on_click_exit_program_button)
                on_click_exit_program_button("exit");
        };
        return exit_program_button;
    }

    std::unique_ptr<Button> GlobalSettingsPage::create_go_back_to_old_ui_button() {
        auto exit_program_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Go back to the old UI"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        exit_program_button->on_click = [&]() {
            if(on_click_exit_program_button)
                on_click_exit_program_button("back-to-old-ui");
        };
        return exit_program_button;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_notification_speed() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Notification speed"), get_color_theme().text_color));

        auto radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        notification_speed_button_ptr = radio_button.get();
        radio_button->add_item(TR("Normal"), "normal");
        radio_button->add_item(TR("Fast"), "fast");
        radio_button->on_selection_changed = [this](std::string_view, std::string_view id) {
            if(id == "normal")
                overlay->set_notification_speed(NotificationSpeed::NORMAL);
            else if(id == "fast")
                overlay->set_notification_speed(NotificationSpeed::FAST);
            return true;
        };
        list->add_widget(std::move(radio_button));

        return list;
    }

    std::unique_ptr<List> GlobalSettingsPage::create_language() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Language"), get_color_theme().text_color));

        auto combo_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        language_combo_box_ptr = combo_box.get();
        combo_box->add_item(TR("System language"), "");
        combo_box->add_item("English", "en");
        combo_box->add_item("Español", "es");
        combo_box->add_item("Français", "fr");
        combo_box->add_item("Magyar", "hu");
        combo_box->add_item("Português", "pt");
        combo_box->add_item("日本語", "ja");
        combo_box->add_item("Русский", "ru");
        combo_box->add_item("Українська", "uk");
        combo_box->add_item("Türkçe", "tr");
        combo_box->add_item("简体中文", "zh_CN");
        combo_box->on_selection_changed = [this](std::string_view, std::string_view id) {
            Translation::instance().load_language(id);
            config.main_config.language = std::string(id);
            if(on_language_changed)
                on_language_changed(get_scroll_y());
            return true;
        };
        list->add_widget(std::move(combo_box));

        return list;
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_application_options_subsection(ScrollablePage *parent_page) {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        List *list_ptr = list.get();
        auto subsection = std::make_unique<Subsection>(TR("General"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));

        {
            auto horizontal_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
            horizontal_list->set_spacing(0.02f);
            horizontal_list->add_widget(create_notification_speed());
            horizontal_list->add_widget(create_language());
            list_ptr->add_widget(std::move(horizontal_list));
        }
        {
            auto exclude_metadata_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Exclude personal metadata from the video (such as audio track title)"));
            exclude_metadata_checkbox_ptr = exclude_metadata_checkbox.get();
            list_ptr->add_widget(std::move(exclude_metadata_checkbox));
        }
        list_ptr->add_widget(std::make_unique<LineSeparator>(LineSeparator::Orientation::HORIZONTAL, subsection->get_inner_size().x));

        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        auto navigate_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        navigate_list->add_widget(create_exit_program_button());
        if(inside_flatpak)
            navigate_list->add_widget(create_go_back_to_old_ui_button());
        list_ptr->add_widget(std::move(navigate_list));

        return subsection;
    }

    std::unique_ptr<Subsection> GlobalSettingsPage::create_application_info_subsection(ScrollablePage *parent_page) {
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);

        char str[128];
        const std::string gsr_version = gsr_info->system_info.gsr_version.to_string();
        snprintf(str, sizeof(str), TR("GSR version: %s"), gsr_version.c_str());
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));

        snprintf(str, sizeof(str), TR("GSR-UI version: %s"), GSR_UI_VERSION);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));

        if(inside_flatpak) {
            snprintf(str, sizeof(str), TR("Flatpak version: %s"), GSR_FLATPAK_VERSION);
            list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));
        }

        snprintf(str, sizeof(str), TR("GPU vendor: %s"), gpu_vendor_to_string(gsr_info->gpu_info.vendor));
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), str, get_color_theme().text_color));

        return std::make_unique<Subsection>(TR("Application info"), std::move(list), mgl::vec2f(parent_page->get_inner_size().x, 0.0f));
    }

    void GlobalSettingsPage::add_widgets() {
        auto scrollable_page = std::make_unique<ScrollablePage>(content_page_ptr->get_inner_size());
        scrollable_page_ptr = scrollable_page.get();

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_appearance_subsection(scrollable_page.get()));
        settings_list->add_widget(create_startup_subsection(scrollable_page.get()));
        settings_list->add_widget(create_keyboard_hotkey_subsection(scrollable_page.get()));
        settings_list->add_widget(create_controller_hotkey_subsection(scrollable_page.get()));
        settings_list->add_widget(create_application_options_subsection(scrollable_page.get()));
        settings_list->add_widget(create_application_info_subsection(scrollable_page.get()));
        scrollable_page->add_widget(std::move(settings_list));

        content_page_ptr->add_widget(std::move(scrollable_page));
    }

    int GlobalSettingsPage::get_scroll_y() const {
        return scrollable_page_ptr ? scrollable_page_ptr->get_scroll_target_y() : 0;
    }

    void GlobalSettingsPage::set_scroll_y(int y) {
        if(scrollable_page_ptr)
            scrollable_page_ptr->set_scroll(y);
    }

    void GlobalSettingsPage::on_navigate_away_from_page() {
        save();
        if(on_page_closed)
            on_page_closed();
    }

    void GlobalSettingsPage::load() {
        if(config.main_config.tint_color.empty())
            tint_color_radio_button_ptr->set_selected_item(gpu_vendor_to_color_name(gsr_info->gpu_info.vendor));
        else
            tint_color_radio_button_ptr->set_selected_item(config.main_config.tint_color);

        startup_radio_button_ptr->set_selected_item(is_xdg_autostart_enabled() ? "start_on_system_startup" : "dont_start_on_system_startup", false, false);

        enable_keyboard_hotkeys_radio_button_ptr->set_selected_item(config.main_config.hotkeys_enable_option, false, false);
        enable_joystick_hotkeys_radio_button_ptr->set_selected_item(config.main_config.joystick_hotkeys_enable_option, false, false);

        notification_speed_button_ptr->set_selected_item(config.main_config.notification_speed);
        language_combo_box_ptr->set_selected_item(config.main_config.language);
        exclude_metadata_checkbox_ptr->set_checked(config.main_config.exclude_metadata);

        load_hotkeys();
    }

    void GlobalSettingsPage::load_hotkeys() {
        turn_replay_on_off_button_ptr->set_text(config.replay_config.start_stop_hotkey.to_string());
        save_replay_button_ptr->set_text(config.replay_config.save_hotkey.to_string());
        save_replay_1_min_button_ptr->set_text(config.replay_config.save_1_min_hotkey.to_string());
        save_replay_10_min_button_ptr->set_text(config.replay_config.save_10_min_hotkey.to_string());

        start_stop_recording_button_ptr->set_text(config.record_config.start_stop_hotkey.to_string());
        pause_unpause_recording_button_ptr->set_text(config.record_config.pause_unpause_hotkey.to_string());
        start_stop_recording_region_button_ptr->set_text(config.record_config.start_stop_region_hotkey.to_string());
        start_stop_recording_window_button_ptr->set_text(config.record_config.start_stop_window_hotkey.to_string());

        start_stop_streaming_button_ptr->set_text(config.streaming_config.start_stop_hotkey.to_string());

        take_screenshot_button_ptr->set_text(config.screenshot_config.take_screenshot_hotkey.to_string());
        take_screenshot_region_button_ptr->set_text(config.screenshot_config.take_screenshot_region_hotkey.to_string());
        take_screenshot_window_button_ptr->set_text(config.screenshot_config.take_screenshot_window_hotkey.to_string());

        show_hide_button_ptr->set_text(config.main_config.show_hide_hotkey.to_string());
    }

    void GlobalSettingsPage::save() {
        configure_hotkey_cancel();
        config.main_config.tint_color = tint_color_radio_button_ptr->get_selected_id();
        config.main_config.hotkeys_enable_option = enable_keyboard_hotkeys_radio_button_ptr->get_selected_id();
        config.main_config.joystick_hotkeys_enable_option = enable_joystick_hotkeys_radio_button_ptr->get_selected_id();
        config.main_config.notification_speed = notification_speed_button_ptr->get_selected_id();
        config.main_config.language = language_combo_box_ptr->get_selected_id();
        config.main_config.exclude_metadata = exclude_metadata_checkbox_ptr->is_checked();
        save_config(config);
    }

    bool GlobalSettingsPage::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!StaticPage::on_event(event, window, offset))
            return false;

        if(configure_hotkey_type == ConfigureHotkeyType::NONE)
            return true;

        Button *configure_hotkey_button = configure_hotkey_get_button_by_active_type();
        if(!configure_hotkey_button)
            return true;

        if(event.type == mgl::Event::KeyPressed) {
            if(event.key.code == mgl::Keyboard::Escape)
                return false;

            if(event.key.code == mgl::Keyboard::Backspace) {
                configure_config_hotkey = {mgl::Keyboard::Unknown, 0};
                configure_hotkey_button->set_text("");
                configure_hotkey_stop_and_save();
                return false;
            }

            if(mgl::Keyboard::key_is_modifier(event.key.code)) {
                configure_config_hotkey.modifiers |= mgl_modifier_to_hotkey_modifier(event.key.code);
                configure_hotkey_button->set_text(configure_config_hotkey.to_string());
            } else if(event.key.code != mgl::Keyboard::Unknown && (configure_config_hotkey.modifiers != 0 || !key_is_alpha_numerical(event.key.code))) {
                configure_config_hotkey.key = event.key.code;
                configure_hotkey_button->set_text(configure_config_hotkey.to_string());
                configure_hotkey_stop_and_save();
            }

            return false;
        } else if(event.type == mgl::Event::KeyReleased) {
            if(event.key.code == mgl::Keyboard::Escape) {
                configure_hotkey_cancel();
                return false;
            }

            if(mgl::Keyboard::key_is_modifier(event.key.code)) {
                configure_config_hotkey.modifiers &= ~mgl_modifier_to_hotkey_modifier(event.key.code);
                configure_hotkey_button->set_text(configure_config_hotkey.to_string());
            }

            return false;
        }

        return true;
    }

    Button* GlobalSettingsPage::configure_hotkey_get_button_by_active_type() {
        switch(configure_hotkey_type) {
            case ConfigureHotkeyType::NONE:
                return nullptr;
            case ConfigureHotkeyType::REPLAY_START_STOP:
                return turn_replay_on_off_button_ptr;
            case ConfigureHotkeyType::REPLAY_SAVE:
                return save_replay_button_ptr;
            case ConfigureHotkeyType::REPLAY_SAVE_1_MIN:
                return save_replay_1_min_button_ptr;
            case ConfigureHotkeyType::REPLAY_SAVE_10_MIN:
                return save_replay_10_min_button_ptr;
            case ConfigureHotkeyType::RECORD_START_STOP:
                return start_stop_recording_button_ptr;
            case ConfigureHotkeyType::RECORD_PAUSE_UNPAUSE:
                return pause_unpause_recording_button_ptr;
            case ConfigureHotkeyType::RECORD_START_STOP_REGION:
                return start_stop_recording_region_button_ptr;
            case ConfigureHotkeyType::RECORD_START_STOP_WINDOW:
                return start_stop_recording_window_button_ptr;
            case ConfigureHotkeyType::STREAM_START_STOP:
                return start_stop_streaming_button_ptr;
            case ConfigureHotkeyType::TAKE_SCREENSHOT:
                return take_screenshot_button_ptr;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_REGION:
                return take_screenshot_region_button_ptr;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_WINDOW:
                return take_screenshot_window_button_ptr;
            case ConfigureHotkeyType::SHOW_HIDE:
                return show_hide_button_ptr;
        }
        return nullptr;
    }

    ConfigHotkey* GlobalSettingsPage::configure_hotkey_get_config_by_active_type() {
        switch(configure_hotkey_type) {
            case ConfigureHotkeyType::NONE:
                return nullptr;
            case ConfigureHotkeyType::REPLAY_START_STOP:
                return &config.replay_config.start_stop_hotkey;
            case ConfigureHotkeyType::REPLAY_SAVE:
                return &config.replay_config.save_hotkey;
            case ConfigureHotkeyType::REPLAY_SAVE_1_MIN:
                return &config.replay_config.save_1_min_hotkey;
            case ConfigureHotkeyType::REPLAY_SAVE_10_MIN:
                return &config.replay_config.save_10_min_hotkey;
            case ConfigureHotkeyType::RECORD_START_STOP:
                return &config.record_config.start_stop_hotkey;
            case ConfigureHotkeyType::RECORD_PAUSE_UNPAUSE:
                return &config.record_config.pause_unpause_hotkey;
            case ConfigureHotkeyType::RECORD_START_STOP_REGION:
                return &config.record_config.start_stop_region_hotkey;
            case ConfigureHotkeyType::RECORD_START_STOP_WINDOW:
                return &config.record_config.start_stop_window_hotkey;
            case ConfigureHotkeyType::STREAM_START_STOP:
                return &config.streaming_config.start_stop_hotkey;
            case ConfigureHotkeyType::TAKE_SCREENSHOT:
                return &config.screenshot_config.take_screenshot_hotkey;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_REGION:
                return &config.screenshot_config.take_screenshot_region_hotkey;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_WINDOW:
                return &config.screenshot_config.take_screenshot_window_hotkey;
            case ConfigureHotkeyType::SHOW_HIDE:
                return &config.main_config.show_hide_hotkey;
        }
        return nullptr;
    }

    void GlobalSettingsPage::for_each_config_hotkey(std::function<void(ConfigHotkey *config_hotkey)> callback) {
        ConfigHotkey *config_hotkeys[] = {
            &config.replay_config.start_stop_hotkey,
            &config.replay_config.save_hotkey,
            &config.replay_config.save_1_min_hotkey,
            &config.replay_config.save_10_min_hotkey,
            &config.record_config.start_stop_hotkey,
            &config.record_config.pause_unpause_hotkey,
            &config.record_config.start_stop_region_hotkey,
            &config.record_config.start_stop_window_hotkey,
            &config.streaming_config.start_stop_hotkey,
            &config.screenshot_config.take_screenshot_hotkey,
            &config.screenshot_config.take_screenshot_region_hotkey,
            &config.screenshot_config.take_screenshot_window_hotkey,
            &config.main_config.show_hide_hotkey
        };
        for(ConfigHotkey *config_hotkey : config_hotkeys) {
            callback(config_hotkey);
        }
    }

    void GlobalSettingsPage::configure_hotkey_start(ConfigureHotkeyType hotkey_type) {
        assert(hotkey_type != ConfigureHotkeyType::NONE);
        configure_config_hotkey = {0, 0};
        configure_hotkey_type = hotkey_type;

        content_page_ptr->set_visible(false);
        hotkey_overlay_ptr->set_visible(true);
        overlay->unbind_all_keyboard_hotkeys();
        configure_hotkey_get_button_by_active_type()->set_text("");

        switch(hotkey_type) {
            case ConfigureHotkeyType::NONE:
                hotkey_configure_action_name = "";
                break;
            case ConfigureHotkeyType::REPLAY_START_STOP:
                hotkey_configure_action_name = TR("Turn replay on/off");
                break;
            case ConfigureHotkeyType::REPLAY_SAVE:
                hotkey_configure_action_name = TR("Save replay");
                break;
            case ConfigureHotkeyType::REPLAY_SAVE_1_MIN:
                hotkey_configure_action_name = TR("Save 1 minute replay");
                break;
            case ConfigureHotkeyType::REPLAY_SAVE_10_MIN:
                hotkey_configure_action_name = TR("Save 10 minute replay");
                break;
            case ConfigureHotkeyType::RECORD_START_STOP:
                hotkey_configure_action_name = TR("Start/stop recording");
                break;
            case ConfigureHotkeyType::RECORD_PAUSE_UNPAUSE:
                hotkey_configure_action_name = TR("Pause/unpause recording");
                break;
            case ConfigureHotkeyType::RECORD_START_STOP_REGION:
                hotkey_configure_action_name = TR("Start/stop recording a region");
                break;
            case ConfigureHotkeyType::RECORD_START_STOP_WINDOW:
                if(gsr_info->system_info.display_server == DisplayServer::X11)
                    hotkey_configure_action_name = TR("Start/stop recording a window");
                else
                    hotkey_configure_action_name = TR("Start/stop recording with desktop portal");
                break;
            case ConfigureHotkeyType::STREAM_START_STOP:
                hotkey_configure_action_name = TR("Start/stop streaming");
                break;
            case ConfigureHotkeyType::TAKE_SCREENSHOT:
                hotkey_configure_action_name = TR("Take a screenshot");
                break;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_REGION:
                hotkey_configure_action_name = TR("Take a screenshot of a region");
                break;
            case ConfigureHotkeyType::TAKE_SCREENSHOT_WINDOW: {
                if(gsr_info->system_info.display_server == DisplayServer::X11)
                    hotkey_configure_action_name = TR("Take a screenshot of a window");
                else
                    hotkey_configure_action_name = TR("Take a screenshot with desktop portal");
                break;
            }
            case ConfigureHotkeyType::SHOW_HIDE:
                hotkey_configure_action_name = TR("Show/hide UI");
                break;
        }
    }

    void GlobalSettingsPage::configure_hotkey_cancel() {
        Button *config_hotkey_button = configure_hotkey_get_button_by_active_type();
        ConfigHotkey *config_hotkey = configure_hotkey_get_config_by_active_type();
        if(config_hotkey_button && config_hotkey)
            config_hotkey_button->set_text(config_hotkey->to_string());

        configure_config_hotkey = {0, 0};
        configure_hotkey_type = ConfigureHotkeyType::NONE;
        content_page_ptr->set_visible(true);
        hotkey_overlay_ptr->set_visible(false);
        overlay->rebind_all_keyboard_hotkeys();
    }

    void GlobalSettingsPage::configure_hotkey_stop_and_save() {
        Button *config_hotkey_button = configure_hotkey_get_button_by_active_type();
        ConfigHotkey *config_hotkey = configure_hotkey_get_config_by_active_type();
        if(config_hotkey_button && config_hotkey) {
            bool hotkey_used_by_another_action = false;
            if(configure_config_hotkey.key != mgl::Keyboard::Unknown) {
                for_each_config_hotkey([&](ConfigHotkey *config_hotkey_item) {
                    if(config_hotkey_item != config_hotkey && *config_hotkey_item == configure_config_hotkey)
                        hotkey_used_by_another_action = true;
                });
            }

            if(hotkey_used_by_another_action) {
                const std::string error_msg = TRF("The hotkey %s is already used for something else", configure_config_hotkey.to_string().c_str());
                overlay->show_notification(error_msg.c_str(), 3.0, mgl::Color(255, 0, 0, 255), mgl::Color(255, 0, 0, 255), NotificationType::NONE);
                config_hotkey_button->set_text(config_hotkey->to_string());
                configure_config_hotkey = {0, 0};
                return;
            }

            *config_hotkey = configure_config_hotkey;
        }

        configure_config_hotkey = {0, 0};
        configure_hotkey_type = ConfigureHotkeyType::NONE;
        content_page_ptr->set_visible(true);
        hotkey_overlay_ptr->set_visible(false);
        overlay->rebind_all_keyboard_hotkeys();
    }
}
