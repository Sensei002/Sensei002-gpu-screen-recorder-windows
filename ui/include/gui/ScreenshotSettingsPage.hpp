#pragma once

#include "StaticPage.hpp"
#include "List.hpp"
#include "ComboBox.hpp"
#include "Entry.hpp"
#include "CheckBox.hpp"
#include "../GsrInfo.hpp"
#include "../Config.hpp"

namespace gsr {
    class PageStack;
    class GsrPage;
    class ScrollablePage;
    class Button;

    class ScreenshotSettingsPage : public StaticPage {
    public:
        ScreenshotSettingsPage(const GsrInfo *gsr_info, Config &config, PageStack *page_stack, bool supports_window_title, bool propery_supports_clipboard_image);
        ScreenshotSettingsPage(const ScreenshotSettingsPage&) = delete;
        ScreenshotSettingsPage& operator=(const ScreenshotSettingsPage&) = delete;

        void load();
        void save();
        void on_navigate_away_from_page() override;

        std::function<void()> on_config_changed;
    private:
        std::unique_ptr<ComboBox> create_record_area_box();
        std::unique_ptr<Widget> create_record_area();
        std::unique_ptr<Entry> create_image_width_entry();
        std::unique_ptr<Entry> create_image_height_entry();
        std::unique_ptr<List> create_image_resolution();
        std::unique_ptr<List> create_image_resolution_section();
        std::unique_ptr<CheckBox> create_restore_portal_session_checkbox();
        std::unique_ptr<List> create_restore_portal_session_section();
        std::unique_ptr<Widget> create_change_image_resolution_section();
        std::unique_ptr<Widget> create_hdr_warning();
        std::unique_ptr<Widget> create_capture_target_section();
        std::unique_ptr<List> create_image_quality_section();
        std::unique_ptr<Widget> create_record_cursor_section();
        std::unique_ptr<Widget> create_image_section();
        std::unique_ptr<List> create_save_directory(const char *label);
        std::unique_ptr<ComboBox> create_image_format_box();
        std::unique_ptr<List> create_image_format_section();
        std::unique_ptr<Widget> create_file_info_section();
        std::unique_ptr<CheckBox> create_save_screenshot_in_game_folder();
        std::unique_ptr<CheckBox> create_save_screenshot_to_clipboard();
        std::unique_ptr<CheckBox> create_save_screenshot_to_disk();
        std::unique_ptr<Widget> create_notifications();
        std::unique_ptr<Widget> create_led_indicator();
        std::unique_ptr<Widget> create_general_section();
        std::unique_ptr<Widget> create_screenshot_indicator_section();
        std::unique_ptr<Widget> create_custom_script_screenshot_section();
        std::unique_ptr<List> create_custom_script_screenshot_entry();
        std::unique_ptr<List> create_custom_script_screenshot();
        std::unique_ptr<Widget> create_settings();
        void add_widgets();

        void save(RecordOptions &record_options);
    private:
        Config &config;
        const GsrInfo *gsr_info = nullptr;
        SupportedCaptureOptions capture_options;

        GsrPage *content_page_ptr = nullptr;
        ScrollablePage *settings_scrollable_page_ptr = nullptr;
        List *image_resolution_list_ptr = nullptr;
        List *restore_portal_session_list_ptr = nullptr;
        List *color_range_list_ptr = nullptr;
        Widget *image_format_ptr = nullptr;
        ComboBox *record_area_box_ptr = nullptr;
        Entry *image_width_entry_ptr = nullptr;
        Entry *image_height_entry_ptr = nullptr;
        CheckBox *record_cursor_checkbox_ptr = nullptr;
        CheckBox *restore_portal_session_checkbox_ptr = nullptr;
        CheckBox *change_image_resolution_checkbox_ptr = nullptr;
        ComboBox *image_quality_box_ptr = nullptr;
        ComboBox *image_format_box_ptr = nullptr;
        Button *save_directory_button_ptr = nullptr;
        CheckBox *save_screenshot_in_game_folder_checkbox_ptr = nullptr;
        CheckBox *save_screenshot_to_clipboard_checkbox_ptr = nullptr;
        CheckBox *save_screenshot_to_disk_checkbox_ptr = nullptr;
        CheckBox *show_notification_checkbox_ptr = nullptr;
        CheckBox *led_indicator_checkbox_ptr = nullptr;
        Entry *create_custom_script_screenshot_entry_ptr = nullptr;

        PageStack *page_stack = nullptr;

        bool supports_window_title = false;
        bool propery_supports_clipboard_image = false;
    };
}
