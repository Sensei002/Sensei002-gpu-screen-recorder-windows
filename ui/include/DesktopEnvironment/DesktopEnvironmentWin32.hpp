#pragma once

#include "DesktopEnvironment.hpp"
#include <stdint.h>

namespace gsr {
    // Windows equivalent of DesktopEnvironmentX11: reports the focused
    // window title via GetForegroundWindow + GetWindowTextW. Optionally also
    // resolves the process name (used for game-name folders).
    class DesktopEnvironmentWin32 : public DesktopEnvironment {
    public:
        DesktopEnvironmentWin32();
        DesktopEnvironmentWin32(const DesktopEnvironmentWin32&) = delete;
        DesktopEnvironmentWin32& operator=(const DesktopEnvironmentWin32&) = delete;
        ~DesktopEnvironmentWin32() = default;

        bool start() override;
        void update() override;
        std::string get_focused_window_title() override;
        // Returns the process name (e.g. "game.exe") of the focused window,
        // or an empty string if unknown.
        std::string get_focused_window_process_name();
    private:
        void *focused_hwnd = nullptr; /* HWND */
    };
}
