#pragma once

#include <stddef.h>
#include <functional>
#include <unordered_map>
#include <string>
#include <poll.h>

#define GSR_RPC_MAX_CONNECTIONS 8
#define GSR_RPC_MAX_POLLS (1 + GSR_RPC_MAX_CONNECTIONS) /* +1 to include the socket_fd itself for accept */
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
        void handle_client_data(int client_fd, PollData &poll_data);
    private:
        int socket_fd = 0;
        struct pollfd polls[GSR_RPC_MAX_POLLS];
        PollData polls_data[GSR_RPC_MAX_POLLS];
        int num_polls = 0;
        std::unordered_map<std::string, RpcCallback> handlers_by_name;
    };
}