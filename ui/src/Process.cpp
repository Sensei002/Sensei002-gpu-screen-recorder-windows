#include "../include/Process.hpp"

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    static void debug_print_args(const char **args) {
        fprintf(stderr, "gsr-ui info: running command:");
        while(*args) {
            fprintf(stderr, " %s", *args);
            ++args;
        }
        fprintf(stderr, "\n");
    }

    static int count_num_args(const char **args) {
        int num_args = 0;
        while(*args) {
            ++num_args;
            ++args;
        }
        return num_args;
    }

    static std::string build_command_line(const char **args, int num_args) {
        std::string result;
        for(int i = 0; i < num_args; ++i) {
            if(!result.empty())
                result += " ";
            /* Quote args that contain spaces. */
            if(strchr(args[i], ' ') || strchr(args[i], '\t')) {
                result += "\"";
                result += args[i];
                result += "\"";
            } else {
                result += args[i];
            }
        }
        return result;
    }

    /* Creates the child process. If |read_fd| is non-null, a pipe is created
       for the child's stdout and its read end is returned through it.
       Returns the process handle (as pid_t) or -1 on error. */
    static pid_t spawn_program(const char **args, int *read_fd, bool debug) {
        if(args[0] == nullptr)
            return -1;

        if(debug)
            debug_print_args(args);

        const int num_args = count_num_args(args);
        const std::string command_line = build_command_line(args, num_args);

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE stdout_read = NULL;
        HANDLE stdout_write = NULL;
        if(read_fd) {
            if(!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
                fprintf(stderr, "gsr-ui error: failed to create stdout pipe, error: %lu\n", (unsigned long)GetLastError());
                return -1;
            }
            /* The child inherits only the write end; the read end must not
               be inherited (else the child holding it open would keep the
               pipe alive after exit). */
            SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
        }

        STARTUPINFOA si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        if(read_fd) {
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = stdout_write;
            si.hStdError = stdout_write;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        }

        PROCESS_INFORMATION pi;
        memset(&pi, 0, sizeof(pi));

        /* CreateProcessA needs a mutable command line. */
        std::string cmd = command_line;
        if(!CreateProcessA(NULL, &cmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "gsr-ui error: failed to launch program %s, error: %lu\n", args[0], (unsigned long)GetLastError());
            if(read_fd) {
                CloseHandle(stdout_read);
                CloseHandle(stdout_write);
            }
            return -1;
        }

        CloseHandle(pi.hThread);
        if(stdout_write)
            CloseHandle(stdout_write);

        if(read_fd)
            *read_fd = (int)(intptr_t)stdout_read;

        return (pid_t)(intptr_t)pi.hProcess;
    }

    bool exec_program_daemonized(const char **args, bool debug) {
        const pid_t pid = spawn_program(args, NULL, debug);
        if(pid == -1)
            return false;
        /* Detached: close our handle so the child isn't waited on. */
        CloseHandle((HANDLE)(intptr_t)pid);
        return true;
    }

    bool exec_program_on_host_daemonized(const char **args, bool debug) {
        /* No flatpak on Windows — same as daemonized. */
        return exec_program_daemonized(args, debug);
    }

    pid_t exec_program(const char **args, int *read_fd, bool debug) {
        return spawn_program(args, read_fd, debug);
    }

    int exec_program_get_stdout(const char **args, std::string &result, bool debug) {
        int read_fd = -1;
        const pid_t pid = spawn_program(args, &read_fd, debug);
        if(pid == -1)
            return -1;

        HANDLE read_handle = (HANDLE)(intptr_t)read_fd;
        char buffer[8192];
        DWORD bytes_read = 0;
        while(ReadFile(read_handle, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0) {
            result.append(buffer, bytes_read);
        }
        CloseHandle(read_handle);

        DWORD exit_code = 0;
        WaitForSingleObject((HANDLE)(intptr_t)pid, INFINITE);
        GetExitCodeProcess((HANDLE)(intptr_t)pid, &exit_code);
        CloseHandle((HANDLE)(intptr_t)pid);
        return (int)exit_code;
    }

    int exec_program_on_host_get_stdout(const char **args, std::string &result, bool debug) {
        return exec_program_get_stdout(args, result, debug);
    }

    pid_t pidof(const char *process_name, pid_t ignore_pid) {
        (void)ignore_pid;
        /* Match the process name with an .exe suffix (case-insensitive). */
        std::string name_with_exe = process_name;
        if(name_with_exe.size() < 4 || name_with_exe.compare(name_with_exe.size() - 4, 4, ".exe") != 0)
            name_with_exe += ".exe";

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if(snapshot == INVALID_HANDLE_VALUE)
            return -1;

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        pid_t result = -1;
        if(Process32FirstW(snapshot, &entry)) {
            do {
                /* Compare case-insensitively. */
                char exe_name[512];
                const int utf8_size = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, exe_name, sizeof(exe_name), NULL, NULL);
                if(utf8_size <= 0)
                    continue;
                if(_stricmp(exe_name, name_with_exe.c_str()) == 0) {
                    result = (pid_t)entry.th32ProcessID;
                    break;
                }
            } while(Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return result;
    }
}
#else
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <limits.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>

#define PIPE_READ 0
#define PIPE_WRITE 1

namespace gsr {
    static void debug_print_args(const char **args) {
        fprintf(stderr, "gsr-ui info: running command:");
        while(*args) {
            fprintf(stderr, " %s", *args);
            ++args;
        }
        fprintf(stderr, "\n");
    }

    static bool is_number(const char *str) {
        for(int i = 0; str[i]; ++i) {
            char c = str[i];
            if(c < '0' || c > '9')
                return false;
        }
        return true;
    }

    static int count_num_args(const char **args) {
        int num_args = 0;
        while(*args) {
            ++num_args;
            ++args;
        }
        return num_args;
    }

    bool exec_program_daemonized(const char **args, bool debug) {
        /* 1 argument */
        if(args[0] == nullptr)
            return false;

        if(debug)
            debug_print_args(args);

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            return false;
        } else if(pid == 0) { /* child */
            setsid();
            signal(SIGHUP, SIG_IGN);

            // Daemonize child to make the parent the init process which will reap the zombie child
            const pid_t second_child = vfork();
            if(second_child == -1)
                _exit(1);
            else if(second_child > 0)
                _exit(0);

            execvp(args[0], (char* const*)args);
            perror("execvp failed");
            _exit(127);
        } else { /* parent */
            int status;
            if(waitpid(pid, &status, 0) == -1)
                perror("waitpid failed");
        }

        return true;
    }

    bool exec_program_on_host_daemonized(const char **args, bool debug) {
        /* On flatpak this runs the program on the host machine with
           flatpak-spawn --host. On Windows this is the same as the regular
           daemonized call (no flatpak). */
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        if(inside_flatpak) {
            const char *flatpak_args[128];
            int num_flatpak_args = 0;
            flatpak_args[num_flatpak_args++] = "flatpak-spawn";
            flatpak_args[num_flatpak_args++] = "--host";
            for(int i = 0; args[i]; ++i) {
                if(num_flatpak_args < 127)
                    flatpak_args[num_flatpak_args++] = args[i];
            }
            flatpak_args[num_flatpak_args] = NULL;
            return exec_program_daemonized(flatpak_args, debug);
        } else {
            return exec_program_daemonized(args, debug);
        }
    }

    pid_t exec_program(const char **args, int *read_fd, bool debug) {
        if(args[0] == nullptr)
            return -1;

        if(debug)
            debug_print_args(args);

        int fds[2];
        if(pipe(fds) == -1) {
            perror("Failed to create pipe");
            return -1;
        }

        const pid_t pid = vfork();
        if(pid == -1) {
            perror("Failed to vfork");
            close(fds[PIPE_READ]);
            close(fds[PIPE_WRITE]);
            return -1;
        } else if(pid == 0) { /* child */
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            dup2(fds[PIPE_WRITE], STDOUT_FILENO);
            dup2(fds[PIPE_WRITE], STDERR_FILENO);
            close(fds[PIPE_READ]);
            close(fds[PIPE_WRITE]);
            execvp(args[0], (char* const*)args);
            perror("execvp failed");
            _exit(127);
        } else { /* parent */
            close(fds[PIPE_WRITE]);

            if(fcntl(fds[PIPE_READ], F_GETFD) == -1) {
                perror("Failed to get pipe flags");
                kill(pid, SIGKILL);
                int status;
                waitpid(pid, &status, 0);
                close(fds[PIPE_READ]);
                return -1;
            }

            *read_fd = fds[PIPE_READ];
            return pid;
        }
    }

    int exec_program_get_stdout(const char **args, std::string &result, bool debug) {
        int read_fd = -1;
        const pid_t process_id = exec_program(args, &read_fd, debug);
        if(process_id == -1)
            return -1;

        char buffer[8192];
        ssize_t bytes_read = 0;
        while((bytes_read = read(read_fd, buffer, sizeof(buffer))) > 0)
            result.append(buffer, bytes_read);

        if(bytes_read == -1)
            fprintf(stderr, "Failed to read from pipe to program %s, error: %s\n", args[0], strerror(errno));

        close(read_fd);

        int status;
        if(waitpid(process_id, &status, 0) == -1) {
            perror("waitpid failed");
            kill(process_id, SIGKILL);
            return -1;
        }

        if(!WIFEXITED(status))
            return -1;

        return WEXITSTATUS(status);
    }

    int exec_program_on_host_get_stdout(const char **args, std::string &result, bool debug) {
        /* On flatpak this runs the program on the host machine with
           flatpak-spawn --host. On Windows this is the same as the regular
           get_stdout call (no flatpak). */
        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        if(inside_flatpak) {
            const char *flatpak_args[128];
            int num_flatpak_args = 0;
            flatpak_args[num_flatpak_args++] = "flatpak-spawn";
            flatpak_args[num_flatpak_args++] = "--host";
            for(int i = 0; args[i]; ++i) {
                if(num_flatpak_args < 127)
                    flatpak_args[num_flatpak_args++] = args[i];
            }
            flatpak_args[num_flatpak_args] = NULL;
            return exec_program_get_stdout(flatpak_args, result, debug);
        } else {
            return exec_program_get_stdout(args, result, debug);
        }
    }

    pid_t pidof(const char *process_name, pid_t ignore_pid) {
        if(!process_name)
            return -1;

        DIR *dir = opendir("/proc");
        if(!dir)
            return -1;

        pid_t result = -1;
        struct dirent *entry;
        while((entry = readdir(dir)) != nullptr) {
            if(!is_number(entry->d_name))
                continue;

            const pid_t pid = (pid_t)atoi(entry->d_name);
            if(pid == ignore_pid)
                continue;

            char cmdline_path[PATH_MAX];
            snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%s/cmdline", entry->d_name);
            FILE *file = fopen(cmdline_path, "r");
            if(!file)
                continue;

            char buffer[1024];
            const size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, file);
            fclose(file);
            if(bytes_read == 0)
                continue;

            buffer[bytes_read] = '\0';
            if(strstr(buffer, process_name)) {
                result = pid;
                break;
            }
        }
        closedir(dir);
        return result;
    }
}
#endif
