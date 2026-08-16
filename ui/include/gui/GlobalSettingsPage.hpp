#pragma once

#include "StaticPage.hpp"
#include "../GsrInfo.hpp"
#include "../Config.hpp"

#include <functional>
#include <mglpp/window/Event.hpp>

namespace gsr {
    class Overlay;
    class GsrPage;
    class PageStack;
    class ScrollablePage;
    class Subsection;
    class RadioButton;
    class Button;
    class List;
    class ComboBox;
    class CustomRendererWidget;
    class CheckBox;

    enum ConfigureHotkeyType {
        NONE,
        REPLAY_START_STOP,
        REPLAY_SAVE,
        REPLAY_SAVE_1_MIN,
        REPLAY_SAVE_10_MIN,
        RECORD_START_STOP,
        RECORD_PAUSE_UNPAUSE,
        RECORD_START_STOP_REGION,
        RECORD_START_STOP_WINDOW,
        STREAM_START_STOP,
        TAKE_SCREENSHOT,
        TAKE_SCREENSHOT_REGION,
        TAKE_SCREENSHOT_WINDOW,
        SHOW_HIDE
    };

    class GlobalSettingsPage : public StaticPage {
    public:
        GlobalSettingsPage(Overlay *overlay, const GsrInfo *gsr_info, Config &config, PageStack *page_stack);
        GlobalSettingsPage(const GlobalSettingsPage&) = delete;
        GlobalSettingsPage& operator=(const GlobalSettingsPage&) = delete;

        void load();
        void save();
        void on_navigate_away_from_page() override;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;

        std::function<void(bool enable, int exit_status)> on_startup_changed;
        std::function<void(std::string_view reason)> on_click_exit_program_button;
        std::function<void(std::string_view hotkey_option)> on_keyboard_hotkey_changed;
        std::function<void(std::string_view hotkey_option)> on_joystick_hotkey_changed;
        std::function<void()> on_page_closed;
        std::function<void(int scroll_y)> on_language_changed;

        int get_scroll_y() const;
        void set_scroll_y(int y);
    private:
        void load_hotkeys();

        std::unique_ptr<Subsection> create_appearance_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Subsection> create_startup_subsection(ScrollablePage *parent_page);
        std::unique_ptr<RadioButton> create_enable_keyboard_hotkeys_button();
        std::unique_ptr<RadioButton> create_enable_joystick_hotkeys_button();
        std::unique_ptr<List> create_show_hide_hotkey_options();
        std::unique_ptr<List> create_replay_on_off_hotkey_options();
        std::unique_ptr<List> create_replay_save_hotkey_options();
        std::unique_ptr<List> create_replay_partial_save_1min_hotkey_options();
        std::unique_ptr<List> create_replay_partial_save_10min_hotkey_options();
        std::unique_ptr<List> create_record_start_stop_hotkey_options();
        std::unique_ptr<List> create_record_pause_unpause_hotkey_options();
        std::unique_ptr<List> create_record_hotkey_window_region_options();
        std::unique_ptr<List> create_record_hotkey_window_options();
        std::unique_ptr<List> create_stream_hotkey_options();
        std::unique_ptr<List> create_screenshot_hotkey_options();
        std::unique_ptr<List> create_screenshot_region_hotkey_options();
        std::unique_ptr<List> create_screenshot_window_hotkey_options();
        std::unique_ptr<List> create_hotkey_control_buttons();
        std::unique_ptr<Subsection> create_keyboard_hotkey_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Subsection> create_controller_hotkey_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Button> create_exit_program_button();
        std::unique_ptr<Button> create_go_back_to_old_ui_button();
        std::unique_ptr<List> create_notification_speed();
        std::unique_ptr<List> create_language();
        std::unique_ptr<Subsection> create_application_options_subsection(ScrollablePage *parent_page);
        std::unique_ptr<Subsection> create_application_info_subsection(ScrollablePage *parent_page);
        void add_widgets();

        Button* configure_hotkey_get_button_by_active_type();
        ConfigHotkey* configure_hotkey_get_config_by_active_type();
        void for_each_config_hotkey(std::function<void(ConfigHotkey *config_hotkey)> callback);
        void configure_hotkey_start(ConfigureHotkeyType hotkey_type);
        void configure_hotkey_cancel();
        void configure_hotkey_stop_and_save();
    private:
        Overlay *overlay = nullptr;
        Config &config;
        const GsrInfo *gsr_info = nullptr;

        GsrPage *content_page_ptr = nullptr;
        PageStack *page_stack = nullptr;
        RadioButton *tint_color_radio_button_ptr = nullptr;
        RadioButton *startup_radio_button_ptr = nullptr;
        RadioButton *enable_keyboard_hotkeys_radio_button_ptr = nullptr;
        RadioButton *enable_joystick_hotkeys_radio_button_ptr = nullptr;

        Button *turn_replay_on_off_button_ptr = nullptr;
        Button *save_replay_button_ptr = nullptr;
        Button *save_replay_1_min_button_ptr = nullptr;
        Button *save_replay_10_min_button_ptr = nullptr;
        Button *start_stop_recording_button_ptr = nullptr;
        Button *pause_unpause_recording_button_ptr = nullptr;
        Button *start_stop_recording_region_button_ptr = nullptr;
        Button *start_stop_recording_window_button_ptr = nullptr;
        Button *start_stop_streaming_button_ptr = nullptr;
        Button *take_screenshot_button_ptr = nullptr;
        Button *take_screenshot_region_button_ptr = nullptr;
        Button *take_screenshot_window_button_ptr = nullptr;
        Button *show_hide_button_ptr = nullptr;
        RadioButton *notification_speed_button_ptr = nullptr;
        ComboBox *language_combo_box_ptr = nullptr;
        CheckBox *exclude_metadata_checkbox_ptr = nullptr;
        ScrollablePage *scrollable_page_ptr = nullptr;

        ConfigHotkey configure_config_hotkey;
        ConfigureHotkeyType configure_hotkey_type = ConfigureHotkeyType::NONE;

        CustomRendererWidget *hotkey_overlay_ptr = nullptr;
        std::string hotkey_configure_action_name;
    };
}