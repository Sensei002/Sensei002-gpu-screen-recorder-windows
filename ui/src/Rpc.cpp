#include "../include/Rpc.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define close _close
#else
#include <unistd.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

namespace gsr {
#ifdef _WIN32
    /* ---- Windows: named pipes ------------------------------------------- */
    /* The server keeps one "listen" instance with a persistent OVERLAPPED
       async connect in flight. When it completes, the instance becomes a
       client pipe and a fresh listen instance is created, so new clients can
       always connect (standard named-pipe server pattern). */

    static const char *pipe_name(const char *name) {
        static char full_name[256];
        snprintf(full_name, sizeof(full_name), "\\\\.\\pipe\\%s", name);
        return full_name;
    }

    static HANDLE create_pipe_instance(void) {
        /* NOTE: FILE_FLAG_FIRST_PIPE_INSTANCE must NOT be passed here — it is
           only valid for the first instance, and CreateNamedPipe FAILS with
           ERROR_ACCESS_DENIED for the second instance created with it. The
           server re-creates the listen instance in accept_listen_client(),
           so the flag would silently break every reconnect after the first
           client (the next client then spins on ERROR_PIPE_BUSY forever). */
        return CreateNamedPipeA(
            pipe_name("gsr-ui"),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            GSR_RPC_MAX_CONNECTIONS,
            GSR_RPC_MAX_MESSAGE_SIZE * 4,
            GSR_RPC_MAX_MESSAGE_SIZE * 4,
            0,
            NULL);
    }

    Rpc::Rpc() {
        num_clients = 0;
    }

    Rpc::~Rpc() {
        /* Order matters: close the listen pipe FIRST. The pending
           ConnectNamedPipe is cancelled by the handle close, and the
           cancellation writes the final status into the OVERLAPPED — so ov
           must still be alive. Freeing ov before closing the pipe writes
           into freed memory and corrupts the heap (0xc0000374). */
        if(listen_fd) {
            CloseHandle((HANDLE)listen_fd);
            listen_fd = 0;
        }
        if(listen_overlapped) {
            OVERLAPPED *ov = (OVERLAPPED*)listen_overlapped;
            if(ov->hEvent)
                CloseHandle(ov->hEvent);
            free(ov);
            listen_overlapped = nullptr;
        }
        for(int i = 0; i < num_clients; ++i) {
            if(client_fds[i])
                CloseHandle((HANDLE)client_fds[i]);
        }
        num_clients = 0;
    }

    /* Starts an async connect on the listen pipe. Returns true if a client is
       already waiting (ERROR_PIPE_CONNECTED). */
    static bool start_listen_connect(HANDLE listen_pipe, OVERLAPPED *ov) {
        memset(ov, 0, sizeof(*ov));
        ov->hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if(ConnectNamedPipe(listen_pipe, ov))
            return true;
        const DWORD err = GetLastError();
        if(err == ERROR_PIPE_CONNECTED)
            return true;
        if(err != ERROR_IO_PENDING) {
            /* Re-arm on unexpected errors (e.g. a client connected and
               disconnected before we could accept). */
            CloseHandle(ov->hEvent);
            ov->hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
            ConnectNamedPipe(listen_pipe, ov);
            return false;
        }
        return false;
    }

    /* Promotes the current listen pipe to a client and arms a fresh listen
       instance. Caller must own the listen_overlapped event. */
    void Rpc::accept_listen_client() {
        OVERLAPPED *ov = (OVERLAPPED*)listen_overlapped;
        if(ov && ov->hEvent) {
            CloseHandle(ov->hEvent);
            ov->hEvent = NULL;
        }
        if(num_clients < GSR_RPC_MAX_CONNECTIONS) {
            client_fds[num_clients] = listen_fd;
            polls_data[num_clients].buffer_size = 0;
            ++num_clients;
        } else {
            /* No room — drop the connection. */
            CloseHandle((HANDLE)listen_fd);
        }
        listen_fd = (intptr_t)create_pipe_instance();
        if(listen_fd && listen_fd != (intptr_t)INVALID_HANDLE_VALUE) {
            /* Re-arm the listen connect on the new instance. */
            listen_immediate_connect = start_listen_connect((HANDLE)listen_fd, ov);
        } else {
            listen_fd = 0;
            listen_immediate_connect = false;
            if(ov && ov->hEvent) {
                CloseHandle(ov->hEvent);
                ov->hEvent = NULL;
            }
        }
    }

    bool Rpc::create(const char *name) {
        (void)name; /* The pipe name is fixed to "gsr-ui" for single-instance semantics. */
        if(listen_fd) {
            fprintf(stderr, "Error: Rpc::create: already created/opened\n");
            return false;
        }

        HANDLE pipe = create_pipe_instance();
        if(pipe == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Error: Rpc::create: failed to create named pipe, error: %lu\n", (unsigned long)GetLastError());
            return false;
        }

        OVERLAPPED *ov = (OVERLAPPED*)calloc(1, sizeof(OVERLAPPED));
        if(!ov) {
            CloseHandle(pipe);
            fprintf(stderr, "Error: Rpc::create: failed to allocate overlapped state\n");
            return false;
        }
        listen_overlapped = ov;
        listen_fd = (intptr_t)pipe;
        listen_immediate_connect = start_listen_connect(pipe, ov);
        return true;
    }

    RpcOpenResult Rpc::open(const char *name) {
        (void)name;
        if(listen_fd) {
            fprintf(stderr, "Error: Rpc::open: already created/opened\n");
            return RpcOpenResult::ERROR;
        }

        const char *full_name = pipe_name("gsr-ui");
        HANDLE file = NULL;
        /* Bounded retry: if every instance is busy for >5s something is stuck
           (e.g. the server accepted a client but never re-armed its listen
           instance) — fail instead of spinning forever. */
        int retries = 0;
        while(true) {
            file = CreateFileA(full_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if(file != INVALID_HANDLE_VALUE)
                break;

            const DWORD err = GetLastError();
            if(err == ERROR_PIPE_BUSY) {
                if(++retries > 50)
                    return RpcOpenResult::ERROR;
                if(!WaitNamedPipeA(full_name, 100))
                    continue;
            } else {
                if(err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_NOT_CONNECTED)
                    fprintf(stderr, "Error: Rpc::open: failed to connect, error: %lu\n", (unsigned long)err);
                return RpcOpenResult::ERROR;
            }
        }

        listen_fd = (intptr_t)file;
        return RpcOpenResult::OK;
    }

    bool Rpc::write(const char *str, size_t size) {
        if(!listen_fd) {
            fprintf(stderr, "Error: Rpc::write: named pipe not created/opened yet\n");
            return false;
        }

        DWORD offset = 0;
        while(offset < (DWORD)size) {
            DWORD bytes_written = 0;
            if(!WriteFile((HANDLE)listen_fd, str + offset, (DWORD)(size - offset), &bytes_written, NULL)) {
                fprintf(stderr, "Error: Rpc::write: failed to write, error: %lu\n", (unsigned long)GetLastError());
                return false;
            }
            if(bytes_written > 0)
                offset += bytes_written;
        }
        return true;
    }

    void Rpc::poll() {
        if(!listen_fd)
            return;

        /* Accept a client that connected before the listen connect was armed. */
        if(listen_immediate_connect) {
            listen_immediate_connect = false;
            accept_listen_client();
        }

        /* Check whether the pending listen connect completed. */
        if(listen_overlapped) {
            OVERLAPPED *ov = (OVERLAPPED*)listen_overlapped;
            DWORD bytes = 0;
            if(GetOverlappedResult((HANDLE)listen_fd, ov, &bytes, FALSE)) {
                /* A client connected. Promote the listen pipe to a client. */
                accept_listen_client();
            }
        }

        /* Read data from each connected client. */
        for(int i = 0; i < num_clients; ++i) {
            HANDLE client = (HANDLE)client_fds[i];
            DWORD avail = 0;
            if(!PeekNamedPipe(client, NULL, 0, NULL, &avail, NULL)) {
                /* Client disconnected (ERROR_BROKEN_PIPE) or error. */
                CloseHandle(client);
                client_fds[i] = client_fds[num_clients - 1];
                polls_data[i] = polls_data[num_clients - 1];
                --num_clients;
                --i;
                continue;
            }

            if(avail > 0) {
                char *write_buffer = polls_data[i].buffer + polls_data[i].buffer_size;
                const DWORD space = sizeof(polls_data[i].buffer) - polls_data[i].buffer_size;
                DWORD num_bytes_read = 0;
                if(!ReadFile(client, write_buffer, space, &num_bytes_read, NULL) || num_bytes_read <= 0) {
                    CloseHandle(client);
                    client_fds[i] = client_fds[num_clients - 1];
                    polls_data[i] = polls_data[num_clients - 1];
                    --num_clients;
                    --i;
                    continue;
                }
                polls_data[i].buffer_size += (int)num_bytes_read;
                handle_client_data(i, polls_data[i]);
            }
        }
    }

    void Rpc::handle_client_data(int client_index, PollData &poll_data) {
        const char *newline_p = (const char*)memchr(poll_data.buffer, '\n', poll_data.buffer_size);
        if(!newline_p)
            return;

        const size_t command_size = newline_p - poll_data.buffer;
        std::string name;
        name.assign(poll_data.buffer, command_size);
        memmove(poll_data.buffer, newline_p + 1, poll_data.buffer_size - (command_size + 1));
        poll_data.buffer_size -= (int)(command_size + 1);

        auto it = handlers_by_name.find(name);
        if(it != handlers_by_name.end())
            it->second(name);

        (void)client_index;
    }

    bool Rpc::add_handler(const std::string &name, RpcCallback callback) {
        return handlers_by_name.insert(std::make_pair(name, std::move(callback))).second;
    }

#else
    /* ---- POSIX: unix abstract domain sockets (unchanged) ---------------- */
    static bool build_abstract_address(const char *name, struct sockaddr_un *addr, socklen_t *addrlen_out) {
        char dir[PATH_MAX];
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if(runtime_dir)
            snprintf(dir, sizeof(dir), "%s", runtime_dir);
        else
            snprintf(dir, sizeof(dir), "/run/user/%d", geteuid());

        if(access(dir, F_OK) != 0)
            snprintf(dir, sizeof(dir), "/tmp");

        char path[PATH_MAX];
        const int path_len = snprintf(path, sizeof(path), "%s/%s", dir, name);
        if(path_len <= 0)
            return false;
        if((size_t)path_len + 1 > sizeof(addr->sun_path))
            return false;

        memset(addr, 0, sizeof(*addr));
        addr->sun_family = AF_UNIX;
        addr->sun_path[0] = '\0';
        memcpy(addr->sun_path + 1, path, (size_t)path_len);
        *addrlen_out = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + (size_t)path_len);
        return true;
    }

    Rpc::Rpc() {
        num_clients = 0;
    }

    Rpc::~Rpc() {
        if(listen_fd > 0)
            close((int)listen_fd);
    }

    bool Rpc::create(const char *name) {
        if(listen_fd > 0) {
            fprintf(stderr, "Error: Rpc::create: already created/opened\n");
            return false;
        }

        struct sockaddr_un addr;
        socklen_t addrlen = 0;
        if(!build_abstract_address(name, &addr, &addrlen)) {
            fprintf(stderr, "Error: Rpc::create: name too long\n");
            return false;
        }

        const int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(socket_fd <= 0) {
            fprintf(stderr, "Error: Rpc::create: failed to create socket, error: %s\n", strerror(errno));
            return false;
        }

        if(bind(socket_fd, (struct sockaddr*)&addr, addrlen) == -1) {
            const int err = errno;
            close(socket_fd);
            fprintf(stderr, "Error: Rpc::create: failed to bind, error: %s\n", strerror(err));
            return false;
        }

        if(listen(socket_fd, GSR_RPC_MAX_CONNECTIONS) == -1) {
            const int err = errno;
            close(socket_fd);
            fprintf(stderr, "Error: Rpc::create: failed to listen, error: %s\n", strerror(err));
            return false;
        }

        listen_fd = socket_fd;
        return true;
    }

    RpcOpenResult Rpc::open(const char *name) {
        if(listen_fd > 0) {
            fprintf(stderr, "Error: Rpc::open: already created/opened\n");
            return RpcOpenResult::ERROR;
        }

        struct sockaddr_un addr;
        socklen_t addrlen = 0;
        if(!build_abstract_address(name, &addr, &addrlen)) {
            fprintf(stderr, "Error: Rpc::open: name too long\n");
            return RpcOpenResult::ERROR;
        }

        const int socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(socket_fd <= 0) {
            fprintf(stderr, "Error: Rpc::open: failed to create socket, error: %s\n", strerror(errno));
            return RpcOpenResult::ERROR;
        }

        while(true) {
            if(connect(socket_fd, (struct sockaddr*)&addr, addrlen) == -1) {
                const int err = errno;
                if(err == EWOULDBLOCK) {
                    usleep(10 * 1000);
                } else {
                    close(socket_fd);
                    if(err != ENOENT && err != ECONNREFUSED)
                        fprintf(stderr, "Error: Rpc::open: failed to connect, error: %s\n", strerror(err));
                    return RpcOpenResult::ERROR;
                }
            } else {
                break;
            }
        }

        listen_fd = socket_fd;
        return RpcOpenResult::OK;
    }

    bool Rpc::write(const char *str, size_t size) {
        if(listen_fd <= 0) {
            fprintf(stderr, "Error: Rpc::write: unix domain socket not created/opened yet\n");
            return false;
        }

        ssize_t offset = 0;
        while(offset < (ssize_t)size) {
            const ssize_t bytes_written = ::write((int)listen_fd, str + offset, size - offset);
            if(bytes_written > 0)
                offset += bytes_written;
        }
        return true;
    }

    void Rpc::poll() {
        if(listen_fd <= 0)
            return;

        struct pollfd polls[1 + GSR_RPC_MAX_CONNECTIONS];
        polls[0].fd = (int)listen_fd;
        polls[0].events = POLLIN;
        polls[0].revents = 0;
        int num_polls = 1;
        for(int i = 0; i < num_clients; ++i) {
            polls[num_polls].fd = (int)client_fds[i];
            polls[num_polls].events = POLLIN;
            polls[num_polls].revents = 0;
            ++num_polls;
        }

        while(::poll(polls, num_polls, 0) > 0) {
            for(int i = 0; i < num_polls; ++i) {
                if(polls[i].fd == (int)listen_fd) {
                    if(polls[i].revents & (POLLERR|POLLHUP)) {
                        close((int)listen_fd);
                        listen_fd = 0;
                        return;
                    }

                    const int client_fd = accept4((int)listen_fd, NULL, NULL, SOCK_CLOEXEC);
                    if(client_fd > 0) {
                        if(num_clients < GSR_RPC_MAX_CONNECTIONS) {
                            client_fds[num_clients] = client_fd;
                            polls_data[num_clients].buffer_size = 0;
                            ++num_clients;
                        } else {
                            close(client_fd);
                        }
                    }
                    continue;
                }

                if(polls[i].revents & POLLIN) {
                    int client_index = -1;
                    for(int c = 0; c < num_clients; ++c) {
                        if((int)client_fds[c] == polls[i].fd) {
                            client_index = c;
                            break;
                        }
                    }
                    if(client_index >= 0)
                        handle_client_data(client_index, polls_data[client_index]);
                }

                if(polls[i].revents & (POLLERR|POLLHUP)) {
                    close(polls[i].fd);
                    int client_index = -1;
                    for(int c = 0; c < num_clients; ++c) {
                        if((int)client_fds[c] == polls[i].fd) {
                            client_index = c;
                            break;
                        }
                    }
                    if(client_index >= 0) {
                        client_fds[client_index] = client_fds[num_clients - 1];
                        polls_data[client_index] = polls_data[num_clients - 1];
                        --num_clients;
                    }
                }

                polls[i].revents = 0;
            }
        }
    }

    void Rpc::handle_client_data(int client_index, PollData &poll_data) {
        const int client_fd = (int)client_fds[client_index];
        char *write_buffer = poll_data.buffer + poll_data.buffer_size;
        const ssize_t num_bytes_read = read(client_fd, write_buffer, sizeof(poll_data.buffer) - poll_data.buffer_size);
        if(num_bytes_read <= 0)
            return;

        poll_data.buffer_size += (int)num_bytes_read;
        const char *newline_p = (const char*)memchr(write_buffer, '\n', num_bytes_read);
        if(!newline_p)
            return;

        const size_t command_size = newline_p - poll_data.buffer;
        std::string name;
        name.assign(poll_data.buffer, command_size);
        memmove(poll_data.buffer, newline_p + 1, poll_data.buffer_size - (command_size + 1));
        poll_data.buffer_size -= (int)(command_size + 1);

        auto it = handlers_by_name.find(name);
        if(it != handlers_by_name.end())
            it->second(name);
    }

    bool Rpc::add_handler(const std::string &name, RpcCallback callback) {
        return handlers_by_name.insert(std::make_pair(name, std::move(callback))).second;
    }
#endif
}
