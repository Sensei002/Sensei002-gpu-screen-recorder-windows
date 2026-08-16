#pragma once

#include <sys/types.h>
#include <string>

namespace gsr {
    enum class GsrMode {
        Replay,
        Record,
        Stream,
        Unknown
    };

    // Arguments ending with NULL
    bool exec_program_daemonized(const char **args, bool debug = true);
    // Arguments ending with NULL.
    // This works the same as |exec_program_get_stdout|, except on flatpak where this runs the program on the
    // host machine with flatpak-spawn --host.
    bool exec_program_on_host_daemonized(const char **args, bool debug = true);
    // Arguments ending with NULL. |read_fd| can be NULL
    pid_t exec_program(const char **args, int *read_fd, bool debug = true);
    // Arguments ending with NULL. Returns the exit status of the program or -1 on error
    int exec_program_get_stdout(const char **args, std::string &result, bool debug = true);
    // Arguments ending with NULL. Returns the exit status of the program or -1 on error.
    // This works the same as |exec_program_get_stdout|, except on flatpak where this runs the program on the
    // host machine with flatpak-spawn --host.
    int exec_program_on_host_get_stdout(const char **args, std::string &result, bool debug = true);
    pid_t pidof(const char *process_name, pid_t ignore_pid);
}