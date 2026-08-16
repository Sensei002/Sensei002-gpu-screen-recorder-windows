/* tests/audio-list-test/main.c — Phase 8, milestone B: the Windows audio
 * listing contract that backs `--list-audio-devices` and
 * `--list-application-audio`, plus the per-app-audio decision surface.
 *
 * Upstream dispatches those flags from args_parser.c into cli/commands.c
 * (not built on Windows — the UI lands in Phase 10). The port-owned
 * equivalents live behind platform/include/audio.h and are what the UI
 * will call:
 *
 *   - gsr_platform_audio_list_devices: WASAPI endpoints (aliases first:
 *     default_output/default_input, then every ACTIVE endpoint) — the data
 *     behind `--list-audio-devices`.
 *   - gsr_platform_audio_format_device_line: the "name (description)"
 *     line format.
 *   - gsr_platform_audio_list_apps: audio sessions on the default render
 *     endpoint (IAudioSessionManager2) — the data behind
 *     `--list-application-audio`.
 *   - gsr_platform_audio_notification_smoke_test: the device-change
 *     notification plumbing round-trips (register + unregister).
 *
 * Per-app audio capture (-a app:NAME) is NOT feasible with WASAPI:
 * loopback is endpoint-wide and there is no per-session capture client.
 * The engine already returns GSR_ERROR_UNSUPPORTED for app tracks (the
 * GSR_APP_AUDIO path is upstream's pipewire build, not defined here) —
 * this test pins the parse side (app: names parse to APPLICATION tracks,
 * which the recorder then rejects honestly) and the decision is
 * documented in docs/upstream-porting-notes.md §3k.
 *
 * The CI runner has no audio endpoints, so the live enumeration returns
 * empty lists here; the tests assert the CONTRACT (shape, formats,
 * graceful empties, honest failures), driven with synthetic data where
 * the contract is about formatting.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "audio_wasapi_internal.h" /* notification smoke test */
#include "../../upstream/include/recorder/audio_input.h"
#include "../../upstream/include/sound.h"

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) \
    do { \
        ++num_checks; \
        if(!(cond)) { \
            ++num_failures; \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static void check_format_line(const gsr_platform_audio_device *device, const char *expected) {
    char buf[512];
    ++num_checks;
    if(gsr_platform_audio_format_device_line(device, buf, sizeof(buf)) != (int)strlen(expected) || strcmp(buf, expected) != 0) {
        ++num_failures;
        fprintf(stderr, "FAIL: format line got '%s', expected '%s'\n", buf, expected);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("audio-list-test: device/app listing contract (headless)\n");

    /* 1. The format contract, synthetic. */
    printf("-- format line\n");
    gsr_platform_audio_device device;
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "default_output");
    snprintf(device.description, sizeof(device.description), "Default output");
    device.direction = GSR_PLATFORM_AUDIO_DIRECTION_OUTPUT;
    device.is_default = true;
    check_format_line(&device, "default_output (Default output)");

    snprintf(device.name, sizeof(device.name), "{0.0.0.00000000}.{abc}");
    snprintf(device.description, sizeof(device.description), "Built-in Speakers");
    check_format_line(&device, "{0.0.0.00000000}.{abc} (Built-in Speakers)");

    /* Buffer-too-small must return -1 (the header contract). */
    char tiny[8];
    ++num_checks;
    if(gsr_platform_audio_format_device_line(&device, tiny, sizeof(tiny)) != -1) {
        ++num_failures;
        fprintf(stderr, "FAIL: too-small buffer should return -1\n");
    }

    /* 2. Device listing: must succeed and never fabricate entries. */
    printf("-- device listing\n");
    gsr_platform_audio_device *devices = NULL;
    int device_count = 0;
    const bool listed = gsr_platform_audio_list_devices(&devices, &device_count);
    CHECK(listed);
    if(listed) {
        printf("audio-list: %d device(s)\n", device_count);
        CHECK(device_count >= 0);
        bool saw_alias_default = false;
        for(int i = 0; i < device_count; ++i) {
            CHECK(devices[i].name[0] != '\0');
            CHECK(devices[i].description[0] != '\0');
            if(strcmp(devices[i].name, "default_output") == 0 || strcmp(devices[i].name, "default_input") == 0) {
                CHECK(devices[i].is_default);
                saw_alias_default = true;
                /* The aliases must format per contract. */
                char buf[512];
                CHECK(gsr_platform_audio_format_device_line(&devices[i], buf, sizeof(buf)) > 0);
            }
            /* The engine's -a validation matches on the name; the names
               must therefore be exactly what the engine compares against. */
            CHECK(strchr(devices[i].name, ' ') == NULL); /* no spaces in names */
        }
        /* Default aliases are only present when a default exists — on the
           runner (no audio) they are legitimately absent. */
        (void)saw_alias_default;
    }
    free(devices);

    /* 3. Application sessions: the `--list-application-audio` data. */
    printf("-- application sessions\n");
    gsr_platform_audio_app *apps = NULL;
    int app_count = 0;
    const bool apps_listed = gsr_platform_audio_list_apps(&apps, &app_count);
    CHECK(apps_listed);
    if(apps_listed) {
        printf("audio-list: %d application session(s)\n", app_count);
        for(int i = 0; i < app_count; ++i) {
            CHECK(apps[i].name[0] != '\0');
            CHECK(apps[i].state >= 0 && apps[i].state <= 2);
            printf("  - %s (pid %lu, state %d)\n", apps[i].name, apps[i].pid, apps[i].state);
        }
    }
    gsr_platform_audio_apps_free(apps);

    /* 4. Device-change notification plumbing: register/unregister. */
    printf("-- device-change notifications\n");
    const bool notif_ok = gsr_platform_audio_notification_smoke_test();
    CHECK(notif_ok);
    printf("audio-list: notification register/unregister %s\n", notif_ok ? "OK" : "FAILED");

    /* 5. Per-app audio parse surface: app: names parse to APPLICATION
       tracks (which the engine rejects on Windows via GSR_APP_AUDIO being
       undefined — the honest unsupported path). */
    printf("-- per-app audio (-a app:NAME)\n");
    gsr_merged_audio_inputs merged;
    CHECK(gsr_merged_audio_inputs_parse(&merged, "app:Spotify") == 0);
    CHECK(merged.num_items == 1);
    CHECK(merged.items[0].type == GSR_AUDIO_INPUT_TYPE_APPLICATION);
    CHECK(strcmp(merged.items[0].name, "Spotify") == 0);
    CHECK(gsr_audio_inputs_has_app_audio(&merged));
    gsr_merged_audio_inputs_deinit(&merged);

    gsr_merged_audio_inputs mixed;
    CHECK(gsr_merged_audio_inputs_parse(&mixed, "default_output|app:Chrome") == 0);
    CHECK(mixed.num_items == 2);
    CHECK(mixed.items[0].type == GSR_AUDIO_INPUT_TYPE_DEVICE);
    CHECK(mixed.items[1].type == GSR_AUDIO_INPUT_TYPE_APPLICATION);
    CHECK(gsr_audio_inputs_has_app_audio(&mixed));
    gsr_merged_audio_inputs_deinit(&mixed);

    /* app-inverse parses too (and the recorder rejects both the same way). */
    gsr_merged_audio_inputs inverse;
    CHECK(gsr_merged_audio_inputs_parse(&inverse, "app-inverse:Game.exe") == 0);
    CHECK(inverse.num_items == 1);
    CHECK(inverse.items[0].type == GSR_AUDIO_INPUT_TYPE_APPLICATION);
    CHECK(inverse.items[0].inverted);
    gsr_merged_audio_inputs_deinit(&inverse);

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    if(num_failures > 0) {
        fprintf(stderr, "FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
