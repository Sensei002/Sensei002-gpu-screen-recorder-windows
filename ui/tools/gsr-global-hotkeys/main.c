#include "keyboard_event.h"
#include "leds.h"

/* C stdlib */
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <stdbool.h>

/* POSIX */
#include <unistd.h>

static void usage(void) {
    fprintf(stderr, "usage: gsr-global-hotkeys [--all|--virtual|--no-grab|--set-led] [Scroll Lock] [on|off]\n");
    fprintf(stderr, "OPTIONS:\n");
    fprintf(stderr, "  --all        Grab all devices.\n");
    fprintf(stderr, "  --virtual    Grab all virtual devices only.\n");
    fprintf(stderr, "  --no-grab    Don't grab devices, only listen to them.\n");
    fprintf(stderr, "  --set-led    Turn device led on/off.\n");
    fprintf(stderr, "EXAMPLES:\n");
    fprintf(stderr, "  gsr-global-hotkeys --all\n");
    fprintf(stderr, "  gsr-global-hotkeys --set-led \"Scroll Lock\" on\n");
    fprintf(stderr, "  gsr-global-hotkeys --set-led \"Scroll Lock\" off\n");
}

static bool is_gsr_global_hotkeys_already_running(void) {
    FILE *f = fopen("/proc/bus/input/devices", "rb");
    if(!f)
        return false;

    bool virtual_keyboard_running = false;
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
        if(strstr(line, "gsr-ui virtual keyboard")) {
            virtual_keyboard_running = true;
            break;
        }
    }

    fclose(f);
    return virtual_keyboard_running;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "C"); /* Sigh... stupid C */

    keyboard_grab_type grab_type = KEYBOARD_GRAB_TYPE_ALL;
    const uid_t user_id = getuid();

    if(argc == 2) {
        const char *grab_type_arg = argv[1];
        if(strcmp(grab_type_arg, "--all") == 0) {
            grab_type = KEYBOARD_GRAB_TYPE_ALL;
        } else if(strcmp(grab_type_arg, "--virtual") == 0) {
            grab_type = KEYBOARD_GRAB_TYPE_VIRTUAL;
        } else if(strcmp(grab_type_arg, "--no-grab") == 0) {
            grab_type = KEYBOARD_GRAB_TYPE_NO_GRAB;
        } else if(strcmp(grab_type_arg, "--set-led") == 0) {
            fprintf(stderr, "Error: missing led name and on/off argument to --set-led\n");
            usage();
            return 1;
        } else {
            fprintf(stderr, "gsr-global-hotkeys error: expected --all, --virtual, --no-grab or --set-led, got %s\n", grab_type_arg);
            usage();
            return 1;
        }
    } else if(argc == 4) {
        /* It's not ideal to use gsr-global-hotkeys for leds, but we do that for now because it's a mess to create another binary for flatpak and distros */
        const char *led_name = argv[2];
        const char *led_enabled_str = argv[3];
        bool led_enabled = false;

        if(strcmp(led_enabled_str, "on") == 0) {
            led_enabled = true;
        } else if(strcmp(led_enabled_str, "off") == 0) {
            led_enabled = false;
        } else {
            fprintf(stderr, "Error: expected \"on\" or \"off\" for --set-led option, got: \"%s\"", led_enabled_str);
            usage();
            return 1;
        }

        if(geteuid() != 0) {
            if(setuid(0) == -1) {
                fprintf(stderr, "gsr-global-hotkeys error: failed to change user to root, global hotkeys will not work. Make sure to set the correct capability on gsr-global-hotkeys\n");
                return 1;
            }
        }

        const bool success = set_leds(led_name, led_enabled);
        setuid(user_id);

        return success ? 0 : 1;
    } else if(argc != 1) {
        fprintf(stderr, "gsr-global-hotkeys error: expected 0 or 1 arguments, got %d argument(s)\n", argc);
        usage();
        return 1;
    }

    if(is_gsr_global_hotkeys_already_running()) {
        fprintf(stderr, "gsr-global-hotkeys error: gsr-global-hotkeys is already running\n");
        return 1;
    }

    if(geteuid() != 0) {
        if(setuid(0) == -1) {
            fprintf(stderr, "gsr-global-hotkeys error: failed to change user to root, global hotkeys will not work. Make sure gsr-global-hotkeys has setuid capability and your root user is enabled and gsr-global-hotkeys is installed to a location mounted without nosuid\n");
            return 1;
        }
    }

    keyboard_event keyboard_ev;
    if(!keyboard_event_init(&keyboard_ev, grab_type != KEYBOARD_GRAB_TYPE_NO_GRAB, grab_type)) {
        fprintf(stderr, "gsr-global-hotkeys error: failed to setup hotplugging and no keyboard input devices were found\n");
        setuid(user_id);
        return 1;
    }

    fprintf(stderr, "gsr-global-hotkeys info: global hotkeys setup, waiting for hotkeys to be pressed\n");

    for(;;) {
        keyboard_event_poll_events(&keyboard_ev, -1);
        if(keyboard_event_stdin_has_failed(&keyboard_ev)) {
            fprintf(stderr, "gsr-global-hotkeys info: stdin closed (parent process likely closed this process), exiting...\n");
            break;
        }
    }

    keyboard_event_deinit(&keyboard_ev);
    setuid(user_id);
    return 0;
}
