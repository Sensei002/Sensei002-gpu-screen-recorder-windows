#pragma once

#include "gui/PageStack.hpp"
#include "gui/CustomRendererWidget.hpp"
#include "GsrInfo.hpp"
#include "Config.hpp"
#include "window_texture.h"
#include "WindowUtils.hpp"
#include "GlobalHotkeys/GlobalHotkeysJoystick.hpp"
#include "AudioPlayer.hpp"
#include "Clipboard/Clipboard.hpp"
#include "LedIndicator.hpp"
#include "CursorTracker/CursorTracker.hpp"
#include "RegionSelector/RegionSelector.hpp"
#include "DesktopEnvironment/DesktopEnvironment.hpp"

#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/system/Clock.hpp>

#include <array>

struct wl_display;

namespace gsr {
    class DropdownButton;
    class GlobalHotkeys;

    enum class RecordingStatus {
        NONE,
        REPLAY,
        RECORD,
        STREAM
    };

    enum class NotificationType {
        NONE,
        RECORD,
        REPLAY,
        STREAM,
        SCREENSHOT,
        NOTICE
    };

    enum class NotificationLevel {
        INFO,
        ERROR,
    };

    enum class RecordForceType {
        NONE,
        REGION,
        WINDOW
    };

    enum class ScreenshotForceType {
        NONE,
        REGION,
        WINDOW
    };

    enum class NotificationSpeed {
        NORMAL,
        FAST
    };

    enum class GameReplayAction {
        IDLE,
        START,
        STOP
    };

    class Overlay {
    public:
        // |wayland_dpy| is a borrowed Wayland display owned by the caller. It
        // must outlive the Overlay. Pass nullptr on X11 sessions.
        Overlay(std::string resources_path, GsrInfo gsr_info, SupportedCaptureOptions capture_options, egl_functions egl_funcs, struct wl_display *wayland_dpy);
        Overlay(const Overlay&) = delete;
        Overlay& operator=(const Overlay&) = delete;
        ~Overlay();

        void handle_events();
        // Returns false if not visible
        bool draw();

        void show();
        void hide_next_frame();
        void toggle_show();
        void toggle_record(RecordForceType force_type);
        void toggle_pause();
        void toggle_stream();
        void toggle_replay();
        void save_replay();
        void save_replay_1_min();
        void save_replay_10_min();
        void take_screenshot();
        void take_screenshot_region();
        void take_screenshot_window();
        void show_notification(const char *str, double timeout_seconds, mgl::Color icon_color, mgl::Color bg_color, NotificationType notification_type, const char *capture_target = nullptr, NotificationLevel notification_level = NotificationLevel::INFO);
        bool is_open() const;
        bool should_exit(std::string &reason) const;
        void exit();
        void go_back_to_old_ui();
        void cancel_region_selection();

        const Config& get_config() const;

        void unbind_all_keyboard_hotkeys();
        void rebind_all_keyboard_hotkeys();

        void set_notification_speed(NotificationSpeed notification_speed);

        bool global_hotkeys_ungrab_keyboard = false;
    private:
        const char* notification_type_to_string(NotificationType notification_type);
        void update_upause_status();

        void hide();

        void handle_x11_events();
        void handle_wayland_events();
        void on_event(mgl::Event &event);

        void recreate_global_hotkeys(std::string_view hotkey_option);
        void update_led_indicator_after_settings_change();
        void recreate_frontpage_ui_components();
        void open_settings_page(int scroll_y = 0);
        void xi_setup();
        void handle_xi_events();
        void process_key_bindings(mgl::Event &event);
        void grab_mouse_and_keyboard();
        void xi_setup_fake_cursor();

        void close_gsr_game_tracker_output();
        void close_gpu_screen_recorder_output();

        double get_time_passed_in_replay_buffer_seconds();
        void update_notification_process_status();
        void save_video_in_current_game_directory(std::string &video_filepath, NotificationType notification_type);
        void on_replay_saved(const char *replay_saved_filepath);
        void process_gsr_game_tracker_output();
        void process_gsr_output();
        void on_gsr_process_error(int exit_code, NotificationType notification_type);
        void update_gsr_process_status();
        void update_gsr_screenshot_process_status();

        void replay_status_update_status();
        void update_power_supply_status();
        bool replay_program_autostart_capture_options_available();
        void update_replay_program_startup_status();
        void update_gsr_game_tracker_replay_status();

        void on_stop_recording(int exit_code, std::string &video_filepath);

        void update_ui_recording_paused();
        void update_ui_recording_unpaused();

        void update_ui_recording_started();
        void update_ui_recording_stopped();

        void update_ui_streaming_started();
        void update_ui_streaming_stopped();

        void update_ui_replay_started();
        void update_ui_replay_stopped();

        void prepare_gsr_output_for_reading();
        void on_press_save_replay();
        void on_press_save_replay_1_min_replay();
        void on_press_save_replay_10_min_replay();
        bool on_press_start_replay(bool disable_notification, bool finished_selection, bool launched_manually = false);
        void on_press_start_record(bool finished_selection, RecordForceType force_type);
        void on_press_start_stream(bool finished_selection);
        void on_press_take_screenshot(bool finished_selection, ScreenshotForceType force_type);
        bool update_compositor_texture(const Monitor &monitor);

        void add_region_command(std::vector<const char*> &args, char *region_str, int region_str_size);
        void add_common_gpu_screen_recorder_args(std::vector<const char*> &args, const MainConfig &main_config, const RecordOptions &record_options, const std::vector<std::string> &audio_tracks, const std::string &video_bitrate, const char *region, char *region_str, int region_str_size, const std::string &region_area_option, RecordForceType force_type = RecordForceType::NONE);

        std::string get_capture_target(const std::string &capture_target, const SupportedCaptureOptions &capture_options);

        void force_window_on_top();
        void stop_region_selection();
    private:
        using KeyBindingCallback = std::function<void()>;
        struct KeyBinding {
            mgl::Event::KeyEvent key_event;
            KeyBindingCallback callback;
        };

        std::unique_ptr<mgl::Window> window;
        mgl::Event event;
        std::string resources_path;
        GsrInfo gsr_info;
        egl_functions egl_funcs;
        Config config;
        Config current_recording_config;

        std::string gsr_icon_path;

        bool visible = false;

        mgl::Texture window_texture_texture;
        mgl::Sprite window_texture_sprite;
        mgl::Texture screenshot_texture;
        mgl::Sprite screenshot_sprite;
        mgl::Rectangle bg_screenshot_overlay;

        mgl::Texture cursor_texture;
        mgl::Sprite cursor_sprite;
        mgl::vec2i cursor_hotspot;

        WindowTexture window_texture;
        PageStack page_stack;
        mgl::Rectangle top_bar_background;
        mgl::Text top_bar_text;
        mgl::Sprite logo_sprite;
        CustomRendererWidget close_button_widget;
        bool close_button_pressed_inside = false;
        uint64_t default_cursor = 0;

        pid_t gpu_screen_recorder_process = -1;
        pid_t notification_process = -1;
        int gpu_screen_recorder_process_output_fd = -1;
        FILE *gpu_screen_recorder_process_output_file = nullptr;
        pid_t gpu_screen_recorder_screenshot_process = -1;

        DropdownButton *replay_dropdown_button_ptr = nullptr;
        DropdownButton *record_dropdown_button_ptr = nullptr;
        DropdownButton *stream_dropdown_button_ptr = nullptr;

        mgl::Clock force_window_on_top_clock;

        RecordingStatus recording_status = RecordingStatus::NONE;
        bool paused = false;
        mgl::Clock paused_clock;
        double paused_total_time_seconds = 0.0;

        mgl::Clock replay_status_update_clock;
        std::string power_supply_online_filepath;
        bool power_supply_connected = false;
        bool replay_program_startup_power_supply_connected = false;

        std::string record_filepath;
        std::string screenshot_filepath;

        Display *xi_display = nullptr;
        int xi_opcode = 0;
        XEvent *xi_input_xev = nullptr;
        XEvent *xi_output_xev = nullptr;

        std::array<KeyBinding, 1> key_bindings;
        bool drawn_first_frame = false;

        bool do_exit = false;
        std::string exit_reason;

        mgl::vec2i window_size = { 1280, 720 };
        mgl::vec2i window_pos = { 0, 0 };

        mgl::Clock show_overlay_clock;
        double show_overlay_timeout_seconds = 0.0;

        std::unique_ptr<GlobalHotkeys> global_hotkeys = nullptr;
        std::unique_ptr<GlobalHotkeysJoystick> global_hotkeys_js = nullptr;
        Display *x11_dpy = nullptr;
        XEvent x11_xev;
        // True when the overlay window is a native Wayland surface (wlr-layer-shell)
        // rather than an X11 window. Many X11-only operations (XGrabPointer,
        // XChangeProperty on the overlay window, click-through atoms, …) are
        // skipped when this is set.
        bool wayland_native_overlay = false;

        int gsr_game_tracker_process_output_fd = -1;
        FILE *gsr_game_tracker_process_output_file = nullptr;
        pid_t gsr_game_tracker_process_id = -1;
        GameReplayAction game_replay_action = GameReplayAction::IDLE;

        struct wl_display *wayland_dpy = nullptr;

        mgl::Clock replay_save_clock;
        bool replay_save_show_notification = false;
        ReplayStartupMode replay_startup_mode = ReplayStartupMode::TURN_ON_AT_SYSTEM_STARTUP;
        bool replay_launched_manually = false;
        bool replay_launched_once = false;
        bool replay_recording = false;
        int replay_save_duration_min = 0;
        mgl::Clock replay_duration_clock;
        double replay_saved_duration_sec = 0.0;
        bool replay_restart_on_save = false;

        mgl::Clock recording_duration_clock;

        AudioPlayer audio_player;
    
        std::unique_ptr<RegionSelector> region_selector;
        bool start_region_capture = false;
        std::function<void()> on_region_selected;

        bool start_window_capture = false;

        std::string recording_capture_target;
        std::string screenshot_capture_target;

        std::unique_ptr<CursorTracker> cursor_tracker;
        mgl::Clock cursor_tracker_update_clock;

        bool hide_ui = false;
        bool reload_ui = false;
        bool reopen_settings_after_reload = false;
        int pending_settings_scroll_y = 0;
        double notification_duration_multiplier = 1.0;
        std::unique_ptr<Clipboard> clipboard;
        bool wayland_native_clipboard = false;

        std::unique_ptr<LedIndicator> led_indicator = nullptr;

        bool supports_window_title = false;
        std::unique_ptr<DesktopEnvironment> desktop_environment;
    };
}
