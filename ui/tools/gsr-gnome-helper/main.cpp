#include <dbus/dbus.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char *EXTENSION_UUID = "gpu-screen-recorder@dec05eba.com";

static const char *INTROSPECTION_XML =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
    "<node>\n"
    "  <interface name='com.dec05eba.gpu_screen_recorder'>\n"
    "    <method name='updateActiveWindow'>\n"
    "      <arg type='s' name='title' direction='in'/>\n"
    "      <arg type='b' name='fullscreen' direction='in'/>\n"
    "      <arg type='s' name='monitorName' direction='in'/>\n"
    "    </method>\n"
    "  </interface>\n"
    "  <interface name='org.freedesktop.DBus.Introspectable'>\n"
    "    <method name='Introspect'>\n"
    "      <arg type='s' name='data' direction='out'/>\n"
    "    </method>\n"
    "  </interface>\n"
    "</node>\n";

static std::string read_file(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if(!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool write_file(const std::string &path, const std::string &contents) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if(!f)
        return false;
    f.write(contents.data(), contents.size());
    return f.good();
}

static int mkdir_p(const std::string &path) {
    if(path.empty())
        return -1;
    size_t pos = 0;
    while(true) {
        pos = path.find('/', pos + 1);
        const std::string sub = (pos == std::string::npos) ? path : path.substr(0, pos);
        if(!sub.empty() && sub != "/") {
            if(mkdir(sub.c_str(), 0755) != 0 && errno != EEXIST) {
                std::cerr << "Error: gsr-gnome-helper: mkdir " << sub << ": " << strerror(errno) << "\n";
                return -1;
            }
        }
        if(pos == std::string::npos)
            break;
    }
    return 0;
}

static std::string get_extension_install_dir() {
    const char *xdg = getenv("XDG_DATA_HOME");
    if(xdg && xdg[0])
        return std::string(xdg) + "/gnome-shell/extensions/" + EXTENSION_UUID;

    const char *home = getenv("HOME");
    if(home && home[0])
        return std::string(home) + "/.local/share/gnome-shell/extensions/" + EXTENSION_UUID;

    return {};
}

static bool install_extension(const std::string &source_dir, const std::string &target_dir) {
    if(target_dir.empty()) {
        std::cerr << "Error: gsr-gnome-helper: cannot determine extension install directory (no $HOME)\n";
        return false;
    }
    if(mkdir_p(target_dir) != 0)
        return false;

    auto copy_one = [&](const char *name) {
        const std::string src = source_dir + "/" + name;
        const std::string dst = target_dir + "/" + name;
        const std::string contents = read_file(src);
        if(contents.empty()) {
            std::cerr << "Error: gsr-gnome-helper: failed to read " << src << "\n";
            return false;
        }
        if(!write_file(dst, contents)) {
            std::cerr << "Error: gsr-gnome-helper: failed to write " << dst << "\n";
            return false;
        }
        return true;
    };

    if(!copy_one("metadata.json"))
        return false;
    if(!copy_one("extension.js"))
        return false;

    std::cerr << "Info: gsr-gnome-helper: installed gnome shell extension to " << target_dir << "\n";
    return true;
}

static bool install_extension_via_host(const std::string &source_dir) {
    const std::string cmd =
        "set -e; "
        "target=\"${XDG_DATA_HOME:-$HOME/.local/share}/gnome-shell/extensions/" + std::string(EXTENSION_UUID) + "\"; "
        "mkdir -p \"$target\" && "
        "cp -f \"" + source_dir + "/metadata.json\" \"$target/metadata.json\" && "
        "cp -f \"" + source_dir + "/extension.js\" \"$target/extension.js\"";

    pid_t pid = fork();
    if(pid == -1) {
        perror("Error: gsr-gnome-helper: fork");
        return false;
    }
    if(pid == 0) {
        const char *args[] = { "flatpak-spawn", "--host", "--", "/bin/sh", "-c", cmd.c_str(), nullptr };
        execvp(args[0], (char* const*)args);
        _exit(127);
    }

    int status = 0;
    while(waitpid(pid, &status, 0) == -1) {
        if(errno != EINTR)
            return false;
    }
    if(!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "Error: gsr-gnome-helper: host install failed (exit "
                  << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) << ")\n";
        return false;
    }
    std::cerr << "Info: gsr-gnome-helper: installed gnome shell extension on host\n";
    return true;
}

class GsrGnomeHelper {
public:
    DBusConnection *connection = nullptr;
    DBusError err;

    std::string active_window_title;
    bool active_window_fullscreen = false;
    std::string active_window_monitor_name;

    bool init() {
        dbus_error_init(&err);

        connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
        if(dbus_error_is_set(&err)) {
            std::cerr << "Error: gsr-gnome-helper: failed to connect to session bus: " << err.message << "\n";
            dbus_error_free(&err);
            return false;
        }
        if(!connection) {
            std::cerr << "Error: gsr-gnome-helper: connection is null\n";
            return false;
        }

        const int ret = dbus_bus_request_name(connection, "com.dec05eba.gpu_screen_recorder",
                                              DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
        if(dbus_error_is_set(&err)) {
            std::cerr << "Error: gsr-gnome-helper: failed to request name: " << err.message << "\n";
            dbus_error_free(&err);
            return false;
        }
        if(ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            std::cerr << "Error: gsr-gnome-helper: not primary owner of the name\n";
            return false;
        }

        std::cerr << "Info: gsr-gnome-helper: DBus server initialized on com.dec05eba.gpu_screen_recorder\n";

        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;
        const char *source_dir =
            !inside_flatpak
            ? GNOME_EXTENSION_SOURCE_DIR
            : "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/share/gsr-ui/gnome-extension";

        const bool installed = inside_flatpak
            ? install_extension_via_host(source_dir)
            : install_extension(source_dir, get_extension_install_dir());
        if(!installed)
            std::cerr << "Warning: gsr-gnome-helper: extension install failed\n";

        // Reload so an upgraded metadata.json / extension.js takes effect without
        // requiring the user to log out. ReloadExtension is a no-op if not yet loaded.
        reload_extension();

        if(!enable_extension()) {
            // gnome-shell only scans ~/.local/share/gnome-shell/extensions at session
            // startup, and exposes no DBus rescan, so a fresh install isn't visible
            // until the next login.
            std::cerr << "Warning: gsr-gnome-helper: failed to enable gnome shell extension; "
                         "log out and back in once to activate it\n";
        }

        return true;
    }

    bool reload_extension() {
        DBusMessage *msg = dbus_message_new_method_call(
            "org.gnome.Shell.Extensions",
            "/org/gnome/Shell/Extensions",
            "org.gnome.Shell.Extensions",
            "ReloadExtension");
        if(!msg)
            return false;

        const char *uuid = EXTENSION_UUID;
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &uuid, DBUS_TYPE_INVALID);

        DBusError local_err;
        dbus_error_init(&local_err);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, msg, 3000, &local_err);
        dbus_message_unref(msg);

        if(dbus_error_is_set(&local_err)) {
            // Older shells may not implement ReloadExtension — that's fine.
            dbus_error_free(&local_err);
        }
        if(reply)
            dbus_message_unref(reply);
        return true;
    }

    bool enable_extension() {
        DBusMessage *msg = dbus_message_new_method_call(
            "org.gnome.Shell.Extensions",
            "/org/gnome/Shell/Extensions",
            "org.gnome.Shell.Extensions",
            "EnableExtension");
        if(!msg)
            return false;

        const char *uuid = EXTENSION_UUID;
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &uuid, DBUS_TYPE_INVALID);

        DBusError local_err;
        dbus_error_init(&local_err);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, msg, 3000, &local_err);
        dbus_message_unref(msg);

        bool result = false;
        if(dbus_error_is_set(&local_err)) {
            std::cerr << "Error: gsr-gnome-helper: EnableExtension: " << local_err.message << "\n";
            dbus_error_free(&local_err);
        } else if(reply) {
            DBusMessageIter args;
            if(dbus_message_iter_init(reply, &args) && dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_BOOLEAN) {
                DBusBasicValue v;
                dbus_message_iter_get_basic(&args, &v);
                result = v.bool_val;
            } else {
                result = true;
            }
        }
        if(reply)
            dbus_message_unref(reply);

        if(result)
            std::cerr << "Info: gsr-gnome-helper: gnome shell extension enabled\n";
        return result;
    }

    void run() {
        while(true) {
            dbus_connection_read_write(connection, 100);
            DBusMessage *msg = dbus_connection_pop_message(connection);
            if(!msg)
                continue;

            if(dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
                handle_introspect(msg);
            } else if(dbus_message_is_method_call(msg, "com.dec05eba.gpu_screen_recorder", "updateActiveWindow")) {
                handle_update_active_window(msg);
            }

            dbus_message_unref(msg);
        }
    }

    void handle_introspect(DBusMessage *msg) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if(!reply)
            return;

        DBusMessageIter args;
        dbus_message_iter_init_append(reply, &args);

        if(!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &INTROSPECTION_XML)) {
            dbus_message_unref(reply);
            return;
        }

        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
    }

    void handle_update_active_window(DBusMessage *msg) {
        DBusMessageIter args;
        DBusBasicValue title;
        DBusBasicValue fullscreen;
        DBusBasicValue monitorName;

        if(!dbus_message_iter_init(msg, &args)) {
            send_error_reply(msg, "No arguments provided");
            return;
        }

        if(dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            send_error_reply(msg, "Expected string argument");
            return;
        }
        dbus_message_iter_get_basic(&args, &title);

        if(!dbus_message_iter_next(&args)) {
            send_error_reply(msg, "Not enough arguments provided");
            return;
        }
        if(dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_BOOLEAN) {
            send_error_reply(msg, "Expected boolean argument");
            return;
        }
        dbus_message_iter_get_basic(&args, &fullscreen);

        if(!dbus_message_iter_next(&args)) {
            send_error_reply(msg, "Not enough arguments provided");
            return;
        }
        if(dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            send_error_reply(msg, "Expected string argument");
            return;
        }
        dbus_message_iter_get_basic(&args, &monitorName);

        if(title.str) {
            if(active_window_title != title.str) {
                active_window_title = title.str;
                std::cout << "Active window title set to: " << active_window_title << "\n";
                std::cout.flush();
            }
        } else {
            send_error_reply(msg, "Failed to read string");
            return;
        }

        if(active_window_fullscreen != fullscreen.bool_val) {
            active_window_fullscreen = fullscreen.bool_val;
            std::cout << "Active window fullscreen state set to: " << active_window_fullscreen << "\n";
            std::cout.flush();
        }

        if(monitorName.str) {
            if(active_window_monitor_name != monitorName.str) {
                active_window_monitor_name = monitorName.str;
                std::cout << "Active window monitor name set to: " << active_window_monitor_name << "\n";
                std::cout.flush();
            }
        } else {
            send_error_reply(msg, "Failed to read string");
            return;
        }

        send_success_reply(msg);
    }

    void send_success_reply(DBusMessage *msg) {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if(reply) {
            dbus_connection_send(connection, reply, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(reply);
        }
    }

    void send_error_reply(DBusMessage *msg, const char *error_msg) {
        DBusMessage *reply = dbus_message_new_error(msg, "com.dec05eba.gpu_screen_recorder.Error", error_msg);
        if(reply) {
            dbus_connection_send(connection, reply, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(reply);
        }
    }

    ~GsrGnomeHelper() {
        if(connection) {
            dbus_bus_release_name(connection, "com.dec05eba.gpu_screen_recorder", nullptr);
            dbus_connection_unref(connection);
        }
    }
};

int main() {
    GsrGnomeHelper helper;
    if(!helper.init())
        return 1;

    helper.run();
    return 0;
}
