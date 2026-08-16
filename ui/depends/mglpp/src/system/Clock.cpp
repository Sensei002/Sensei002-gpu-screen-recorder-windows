#include "../../include/mglpp/system/Clock.hpp"

namespace mgl {
    Clock::Clock() {
        mgl_clock_init(&clock);
    }

    double Clock::restart() {
        return mgl_clock_restart(&clock);
    }

    double Clock::get_elapsed_time_seconds() {
        return mgl_clock_get_elapsed_time_seconds(&clock);
    }
}