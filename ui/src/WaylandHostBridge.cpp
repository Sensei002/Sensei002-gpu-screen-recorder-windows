#include "../include/WaylandHostBridge.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <wayland-client.h>

#define GSR_UI_UNIX_SOCKET_DOMAIN_FD 3

namespace gsr {
    static const char *bridge_host_path =
        "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/gsr-wayland-bridge";

    static int recv_fd(int sock) {
        char dummy = 0;
        iovec iov = { &dummy, 1 };
        char cbuf[CMSG_SPACE(sizeof(int))];
        memset(cbuf, 0, sizeof(cbuf));
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = cbuf;
        msg.msg_controllen = sizeof(cbuf);

        const ssize_t n = recvmsg(sock, &msg, 0);
        if(n <= 0)
            return -1;

        for(cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
            if(c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
                int fd = -1;
                memcpy(&fd, CMSG_DATA(c), sizeof(int));
                return fd;
            }
        }
        return -1;
    }

    static wl_display* connect_via_bridge() {
        const char *wayland_display = getenv("WAYLAND_DISPLAY");

        int sv[2];
        if(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
            perror("WaylandHostBridge: socketpair");
            return nullptr;
        }

        const pid_t pid = fork();
        if(pid < 0) {
            perror("WaylandHostBridge: fork");
            close(sv[0]);
            close(sv[1]);
            return nullptr;
        }

        if(pid == 0) {
            close(sv[0]);
            if(sv[1] != GSR_UI_UNIX_SOCKET_DOMAIN_FD) {
                dup2(sv[1], GSR_UI_UNIX_SOCKET_DOMAIN_FD);
                close(sv[1]);
            }
            const int flags = fcntl(GSR_UI_UNIX_SOCKET_DOMAIN_FD, F_GETFD);
            if(flags >= 0)
                fcntl(GSR_UI_UNIX_SOCKET_DOMAIN_FD, F_SETFD, flags & ~FD_CLOEXEC);

            char forward_fd_arg[32];
            snprintf(forward_fd_arg, sizeof(forward_fd_arg), "--forward-fd=%d", GSR_UI_UNIX_SOCKET_DOMAIN_FD);

            execlp("flatpak-spawn", "flatpak-spawn", "--host", forward_fd_arg, bridge_host_path, wayland_display, (char*)nullptr);
            _exit(127);
        }

        close(sv[1]);
        const int wayland_fd = recv_fd(sv[0]);
        close(sv[0]);

        int status = 0;
        waitpid(pid, &status, 0);

        if(wayland_fd < 0) {
            fprintf(stderr, "WaylandHostBridge: gsr-wayland-bridge did not return a wayland fd\n");
            return nullptr;
        }

        wl_display *dpy = wl_display_connect_to_fd(wayland_fd);
        if(!dpy) {
            close(wayland_fd);
            fprintf(stderr, "WaylandHostBridge: wl_display_connect_to_fd failed\n");
            return nullptr;
        }
        return dpy;
    }

    wl_display* wayland_connect_to_host() {
        const bool inside_flatpak = getenv("FLATPAK_ID") != nullptr;
        if(inside_flatpak) {
            wl_display *dpy = connect_via_bridge();
            if(dpy)
                return dpy;
            fprintf(stderr, "WaylandHostBridge: falling back to sandboxed wl_display_connect\n");
        }
        return wl_display_connect(nullptr);
    }
}
