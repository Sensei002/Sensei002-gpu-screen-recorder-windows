/* LedIndicatorWin32.cpp — Windows no-op implementation of LedIndicator.
 *
 * The upstream LedIndicator drives a keyboard LED through sysfs
 * (/sys/class/leds/...) to show the recording state on Linux. Windows has no
 * equivalent sysfs interface, so the class is a no-op here; the UI code that
 * toggles it (update_led_indicator_after_settings_change) keeps working
 * unchanged. Built instead of LedIndicator.cpp on Windows.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "../include/LedIndicator.hpp"

namespace gsr {
    LedIndicator::LedIndicator() = default;
    LedIndicator::~LedIndicator() = default;

    void LedIndicator::set_led(bool enabled) {
        (void)enabled;
    }

    void LedIndicator::blink() {
    }

    void LedIndicator::update() {
    }
}
