#pragma once

#include "StaticPage.hpp"
#include "List.hpp"
#include "ComboBox.hpp"
#include "Entry.hpp"
#include "RadioButton.hpp"
#include "CheckBox.hpp"
#include "Button.hpp"
#include "../GsrInfo.hpp"
#include "../Config.hpp"

#include <functional>

namespace gsr {
    class GsrPage;
    class PageStack;
    class ScrollablePage;
    class Label;
    class LineSeparator;
    class Subsection;

    enum class AudioDeviceType {
        OUTPUT,
        INPUT
    };

    enum class WebcamBoxResizeCorner {
        NONE,
        //TOP_LEFT,
        //TOP_RIGHT,
        //BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    class SettingsPage : public StaticPage {
    public:
        enum class Type {
            REPLAY,
            RECORD,
            STREAM
        };

        SettingsPage(Type type, const GsrInfo *gsr_info, Config &config, PageStack *page_stack, bool supports_window_title);
        SettingsPage(const SettingsPage&) = delete;
        SettingsPage& operator=(const SettingsPage&) = delete;

        void load();
        void save();
        void on_navigate_away_from_page() override;

        std::function<void()> on_config_changed;
    private:
        std::unique_ptr<RadioButton> create_view_radio_button();
        std::unique_ptr<ComboBox> create_record_area_box();
        std::unique_ptr<Widget> create_record_area();
        std::unique_ptr<Entry> create_area_width_entry();
        std::unique_ptr<Entry> create_area_height_entry();
        std::unique_ptr<List> create_area_size();
        std::unique_ptr<List> create_area_size_section();
        std::unique_ptr<Entry> create_video_width_entry();
        std::unique_ptr<Entry> create_video_height_entry();
        std::unique_ptr<List> create_video_resolution();
        std::unique_ptr<List> create_video_resolution_section();
        std::unique_ptr<CheckBox> create_restore_portal_session_checkbox();
        std::unique_ptr<List> create_restore_portal_session_section();
        std::unique_ptr<Widget> create_change_video_resolution_section();
        std::unique_ptr<Widget> create_hdr_warning();
        std::unique_ptr<Widget> create_capture_target_section();
        std::unique_ptr<List> create_webcam_sources();
        std::unique_ptr<List> create_webcam_video_setups();
        std::unique_ptr<List> create_webcam_video_format();
        std::unique_ptr<List> create_webcam_video_setup_list();
        std::unique_ptr<Widget> create_webcam_location_widget();
        std::unique_ptr<CheckBox> create_flip_camera_checkbox();
        std::unique_ptr<List> create_webcam_body();
        std::unique_ptr<Widget> create_webcam_section();
        std::unique_ptr<ComboBox> create_audio_device_selection_combobox(AudioDeviceType device_type);
        std::unique_ptr<Button> create_remove_audio_device_button(List *audio_input_list_ptr, List *audio_device_list_ptr);
        std::unique_ptr<List> create_audio_device(AudioDeviceType device_type, List *audio_input_list_ptr);
        std::unique_ptr<Button> create_add_audio_track_button();
        void update_application_audio_warning_visibility();
        std::unique_ptr<Button> create_add_audio_output_device_button(List *audio_input_list_ptr);
        std::unique_ptr<Button> create_add_audio_input_device_button(List *audio_input_list_ptr);
        std::unique_ptr<ComboBox> create_application_audio_selection_combobox(List *application_audio_row);
        std::unique_ptr<List> create_application_audio(List *audio_input_list_ptr);
        std::unique_ptr<List> create_custom_application_audio(List *audio_input_list_ptr);
        std::unique_ptr<Button> create_add_application_audio_button(List *audio_input_list_ptr);
        std::unique_ptr<List> create_add_audio_buttons(List *audio_input_list_ptr);
        std::unique_ptr<List> create_audio_input_section();
        std::unique_ptr<CheckBox> create_application_audio_invert_checkbox();
        std::unique_ptr<Widget> create_application_audio_warning();
        std::unique_ptr<List> create_audio_track_title_and_remove(Subsection *audio_track_subsection, const char *title);
        std::unique_ptr<Subsection> create_audio_track_section(Widget *parent_widget);
        std::unique_ptr<List> create_audio_track_custom_name_input();
        std::unique_ptr<List> create_audio_track_section_list();
        std::unique_ptr<Widget> create_audio_section();
        std::unique_ptr<List> create_video_quality_box();
        std::unique_ptr<List> create_video_bitrate_entry();
        std::unique_ptr<List> create_video_bitrate();
        std::unique_ptr<ComboBox> create_color_range_box();
        std::unique_ptr<List> create_color_range();
        std::unique_ptr<List> create_video_quality_section();
        std::unique_ptr<ComboBox> create_video_codec_box();
        std::unique_ptr<List> create_video_codec();
        std::unique_ptr<ComboBox> create_audio_codec_box();
        std::unique_ptr<List> create_audio_codec();
        std::unique_ptr<Entry> create_framerate_entry();
        std::unique_ptr<List> create_framerate();
        std::unique_ptr<ComboBox> create_framerate_mode_box();
        std::unique_ptr<List> create_framerate_mode();
        std::unique_ptr<List> create_framerate_section();
        std::unique_ptr<Widget> create_record_cursor_section();
        std::unique_ptr<Widget> create_enable_vulkan_video_encoding_section();
        std::unique_ptr<Widget> create_video_section();
        std::unique_ptr<Widget> create_settings();
        void add_widgets();

        void add_page_specific_widgets();

        std::unique_ptr<List> create_save_directory(const char *label);
        std::unique_ptr<ComboBox> create_container_box();
        std::unique_ptr<List> create_container_section();
        std::unique_ptr<List> create_replay_time_entry();
        std::unique_ptr<List> create_replay_time();
        std::unique_ptr<List> create_replay_storage();
        std::unique_ptr<RadioButton> create_start_replay_automatically();
        std::unique_ptr<Widget> create_start_replay_automatically_section();
        std::unique_ptr<CheckBox> create_save_replay_in_game_folder();
        std::unique_ptr<CheckBox> create_restart_replay_on_save();
        std::unique_ptr<Label> create_estimated_replay_file_size();
        void update_estimated_replay_file_size(std::string_view replay_storage_type);
        void update_replay_time_text();
        std::unique_ptr<CheckBox> create_save_recording_in_game_folder();
        std::unique_ptr<Label> create_estimated_record_file_size();
        void update_estimated_record_file_size();
        std::unique_ptr<CheckBox> create_led_indicator();
        std::unique_ptr<CheckBox> create_notifications();
        std::unique_ptr<List> create_indicator();
        std::unique_ptr<Widget> create_low_power_mode();
        void add_replay_widgets();
        void add_record_widgets();

        std::unique_ptr<ComboBox> create_streaming_service_box();
        std::unique_ptr<List> create_streaming_service_section();
        std::unique_ptr<List> create_stream_key_section();
        std::unique_ptr<List> create_stream_kick_url();
        std::unique_ptr<List> create_stream_kick_key();
        std::unique_ptr<List> create_stream_kick_section();
        std::unique_ptr<List> create_stream_custom_url();
        std::unique_ptr<List> create_stream_custom_key();
        std::unique_ptr<List> create_stream_custom_section();
        std::unique_ptr<ComboBox> create_stream_container_box();
        std::unique_ptr<List> create_stream_container();
        void add_stream_widgets();

        void load_audio_tracks(const RecordOptions &record_options);
        void load_common(RecordOptions &record_options);
        void load_replay();
        void load_record();
        void load_stream();

        void save_common(RecordOptions &record_options);
        void save_replay();
        void save_record();
        void save_stream();

        void view_changed(bool advanced_view);

        RecordOptions& get_current_record_options();
    private:
        Type type;
        Config &config;
        const GsrInfo *gsr_info = nullptr;
        std::vector<AudioDevice> audio_devices;
        std::vector<std::string> application_audio;
        SupportedCaptureOptions capture_options;

        GsrPage *content_page_ptr = nullptr;
        ScrollablePage *settings_scrollable_page_ptr = nullptr;
        List *settings_list_ptr = nullptr;
        List *area_size_list_ptr = nullptr;
        List *video_resolution_list_ptr = nullptr;
        List *restore_portal_session_list_ptr = nullptr;
        List *color_range_list_ptr = nullptr;
        Widget *video_codec_ptr = nullptr;
        Widget *audio_codec_ptr = nullptr;
        List *framerate_mode_list_ptr = nullptr;
        ComboBox *record_area_box_ptr = nullptr;
        Entry *area_width_entry_ptr = nullptr;
        Entry *area_height_entry_ptr = nullptr;
        Entry *video_width_entry_ptr = nullptr;
        Entry *video_height_entry_ptr = nullptr;
        Entry *framerate_entry_ptr = nullptr;
        Entry *video_bitrate_entry_ptr = nullptr;
        List *video_bitrate_list_ptr = nullptr;
        CheckBox *change_video_resolution_checkbox_ptr = nullptr;
        ComboBox *color_range_box_ptr = nullptr;
        ComboBox *video_quality_box_ptr = nullptr;
        ComboBox *video_codec_box_ptr = nullptr;
        ComboBox *audio_codec_box_ptr = nullptr;
        ComboBox *framerate_mode_box_ptr = nullptr;
        RadioButton *view_radio_button_ptr = nullptr;
        CheckBox *record_cursor_checkbox_ptr = nullptr;
        CheckBox *restore_portal_session_checkbox_ptr = nullptr;
        ComboBox *container_box_ptr = nullptr;
        ComboBox *streaming_service_box_ptr = nullptr;
        List *stream_key_list_ptr = nullptr;
        List *custom_stream_list_ptr = nullptr;
        List *kick_stream_list_ptr = nullptr;
        CheckBox *save_replay_in_game_folder_ptr = nullptr;
        CheckBox *restart_replay_on_save = nullptr;
        Label *estimated_file_size_ptr = nullptr;
        CheckBox *save_recording_in_game_folder_ptr = nullptr;
        Button *save_directory_button_ptr = nullptr;
        Entry *twitch_stream_key_entry_ptr = nullptr;
        Entry *youtube_stream_key_entry_ptr = nullptr;
        Entry *rumble_stream_key_entry_ptr = nullptr;
        Entry *kick_stream_url_entry_ptr = nullptr;
        Entry *kick_stream_key_entry_ptr = nullptr;
        Entry *stream_url_entry_ptr = nullptr;
        Entry *stream_key_entry_ptr = nullptr;
        Entry *replay_time_entry_ptr = nullptr;
        RadioButton *replay_storage_button_ptr = nullptr;
        Label *replay_time_label_ptr = nullptr;
        RadioButton *turn_on_replay_automatically_mode_ptr = nullptr;
        Subsection *audio_section_ptr = nullptr;
        List *audio_track_section_list_ptr = nullptr;
        CheckBox *led_indicator_checkbox_ptr = nullptr;
        CheckBox *show_notification_checkbox_ptr = nullptr;
        ComboBox *webcam_sources_box_ptr = nullptr;
        ComboBox *webcam_video_setup_box_ptr = nullptr;
        ComboBox *webcam_video_format_box_ptr = nullptr;
        List *webcam_body_list_ptr = nullptr;
        CheckBox *flip_camera_horizontally_checkbox_ptr = nullptr;
        CheckBox *low_power_mode_checkbox_ptr = nullptr;
        CheckBox *replay_power_supply_checkbox_ptr = nullptr;
        CheckBox *enable_vulkan_checkbox_ptr = nullptr;
        List *vulkan_video_list_ptr = nullptr;

        PageStack *page_stack = nullptr;

        mgl::vec2f webcam_box_pos;
        mgl::vec2f webcam_box_size;

        mgl::vec2f webcam_box_drawn_pos;
        mgl::vec2f webcam_box_drawn_size;
        mgl::vec2f webcam_box_grab_offset;

        mgl::vec2f camera_screen_size;
        mgl::vec2f screen_inner_size;
        bool moving_webcam_box = false;

        WebcamBoxResizeCorner webcam_resize_corner = WebcamBoxResizeCorner::NONE;
        mgl::vec2f webcam_resize_start_pos;
        mgl::vec2f webcam_box_pos_resize_start;
        mgl::vec2f webcam_box_size_resize_start;

        std::optional<GsrCamera> selected_camera;
        std::optional<GsrCameraSetup> selected_camera_setup;

        bool supports_window_title = false;
    };
}