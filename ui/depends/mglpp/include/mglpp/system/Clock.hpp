#ifndef MGLPP_CLOCK_HPP
#define MGLPP_CLOCK_HPP

extern "C" {
#include <mgl/system/clock.h>
}

namespace mgl {
    class Clock {
    public:
        Clock();
        
        /* Returns the elapsed time in seconds since the last restart or init, before resetting the clock */
        double restart();
        double get_elapsed_time_seconds();
    private:
        mgl_clock clock;
    };
}

#endif /* MGLPP_CLOCK_HPP */
