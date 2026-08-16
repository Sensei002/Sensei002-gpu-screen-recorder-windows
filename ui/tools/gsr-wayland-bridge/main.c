#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>

#define GSR_UI_UNIX_SOCKET_DOMAIN_FD 3

static int build_socket_path(const char *wayland_display, char *out, size_t out_size) {
    if(wayland_display[0] == '/') {
        if((size_t)snprintf(out, out_size, "%s", wayland_display) >= out_size) {
            fprintf(stderr, "gsr-wayland-bridge: WAYLAND_DISPLAY path too long\n");
            return -1;
        }
        return 0;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if(!runtime_dir || !*runtime_dir) {
        fprintf(stderr, "gsr-wayland-bridge: XDG_RUNTIME_DIR not set\n");
        return -1;
    }

    if((size_t)snprintf(out, out_size, "%s/%s", runtime_dir, wayland_display) >= out_size) {
        fprintf(stderr, "gsr-wayland-bridge: socket path too long\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *wayland_display = argc >= 2 ? argv[1] : NULL;
    if(!wayland_display || !*wayland_display)
        wayland_display = "wayland-0";

    char path[256];
    if(build_socket_path(wayland_display, path, sizeof(path)) != 0)
        return 1;

    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) {
        perror("gsr-wayland-bridge: socket");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if(strlen(path) >= sizeof(addr.sun_path)) {
        fprintf(stderr, "gsr-wayland-bridge: socket path too long for sockaddr_un\n");
        close(fd);
        return 1;
    }
    strcpy(addr.sun_path, path);

    if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "gsr-wayland-bridge: connect(%s): %s\n", path, strerror(errno));
        close(fd);
        return 1;
    }

    char dummy = 0;
    struct iovec iov = { &dummy, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof(cbuf));
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = cbuf;
    m.msg_controllen = sizeof(cbuf);

    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(int));

    if(sendmsg(GSR_UI_UNIX_SOCKET_DOMAIN_FD, &m, 0) < 0) {
        perror("gsr-wayland-bridge: sendmsg");
        close(fd);
        return 1;
    }

    return 0;
}
