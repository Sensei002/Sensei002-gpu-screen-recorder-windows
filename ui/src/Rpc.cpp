#include "../include/Rpc.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace gsr {
    static bool build_abstract_address(const char *name, struct sockaddr_un *addr, socklen_t *addrlen_out) {
        char dir[PATH_MAX];
        const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
        if(runtime_dir)
            snprintf(dir, sizeof(dir), "%s", runtime_dir);
        else
            snprintf(dir, sizeof(dir), "/run/user/%d", geteuid());

        if(access(dir, F_OK) != 0)
            snprintf(dir, sizeof(dir), "/tmp");

        /* Stay human-readable so the name shows up sensibly in
           /proc/net/unix and `ss -xa`. Abstract names print with the
           leading NUL rendered as '@'. */
        char path[PATH_MAX];
        const int path_len = snprintf(path, sizeof(path), "%s/%s", dir, name);
        if(path_len <= 0)
            return false;
        /* Need room for the leading NUL byte plus path_len bytes of name. */
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
        num_polls = 0;
    }

    Rpc::~Rpc() {
        if(socket_fd > 0)
            close(socket_fd);
        /* No filesystem path to unlink — abstract sockets are reclaimed by
           the kernel on close/process-exit. */
    }

    bool Rpc::create(const char *name) {
        if(socket_fd > 0) {
            fprintf(stderr, "Error: Rpc::create: already created/opened\n");
            return false;
        }

        struct sockaddr_un addr;
        socklen_t addrlen = 0;
        if(!build_abstract_address(name, &addr, &addrlen)) {
            fprintf(stderr, "Error: Rpc::create: name too long\n");
            return false;
        }

        socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(socket_fd <= 0) {
            fprintf(stderr, "Error: Rpc::create: failed to create socket, error: %s\n", strerror(errno));
            return false;
        }

        if(bind(socket_fd, (struct sockaddr*)&addr, addrlen) == -1) {
            const int err = errno;
            close(socket_fd);
            socket_fd = 0;

            fprintf(stderr, "Error: Rpc::create: failed to bind, error: %s\n", strerror(err));
            return false;
        }

        if(listen(socket_fd, GSR_RPC_MAX_CONNECTIONS) == -1) {
            const int err = errno;
            close(socket_fd);
            socket_fd = 0;

            fprintf(stderr, "Error: Rpc::create: failed to listen, error: %s\n", strerror(err));
            return false;
        }

        polls[0].fd = socket_fd;
        polls[0].events = POLLIN;
        polls[0].revents = 0;
        ++num_polls;

        return true;
    }

    RpcOpenResult Rpc::open(const char *name) {
        if(socket_fd > 0) {
            fprintf(stderr, "Error: Rpc::open: already created/opened\n");
            return RpcOpenResult::ERROR;
        }

        struct sockaddr_un addr;
        socklen_t addrlen = 0;
        if(!build_abstract_address(name, &addr, &addrlen)) {
            fprintf(stderr, "Error: Rpc::open: name too long\n");
            return RpcOpenResult::ERROR;
        }

        socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
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
                    socket_fd = 0;
                    if(err != ENOENT && err != ECONNREFUSED)
                        fprintf(stderr, "Error: Rpc::open: failed to connect, error: %s\n", strerror(err));
                    return RpcOpenResult::ERROR;
                }
            } else {
                break;
            }
        }

        return RpcOpenResult::OK;
    }

    bool Rpc::write(const char *str, size_t size) {
        if(socket_fd <= 0) {
            fprintf(stderr, "Error: Rpc::write: unix domain socket not created/opened yet\n");
            return false;
        }

        ssize_t offset = 0;
        while(offset < (ssize_t)size) {
            const ssize_t bytes_written = ::write(socket_fd, str + offset, size - offset);
            if(bytes_written > 0)
                offset += bytes_written;
        }
        return true;
    }

    void Rpc::poll() {
        if(socket_fd <= 0) {
            //fprintf(stderr, "Error: Rpc::poll: unix domain socket not created/opened yet\n");
            return;
        }

        std::string name;
        while(::poll(polls, num_polls, 0) > 0) {
            for(int i = 0; i < num_polls; ++i) {
                if(polls[i].fd == socket_fd) {
                    if(polls[i].revents & (POLLERR|POLLHUP)) {
                        close(socket_fd);
                        socket_fd = 0;
                        return;
                    }

                    const int client_fd = accept4(socket_fd, NULL, NULL, SOCK_CLOEXEC);
                    if(num_polls >= GSR_RPC_MAX_POLLS) {
                        if(errno != EWOULDBLOCK)
                            fprintf(stderr, "Error: Rpc::poll: unable to accept more clients, error: %s\n", strerror(errno));
                    } else {
                        polls[num_polls].fd = client_fd;
                        polls[num_polls].events = POLLIN;
                        polls[num_polls].revents = 0;
                        ++num_polls;
                    }
                    continue;
                }

                if(polls[i].revents & POLLIN)
                    handle_client_data(polls[i].fd, polls_data[i]);

                if(polls[i].revents & (POLLERR|POLLHUP)) {
                    close(polls[i].fd);
                    polls[i] = polls[num_polls - 1];

                    memcpy(polls_data[i].buffer, polls_data[num_polls - 1].buffer, polls_data[num_polls - 1].buffer_size);
                    polls_data[i].buffer_size = polls_data[num_polls - 1].buffer_size;

                    --num_polls;
                    --i;
                }

                polls[i].revents = 0;
            }
        }
    }

    void Rpc::handle_client_data(int client_fd, PollData &poll_data) {
        char *write_buffer = poll_data.buffer + poll_data.buffer_size;
        const ssize_t num_bytes_read = read(client_fd, write_buffer, sizeof(poll_data.buffer) - poll_data.buffer_size);
        if(num_bytes_read <= 0)
            return;

        poll_data.buffer_size += num_bytes_read;
        const char *newline_p = (const char*)memchr(write_buffer, '\n', num_bytes_read);
        if(!newline_p)
            return;

        const size_t command_size = newline_p - poll_data.buffer;
        std::string name;
        name.assign(poll_data.buffer, command_size);
        memmove(poll_data.buffer, newline_p + 1, poll_data.buffer_size - (command_size + 1));
        poll_data.buffer_size -= (command_size + 1);

        auto it = handlers_by_name.find(name);
        if(it != handlers_by_name.end())
            it->second(name);
    }

    bool Rpc::add_handler(const std::string &name, RpcCallback callback) {
        return handlers_by_name.insert(std::make_pair(name, std::move(callback))).second;
    }
}
