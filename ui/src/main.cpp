#include "../include/GsrInfo.hpp"
#include "../include/Overlay.hpp"
#include "../include/Utils.hpp"
#include "../include/gui/Utils.hpp"
#include "../include/Process.hpp"
#include "../include/Rpc.hpp"
#include "../include/GlobalHotkeys/GlobalHotkeys.hpp"
#include "../include/Theme.hpp"
#include "../include/Translation.hpp"
#include "../include/WaylandHostBridge.hpp"

#ifndef _WIN32
#include <wayland-client.h>
#else
#include <windows.h>
#endif
#include <signal.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <unistd.h>

#include <mglpp/mglpp.hpp>
#include <mglpp/system/Clock.hpp>

// TODO: Make keyboard/controller controllable for steam deck (and other controllers).
// TODO: Add systray by using org.kde.StatusNotifierWatcher/etc dbus directly.
// TODO: Make sure the overlay always stays on top. Test with starting the overlay and then opening youtube in fullscreen.
//       This is done in Overlay::force_window_on_top, but it's not called right now. It cant be used because the overlay will be on top of
//       notifications.

extern "C" {
#include <mgl/mgl.h>
}

static sig_atomic_t running = 1;
static sig_atomic_t killed = 0;
static void sigint_handler(int signal) {
    (void)signal;
    killed = 1;
    running = 0;
}

static void signal_ignore(int) {

}

#ifdef _WIN32
/* Session end/logoff: the UI is a console-subsystem app (int main, no
   -mwindows), so Windows delivers CTRL_LOGOFF/CTRL_SHUTDOWN (and close)
   here instead of WM_ENDSESSION. Exit the main loop so the overlay tears
   down cleanly — same as SIGINT/SIGTERM. The engine process gets its own
   CTRL_LOGOFF via its SetConsoleCtrlHandler and saves the recording. */
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch(ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            /* Same as sigint_handler: killed makes main() return exit code 0
               (on Windows the display server is always "connected", so the
               fall-through exit path would otherwise report a crash). */
            killed = 1;
            running = 0;
            return TRUE;
    }
    return FALSE;
}
#endif

static void disable_prime_run() {
#ifndef _WIN32
    unsetenv("__NV_PRIME_RENDER_OFFLOAD");
    unsetenv("__NV_PRIME_RENDER_OFFLOAD_PROVIDER");
    unsetenv("__GLX_VENDOR_LIBRARY_NAME");
    unsetenv("__VK_LAYER_NV_optimus");
    unsetenv("DRI_PRIME");
#endif
}

static void rpc_add_commands(gsr::Rpc *rpc, gsr::Overlay *overlay) {
    rpc->add_handler("show_ui", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->show();
    });

    rpc->add_handler("toggle-show", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_show();
    });

    rpc->add_handler("toggle-record", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_record(gsr::RecordForceType::NONE);
    });

    rpc->add_handler("toggle-pause", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_pause();
    });

    rpc->add_handler("toggle-record-region", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_record(gsr::RecordForceType::REGION);
    });

    rpc->add_handler("toggle-record-window", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_record(gsr::RecordForceType::WINDOW);
    });

    rpc->add_handler("toggle-stream", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_stream();
    });

    rpc->add_handler("toggle-replay", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->toggle_replay();
    });

    rpc->add_handler("replay-save", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay();
    });

    rpc->add_handler("replay-save-1-min", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay_1_min();
    });

    rpc->add_handler("replay-save-10-min", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->save_replay_10_min();
    });

    rpc->add_handler("take-screenshot", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot();
    });

    rpc->add_handler("take-screenshot-region", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot_region();
    });

    rpc->add_handler("take-screenshot-window", [overlay](const std::string &name) {
        fprintf(stderr, "rpc command executed: %s\n", name.c_str());
        overlay->take_screenshot_window();
    });
}


static void set_display_server_environment_variables() {
#ifndef _WIN32
    // Some users dont have properly setup environments (no display manager that does systemctl --user import-environment DISPLAY WAYLAND_DISPLAY)
    const char *display = getenv("DISPLAY");
    if(!display) {
        display = ":0";
        setenv("DISPLAY", display, true);
    }

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if(!wayland_display) {
        wayland_display = "wayland-0";
        setenv("WAYLAND_DISPLAY", wayland_display, true);
    }
#endif
}

static void usage() {
    printf("usage: gsr-ui [action]\n");
    printf("OPTIONS:\n");
    printf("  action  The launch action. Should be either \"launch-show\", \"launch-hide\", \"launch-hide-announce\" or \"launch-daemon\". Optional, defaults to \"launch-hide\".\n");
    printf("          If \"launch-show\" is used then the program starts and the UI is immediately opened and can be shown/hidden with Alt+Z.\n");
    printf("          If \"launch-hide\" is used then the program starts but the UI is not opened until Alt+Z is pressed. The UI will be opened if the program is already running in another process.\n");
    printf("          If \"launch-hide-announce\" is used then the program starts but the UI is not opened until Alt+Z is pressed and a notification tells the user to press Alt+Z. The UI will be opened if the program is already running in another process.\n");
    printf("          If \"launch-daemon\" is used then the program starts but the UI is not opened until Alt+Z is pressed. The UI will not be opened if the program is already running in another process.\n");
    exit(1);
}

enum class LaunchAction {
    LAUNCH_SHOW,
    LAUNCH_HIDE,
    LAUNCH_HIDE_ANNOUNCE,
    LAUNCH_DAEMON,
    INSTALL_STARTUP
};

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C
#ifdef __GLIBC__
    mallopt(M_MMAP_THRESHOLD, 65536);
#endif

    std::string resources_path;
#ifdef _WIN32
    /* Windows: resources (images/, translations/, theme/) live next to the
       gsr-ui.exe binary. */
    {
        char exe_path[MAX_PATH];
        const DWORD exe_path_size = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        if(exe_path_size > 0 && exe_path_size < sizeof(exe_path)) {
            char *last_separator = strrchr(exe_path, '\\');
            if(last_separator) {
                *last_separator = '\0';
                resources_path = exe_path;
                resources_path += "/";
            }
        }
        if(resources_path.empty())
            resources_path = "./";
    }
#else
    if(access("sibs-build/linux_x86_64/debug/gsr-ui", F_OK) == 0) {
        resources_path = "./";
    } else {
#ifdef GSR_UI_RESOURCES_PATH
        resources_path = GSR_UI_RESOURCES_PATH "/";
#else
        resources_path = "/usr/share/gsr-ui/";
#endif
    }
#endif

    std::optional<gsr::Config> config = read_config(gsr::SupportedCaptureOptions{});
    gsr::Translation::instance().init((resources_path + "translations/").c_str(), config.has_value() ? config.value().main_config.language.c_str() : nullptr);

#ifndef _WIN32
    if(geteuid() == 0) {
        fprintf(stderr, "Error: don't run gsr-ui as the root user\n");
        return 1;
    }
#endif

    LaunchAction launch_action = LaunchAction::LAUNCH_HIDE;
    if(argc == 1) {
        launch_action = LaunchAction::LAUNCH_HIDE;
    } else if(argc == 2) {
        const char *launch_action_opt = argv[1];
        if(strcmp(launch_action_opt, "launch-show") == 0) {
            launch_action = LaunchAction::LAUNCH_SHOW;
        } else if(strcmp(launch_action_opt, "launch-hide") == 0) {
            launch_action = LaunchAction::LAUNCH_HIDE;
        } else if(strcmp(launch_action_opt, "launch-hide-announce") == 0) {
            launch_action = LaunchAction::LAUNCH_HIDE_ANNOUNCE;
        } else if(strcmp(launch_action_opt, "launch-daemon") == 0) {
            launch_action = LaunchAction::LAUNCH_DAEMON;
        } else if(strcmp(launch_action_opt, "install-startup") == 0) {
            launch_action = LaunchAction::INSTALL_STARTUP;
        } else {
            printf("error: invalid action \"%s\", expected \"launch-show\", \"launch-hide\", \"launch-hide-announce\" or \"launch-daemon\".\n", launch_action_opt);
            usage();
        }
    } else {
        usage();
    }

    if(launch_action == LaunchAction::INSTALL_STARTUP)
        return gsr::set_xdg_autostart(true);

    set_display_server_environment_variables();

    const std::string gsr_icon_path = resources_path + "images/gpu_screen_recorder_logo.png";
    std::string show_hide_hotkey_str = "Alt+Z";
    if(config.has_value())
        show_hide_hotkey_str = config.value().main_config.show_hide_hotkey.to_string(true, true);

    auto rpc = std::make_unique<gsr::Rpc>();
    const gsr::RpcOpenResult rpc_open_result = rpc->open("gsr-ui");

    if(rpc_open_result == gsr::RpcOpenResult::OK) {
        if(launch_action == LaunchAction::LAUNCH_DAEMON)
            return 1;

        if(rpc->write("show_ui\n", 8)) {
            fprintf(stderr, "Error: another instance of gsr-ui is already running, opening that one instead\n");
        } else {
            fprintf(stderr, "Error: failed to send command to running gsr-ui instance, user will have to open the UI manually with %s\n", show_hide_hotkey_str.c_str());
            const std::string msg = TRF("Another instance of GPU Screen Recorder UI is already running. Press %s to open the UI.", show_hide_hotkey_str.c_str());
            const char *args[] = {
                "gsr-notify", "--text", msg.c_str(), "--timeout", "5.0",
                "--icon-color", "ffffff", "--icon", gsr_icon_path.c_str(), "--bg-color", "ff0000", nullptr
            };
            gsr::exec_program_daemonized(args);
        }
        return 1;
    }

    if(!rpc->create("gsr-ui"))
        fprintf(stderr, "Error: Failed to create rpc\n");

    if(gsr::pidof("gpu-screen-recorder", -1) != -1) {
        const char *args[] = {
            "gsr-notify", "--text", TR("GPU Screen Recorder is already running in another process. Please close it before using GPU Screen Recorder UI."),
            "--timeout", "5.0", "--icon-color", "ffffff", "--icon", gsr_icon_path.c_str(), "--bg-color", "ff0000", nullptr
        };
        gsr::exec_program_daemonized(args);
    }

    // Open the Wayland display ourselves (so we can route through the flatpak
    // host bridge) and lend it to mgl. Owned and disconnected by main(), see
    // the bottom of this function.
    struct wl_display *wayland_dpy = nullptr;
#ifndef _WIN32
    if(getenv("WAYLAND_DISPLAY"))
        wayland_dpy = gsr::wayland_connect_to_host();

    const mgl_window_system mgl_backend = gsr::is_wayland_layer_shell_overlay_session()
        ? MGL_WINDOW_SYSTEM_WAYLAND
        : MGL_WINDOW_SYSTEM_X11;
    const int mgl_init_result = (mgl_backend == MGL_WINDOW_SYSTEM_WAYLAND && wayland_dpy)
        ? mgl_init_with_wayland_display(wayland_dpy)
        : mgl_init(mgl_backend);
    if(mgl_init_result != 0) {
        fprintf(stderr, "Error: failed to initialize mgl. Failed to connect to the %s server or set up opengl\n",
            mgl_backend == MGL_WINDOW_SYSTEM_WAYLAND ? "Wayland" : "X11");
        if(wayland_dpy)
            wl_display_disconnect(wayland_dpy);
        return 1;
    }

    // Stop nvidia driver from buffering frames
    setenv("__GL_MaxFramesAllowed", "1", true);
    // If this is set to 1 then cuGraphicsGLRegisterImage will fail for egl context with error: invalid OpenGL or DirectX context,
    // so we overwrite it
    setenv("__GL_THREADED_OPTIMIZATIONS", "0", true);
    // Some people set this to force all applications to vsync on nvidia, but this makes eglSwapBuffers never return.
    unsetenv("__GL_SYNC_TO_VBLANK");
    // Same as above, but for amd/intel
    unsetenv("vblank_mode");
#else
    const int mgl_init_result = mgl_init(MGL_WINDOW_SYSTEM_WIN32);
    if(mgl_init_result != 0) {
        fprintf(stderr, "Error: failed to initialize mgl (Win32 backend)\n");
        return 1;
    }
#endif

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    signal(SIGUSR1, signal_ignore);
    signal(SIGUSR2, signal_ignore);
    signal(SIGRTMIN, signal_ignore);
    signal(SIGRTMIN+1, signal_ignore);
    signal(SIGRTMIN+2, signal_ignore);
    signal(SIGRTMIN+3, signal_ignore);
    signal(SIGRTMIN+4, signal_ignore);
    signal(SIGRTMIN+5, signal_ignore);
    signal(SIGRTMIN+6, signal_ignore);

#ifdef _WIN32
    /* The UI is a GUI app with a console attached (console subsystem): hide
       the console window so autostart at logon doesn't flash a black box,
       but keep the console attached so CTRL_LOGOFF/CTRL_SHUTDOWN still get
       delivered to console_ctrl_handler above. */
    if(GetConsoleWindow())
        ShowWindow(GetConsoleWindow(), SW_HIDE);
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

    gsr::GsrInfo gsr_info;
    // TODO: Show the error in ui
    gsr::GsrInfoExitStatus gsr_info_exit_status = gsr::get_gpu_screen_recorder_info(&gsr_info);
    if(gsr_info_exit_status != gsr::GsrInfoExitStatus::OK) {
        fprintf(stderr, "Error: failed to get gpu-screen-recorder info, error: %d\n", (int)gsr_info_exit_status);
        exit(1);
    }

    const gsr::DisplayServer display_server = gsr_info.system_info.display_server;
    if(display_server == gsr::DisplayServer::WAYLAND) {
        fprintf(stderr, "Warning: Wayland doesn't support this program properly and XWayland is required. Things may not work as expected. Use X11 if you experience issues.\n");
    } else {
        // Cant get window texture when prime-run is used
        disable_prime_run();
    }

    gsr::SupportedCaptureOptions capture_options = gsr::get_supported_capture_options(gsr_info);

#ifndef _WIN32
    mgl_context *context = mgl_get_context();

    egl_functions egl_funcs;
    egl_funcs.eglGetError = (decltype(egl_funcs.eglGetError))context->gl.eglGetProcAddress("eglGetError");
    egl_funcs.eglCreateImage = (decltype(egl_funcs.eglCreateImage))context->gl.eglGetProcAddress("eglCreateImage");
    egl_funcs.eglDestroyImage = (decltype(egl_funcs.eglDestroyImage))context->gl.eglGetProcAddress("eglDestroyImage");
    egl_funcs.glEGLImageTargetTexture2DOES = (decltype(egl_funcs.glEGLImageTargetTexture2DOES))context->gl.eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if(!egl_funcs.eglGetError || !egl_funcs.eglCreateImage || !egl_funcs.eglDestroyImage || !egl_funcs.glEGLImageTargetTexture2DOES) {
        fprintf(stderr, "Error: required opengl functions not available on your system\n");
        exit(1);
    }
#else
    /* On Windows the X11 window-texture path (the only consumer of egl_funcs)
       is disabled — update_compositor_texture is a no-op — so leave the
       function table empty. */
    egl_functions egl_funcs = {};
#endif

    fprintf(stderr, "Info: gsr ui is now ready, waiting for inputs. Press %s to show/hide the overlay\n", show_hide_hotkey_str.c_str());

    auto overlay = std::make_unique<gsr::Overlay>(resources_path, std::move(gsr_info), std::move(capture_options), egl_funcs, wayland_dpy);
#ifdef _WIN32
    // If the show/hide hotkey (Alt+Z by default) failed to register — almost
    // always because another application owns the same combination (the
    // NVIDIA App / GeForce Experience overlay grabs Alt+Z by default) — a
    // launch-hide/launch-daemon UI would be unreachable. Force-show it so
    // the user lands in the UI and can change the hotkey in Settings (or
    // disable the other application's hotkey).
    if(!overlay->show_hide_hotkey_bound && launch_action != LaunchAction::LAUNCH_SHOW)
        overlay->show();
#endif
    if(launch_action == LaunchAction::LAUNCH_SHOW)
        overlay->show();
    else if(launch_action == LaunchAction::LAUNCH_HIDE_ANNOUNCE) {
        const std::string msg = TRF("Press %s to open the GPU Screen Recorder UI", show_hide_hotkey_str.c_str());
        overlay->show_notification(msg.c_str(), 5.0, mgl::Color(255, 255, 255), gsr::get_color_theme().tint_color, gsr::NotificationType::NOTICE, nullptr, gsr::NotificationLevel::ERROR);
    }

    rpc_add_commands(rpc.get(), overlay.get());

    // Evacuating from lennart's botnet to XDG (a.k.a. 'the spec that nobody actually follows').
    // TODO: Remove all this garbage related to systemd in 1-2 months and remove the systemd service file as well.
    // This garbage shit is needed because the systemd user daemon isn't always available at xdg autostart o algo
    const bool systemd_service_ready = gsr::wait_until_systemd_user_service_available();
    constexpr const char *deprecated_systemd_service_name = "gpu-screen-recorder-ui.service";
    if(systemd_service_ready && gsr::is_systemd_service_enabled(deprecated_systemd_service_name)) {
        const int autostart_result = gsr::set_xdg_autostart(true);
        if(autostart_result == 67) {
            const bool is_flatpak = getenv("FLATPAK_ID") != nullptr;
            const char *startup_command = is_flatpak ? "flatpak run com.dec05eba.gpu_screen_recorder gsr-ui" : "gsr-ui launch-daemon";
            overlay->show_notification(
                TRF("GPU Screen Recorder UI autostart via systemd is deprecated. To migrate: install and configure 'dex' (recommended), or manually add '%s' to your desktop autostart entries.", startup_command).c_str(),
                10.0, mgl::Color(255, 255, 255), mgl::Color(255, 0, 0),
                gsr::NotificationType::NOTICE, nullptr, gsr::NotificationLevel::ERROR);
        } else {
            gsr::disable_systemd_service(deprecated_systemd_service_name);
            overlay->show_notification(
                TR("GPU Screen Recorder UI startup has been switched from systemd service to XDG autostart."),
                8.0, mgl::Color(255, 255, 255), gsr::get_color_theme().tint_color,
                gsr::NotificationType::NOTICE, nullptr, gsr::NotificationLevel::INFO);
        }
    }

    gsr::replace_xdg_autostart_with_current_gsr_type();

    // TODO: Add hotkeys in Overlay when using x11 global hotkeys. The hotkeys in Overlay should duplicate each key that is used for x11 global hotkeys.

    std::string exit_reason;
    mgl::Clock frame_delta_clock;

    while(running && mgl_is_connected_to_display_server() && !overlay->should_exit(exit_reason)) {
        const double frame_delta_seconds = frame_delta_clock.restart();
        gsr::set_frame_delta_seconds(frame_delta_seconds);

        rpc->poll();
        overlay->handle_events();
        if(!overlay->draw()) {
            usleep(100 * 1000); // 100ms
            mgl_ping_display_server();
        }
    }

    const bool connected_to_display_server = mgl_is_connected_to_display_server();

    fprintf(stderr, "Info: shutting down!\n");
    rpc.reset();
    overlay.reset();
    mgl_deinit();
#ifndef _WIN32
    /* Disconnect the borrowed Wayland display only after mgl_deinit and the
       overlay destructor — both may use the connection during teardown. */
    if(wayland_dpy)
        wl_display_disconnect(wayland_dpy);
#endif

    if(exit_reason == "back-to-old-ui") {
        gsr::set_xdg_autostart(false);
        const char *args[] = { "gpu-screen-recorder-gtk", "use-old-ui", nullptr };
        execvp(args[0], (char* const*)args);
        return 0;
    } else if(exit_reason == "exit") {
        return 0;
    }

    if(killed)
        return 0;

    if(connected_to_display_server)
        return 1;

    return 0;
}
