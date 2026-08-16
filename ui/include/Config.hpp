
#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <optional>

#define GSR_CONFIG_FILE_VERSION 2

namespace gsr {
    struct SupportedCaptureOptions;

    enum class ReplayStartupMode {
        DONT_TURN_ON_AUTOMATICALLY,
        TURN_ON_AT_SYSTEM_STARTUP,
        TURN_ON_AT_GAME_LAUNCH
    };

    ReplayStartupMode replay_startup_string_to_type(const char *startup_mode_str);

    struct ConfigHotkey {
        int64_t key = 0;        // Mgl key
        uint32_t modifiers = 0; // HotkeyModifier

        bool operator==(const ConfigHotkey &other) const;
        bool operator!=(const ConfigHotkey &other) const;

        std::string to_string(bool spaces = true, bool modifier_side = true) const;
    };

    struct AudioTrack {
        std::string name;
        std::vector<std::string> audio_inputs; // ids
        bool application_audio_invert = false;

        bool operator==(const AudioTrack &other) const;
        bool operator!=(const AudioTrack &other) const;
    };

    struct RecordOptions {
        std::string record_area_option = "screen";
        int32_t record_area_width = 0;
        int32_t record_area_height = 0;
        int32_t video_width = 0;
        int32_t video_height = 0;
        int32_t fps = 60;
        int32_t video_bitrate = 8000;
        bool change_video_resolution = false;
        std::vector<AudioTrack> audio_tracks_list;
        std::string color_range = "limited";
        std::string video_quality = "very_high";
        std::string video_codec = "auto";
        std::string audio_codec = "opus";
        std::string framerate_mode = "auto";
        bool advanced_view = false;
        bool overclock = false;
        bool record_cursor = true;
        bool restore_portal_session = true;
        bool low_power_mode = false;
        bool enable_vulkan_video_encoding = false;

        std::string webcam_source;
        bool webcam_flip_horizontally = false;
        std::string webcam_video_format = "auto";
        int32_t webcam_camera_width = 0;
        int32_t webcam_camera_height = 0;
        int32_t webcam_camera_fps = 0;
        int32_t webcam_x = 0; // A value between 0 and 100 (percentage)
        int32_t webcam_y = 0; // A value between 0 and 100 (percentage)
        int32_t webcam_width = 30; // A value between 0 and 100 (percentage), 0 = Don't scale it
        int32_t webcam_height = 30; // A value between 0 and 100 (percentage), 0 = Don't scale it

        bool show_notifications = true;
        bool use_led_indicator = false;
    };

    struct MainConfig {
        int32_t config_file_version = GSR_CONFIG_FILE_VERSION;
        bool software_encoding_warning_shown = false;
        bool wayland_warning_shown = false;
        bool exclude_metadata = false;
        std::string hotkeys_enable_option = "enable_hotkeys";
        std::string joystick_hotkeys_enable_option = "disable_hotkeys";
        std::string tint_color;
        std::string notification_speed = "normal";
        std::string language;
        ConfigHotkey show_hide_hotkey;
    };

    struct YoutubeStreamConfig {
        std::string stream_key;
    };

    struct TwitchStreamConfig {
        std::string stream_key;
    };

    struct RumbleStreamConfig {
        std::string stream_key;
    };

    struct KickStreamConfig {
        std::string stream_url;
        std::string stream_key;
    };

    struct CustomStreamConfig {
        std::string url;
        std::string key;
        std::string container = "flv";
    };

    struct StreamingConfig {
        RecordOptions record_options;
        std::string streaming_service = "twitch";
        YoutubeStreamConfig youtube;
        TwitchStreamConfig twitch;
        RumbleStreamConfig rumble;
        KickStreamConfig kick;
        CustomStreamConfig custom;
        ConfigHotkey start_stop_hotkey;
    };

    struct RecordConfig {
        RecordOptions record_options;
        bool save_video_in_game_folder = false;
        std::string save_directory;
        std::string container = "mp4";
        ConfigHotkey start_stop_hotkey;
        ConfigHotkey pause_unpause_hotkey;
        ConfigHotkey start_stop_region_hotkey;
        ConfigHotkey start_stop_window_hotkey;
    };

    struct ReplayConfig {
        RecordOptions record_options;
        std::string turn_on_replay_automatically_mode = "dont_turn_on_automatically";
        bool save_video_in_game_folder = false;
        bool restart_replay_on_save = false;
        bool only_start_replay_if_power_supply_connected = false;
        std::string save_directory;
        std::string container = "mp4";
        int32_t replay_time = 60;
        std::string replay_storage = "ram";
        ConfigHotkey start_stop_hotkey;
        ConfigHotkey save_hotkey;
        ConfigHotkey save_1_min_hotkey;
        ConfigHotkey save_10_min_hotkey;
    };

    struct ScreenshotConfig {
        std::string record_area_option = "screen";
        int32_t image_width = 0;
        int32_t image_height = 0;
        bool change_image_resolution = false;
        std::string image_quality = "very_high";
        std::string image_format = "jpg";
        bool record_cursor = true;
        bool restore_portal_session = true;

        bool save_screenshot_in_game_folder = false;
        bool save_screenshot_to_clipboard = false;
        bool save_screenshot_to_disk = true;
        bool show_notifications = true;
        bool use_led_indicator = false;
        std::string save_directory;
        ConfigHotkey take_screenshot_hotkey;
        ConfigHotkey take_screenshot_region_hotkey;
        ConfigHotkey take_screenshot_window_hotkey; // Or desktop portal, on wayland

        std::string custom_script;
    };

    struct Config {
        Config(const SupportedCaptureOptions &capture_options);
        bool operator==(const Config &other);
        bool operator!=(const Config &other);

        void set_hotkeys_to_default();

        MainConfig main_config;
        StreamingConfig streaming_config;
        RecordConfig record_config;
        ReplayConfig replay_config;
        ScreenshotConfig screenshot_config;
    };

    std::optional<Config> read_config(const SupportedCaptureOptions &capture_options);
    void save_config(Config &config);
}
