#include "native_games.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <locale.h>
#include <signal.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>

static void *gsr_memmem(const void *hstk, size_t hlen, const void *ndl, size_t nlen)
{
    if (!nlen) return (void *)hstk;
    if (hlen < nlen) return NULL;
    const unsigned char *h = hstk;
    const unsigned char *n = ndl;
    for (size_t i = 0, last = hlen - nlen; i <= last; ++i)
        if (h[i] == n[0] && memcmp(h + i, n, nlen) == 0)
            return (void *)(h + i);
    return NULL;
}

#define MAX_GAMES        32
#define ENVIRON_BUF_SIZE (64 * 1024)
#define CMDLINE_BUF_SIZE 4096
#define RECV_BUF_SIZE    8192

static pid_t games[MAX_GAMES]; /* 0 = empty slot */
static int   game_count = 0;
static char  environ_buf[ENVIRON_BUF_SIZE];
static char  cmdline_buf[CMDLINE_BUF_SIZE];
static char  recv_buf[RECV_BUF_SIZE];

typedef struct {
    const char *value; /* Null-terminated. Null if not defined in the process */
    size_t value_len;
} EnvVar;

typedef struct {
    EnvVar *value;
    const char *name;
    size_t name_len;
} EnvVarNamed;

typedef struct {
    EnvVar steam_overlay_game_id;
    EnvVar steam_app_id;
    EnvVar steam_game_id;
    EnvVar wineloader;
    EnvVar wineloadernoexec;
    EnvVar steam_base_folder;
    EnvVar steam_compat_app_id;
    EnvVar umu_invocation_id;
} EnvVars;

static EnvVarNamed env_var_named_init(EnvVar *value, const char *name) {
    return (EnvVarNamed) {
        .value = value,
        .name = name,
        .name_len = strlen(name),
    };
}

static int find_slot_by_pid(pid_t pid) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i] == pid)
            return i;
    }
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (!games[i])
            return i;
    }
    return -1;
}

static void add_game(pid_t pid) {
    int slot = find_free_slot();
    if (slot < 0) return;
    games[slot] = pid;
    if (game_count++ == 0) {
        printf("Game launched\n");
        fflush(stdout);
    }
}

static void remove_game_by_slot(int slot) {
    games[slot] = 0;
    if (--game_count == 0) {
        printf("Game exited\n");
        fflush(stdout);
    }
}

static void remove_game(pid_t pid) {
    int slot = find_slot_by_pid(pid);
    if (slot < 0) return;
    remove_game_by_slot(slot);
}

static ssize_t read_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, size - 1);
    close(fd);
    if (n >= 0) buf[n] = '\0';
    return n;
}

static void read_environment_variables(const char *env, size_t env_size, EnvVars *env_vars) {
    const EnvVarNamed env_var_refs[] = {
        /* This is set for non-steam games added to steam library */
        env_var_named_init(&env_vars->steam_overlay_game_id, "SteamOverlayGameId"),
        /* This is set for regular steam apps */
        env_var_named_init(&env_vars->steam_app_id, "SteamAppId"),
        env_var_named_init(&env_vars->steam_game_id, "SteamGameId"),
        env_var_named_init(&env_vars->wineloader, "WINELOADER"),
        env_var_named_init(&env_vars->wineloadernoexec, "WINELOADERNOEXEC"),
        env_var_named_init(&env_vars->steam_base_folder, "STEAM_BASE_FOLDER"),
        env_var_named_init(&env_vars->steam_compat_app_id, "STEAM_COMPAT_APP_ID"),
        env_var_named_init(&env_vars->umu_invocation_id, "UMU_INVOCATION_ID"),
    };

    size_t index = 0;
    while (index < env_size) {
        const char *env_start = env + index;
        const char *env_end = memchr(env_start, '\0', env_size - index);
        if(!env_end)
            break;

        const size_t env_len = env_end - env_start;
        for(size_t i = 0; i < sizeof(env_var_refs) / sizeof(env_var_refs[0]); ++i) {
            const EnvVarNamed *env_var_ref = &env_var_refs[i];
            if(env_len >= env_var_ref->name_len + 1 && memcmp(env_start, env_var_ref->name, env_var_ref->name_len) == 0 && env_start[env_var_ref->name_len] == '=') {
                env_var_ref->value->value = env_start + env_var_ref->name_len + 1;
                env_var_ref->value->value_len = env_len - (env_var_ref->name_len + 1);
                break;
            }
        }

        index += env_len + 1;
    }
}

static size_t get_argv_len(const char *cmdline, size_t size) {
    const char *argv0_end = memchr(cmdline, '\0', size);
    if(argv0_end)
        return argv0_end - cmdline;
    else
        return 0;
}

static const char* memchr_reverse(const char *p, int c, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        if(p[size - 1 - i] == c)
            return &p[size - 1 - i];
    }
    return NULL;
}

static bool memeql(const char *haystack, size_t haystack_size, const char *needle) {
    const size_t needle_size = strlen(needle);
    return haystack_size == needle_size && memcmp(haystack, needle, needle_size) == 0;
}

static const char* process_get_basename(const char *argv0, size_t argv0_len, size_t *basename_len) {
    *basename_len = 0;
    const char *base = memchr_reverse(argv0, '/', argv0_len);
    base = base ? base + 1 : argv0;
    *basename_len = argv0 + argv0_len - base;
    return base;
}

static bool is_wine_binary(const char *process_basename, size_t process_basename_len) {
    return memeql(process_basename, process_basename_len, "wine") ||
           memeql(process_basename, process_basename_len, "wine64") ||
           memeql(process_basename, process_basename_len, "wine-preloader") ||
           memeql(process_basename, process_basename_len, "wine64-preloader");
}

static bool is_wine_server(const char *process_basename, size_t process_basename_len) {
    return memeql(process_basename, process_basename_len, "wineserver");
}

static bool is_windows_system_process(const char *cmdline, size_t cmdline_len) {
    return !!gsr_memmem(cmdline, cmdline_len, "system32", 8);
}

// static bool is_proton_tool(const char *cmdline, size_t cmdline_len) {
//     return !!gsr_memmem(cmdline, cmdline_len, "compatibilitytools.d", 20);
// }

static bool is_steam_tool(const char *process_name, size_t process_name_len) {
    return !!gsr_memmem(process_name, process_name_len, "Steam/bin", 9);
}

static bool is_blacklisted_application(const char *cmdline, size_t cmdline_len) {
    const char *names[] = { "/xalia.exe", "/d3ddriverquery", NULL };
    for(size_t i = 0; names[i]; ++i) {
        const char *name = names[i];
        if(gsr_memmem(cmdline, cmdline_len, name, strlen(name)))
            return true;
    }
    return false;
}

static bool has_game_arch_suffix(const char *process_basename, size_t process_basename_len) {
    static const char *suffixes[] = { ".x86_64", ".x64", ".x86" };
    for (int i = 0; i < 3; i++) {
        const size_t slen = strlen(suffixes[i]);
        if (process_basename_len >= slen && memcmp(process_basename + process_basename_len - slen, suffixes[i], slen) == 0)
            return true;
    }
    return false;
}

static void check_process(pid_t pid) {
    if (find_slot_by_pid(pid) >= 0) return;

    char    path[64];
    ssize_t env_n, cmd_n;
    bool is_steam_app = false;
    /* Proton launched for a non-steam game has this while it doesn't have SteamAppId nor is the process called wine */
    bool has_wine_env = false;

    snprintf(path, sizeof(path), "/proc/%d/environ", pid);
    env_n = read_file(path, environ_buf, sizeof(environ_buf));
    EnvVars env_vars = {0};

    if (env_n > 0) {
        read_environment_variables(environ_buf, env_n, &env_vars);
        const char *appid = env_vars.steam_app_id.value;
        if (!appid) appid = env_vars.steam_game_id.value;
        if (!appid) appid = env_vars.steam_overlay_game_id.value;
        if (appid && appid[0] >= '1' && appid[0] <= '9') {
            add_game(pid);
            return;
        }

        has_wine_env = env_vars.wineloader.value;
        has_wine_env = has_wine_env || env_vars.wineloadernoexec.value;
        is_steam_app = env_vars.steam_base_folder.value || env_vars.steam_compat_app_id.value != NULL;
    }

    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    cmd_n = read_file(path, cmdline_buf, sizeof(cmdline_buf));
    if(cmd_n <= 0)
        return;

    const char *argv0 = cmdline_buf;
    const size_t argv0_len = get_argv_len(cmdline_buf, cmd_n);

    const char *argv1 = cmdline_buf + argv0_len + 1;
    const size_t argv1_len = (size_t)cmd_n > argv0_len + 1 ? get_argv_len(argv1, cmd_n - (argv0_len + 1)) : 0;

    size_t process_basename_len = 0;
    const char *process_basename = process_get_basename(argv0, argv0_len, &process_basename_len);

    if ((is_wine_binary(process_basename, process_basename_len)
            || has_game_arch_suffix(process_basename, process_basename_len)
            || has_wine_env
            || env_vars.umu_invocation_id.value
            || is_process_name_native_game(process_basename, process_basename_len))
        && !is_wine_server(process_basename, process_basename_len)
        && !memeql(argv1, argv1_len, "--version")
        && (!is_steam_app || env_vars.steam_overlay_game_id.value != NULL || env_vars.umu_invocation_id.value)
        && !is_windows_system_process(cmdline_buf, cmd_n)
        //&& !is_proton_tool(cmdline_buf, cmd_n)
        && !is_steam_tool(argv0, argv0_len)
        && !is_blacklisted_application(cmdline_buf, cmd_n))
    {
        // fprintf(stderr, "argv0: |%.*s|\n", (int)argv0_len, argv0);
        // fprintf(stderr, "*******\n");
        // fprintf(stderr, "env: ");
        // write(STDOUT_FILENO, environ_buf, env_n);
        // fprintf(stderr, "\ncmdline: ");
        // write(STDOUT_FILENO, cmdline_buf, cmd_n);
        // fprintf(stderr, "\n");
        // fprintf(stderr, "*******\n");
        // fprintf(stderr, "\n\n\n");
        add_game(pid);
    }
}

static void handle_proc_event(const struct proc_event *ev) {
    switch (ev->what) {
    case PROC_EVENT_EXEC:
        check_process(ev->event_data.exec.process_tgid);
        break;
    case PROC_EVENT_EXIT:
        /* Only act when the whole process (not just a thread) exits */
        if (ev->event_data.exit.process_pid == ev->event_data.exit.process_tgid)
            remove_game(ev->event_data.exit.process_tgid);
        break;
    default:
        break;
    }
}

static void process_netlink_msg(const char *buf, size_t len) {
    const struct nlmsghdr *nl = (const struct nlmsghdr *)buf;
    for (; NLMSG_OK(nl, (unsigned int)len); nl = NLMSG_NEXT(nl, len)) {
        if (nl->nlmsg_type == NLMSG_NOOP  ||
            nl->nlmsg_type == NLMSG_ERROR ||
            nl->nlmsg_type == NLMSG_OVERRUN)
            continue;
        if (nl->nlmsg_len < NLMSG_HDRLEN + sizeof(struct cn_msg))
            continue;
        const struct cn_msg *cn = (const struct cn_msg *)NLMSG_DATA(nl);
        if (cn->id.idx != CN_IDX_PROC || cn->id.val != CN_VAL_PROC)
            continue;
        if (cn->len < sizeof(struct proc_event))
            continue;
        if ((size_t)(nl->nlmsg_len - NLMSG_HDRLEN - sizeof(struct cn_msg)) < cn->len)
            continue;
        handle_proc_event((const struct proc_event *)cn->data);
    }
}

static int send_mcast_op(int sock, enum proc_cn_mcast_op op) {
    /* cn_msg ends with data[0], so we can't place fields after it in a struct.
     * Use a flat buffer and fill via pointers instead. */
    char buf[NLMSG_SPACE(sizeof(struct cn_msg) + sizeof(enum proc_cn_mcast_op))];
    memset(buf, 0, sizeof(buf));

    struct nlmsghdr *nl = (struct nlmsghdr *)buf;
    nl->nlmsg_len  = sizeof(buf);
    nl->nlmsg_type = NLMSG_DONE;
    nl->nlmsg_pid  = (unsigned int)getpid();

    struct cn_msg *cn = (struct cn_msg *)NLMSG_DATA(nl);
    cn->id.idx = CN_IDX_PROC;
    cn->id.val = CN_VAL_PROC;
    cn->len    = sizeof(enum proc_cn_mcast_op);
    memcpy(cn->data, &op, sizeof(op));

    return send(sock, buf, sizeof(buf), 0) < 0 ? -1 : 0;
}

static int setup_netlink(void) {
    int sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (sock < 0) {
        perror("socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)");
        return -1;
    }

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = CN_IDX_PROC;
    sa.nl_pid    = (unsigned int)getpid();

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    if (send_mcast_op(sock, PROC_CN_MCAST_LISTEN) < 0) {
        perror("send PROC_CN_MCAST_LISTEN");
        close(sock);
        return -1;
    }

    return sock;
}

static void sweep_dead_games(void) {
    for (int i = 0; i < MAX_GAMES; i++) {
        if (games[i] && kill(games[i], 0) < 0 && errno == ESRCH)
            remove_game_by_slot(games[i]);
    }
}

static void scan_existing_processes(void) {
    DIR *d = opendir("/proc");
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *s = ent->d_name;
        if (*s < '1' || *s > '9') continue;
        pid_t pid = 0;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') { pid = 0; break; }
            pid = pid * 10 + (*s - '0');
        }
        if (pid > 0) check_process(pid);
    }
    closedir(d);
}

int main(void) {
    setlocale(LC_ALL, "C"); // Sigh... stupid C

    memset(games, 0, sizeof(games));

    int sock = setup_netlink();
    if (sock < 0) return 1;

    scan_existing_processes();

    for (;;) {
        ssize_t n = recv(sock, recv_buf, sizeof(recv_buf), MSG_TRUNC);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ENOBUFS) {
                sweep_dead_games();
                scan_existing_processes();
                continue;
            } else {
                perror("recv");
                break;
            }
        }

        if ((size_t)n > sizeof(recv_buf)) {
            /* Datagram larger than recv_buf was truncated; we may have lost events. */
            sweep_dead_games();
            scan_existing_processes();
            continue;
        }

        process_netlink_msg(recv_buf, (size_t)n);
    }

    send_mcast_op(sock, PROC_CN_MCAST_IGNORE);
    close(sock);
    return 0;
}
