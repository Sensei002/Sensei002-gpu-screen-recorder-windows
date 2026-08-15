#include "kms_client.h"
#include "../../include/log.h"
#include "../../include/utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <poll.h>
#ifdef __linux__
#include <sys/capability.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#define GSR_SOCKET_PAIR_LOCAL  0
#define GSR_SOCKET_PAIR_REMOTE 1

static void cleanup_socket(gsr_kms_client *self, bool kill_server);
static int gsr_kms_client_replace_connection(gsr_kms_client *self);

static void close_fds(gsr_kms_response *response) {
    for(int i = 0; i < response->num_items; ++i) {
        for(int j = 0; j < response->items[i].num_dma_bufs; ++j) {
            gsr_kms_response_dma_buf *dma_buf = &response->items[i].dma_buf[j];
            if(dma_buf->fd > 0) {
                close(dma_buf->fd);
                dma_buf->fd = -1;
            }
        }
        response->items[i].num_dma_bufs = 0;
    }
    response->num_items = 0;
}

static int send_msg_to_server(int server_fd, gsr_kms_request *request) {
    struct iovec iov;
    iov.iov_base = request;
    iov.iov_len = sizeof(*request);

    struct msghdr response_message = {0};
    response_message.msg_iov = &iov;
    response_message.msg_iovlen = 1;

    char cmsgbuf[CMSG_SPACE(sizeof(int) * 1)];
    memset(cmsgbuf, 0, sizeof(cmsgbuf));

    if(request->new_connection_fd > 0) {
        response_message.msg_control = cmsgbuf;
        response_message.msg_controllen = sizeof(cmsgbuf);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&response_message);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 1);

        int *fds = (int*)CMSG_DATA(cmsg);
        fds[0] = request->new_connection_fd;

        response_message.msg_controllen = cmsg->cmsg_len;
    }

    return sendmsg(server_fd, &response_message, 0);
}

static int recv_msg_from_server(int server_pid, int server_fd, gsr_kms_response *response) {
    struct iovec iov;
    iov.iov_base = response;
    iov.iov_len = sizeof(*response);

    struct msghdr response_message = {0};
    response_message.msg_iov = &iov;
    response_message.msg_iovlen = 1;

    char cmsgbuf[CMSG_SPACE(sizeof(int) * GSR_KMS_MAX_ITEMS * GSR_KMS_MAX_DMA_BUFS)];
    memset(cmsgbuf, 0, sizeof(cmsgbuf));
    response_message.msg_control = cmsgbuf;
    response_message.msg_controllen = sizeof(cmsgbuf);

    int res = 0;
    for(;;) {
        res = recvmsg(server_fd, &response_message, MSG_DONTWAIT);
        if(res <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // If we are replacing the connection and closing the application at the same time
            // then recvmsg can get stuck (because the server died), so we prevent that by doing
            // non-blocking recvmsg and checking if the server died
            int status = 0;
            int wait_result = waitpid(server_pid, &status, WNOHANG);
            if(wait_result != 0) {
                res = -1;
                break;
            }
            usleep(1000);
        } else {
            break;
        }
    }

    if(res > 0 && response->num_items > 0) {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&response_message);
        if(cmsg) {
            int *fds = (int*)CMSG_DATA(cmsg);
            int fd_index = 0;
            for(int i = 0; i < response->num_items; ++i) {
                for(int j = 0; j < response->items[i].num_dma_bufs; ++j) {
                    gsr_kms_response_dma_buf *dma_buf = &response->items[i].dma_buf[j];
                    dma_buf->fd = fds[fd_index++];
                }
            }
        } else {
            close_fds(response);
        }
    }

    return res;
}

/* We have to use $HOME because in flatpak there is no simple path that is accessible, read and write, that multiple flatpak instances can access */
static bool create_socket_path(char *output_path, size_t output_path_size) {
    const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
    const char *home = getenv("HOME");
    // Portable home with AppImage can cause the socket path to be longer than 108 characters (unix domain socket path max length).
    // Using gsr-kms-socket in $HOME is only needed in flatpak, so use /tmp everywhere else instead.
    if(!home || !inside_flatpak)
        home = "/tmp";

    char random_characters[11];
    random_characters[10] = '\0';
    if(!generate_random_characters_standard_alphabet(random_characters, 10))
        return false;

    snprintf(output_path, output_path_size, "%s/.gsr-kms-socket-%s", home, random_characters);
    return true;
}

#ifdef __linux__
static bool readlink_realpath(const char *filepath, char *buffer) {
    char symlinked_path[PATH_MAX];
    ssize_t bytes_written = readlink(filepath, symlinked_path, sizeof(symlinked_path) - 1);
    if(bytes_written == -1 && errno == EINVAL) {
        /* Not a symlink */
        snprintf(symlinked_path, sizeof(symlinked_path), "%s", filepath);
    } else if(bytes_written == -1) {
        return false;
    } else {
        symlinked_path[bytes_written] = '\0';
    }

    if(!realpath(symlinked_path, buffer))
        return false;

    return true;
}
#endif

static bool strcat_safe(char *str, int size, const char *str_to_add) {
    const int str_len = strlen(str);
    const int str_to_add_len = strlen(str_to_add);
    if(str_len + str_to_add_len + 1 >= size)
        return false;

    memcpy(str + str_len, str_to_add, str_to_add_len);
    str[str_len + str_to_add_len] = '\0';
    return true;
}

static void file_get_directory(char *filepath) {
    char *end = strrchr(filepath, '/');
    if(end == NULL)
        filepath[0] = '\0';
    else
        *end = '\0';
}

static bool find_program_in_path(const char *program_name, char *filepath, int filepath_len) {
    const char *path = getenv("PATH");
    if(!path)
        return false;

    int program_name_len = strlen(program_name);
    const char *end = path + strlen(path);
    while(path != end) {
        const char *part_end = strchr(path, ':');
        const char *next = part_end;
        if(part_end) {
            next = part_end + 1;
        } else {
            part_end = end;
            next = end;
        }

        int len = part_end - path;
        if(len + 1 + program_name_len < filepath_len) {
            memcpy(filepath, path, len);
            filepath[len] = '/';
            memcpy(filepath + len + 1, program_name, program_name_len);
            filepath[len + 1 + program_name_len] = '\0';

            if(access(filepath, F_OK) == 0)
                return true;
        }

        path = next;
    }

    return false;
}

int gsr_kms_client_init(gsr_kms_client *self, const char *card_path) {
    int result = -1;
    self->kms_server_pid = -1;
    self->initial_socket_fd = -1;
    self->initial_client_fd = -1;
    self->initial_socket_path[0] = '\0';
    self->socket_pair[0] = -1;
    self->socket_pair[1] = -1;
    struct sockaddr_un local_addr = {0};
    struct sockaddr_un remote_addr = {0};

    if(!create_socket_path(self->initial_socket_path, sizeof(self->initial_socket_path))) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to create path to kms socket");
        return -1;
    }

    char server_filepath[PATH_MAX];
#ifdef __linux__
    if(!readlink_realpath("/proc/self/exe", server_filepath)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to resolve /proc/self/exe");
        return -1;
    }

#elif defined(__FreeBSD__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, getpid() };
    size_t size = PATH_MAX;

    if (sysctl(mib, 4, server_filepath, &size, NULL, 0) != 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to resolve pathname using sysctl");
        return -1;
    }

#else
#error "Implement it by yourself"
#endif
    file_get_directory(server_filepath);

    if(!strcat_safe(server_filepath, sizeof(server_filepath), "/gsr-kms-server")) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: gsr-kms-server path too long");
        return -1;
    }

    if(access(server_filepath, F_OK) != 0) {
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: gsr-kms-server is not installed in the same directory as gpu-screen-recorder (%s not found), looking for gsr-kms-server in PATH instead", server_filepath);
        if(!find_program_in_path("gsr-kms-server", server_filepath, sizeof(server_filepath)) || access(server_filepath, F_OK) != 0) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: gsr-kms-server was not found in PATH. Please install gpu-screen-recorder properly");
            return -1;
        }
    }

    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: setting up connection to %s", server_filepath);

    const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
    const char *home = getenv("HOME");
    if(!home)
        home = "/tmp";

    bool has_perm = 0;
    if(geteuid() == 0) {
        has_perm = true;
    } else {
#ifdef __linux__
        cap_t kms_server_cap = cap_get_file(server_filepath);
        if(kms_server_cap) {
            cap_flag_value_t res = CAP_CLEAR;
            cap_get_flag(kms_server_cap, CAP_SYS_ADMIN, CAP_PERMITTED, &res);
            if(res == CAP_SET) {
                //fprintf(stderr, "has permission!\n");
                has_perm = true;
            } else {
                //fprintf(stderr, "No permission:(\n");
            }
            cap_free(kms_server_cap);
        } else if(!inside_flatpak) {
            if(errno == ENODATA)
                gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: gsr-kms-server is missing sys_admin cap and will require root authentication. To bypass this automatically, run: sudo setcap cap_sys_admin+ep '%s'", server_filepath);
            else
                gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: failed to get cap");
        }
#else
        gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: platform doesn't support cap");
#endif
    }

    if(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, self->socket_pair) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: socketpair failed, error: %s", strerror(errno));
        goto err;
    }

    self->initial_socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(self->initial_socket_fd == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: socket failed, error: %s", strerror(errno));
        goto err;
    }

    local_addr.sun_family = AF_UNIX;
    snprintf(local_addr.sun_path, sizeof(local_addr.sun_path), "%s", (const char*)self->initial_socket_path);

    const mode_t prev_mask = umask(0000);
    const int bind_res = bind(self->initial_socket_fd, (struct sockaddr*)&local_addr, sizeof(local_addr.sun_family) + strlen(local_addr.sun_path));
    umask(prev_mask);

    if(bind_res == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to bind socket, error: %s", strerror(errno));
        goto err;
    }

    if(listen(self->initial_socket_fd, 1) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to listen on socket, error: %s", strerror(errno));
        goto err;
    }

    pid_t pid = fork();
    if(pid == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: fork failed, error: %s", strerror(errno));
        goto err;
    } else if(pid == 0) { /* child */
        if(inside_flatpak) {
            const char *args[] = { "flatpak-spawn", "--host", "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/kms-server-proxy", self->initial_socket_path, card_path, home, NULL };
            execvp(args[0], (char *const*)args);
        } else if(has_perm) {
            const char *args[] = { server_filepath, self->initial_socket_path, card_path, NULL };
            execvp(args[0], (char *const*)args);
        } else {
            const char *args[] = { "pkexec", server_filepath, self->initial_socket_path, card_path, NULL };
            execvp(args[0], (char *const*)args);
        }
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: failed to launch \"gsr-kms-server\", error: %s", strerror(errno));
        _exit(127);
    } else { /* parent */
        self->kms_server_pid = pid;
    }

    // We need this dumb-shit retardation with unix domain socket and then replace it with socketpair because
    // pkexec doesn't work with socketpair................
    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: waiting for server to connect");
    struct pollfd poll_fd = {
        .fd = self->initial_socket_fd,
        .events = POLLIN,
        .revents = 0
    };
    for(;;) {
        int poll_res = poll(&poll_fd, 1, 100);
        if(poll_res > 0 && (poll_fd.revents & POLLIN)) {
            socklen_t sock_len = 0;
            self->initial_client_fd = accept(self->initial_socket_fd, (struct sockaddr*)&remote_addr, &sock_len);
            if(self->initial_client_fd == -1) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: accept failed on socket, error: %s", strerror(errno));
                goto err;
            }
            break;
        } else {
            int status = 0;
            int wait_result = waitpid(self->kms_server_pid, &status, WNOHANG);
            if(wait_result != 0) {
                int exit_code = -1;
                if(WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_init: kms server died or never started, exit code: %d", exit_code);
                self->kms_server_pid = -1;
                if(exit_code != 0)
                    result = exit_code;
                goto err;
            }
        }
        poll_fd.revents = 0;
    }
    gsr_log(GSR_LOG_LEVEL_INFO, "gsr_kms_client_init: server connected");

    gsr_log(GSR_LOG_LEVEL_INFO, "replacing file-backed unix domain socket with socketpair");
    if(gsr_kms_client_replace_connection(self) != 0)
        goto err;

    cleanup_socket(self, false);
    gsr_log(GSR_LOG_LEVEL_INFO, "using socketpair");

    return 0;

    err:
    gsr_kms_client_deinit(self);
    return result;
}

void cleanup_socket(gsr_kms_client *self, bool kill_server) {
    if(self->initial_client_fd > 0) {
        close(self->initial_client_fd);
        self->initial_client_fd = -1;
    }

    if(self->initial_socket_fd > 0) {
        close(self->initial_socket_fd);
        self->initial_socket_fd = -1;
    }

    if(kill_server) {
        for(int i = 0; i < 2; ++i) {
            if(self->socket_pair[i] > 0) {
                close(self->socket_pair[i]);
                self->socket_pair[i] = -1;
            }
        }
    }

    if(kill_server && self->kms_server_pid > 0) {
        kill(self->kms_server_pid, SIGKILL);
        // TODO:
        //int status;
        //waitpid(self->kms_server_pid, &status, 0);
        self->kms_server_pid = -1;
    }

    if(self->initial_socket_path[0] != '\0') {
        remove(self->initial_socket_path);
        self->initial_socket_path[0] = '\0';
    }
}

void gsr_kms_client_deinit(gsr_kms_client *self) {
    cleanup_socket(self, true);
}

int gsr_kms_client_replace_connection(gsr_kms_client *self) {
    gsr_kms_response response;
    response.version = 0;
    response.result = KMS_RESULT_FAILED_TO_SEND;
    response.err_msg[0] = '\0';

    gsr_kms_request request;
    request.version = GSR_KMS_PROTOCOL_VERSION;
    request.type = KMS_REQUEST_TYPE_REPLACE_CONNECTION;
    request.new_connection_fd = self->socket_pair[GSR_SOCKET_PAIR_REMOTE];
    if(send_msg_to_server(self->initial_client_fd, &request) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_replace_connection: failed to send request message to server");
        return -1;
    }

    const int recv_res = recv_msg_from_server(self->kms_server_pid, self->socket_pair[GSR_SOCKET_PAIR_LOCAL], &response);
    if(recv_res == 0) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_kms_client_replace_connection: kms server shut down");
        return -1;
    } else if(recv_res == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_replace_connection: failed to receive response");
        return -1;
    }

    if(response.version != GSR_KMS_PROTOCOL_VERSION) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_replace_connection: expected gsr-kms-server protocol version to be %u, but it's %u. please reinstall gpu screen recorder", GSR_KMS_PROTOCOL_VERSION, response.version);
        /*close_fds(response);*/
        return -1;
    }

    return 0;
}

int gsr_kms_client_get_kms(gsr_kms_client *self, gsr_kms_response *response) {
    response->version = 0;
    response->result = KMS_RESULT_FAILED_TO_SEND;
    response->err_msg[0] = '\0';
    response->num_items = 0;

    gsr_kms_request request;
    request.version = GSR_KMS_PROTOCOL_VERSION;
    request.type = KMS_REQUEST_TYPE_GET_KMS;
    request.new_connection_fd = 0;
    if(send_msg_to_server(self->socket_pair[GSR_SOCKET_PAIR_LOCAL], &request) == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_get_kms: failed to send request message to server");
        strcpy(response->err_msg, "failed to send");
        return -1;
    }

    const int recv_res = recv_msg_from_server(self->kms_server_pid, self->socket_pair[GSR_SOCKET_PAIR_LOCAL], response);
    if(recv_res == 0) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_kms_client_get_kms: kms server shut down");
        strcpy(response->err_msg, "failed to receive");
        return -1;
    } else if(recv_res == -1) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_get_kms: failed to receive response");
        strcpy(response->err_msg, "failed to receive");
        return -1;
    }

    if(response->version != GSR_KMS_PROTOCOL_VERSION) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_kms_client_get_kms: expected gsr-kms-server protocol version to be %u, but it's %u. please reinstall gpu screen recorder", GSR_KMS_PROTOCOL_VERSION, response->version);
        /*close_fds(response);*/
        strcpy(response->err_msg, "mismatching protocol version");
        return -1;
    }

    return 0;
}
