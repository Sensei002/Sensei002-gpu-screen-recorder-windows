#pragma once

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <unordered_map>
#include <string>

#define GSR_RPC_MAX_CONNECTIONS 8
#define GSR_RPC_MAX_MESSAGE_SIZE 128

namespace gsr {
    using RpcCallback = std::function<void(const std::string &name)>;

    enum class RpcOpenResult {
        OK,
        CONNECTION_REFUSED,
        ERROR
    };

    class Rpc {
    public:
        struct PollData {
            char buffer[GSR_RPC_MAX_MESSAGE_SIZE];
            int buffer_size = 0;
        };

        Rpc();
        Rpc(const Rpc&) = delete;
        Rpc& operator=(const Rpc&) = delete;
        ~Rpc();

        bool create(const char *name);
        RpcOpenResult open(const char *name);
        bool write(const char *str, size_t size);
        void poll();

        bool add_handler(const std::string &name, RpcCallback callback);
    private:
        void handle_client_data(int client_index, PollData &poll_data);
        void accept_listen_client();
    private:
        /* Platform-neutral: on POSIX this is the listen socket fd, on Windows
           the listen pipe handle (cast to intptr_t). */
        intptr_t listen_fd = 0;
        /* Windows: persistent OVERLAPPED for the listen pipe's async connect
           (opaque pointer, allocated in Rpc.cpp). NULL on POSIX. */
        void *listen_overlapped = nullptr;
        /* Windows: a client connected before ConnectNamedPipe was armed
           (ERROR_PIPE_CONNECTED) — accept it on the next poll. */
        bool listen_immediate_connect = false;
        int num_clients = 0;
        intptr_t client_fds[GSR_RPC_MAX_CONNECTIONS];
        PollData polls_data[GSR_RPC_MAX_CONNECTIONS];
        std::unordered_map<std::string, RpcCallback> handlers_by_name;
    };
}
