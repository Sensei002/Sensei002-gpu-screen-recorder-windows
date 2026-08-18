/* tests/platform-test/main.c — unit tests for the Phase 3 platform layer.
 *
 * Exercises the port-owned interfaces in platform/include/ that are pure
 * logic and therefore fully testable headless on windows-latest:
 *
 *   - filesystem: Windows filename sanitization, path joining, UTF-8/UTF-16
 *     conversion, the Videos folder, and the save-filepath naming contract
 *     against the REAL upstream function (recorder/muxer.c);
 *   - IPC protocol codec: request/reply JSON wire format byte-identical to
 *     upstream (src/cli/ipc.c) and the deferred-request state machine;
 *   - config: schema-driven config_ui key=value round-trip + validation;
 *   - codec caps: capability table -> available -k options + encoder
 *     fallback decision (no GPU needed, brief §64);
 *   - display/info/audio/capture/time helpers: the `--list-monitors` and
 *     `--info` line formats, backend selection, device line, clocks;
 *   - display (Phase 4): DXGI monitor enumeration (headless smoke test on
 *     the runner's virtual display) + the pure name-mapping / rotation /
 *     vendor logic that the capture backends (Phases 5/6) will use to
 *     resolve -w monitor arguments.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "filesystem.h"
#include "ipc.h"
#include "config.h"
#include "codec_caps.h"
#include "display.h"
#include "audio.h"
#include "capture.h"
#include "gsr_time.h"
#include "thread.h"

#include "../../upstream/include/utils.h" /* gsr_get_date_only_str, create_directory_recursive, clock_get_monotonic_seconds */
#include "../../upstream/include/recorder/muxer.h" /* gsr_create_new_recording_filepath_from_timestamp */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <direct.h> /* _rmdir */

static int num_failures = 0;
static int num_checks = 0;

/* Crash checkpoint: printed to stderr (unbuffered) so it survives a
   segfault even when stdout is lost. Temporary Phase 3 CI diagnostics. */
#define NOTE(...) fprintf(stderr, "NOTE " __VA_ARGS__)

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

/* ------------------------------------------------------------- filesystem */

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* Validates the "YYYY-MM-DD_HH-MM-SS" shape (19 chars). */
static bool is_timestamp(const char *str) {
    if(strlen(str) != 19)
        return false;
    for(int i = 0; i < 19; ++i) {
        if(i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
            continue; /* separators */
        if(!is_digit(str[i]))
            return false;
    }
    return str[4] == '-' && str[7] == '-' && str[10] == '_' && str[13] == '-' && str[16] == '-';
}

static void test_filesystem(void) {
    printf("-- filesystem\n");
    NOTE("filesystem: start\n");

    /* Windows-invalid characters -> '_' */
    char out[128];
    CHECK(gsr_platform_path_sanitize_filename("a<b>c:d\"e/f\\g|h?i*j", out, sizeof(out)));
    CHECK(strcmp(out, "a_b_c_d_e_f_g_h_i_j") == 0);

    /* Control characters -> '_' (split the hex escapes so the following
       'b' is not swallowed into a single out-of-range hex value) */
    CHECK(gsr_platform_path_sanitize_filename("a\x01\x1f" "b", out, sizeof(out)));
    CHECK(strcmp(out, "a__b") == 0);

    /* Trailing dots/spaces are trimmed */
    CHECK(gsr_platform_path_sanitize_filename("name.", out, sizeof(out)));
    CHECK(strcmp(out, "name") == 0);
    CHECK(gsr_platform_path_sanitize_filename("name ", out, sizeof(out)));
    CHECK(strcmp(out, "name") == 0);
    CHECK(gsr_platform_path_sanitize_filename("name.. ", out, sizeof(out)));
    CHECK(strcmp(out, "name") == 0);

    /* Reserved device names (with and without extension) */
    CHECK(gsr_platform_path_sanitize_filename("CON", out, sizeof(out)));
    CHECK(strcmp(out, "_CON") == 0);
    CHECK(gsr_platform_path_sanitize_filename("con.mp4", out, sizeof(out)));
    CHECK(strcmp(out, "_con.mp4") == 0);
    CHECK(gsr_platform_path_sanitize_filename("COM1", out, sizeof(out)));
    CHECK(strcmp(out, "_COM1") == 0);
    CHECK(gsr_platform_path_sanitize_filename("nul", out, sizeof(out)));
    CHECK(strcmp(out, "_nul") == 0);
    CHECK(gsr_platform_path_sanitize_filename("LPT9.txt", out, sizeof(out)));
    CHECK(strcmp(out, "_LPT9.txt") == 0);

    /* Near-misses are fine */
    CHECK(gsr_platform_path_sanitize_filename("console", out, sizeof(out)));
    CHECK(strcmp(out, "console") == 0);
    CHECK(gsr_platform_path_sanitize_filename("com10", out, sizeof(out)));
    CHECK(strcmp(out, "com10") == 0);

    /* All-invalid name -> false with empty output */
    CHECK(!gsr_platform_path_sanitize_filename("...", out, sizeof(out)));
    CHECK(out[0] == '\0');

    /* UTF-8 content is preserved byte-for-byte */
    CHECK(gsr_platform_path_sanitize_filename("Caf\u00e9 \u2014 \u65e5\u672c\u8a9e", out, sizeof(out)));
    CHECK(strcmp(out, "Caf\u00e9 \u2014 \u65e5\u672c\u8a9e") == 0);
    NOTE("filesystem: sanitize done\n");

    /* Path joining */
    CHECK(gsr_platform_path_join("C:/foo", "bar", out, sizeof(out)));
    CHECK(strcmp(out, "C:/foo/bar") == 0);
    CHECK(gsr_platform_path_join("C:/foo/", "bar", out, sizeof(out)));
    CHECK(strcmp(out, "C:/foo/bar") == 0);
    CHECK(gsr_platform_path_join("C:/foo", "/bar", out, sizeof(out)));
    CHECK(strcmp(out, "C:/foo/bar") == 0);
    CHECK(gsr_platform_path_join("", "bar", out, sizeof(out)));
    CHECK(strcmp(out, "bar") == 0);
    CHECK(gsr_platform_path_join("C:/foo", "", out, sizeof(out)));
    CHECK(strcmp(out, "C:/foo") == 0);
    /* "a/b" + NUL is exactly 4 bytes, so a 4-byte buffer fits; 3 does not. */
    CHECK(!gsr_platform_path_join("a", "b", out, 3)); /* too small */

    NOTE("filesystem: path-join done\n");

    /* UTF-8 <-> UTF-16 round-trip */
    wchar_t wide[128];
    CHECK(gsr_platform_utf8_to_wide("Caf\u00e9 \u2014 \u65e5\u672c\u8a9e", wide, 128));
    char utf8[128];
    CHECK(gsr_platform_wide_to_utf8(wide, utf8, sizeof(utf8)));
    CHECK(strcmp(utf8, "Caf\u00e9 \u2014 \u65e5\u672c\u8a9e") == 0);
    /* Invalid UTF-8 is rejected */
    CHECK(!gsr_platform_utf8_to_wide("\xff\xfe", wide, 128));
    NOTE("filesystem: utf8 done\n");

    /* Videos dir: non-empty, looks absolute (drive letter or UNC). */
    char videos_dir[512];
    CHECK(gsr_platform_get_videos_dir(videos_dir, sizeof(videos_dir)));
    CHECK(strlen(videos_dir) > 2);
    CHECK(isalpha((unsigned char)videos_dir[0]) && videos_dir[1] == ':');

    /* Save-filepath naming through the real upstream function */
    char filepath[PATH_MAX];
    CHECK(gsr_platform_create_recording_filepath(filepath, sizeof(filepath), "test-save-path", "Replay", "mp4", false));
    const char *base = strrchr(filepath, '/');
    CHECK(base != NULL);
    ++base;
    CHECK(strncmp(base, "Replay_", 7) == 0);
    char timestamp[32];
    memcpy(timestamp, base + 7, 19);
    timestamp[19] = '\0';
    CHECK(is_timestamp(timestamp));
    CHECK(gsr_string_ends_with(base, ".mp4"));
    remove(filepath);
    _rmdir("test-save-path");
    NOTE("filesystem: save-filepath done\n");

    /* -df date folders: <dir>/YYYY-MM-DD/Replay_HH-MM-SS.ext */
    char date_only[32];
    gsr_get_date_only_str(date_only, sizeof(date_only));
    NOTE("filesystem: -df date_only done\n");
    CHECK(gsr_platform_create_recording_filepath(filepath, sizeof(filepath), "test-save-path", "Replay", "mp4", true));
    NOTE("filesystem: -df create done\n");
    char expected_dir[PATH_MAX];
    snprintf(expected_dir, sizeof(expected_dir), "test-save-path/%s", date_only);
    NOTE("filesystem: -df expected_dir done\n");
    CHECK(gsr_string_starts_with(filepath, strlen(filepath), expected_dir));
    NOTE("filesystem: -df starts_with done\n");
    CHECK(strchr(filepath + strlen(expected_dir), '/') != NULL);
    NOTE("filesystem: -df strchr done\n");
    struct stat st;
    CHECK(stat(expected_dir, &st) == 0 && S_ISDIR(st.st_mode));
    NOTE("filesystem: -df stat done\n");
    remove(filepath);
    _rmdir(expected_dir);
    _rmdir("test-save-path");
    NOTE("filesystem: -df cleanup done\n");
}

/* ------------------------------------------------------------- ipc codec */

static void test_ipc_protocol(void) {
    printf("-- ipc protocol\n");

    char buf[512];

    /* Request with data (byte-identical to upstream gsr-cli) */
    const int n = gsr_platform_ipc_build_request(buf, sizeof(buf), 1, "save-replay", "{\"seconds\":30}");
    const char *expected = "{\"id\":1,\"name\":\"save-replay\",\"data\":{\"seconds\":30}}\n";
    CHECK(n == (int)strlen(expected));
    CHECK(strcmp(buf, expected) == 0);

    /* Request without data */
    const char *expected2 = "{\"id\":7,\"name\":\"toggle-pause\"}\n";
    CHECK(gsr_platform_ipc_build_request(buf, sizeof(buf), 7, "toggle-pause", NULL) == (int)strlen(expected2));
    CHECK(strcmp(buf, expected2) == 0);

    /* Parse back the newline-terminated request (sj skips trailing ws) */
    int64_t id = 0;
    char name[64];
    bool has_data = false;
    char error[256];
    CHECK(gsr_platform_ipc_parse_request((char*)expected, strlen(expected), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(id == 1);
    CHECK(strcmp(name, "save-replay") == 0);
    CHECK(has_data);

    CHECK(gsr_platform_ipc_parse_request((char*)expected2, strlen(expected2), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(id == 7);
    CHECK(strcmp(name, "toggle-pause") == 0);
    CHECK(!has_data);

    /* Validation errors match upstream's messages */
    const char *req_missing_id = "{}\n";
    CHECK(!gsr_platform_ipc_parse_request((char*)req_missing_id, strlen(req_missing_id), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(strstr(error, "'id'") != NULL);

    const char *req_bad_id = "{\"id\":\"x\",\"name\":\"stop\"}\n";
    CHECK(!gsr_platform_ipc_parse_request((char*)req_bad_id, strlen(req_bad_id), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(strstr(error, "integer") != NULL);

    const char *req_missing_name = "{\"id\":1}\n";
    CHECK(!gsr_platform_ipc_parse_request((char*)req_missing_name, strlen(req_missing_name), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(strstr(error, "'name'") != NULL);

    const char *req_bad_name = "{\"id\":1,\"name\":5}\n";
    CHECK(!gsr_platform_ipc_parse_request((char*)req_bad_name, strlen(req_bad_name), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(strstr(error, "string") != NULL);

    const char *req_not_json = "not json\n";
    CHECK(!gsr_platform_ipc_parse_request((char*)req_not_json, strlen(req_not_json), &id, name, sizeof(name), &has_data, error, sizeof(error)));
    CHECK(strstr(error, "json object") != NULL);

    /* Replies: bare ok */
    const char *reply_ok = "{\"id\":1,\"result\":\"ok\"}\n";
    CHECK(gsr_platform_ipc_build_reply(buf, sizeof(buf), 1, true, NULL) == (int)strlen(reply_ok));
    CHECK(strcmp(buf, reply_ok) == 0);

    /* Reply with a filepath (data is JSON-escaped) */
    const char *reply_data = "{\"id\":1,\"result\":\"ok\",\"data\":\"C:\\\\Users\\\\a\\\\Replay_2026-08-05_14-04-22.mp4\"}\n";
    CHECK(gsr_platform_ipc_build_reply(buf, sizeof(buf), 1, true, "C:\\Users\\a\\Replay_2026-08-05_14-04-22.mp4") == (int)strlen(reply_data));
    CHECK(strcmp(buf, reply_data) == 0);

    /* Error reply: message goes in data */
    const char *reply_err = "{\"id\":1,\"result\":\"error\",\"data\":\"boom\"}\n";
    CHECK(gsr_platform_ipc_build_reply(buf, sizeof(buf), 1, false, "boom") == (int)strlen(reply_err));
    CHECK(strcmp(buf, reply_err) == 0);

    /* Deferred request name mapping */
    CHECK(gsr_platform_ipc_deferred_type_from_request_name("stop") == GSR_PLATFORM_IPC_DEFERRED_STOP);
    CHECK(gsr_platform_ipc_deferred_type_from_request_name("save-replay") == GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY);
    CHECK(gsr_platform_ipc_deferred_type_from_request_name("stop-replay-recording") == GSR_PLATFORM_IPC_DEFERRED_STOP_REPLAY_RECORDING);
    CHECK(gsr_platform_ipc_deferred_type_from_request_name("set-paused") == GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT);

    /* Deferred state machine */
    gsr_platform_ipc_deferred_request requests[GSR_PLATFORM_IPC_DEFERRED_TYPE_COUNT] = {0};
    CHECK(gsr_platform_ipc_deferred_set_pending(requests, GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY, 42));
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].state == GSR_PLATFORM_IPC_DEFERRED_STATE_PENDING);
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].request_id == 42);
    CHECK(!gsr_platform_ipc_deferred_set_pending(requests, GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY, 43)); /* already pending */
    gsr_platform_ipc_deferred_set_completed(requests, GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY, true, "out.mp4");
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].state == GSR_PLATFORM_IPC_DEFERRED_STATE_COMPLETED);
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].success);
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].has_filepath);
    CHECK(strcmp(requests[GSR_PLATFORM_IPC_DEFERRED_SAVE_REPLAY].filepath, "out.mp4") == 0);

    /* A different deferred type is independent */
    CHECK(gsr_platform_ipc_deferred_set_pending(requests, GSR_PLATFORM_IPC_DEFERRED_STOP, 7));
    gsr_platform_ipc_deferred_set_completed(requests, GSR_PLATFORM_IPC_DEFERRED_STOP, false, NULL);
    CHECK(requests[GSR_PLATFORM_IPC_DEFERRED_STOP].state == GSR_PLATFORM_IPC_DEFERRED_STATE_COMPLETED);
    CHECK(!requests[GSR_PLATFORM_IPC_DEFERRED_STOP].success);
    CHECK(!requests[GSR_PLATFORM_IPC_DEFERRED_STOP].has_filepath);
}

/* ---------------------------------------------------------------- config */

static void test_config(void) {
    printf("-- config\n");

    size_t num_options = 0;
    const gsr_config_option *schema = gsr_config_get_ui_schema(&num_options);
    CHECK(schema != NULL);
    CHECK(num_options > 10);

    CHECK(create_directory_recursive("test-config") == 0);

    gsr_config config;
    CHECK(gsr_config_init(&config, schema, num_options));

    /* Defaults */
    int64_t int_value = 0;
    bool bool_value = false;
    const char *string_value = NULL;
    CHECK(gsr_config_get_int(&config, "main.config_file_version", &int_value) && int_value == 1);
    CHECK(gsr_config_get_bool(&config, "main.hotkeys_enable_option", &bool_value) && bool_value);
    CHECK(gsr_config_get_string(&config, "main.show_hide_hotkey", &string_value) && strcmp(string_value, "alt+z") == 0);
    CHECK(gsr_config_get_string(&config, "replay.replay_storage", &string_value) && strcmp(string_value, "ram") == 0);

    /* Setters */
    CHECK(gsr_config_set_int(&config, "record.fps", 120));
    CHECK(!gsr_config_set_int(&config, "record.fps", 99999)); /* out of range */
    CHECK(!gsr_config_set_string(&config, "replay.replay_storage", "banana")); /* not an allowed value */
    CHECK(gsr_config_set_string(&config, "replay.replay_storage", "disk"));
    CHECK(gsr_config_set_bool(&config, "main.exclude_metadata", true));
    CHECK(gsr_config_set_string(&config, "record.save_directory", "C:\\Videos\\My=Folder")); /* '=' in value */

    /* Save + round-trip */
    CHECK(gsr_config_save(&config, "test-config/config_ui"));
    gsr_config config2;
    CHECK(gsr_config_init(&config2, schema, num_options));
    int errors = -1;
    CHECK(gsr_config_load(&config2, "test-config/config_ui", &errors));
    CHECK(errors == 0);
    CHECK(gsr_config_get_int(&config2, "record.fps", &int_value) && int_value == 120);
    CHECK(gsr_config_get_string(&config2, "replay.replay_storage", &string_value) && strcmp(string_value, "disk") == 0);
    CHECK(gsr_config_get_bool(&config2, "main.exclude_metadata", &bool_value) && bool_value);
    CHECK(gsr_config_get_string(&config2, "record.save_directory", &string_value) && strcmp(string_value, "C:\\Videos\\My=Folder") == 0);
    /* The failed setter must not have changed the value */
    CHECK(gsr_config_get_int(&config2, "record.fps", &int_value) && int_value == 120);

    /* Missing file = defaults, not an error */
    gsr_config config3;
    CHECK(gsr_config_init(&config3, schema, num_options));
    CHECK(gsr_config_load(&config3, "test-config/does-not-exist", NULL));
    CHECK(gsr_config_get_string(&config3, "replay.replay_storage", &string_value) && strcmp(string_value, "ram") == 0);

    /* Out-of-range + invalid + unknown + malformed lines (CRLF endings) */
    FILE *f = fopen("test-config/config_ui", "w");
    CHECK(f != NULL);
    fprintf(f, "record.fps=99999\r\n");          /* out of range -> error, keep default */
    fprintf(f, "replay.replay_storage=banana\r\n"); /* not an allowed value -> error */
    fprintf(f, "main.bogus_key=1\r\n");          /* unknown -> skipped silently */
    fprintf(f, "no_equals_sign\r\n");            /* malformed -> skipped */
    fclose(f);
    gsr_config config4;
    CHECK(gsr_config_init(&config4, schema, num_options));
    errors = 0;
    CHECK(gsr_config_load(&config4, "test-config/config_ui", &errors));
    CHECK(errors == 2);
    CHECK(gsr_config_get_int(&config4, "record.fps", &int_value) && int_value == 60); /* default kept */
    CHECK(gsr_config_get_string(&config4, "replay.replay_storage", &string_value) && strcmp(string_value, "ram") == 0);

    gsr_config_deinit(&config4);
    gsr_config_deinit(&config3);
    gsr_config_deinit(&config2);
    gsr_config_deinit(&config);
    remove("test-config/config_ui");
    _rmdir("test-config");
}

/* ------------------------------------------------------------ codec caps */

static void test_codec_caps(void) {
    printf("-- codec caps\n");

    /* -k option names */
    CHECK(strcmp(gsr_platform_codec_to_k_option(GSR_PLATFORM_CODEC_H264), "h264") == 0);
    CHECK(strcmp(gsr_platform_codec_to_k_option(GSR_PLATFORM_CODEC_HEVC_HDR), "hevc_hdr") == 0);
    CHECK(strcmp(gsr_platform_codec_to_k_option(GSR_PLATFORM_CODEC_AV1_10BIT), "av1_10bit") == 0);

    gsr_platform_codec list[16];

    /* No GPU at all: only h264 (software libx264) */
    gsr_supported_video_codecs none = {0};
    CHECK(gsr_platform_codec_build_available_list(&none, true, list, 16) == 1);
    CHECK(list[0] == GSR_PLATFORM_CODEC_H264);

    /* Hardware encoding off: h264 only, even with a full GPU */
    gsr_supported_video_codecs full = {
        .h264 = { true, false, {3840, 2160} },
        .hevc = { true, false, {3840, 2160} },
        .hevc_hdr = { true, false, {3840, 2160} },
        .hevc_10bit = { true, false, {3840, 2160} },
        .av1 = { true, false, {3840, 2160} },
        .av1_hdr = { true, false, {3840, 2160} },
        .av1_10bit = { true, false, {3840, 2160} },
    };
    CHECK(gsr_platform_codec_build_available_list(&full, false, list, 16) == 1);
    CHECK(list[0] == GSR_PLATFORM_CODEC_H264);

    /* Full modern NVENC: h264, hevc(+hdr/10bit), av1(+hdr/10bit), no vp8/vp9 */
    const int full_count = gsr_platform_codec_build_available_list(&full, true, list, 16);
    CHECK(full_count == 7);
    CHECK(list[0] == GSR_PLATFORM_CODEC_H264);
    CHECK(list[1] == GSR_PLATFORM_CODEC_HEVC);
    CHECK(list[2] == GSR_PLATFORM_CODEC_HEVC_HDR);
    CHECK(list[3] == GSR_PLATFORM_CODEC_HEVC_10BIT);
    CHECK(list[4] == GSR_PLATFORM_CODEC_AV1);
    CHECK(list[5] == GSR_PLATFORM_CODEC_AV1_HDR);
    CHECK(list[6] == GSR_PLATFORM_CODEC_AV1_10BIT);

    /* HDR gating: 8-bit HEVC-only GPU must not offer hevc_hdr/hevc_10bit */
    gsr_supported_video_codecs hevc8 = {
        .h264 = { false, false, {0, 0} },
        .hevc = { true, false, {1920, 1080} },
    };
    const int hevc8_count = gsr_platform_codec_build_available_list(&hevc8, true, list, 16);
    CHECK(hevc8_count == 2); /* h264 (software) + hevc */
    CHECK(list[1] == GSR_PLATFORM_CODEC_HEVC);

    /* Max resolution propagates through the probe data */
    CHECK(full.av1.max_resolution.x == 3840 && full.av1.max_resolution.y == 2160);

    /* Encoder selection */
    CHECK(gsr_platform_encoder_select(true, true, false, true) == GSR_PLATFORM_ENCODER_GPU);
    CHECK(gsr_platform_encoder_select(true, false, true, true) == GSR_PLATFORM_ENCODER_CPU);   /* fallback */
    CHECK(gsr_platform_encoder_select(true, false, false, true) == GSR_PLATFORM_ENCODER_NONE); /* no fallback */
    CHECK(gsr_platform_encoder_select(true, false, true, false) == GSR_PLATFORM_ENCODER_NONE); /* no cpu either */
    CHECK(gsr_platform_encoder_select(false, true, false, true) == GSR_PLATFORM_ENCODER_CPU);
    CHECK(gsr_platform_encoder_select(false, false, false, false) == GSR_PLATFORM_ENCODER_NONE);
}

/* ------------------------------------------------- display/info/audio/time */

static void test_display_and_misc(void) {
    printf("-- display/info/misc\n");

    char buf[256];

    /* --list-monitors line: name|WxH */
    gsr_platform_monitor monitor = {
        .name = "\\\\.\\DISPLAY1",
        .width = 1920,
        .height = 1080,
    };
    CHECK(gsr_platform_display_format_monitor_line(&monitor, buf, sizeof(buf)) == (int)strlen("\\\\.\\DISPLAY1|1920x1080"));
    CHECK(strcmp(buf, "\\\\.\\DISPLAY1|1920x1080") == 0);

    /* --info section and key|value lines */
    CHECK(gsr_platform_info_write_section(buf, sizeof(buf), "system_info") == (int)strlen("section=system_info\n"));
    CHECK(strcmp(buf, "section=system_info\n") == 0);
    CHECK(gsr_platform_info_write_key_value(buf, sizeof(buf), "video_codecs", "h264") == (int)strlen("video_codecs|h264\n"));
    CHECK(strcmp(buf, "video_codecs|h264\n") == 0);

    /* Audio device line: name (description) */
    gsr_platform_audio_device device = {
        .name = "Default Output",
        .description = "Built-in Speakers",
        .direction = GSR_PLATFORM_AUDIO_DIRECTION_OUTPUT,
        .is_default = true,
    };
    CHECK(gsr_platform_audio_format_device_line(&device, buf, sizeof(buf)) == (int)strlen("Default Output (Built-in Speakers)"));
    CHECK(strcmp(buf, "Default Output (Built-in Speakers)") == 0);

    /* Capture backend selection + names */
    CHECK(gsr_platform_capture_select_backend(true, true) == GSR_CAPTURE_BACKEND_WGC);
    CHECK(gsr_platform_capture_select_backend(true, false) == GSR_CAPTURE_BACKEND_WGC);
    CHECK(gsr_platform_capture_select_backend(false, true) == GSR_CAPTURE_BACKEND_DXGI_DUPLICATION);
    CHECK(strcmp(gsr_platform_capture_backend_name(GSR_CAPTURE_BACKEND_WGC), "Windows Graphics Capture") == 0);
    CHECK(strcmp(gsr_platform_capture_backend_name(GSR_CAPTURE_BACKEND_DXGI_DUPLICATION), "Desktop Duplication") == 0);

    /* Phase 6: DXGI rotation mapping (pure). DXGI_MODE_ROTATION values:
       UNSPECIFIED=0, IDENTITY=1, ROTATE90=2, ROTATE180=3, ROTATE270=4. */
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(1) == GSR_PLATFORM_WGC_ROT_0);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(2) == GSR_PLATFORM_WGC_ROT_90);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(3) == GSR_PLATFORM_WGC_ROT_180);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(4) == GSR_PLATFORM_WGC_ROT_270);
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(0) == GSR_PLATFORM_WGC_ROT_0);  /* UNSPECIFIED */
    CHECK(gsr_platform_dxgi_rotation_from_dxgi(99) == GSR_PLATFORM_WGC_ROT_0); /* garbage */
    CHECK(gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_90));
    CHECK(gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_270));
    CHECK(!gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_0));
    CHECK(!gsr_platform_dxgi_rotation_swaps_size(GSR_PLATFORM_WGC_ROT_180));

    /* Clocks */
    const int64_t ns1 = gsr_platform_time_monotonic_ns();
    CHECK(ns1 > 0);
    CHECK(gsr_platform_time_wall_clock_ms() > 0);
    const double secs = gsr_platform_time_monotonic_seconds();
    CHECK(secs > 0.0);
    const double ns_as_secs = (double)ns1 / 1000000000.0;
    CHECK(fabs(ns_as_secs - secs) < 0.01); /* same time base */
    Sleep(5);
    CHECK(gsr_platform_time_monotonic_ns() > ns1); /* advances */

    /* Thread naming must not crash (no-op on old systems) */
    gsr_platform_thread_set_current_name("platform-test");
}

/* ------------------------------------------------------- display logic (Phase 4) */

/* Device names contain backslashes ("\\.\DISPLAY1"), which are painful to
   spell in C string literals; the tests build names at runtime from the
   structs instead (strcpy is fine here — the source arrays are fixed-size). */
static void test_display_logic(void) {
    printf("-- display logic\n");

    /* Synthetic monitor list (what the Phase 4 DXGI enumeration fills). */
    gsr_platform_monitor monitors[3] = {
        {
            .name = "\\\\.\\DISPLAY1",
            .friendly_name = "DELL U2720Q",
            .width = 1920, .height = 1080, .rotation_degrees = 0,
        },
        {
            .name = "\\\\.\\DISPLAY2",
            .friendly_name = "Generic PnP Monitor",
            .width = 3840, .height = 2160, .rotation_degrees = 90,
        },
        {
            .name = "\\\\.\\DISPLAY3",
            .friendly_name = "",
            .width = 1920, .height = 1080, .rotation_degrees = 270,
        },
    };

    /* find_monitor: canonical device name, case-insensitive */
    char lower[64];
    strcpy(lower, monitors[0].name);
    for(size_t i = 0; lower[i]; ++i)
        lower[i] = (char)tolower((unsigned char)lower[i]);
    CHECK(gsr_platform_display_find_monitor(monitors, 3, lower) == 0);
    CHECK(gsr_platform_display_find_monitor(monitors, 3, monitors[1].name) == 1);
    /* DXGI's DXGI_OUTPUT_DESC.DeviceName emits the device name with a single
       leading backslash pair ("\\.\DISPLAY1") while GetMonitorInfoW returns
       the canonical "\\.\DISPLAY1"; the lookup must treat both as equal
       (this is the bug that made "display not found" / record fail). */
    char dxgi_form[64];
    strcpy(dxgi_form, monitors[0].name); /* "\\.\DISPLAY1" */
    memmove(dxgi_form, dxgi_form + 1, strlen(dxgi_form)); /* "\\.\DISPLAY1" */
    CHECK(gsr_platform_display_find_monitor(monitors, 3, dxgi_form) == 0);
    /* friendly name, case-insensitive */
    CHECK(gsr_platform_display_find_monitor(monitors, 3, "dell u2720q") == 0);
    CHECK(gsr_platform_display_find_monitor(monitors, 3, "GENERIC PNP MONITOR") == 1);
    /* no match (incl. upstream-style DRM connector names: no Windows
       equivalent unless a device/friendly name matches) */
    char bogus[64];
    strcpy(bogus, monitors[0].name);
    bogus[strlen(bogus) - 1] = '9'; /* "\\.\DISPLAY9" */
    CHECK(gsr_platform_display_find_monitor(monitors, 3, bogus) == -1);
    CHECK(gsr_platform_display_find_monitor(monitors, 3, "DP-1") == -1);
    /* empty friendly name is skipped, not matched */
    CHECK(gsr_platform_display_find_monitor(monitors, 3, "") == -1);
    /* bad inputs */
    CHECK(gsr_platform_display_find_monitor(NULL, 3, "x") == -1);
    CHECK(gsr_platform_display_find_monitor(monitors, 0, "x") == -1);
    CHECK(gsr_platform_display_find_monitor(monitors, 3, NULL) == -1);

    /* effective size: 90/270 swap, 0/180 do not */
    int w = 0, h = 0;
    CHECK(gsr_platform_display_effective_size(&monitors[0], &w, &h) && w == 1920 && h == 1080);
    CHECK(gsr_platform_display_effective_size(&monitors[1], &w, &h) && w == 2160 && h == 3840);
    CHECK(gsr_platform_display_effective_size(&monitors[2], &w, &h) && w == 1080 && h == 1920);
    gsr_platform_monitor rot180 = monitors[0];
    rot180.rotation_degrees = 180;
    CHECK(gsr_platform_display_effective_size(&rot180, &w, &h) && w == 1920 && h == 1080);
    CHECK(!gsr_platform_display_effective_size(NULL, &w, &h));
    CHECK(!gsr_platform_display_effective_size(&monitors[0], NULL, &h));

    /* --list-monitors line uses the EFFECTIVE size (upstream Wayland parity) */
    char line[256];
    char expected[128];
    CHECK(gsr_platform_display_format_monitor_line(&monitors[1], line, sizeof(line)) > 0);
    snprintf(expected, sizeof(expected), "%s|2160x3840", monitors[1].name);
    CHECK(strcmp(line, expected) == 0);
    CHECK(gsr_platform_display_format_monitor_line(&rot180, line, sizeof(line)) > 0);
    snprintf(expected, sizeof(expected), "%s|1920x1080", monitors[0].name);
    CHECK(strcmp(line, expected) == 0);

    /* vendor ids */
    CHECK(strcmp(gsr_platform_display_vendor_name(0x10DE), "NVIDIA") == 0);
    CHECK(strcmp(gsr_platform_display_vendor_name(0x1002), "AMD") == 0);
    CHECK(strcmp(gsr_platform_display_vendor_name(0x1022), "AMD") == 0);
    CHECK(strcmp(gsr_platform_display_vendor_name(0x8086), "Intel") == 0);
    CHECK(strcmp(gsr_platform_display_vendor_name(0x1414), "Microsoft") == 0);
    CHECK(strcmp(gsr_platform_display_vendor_name(0x1234), "Unknown") == 0);
}

/* ---------------------------------------------- display enumeration (Phase 4) */

/* Headless smoke test: the CI runner has a virtual display, so the real
   DXGI enumeration must return at least one monitor with sane fields.
   Tolerant of 1+ monitors / unknown resolutions (roadmap Phase 4). */
static void test_display_enumeration(void) {
    printf("-- display enumeration\n");

    gsr_platform_monitor *monitors = NULL;
    int count = 0;
    CHECK(gsr_platform_display_list_monitors(&monitors, &count));
    CHECK(count >= 1); /* runner's virtual display */

    if(count >= 1) {
        int primaries = 0;
        for(int i = 0; i < count; ++i) {
            CHECK(monitors[i].name[0] != '\0');
            CHECK(monitors[i].width > 0 && monitors[i].height > 0);
            CHECK(monitors[i].refresh_rate > 0.0);
            CHECK(monitors[i].dpi > 0);
            CHECK(monitors[i].rotation_degrees == 0 || monitors[i].rotation_degrees == 90 ||
                  monitors[i].rotation_degrees == 180 || monitors[i].rotation_degrees == 270);
            CHECK(monitors[i].adapter_name[0] != '\0');
            CHECK(monitors[i].adapter_vendor[0] != '\0');

            char line[256];
            CHECK(gsr_platform_display_format_monitor_line(&monitors[i], line, sizeof(line)) > 0);
            CHECK(strchr(line, '|') != NULL);
            CHECK(strchr(line, 'x') != NULL);

            /* Device names are unique: round-trip must find the exact index */
            CHECK(gsr_platform_display_find_monitor(monitors, count, monitors[i].name) == i);
            /* Friendly names can collide (two "Generic PnP Monitor"), so only
               require a match, not the exact index */
            if(monitors[i].friendly_name[0] != '\0')
                CHECK(gsr_platform_display_find_monitor(monitors, count, monitors[i].friendly_name) >= 0);

            if(monitors[i].is_primary)
                ++primaries;
        }
        CHECK(primaries == 1);
        CHECK(gsr_platform_display_find_monitor(monitors, count, "No Such Monitor") == -1);
    }

    free(monitors);
}

/* ---------------------------------------------- Phase 5 WGC pure logic */

static void test_wgc_helpers(void) {
    printf("-- wgc capture helpers\n");

    /* Rotation mapping: monitor degrees -> gsr_rotation enum */
    CHECK(gsr_platform_wgc_rotation_from_monitor(0) == GSR_PLATFORM_WGC_ROT_0);
    CHECK(gsr_platform_wgc_rotation_from_monitor(90) == GSR_PLATFORM_WGC_ROT_90);
    CHECK(gsr_platform_wgc_rotation_from_monitor(180) == GSR_PLATFORM_WGC_ROT_180);
    CHECK(gsr_platform_wgc_rotation_from_monitor(270) == GSR_PLATFORM_WGC_ROT_270);
    CHECK(gsr_platform_wgc_rotation_from_monitor(45) == GSR_PLATFORM_WGC_ROT_0);
    CHECK(gsr_platform_wgc_rotation_from_monitor(-90) == GSR_PLATFORM_WGC_ROT_0);

    /* Flip mapping: upstream GSR_FLIP_HORIZONTAL/VERTICAL bits (0/1) */
    CHECK(gsr_platform_wgc_flip_from_source(0) == GSR_PLATFORM_WGC_FLIP_NONE);
    CHECK(gsr_platform_wgc_flip_from_source(1u << 0) == GSR_PLATFORM_WGC_FLIP_HORIZONTAL);
    CHECK(gsr_platform_wgc_flip_from_source(1u << 1) == GSR_PLATFORM_WGC_FLIP_VERTICAL);
    CHECK(gsr_platform_wgc_flip_from_source((1u << 0) | (1u << 1)) ==
          (GSR_PLATFORM_WGC_FLIP_HORIZONTAL | GSR_PLATFORM_WGC_FLIP_VERTICAL));

    /* WGC frames are BGRA8 (DXGI_FORMAT_B8G8R8A8_UNORM = 87); anything
       else is treated as RGB (never hit today). */
    CHECK(gsr_platform_wgc_source_color_from_pixel_format(87) == GSR_PLATFORM_WGC_SOURCE_BGR);
    CHECK(gsr_platform_wgc_source_color_from_pixel_format(28) == GSR_PLATFORM_WGC_SOURCE_RGB);
    CHECK(gsr_platform_wgc_source_color_from_pixel_format(0) == GSR_PLATFORM_WGC_SOURCE_RGB);

    /* Device selection: hardware when available, WARP otherwise (CI has no
       real GPU, so the WGC backend falls back to WARP there). */
    CHECK(gsr_platform_wgc_select_device(true) == GSR_PLATFORM_WGC_DEVICE_HARDWARE);
    CHECK(gsr_platform_wgc_select_device(false) == GSR_PLATFORM_WGC_DEVICE_WARP);

    /* Damage state machine (the recorder's tick/is_damaged/clear_damage/
       capture contract from recorder.c). */
    gsr_platform_wgc_damage damage;
    gsr_platform_wgc_damage_init(&damage);
    CHECK(!gsr_platform_wgc_damage_is_damaged(&damage));

    gsr_platform_wgc_damage_on_frame(&damage); /* tick() delivered a frame */
    CHECK(gsr_platform_wgc_damage_is_damaged(&damage));

    gsr_platform_wgc_damage_consume(&damage); /* clear_damage() */
    CHECK(!gsr_platform_wgc_damage_is_damaged(&damage));

    gsr_platform_wgc_damage_on_frame(&damage); /* next frame arrives */
    CHECK(gsr_platform_wgc_damage_is_damaged(&damage));

    /* Backend selection is already covered by the Phase 3 checks above
       (select_backend + backend_name); the WGC runtime probe itself
       (gsr_platform_capture_backend_available) is exercised by the
       wgc-self-test binary, which needs a real capture session. */
}

int main(void) {
    /* Unbuffered stdout: section headers survive a crash for diagnosis. */
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("platform-test: Phase 3 platform layer unit tests\n");

    test_filesystem();
    test_ipc_protocol();
    test_config();
    test_codec_caps();
    test_display_and_misc();
    test_display_logic();
    test_display_enumeration();
    test_wgc_helpers();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
