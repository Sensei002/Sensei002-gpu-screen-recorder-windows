#pragma once

#include <sys/types.h>
#include <vector>
#include <mglpp/system/Clock.hpp>

namespace gsr {
    class LedIndicator {
    public:
        LedIndicator();
        LedIndicator(const LedIndicator&) = delete;
        LedIndicator& operator=(const LedIndicator&) = delete;
        ~LedIndicator();

        void set_led(bool enabled);
        void blink();
        void update();
    private:
        bool run_gsr_global_hotkeys_set_leds(bool enabled);
        void update_led(bool new_state);
        void update_led_with_active_status();
        void check_led_status_outdated();
    private:
        pid_t gsr_global_hotkeys_pid = -1;
        bool led_indicator_on = false;
        bool led_enabled = false;
        bool perform_blink = false;
        mgl::Clock blink_timer;

        std::vector<int> led_brightness_files;
        mgl::Clock read_led_brightness_timer;
    };
}