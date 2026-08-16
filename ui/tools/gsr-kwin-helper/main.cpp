#include <dbus/dbus.h>
#include <unistd.h>
#include <iostream>
#include <string>

static const char* INTROSPECTION_XML =
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


class GsrKwinHelper {
public:
    std::string active_window_title;
    bool active_window_fullscreen;
    std::string active_window_monitor_name;
    DBusConnection* connection = nullptr;
    DBusError err;

    bool init() {
        dbus_error_init(&err);

        connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
        if (dbus_error_is_set(&err)) {
            std::cerr << "Error: gsr-kwin-helper: failed to connect to session bus: " << err.message << "\n";
            dbus_error_free(&err);
            return false;
        }

        if (!connection) {
            std::cerr << "Error: gsr-kwin-helper: connection is null\n";
            return false;
        }

        int ret = dbus_bus_request_name(connection, "com.dec05eba.gpu_screen_recorder",
                                       DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
        if (dbus_error_is_set(&err)) {
            std::cerr << "Error: gsr-kwin-helper: failed to request name: " << err.message << "\n";
            dbus_error_free(&err);
            return false;
        }

        if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
            std::cerr << "Error: gsr-kwin-helper: not primary owner of the name\n";
            return false;
        }

        std::cerr << "Info: gsr-kwin-helper: DBus server initialized on com.dec05eba.gpu_screen_recorder\n";

        const bool inside_flatpak = access("/app/manifest.json", F_OK) == 0;

        const char *helper_path =
            !inside_flatpak 
            ? KWIN_HELPER_SCRIPT_PATH 
            : "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/share/gsr-ui/gsrkwinhelper.js";

        std::cerr << "Info: gsr-kwin-helper: KWin script path: " << helper_path << std::endl;

        if (!load_kwin_script(connection, helper_path)) {
            std::cerr << "Warning: gsr-kwin-helper: failed to load KWin script\n";
        }

        return true;
    }

    void run() {
        while (true) {
            dbus_connection_read_write(connection, 100);
            DBusMessage* msg = dbus_connection_pop_message(connection);

            if (!msg) {
                continue;
            }

            if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect")) {
                handle_introspect(msg);
            } else if (dbus_message_is_method_call(msg, "com.dec05eba.gpu_screen_recorder", "updateActiveWindow")) {
                handle_update_active_window(msg);
            }

            dbus_message_unref(msg);
        }
    }

    void handle_introspect(DBusMessage* msg) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (!reply) return;

        DBusMessageIter args;
        dbus_message_iter_init_append(reply, &args);
        
        if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &INTROSPECTION_XML)) {
            dbus_message_unref(reply);
            return;
        }

        dbus_connection_send(connection, reply, nullptr);
        dbus_connection_flush(connection);
        dbus_message_unref(reply);
    }

    void handle_update_active_window(DBusMessage* msg) {
        DBusMessageIter args;
        DBusBasicValue title;
        DBusBasicValue fullscreen;
        DBusBasicValue monitorName;

        if (!dbus_message_iter_init(msg, &args)) {
            send_error_reply(msg, "No arguments provided");
            return;
        }

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            send_error_reply(msg, "Expected string argument");
            return;
        }

        dbus_message_iter_get_basic(&args, &title);

        if (!dbus_message_iter_next(&args)) {
            send_error_reply(msg, "Not enough arguments provided");
            return;
        }

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_BOOLEAN) {
            send_error_reply(msg, "Expected boolean argument");
            return;
        }

        dbus_message_iter_get_basic(&args, &fullscreen);

        if (!dbus_message_iter_next(&args)) {
            send_error_reply(msg, "Not enough arguments provided");
            return;
        }

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING) {
            send_error_reply(msg, "Expected string argument");
            return;
        }

        dbus_message_iter_get_basic(&args, &monitorName);
        
        if (title.str) {
            if (active_window_title != title.str) {
                active_window_title = title.str;
                std::cout << "Active window title set to: " << active_window_title << "\n";
                std::cout.flush();
            }
        } else {
            send_error_reply(msg, "Failed to read string");
            return;
        }

        if (active_window_fullscreen != fullscreen.bool_val) {
            active_window_fullscreen = fullscreen.bool_val;
            std::cout << "Active window fullscreen state set to: " << active_window_fullscreen << "\n";
            std::cout.flush();
        }

        if (monitorName.str) {
            if (active_window_monitor_name != monitorName.str) {
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

    void send_success_reply(DBusMessage* msg) {
        DBusMessage* reply = dbus_message_new_method_return(msg);
        if (reply) {
            dbus_connection_send(connection, reply, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(reply);
        }
    }

    void send_error_reply(DBusMessage* msg, const char* error_msg) {
        DBusMessage* reply = dbus_message_new_error(msg, "com.dec05eba.gpu_screen_recorder.Error", error_msg);
        if (reply) {
            dbus_connection_send(connection, reply, nullptr);
            dbus_connection_flush(connection);
            dbus_message_unref(reply);
        }
    }

    bool call_kwin_method(DBusConnection* conn, const char* method, 
                      const char* arg1 = nullptr, const char* arg2 = nullptr) {
        DBusMessage* msg = dbus_message_new_method_call(
            "org.kde.KWin",        
            "/Scripting",              
            "org.kde.kwin.Scripting", 
            method 
        );
        
        if (!msg) {
            std::cerr << "Error: gsr-kwin-helper: failed to create message for " << method << "\n";
            return false;
        }
        
        if (arg1) {
            dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg1, DBUS_TYPE_INVALID);
            if (arg2) {
                dbus_message_append_args(msg, DBUS_TYPE_STRING, &arg2, DBUS_TYPE_INVALID);
            }
        }
        
        DBusError err;
        dbus_error_init(&err);
        
        // Send message and wait for reply (with 1 second timeout)
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(conn, msg, 1000, &err);
        
        dbus_message_unref(msg);
        
        if (dbus_error_is_set(&err)) {
            std::cerr << "Error: gsr-kwin-helper: error calling " << method << ": " << err.message << "\n";
            dbus_error_free(&err);
            return false;
        }
        
        if (reply) {
            dbus_message_unref(reply);
        }
        
        return true;
    }

    bool load_kwin_script(DBusConnection* conn, const char* script_path) {
        // Unload existing script
        call_kwin_method(conn, "unloadScript", "gsrkwinhelper");
        
        if (!call_kwin_method(conn, "loadScript", script_path, "gsrkwinhelper")) {
            std::cerr << "Error: gsr-kwin-helper: failed to load KWin script\n";
            return false;
        }
        
        if (!call_kwin_method(conn, "start")) {
            std::cerr << "Error: gsr-kwin-helper: failed to start KWin script\n";
            return false;
        }
        
        std::cerr << "Info: gsr-kwin-helper: KWin script loaded and started successfully\n";
        return true;
    }

    ~GsrKwinHelper() {
        if (connection) {
            dbus_bus_release_name(connection, "com.dec05eba.gpu_screen_recorder", nullptr);
            dbus_connection_unref(connection);
        }
    }
};

int main() {
    GsrKwinHelper helper;
    
    if (!helper.init()) {
        return 1;
    }

    helper.run();
    
    return 0;
}
