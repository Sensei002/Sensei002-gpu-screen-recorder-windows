/* tests/gsr-core-test/main.c — unit tests for the portable engine core.
 *
 * Exercises the platform-independent parts of the upstream engine exactly as
 * they will run on Windows: CLI parsing (args_parser), audio input parsing
 * (audio_input), the recording clock, the RAM and disk replay buffers, the
 * JSON helpers, and the portable utils subset (reimplemented for Windows in
 * platform/windows/gsr_utils_win32.c — behavioral parity is checked here).
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "utils.h"
#include "args_parser.h"
#include "json.h"
#include "recorder/error.h"
#include "recorder/audio_input.h"
#include "recorder/recording_clock.h"
#include "replay_buffer/replay_buffer.h"
#include "replay_buffer/replay_buffer_ram.h"
#include "replay_buffer/replay_buffer_disk.h"
#include "defs.h"

#include <libgen.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <sys/stat.h>
#include <direct.h> /* _rmdir */

static int num_failures = 0;
static int num_checks = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

/* ------------------------------------------------------------------ utils */

typedef struct {
    char parts[4][16];
    int num_parts;
} split_userdata;

static bool split_callback(const char *str, size_t size, void *userdata) {
    split_userdata *ud = userdata;
    if(ud->num_parts >= 4)
        return false;
    snprintf(ud->parts[ud->num_parts], sizeof(ud->parts[0]), "%.*s", (int)size, str);
    ++ud->num_parts;
    return true;
}

static void test_utils(void) {
    printf("-- utils\n");

    split_userdata ud = {0};
    gsr_string_split("a|b|c", '|', split_callback, &ud);
    CHECK(ud.num_parts == 3);
    CHECK(strcmp(ud.parts[0], "a") == 0);
    CHECK(strcmp(ud.parts[1], "b") == 0);
    CHECK(strcmp(ud.parts[2], "c") == 0);

    CHECK(gsr_string_starts_with("hello world", 11, "hello"));
    CHECK(!gsr_string_starts_with("hello world", 11, "world"));
    CHECK(gsr_string_ends_with("hello", "lo"));
    CHECK(!gsr_string_ends_with("hello", "he"));

    int number = 0;
    CHECK(gsr_string_to_int("42", 2, &number) && number == 42);
    CHECK(gsr_string_to_int("-7", 2, &number) && number == -7);

    /* LLP64 check: int64 must not be truncated to 32-bit long */
    int64_t number64 = 0;
    CHECK(gsr_string_to_int64("5000000000", 10, &number64) && number64 == 5000000000LL);
    CHECK(gsr_string_to_int64("-9000000000", 11, &number64) && number64 == -9000000000LL);

    /* scale_keep_aspect_ratio */
    vec2i scaled = scale_keep_aspect_ratio((vec2i){1920, 1080}, (vec2i){1280, 720});
    CHECK(scaled.x == 1280 && scaled.y == 720);
    scaled = scale_keep_aspect_ratio((vec2i){1920, 1080}, (vec2i){100, 100});
    CHECK(scaled.x == 100 && scaled.y == 56);
    /* Extreme aspect ratio must not truncate to 0 */
    scaled = scale_keep_aspect_ratio((vec2i){10000, 1}, (vec2i){100, 100});
    CHECK(scaled.x >= 1 && scaled.y >= 1);

    /* create_directory_recursive + date strings */
    char dir_path[] = "test-dirs/a/b/c";
    CHECK(create_directory_recursive(dir_path) == 0);
    struct stat st;
    CHECK(stat("test-dirs/a/b/c", &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(create_directory_recursive(dir_path) == 0); /* idempotent */
    _rmdir("test-dirs/a/b/c");
    _rmdir("test-dirs/a/b");
    _rmdir("test-dirs/a");
    _rmdir("test-dirs");

    char date_str[128];
    gsr_get_date_str(date_str, sizeof(date_str));
    CHECK(strlen(date_str) == 19);
    CHECK(date_str[4] == '-' && date_str[7] == '-' && date_str[10] == '_');

    /* libgen shim (behavior parity with POSIX) */
    char p1[] = "a/b/c";
    CHECK(strcmp(dirname(p1), "a/b") == 0);
    char p2[] = "onlyfile";
    CHECK(strcmp(dirname(p2), ".") == 0);
    char p3[] = "/root";
    CHECK(strcmp(dirname(p3), "/") == 0);
    char p4[] = "dir\\file.txt";
    CHECK(strcmp(dirname(p4), "dir") == 0);
    char p5[] = "/a/b.txt";
    CHECK(strcmp(basename(p5), "b.txt") == 0);
}

/* ------------------------------------------------------------ audio input */

static void test_audio_input(void) {
    printf("-- audio input parsing\n");

    gsr_merged_audio_inputs merged;
    CHECK(gsr_merged_audio_inputs_parse(&merged, "device:default_output") == GSR_ERROR_OK);
    CHECK(merged.num_items == 1);
    CHECK(merged.items[0].type == GSR_AUDIO_INPUT_TYPE_DEVICE);
    CHECK(strcmp(merged.items[0].name, "default_output") == 0);
    CHECK(!merged.has_custom_name);
    gsr_merged_audio_inputs_deinit(&merged);

    CHECK(gsr_merged_audio_inputs_parse(&merged, "app:firefox|app-inverse:chrome|name:My Track") == GSR_ERROR_OK);
    CHECK(merged.num_items == 2);
    CHECK(merged.items[0].type == GSR_AUDIO_INPUT_TYPE_APPLICATION);
    CHECK(!merged.items[0].inverted);
    CHECK(strcmp(merged.items[0].name, "firefox") == 0);
    CHECK(merged.items[1].type == GSR_AUDIO_INPUT_TYPE_APPLICATION);
    CHECK(merged.items[1].inverted);
    CHECK(strcmp(merged.items[1].name, "chrome") == 0);
    CHECK(merged.has_custom_name);
    CHECK(strcmp(merged.track_name, "My Track") == 0);
    CHECK(gsr_audio_inputs_has_app_audio(&merged));
    CHECK(!gsr_audio_inputs_should_use_amix(&merged)); /* 0 devices */
    gsr_merged_audio_inputs_deinit(&merged);

    CHECK(gsr_merged_audio_inputs_parse(&merged, "device:a|device:b") == GSR_ERROR_OK);
    CHECK(gsr_audio_inputs_should_use_amix(&merged)); /* 2 devices, no app audio */
    gsr_merged_audio_inputs_deinit(&merged);

    /* Full track parse with a device list (mirrors what -a does) */
    gsr_audio_devices devices = {0};
    snprintf(devices.default_output, sizeof(devices.default_output), "Default Output");
    devices.items = calloc(1, sizeof(gsr_audio_device));
    devices.capacity_items = 1;
    snprintf(devices.items[0].name, sizeof(devices.items[0].name), "Default Output");
    snprintf(devices.items[0].description, sizeof(devices.items[0].description), "Built-in Speakers");
    devices.num_items = 1;

    const char *audio_args[] = { "device:Default Output", "default_output|app:firefox" };
    gsr_audio_input_tracks tracks;
    CHECK(gsr_audio_input_tracks_parse(&tracks, audio_args, 2, &devices) == GSR_ERROR_OK);
    CHECK(tracks.num_items == 2);
    CHECK(strcmp(tracks.items[0].track_name, "Devices: Built-in Speakers") == 0);
    CHECK(strcmp(tracks.items[1].track_name, "Devices: Default output. Applications: firefox") == 0);
    CHECK(gsr_audio_input_tracks_has_app_audio(&tracks));
    gsr_audio_input_tracks_deinit(&tracks);

    /* Unknown device must be rejected (GSR_ERROR_AUDIO_DEVICE_NOT_FOUND) */
    const char *bad_args[] = { "device:Not-A-Device" };
    CHECK(gsr_audio_input_tracks_parse(&tracks, bad_args, 1, &devices) == GSR_ERROR_AUDIO_DEVICE_NOT_FOUND);

    /* gsr_audio_devices_deinit lives in the Linux audio backend (sound.c),
       so the test frees its own allocation directly. */
    free(devices.items);
}

/* --------------------------------------------------------- recording clock */

static void test_recording_clock(void) {
    printf("-- recording clock\n");

    gsr_recording_clock *clock = gsr_recording_clock_create();
    CHECK(clock != NULL);

    gsr_recording_clock_start(clock);
    const double start_time = gsr_recording_clock_get_start_time(clock);
    Sleep(30);
    const double t1 = gsr_recording_clock_get_time(clock);
    /* get_time() returns absolute monotonic time; elapsed = t1 - start_time */
    CHECK(t1 - start_time >= 0.02);

    /* Upstream pause semantics: get_time() is real-time based, so it keeps
       advancing while paused; the paused interval is subtracted from the
       clock retroactively when unpausing. The engine only reads the clock
       while not paused (frames are not captured during a pause), so this
       is the contract that matters. */
    gsr_recording_clock_set_paused(clock, true);
    CHECK(gsr_recording_clock_is_paused(clock));
    const double paused_t1 = gsr_recording_clock_get_time(clock);
    Sleep(50);
    const double mid_pause = gsr_recording_clock_get_time(clock);
    CHECK(mid_pause - paused_t1 >= 0.03); /* real time keeps flowing */

    gsr_recording_clock_set_paused(clock, false);
    CHECK(!gsr_recording_clock_is_paused(clock));
    const double t2 = gsr_recording_clock_get_time(clock);
    /* The paused 50ms must not count: after unpausing the clock jumps back
       to (approximately) where it was when the pause started. */
    CHECK(fabs(t2 - paused_t1) < 0.02);
    CHECK(t2 - start_time >= 0.02); /* pre-pause elapsed still counts */

    gsr_recording_clock_destroy(clock);
}

/* ---------------------------------------------------------- replay buffer */

static AVPacket* make_packet(int index, size_t size, bool keyframe, int stream_index) {
    AVPacket *packet = av_packet_alloc();
    av_new_packet(packet, (int)size);
    memset(packet->data, (unsigned char)(index & 0xFF), size);
    packet->stream_index = stream_index;
    if(keyframe)
        packet->flags |= AV_PKT_FLAG_KEY;
    return packet;
}

static void test_replay_buffer_ram(void) {
    printf("-- replay buffer (RAM)\n");

    gsr_replay_buffer *rb = gsr_replay_buffer_ram_create(4);
    CHECK(rb != NULL);

    const double now = clock_get_monotonic_seconds();

    /* Ring buffer wrap-around: 6 appends into capacity 4 */
    for(int i = 0; i < 6; ++i) {
        AVPacket *packet = make_packet(i, 16, i == 1 || i == 5, 0);
        CHECK(gsr_replay_buffer_append(rb, packet, now - (double)(6 - i)));
        av_packet_free(&packet);
    }

    gsr_replay_buffer_iterator iterator = {0, 0};
    gsr_replay_buffer_iterator found = gsr_replay_buffer_find_packet_index_by_time_passed(rb, 100);
    CHECK(found.packet_index == 0 && found.file_index == 0);
    found = gsr_replay_buffer_find_packet_index_by_time_passed(rb, 0);
    CHECK(found.packet_index == 3 && found.file_index == 0);

    /* find_keyframe: after the ring wrap the stored packets are appends
       2,3,4,5; only append #5 is a keyframe, at iteration index 3. */
    found = gsr_replay_buffer_find_keyframe(rb, (gsr_replay_buffer_iterator){0, 0}, 0, false);
    CHECK(found.packet_index == 3);

    /* Iterator order after wrap: appends 2,3,4,5 */
    int count = 0;
    iterator = (gsr_replay_buffer_iterator){0, 0};
    do {
        AVPacket *packet = gsr_replay_buffer_iterator_get_packet(rb, iterator);
        CHECK(packet->data[0] == (unsigned char)((count + 2) & 0xFF));
        ++count;
    } while(gsr_replay_buffer_iterator_next(rb, &iterator));
    CHECK(count == 4);

    /* Clone reads the same data */
    gsr_replay_buffer *clone = gsr_replay_buffer_clone(rb);
    CHECK(clone != NULL);
    iterator = (gsr_replay_buffer_iterator){0, 0};
    AVPacket *orig_packet = gsr_replay_buffer_iterator_get_packet(rb, iterator);
    AVPacket *clone_packet = gsr_replay_buffer_iterator_get_packet(clone, iterator);
    CHECK(clone_packet->size == orig_packet->size);
    CHECK(memcmp(clone_packet->data, orig_packet->data, orig_packet->size) == 0);
    gsr_replay_buffer_destroy(clone);

    gsr_replay_buffer_clear(rb);
    found = gsr_replay_buffer_find_packet_index_by_time_passed(rb, 0);
    CHECK(found.packet_index == 0 && found.file_index == 0);

    gsr_replay_buffer_destroy(rb);
}

static void test_replay_buffer_disk(void) {
    printf("-- replay buffer (disk)\n");

    const char *dir = "test-replay-disk";
    gsr_replay_buffer *rb = gsr_replay_buffer_disk_create(dir, 10.0);
    CHECK(rb != NULL);

    const double now = clock_get_monotonic_seconds();
    for(int i = 0; i < 5; ++i) {
        AVPacket *packet = make_packet(i, 32, i == 0, 0);
        CHECK(gsr_replay_buffer_append(rb, packet, now - (double)(5 - i)));
        av_packet_free(&packet);
    }

    gsr_replay_buffer_iterator iterator = {0, 0};
    int count = 0;
    do {
        AVPacket *packet = gsr_replay_buffer_iterator_get_packet(rb, iterator);
        CHECK(packet->size == 32);
        uint8_t *data = gsr_replay_buffer_iterator_get_packet_data(rb, iterator);
        CHECK(data != NULL);
        CHECK(data[0] == (unsigned char)(count & 0xFF));
        free(data);
        ++count;
    } while(gsr_replay_buffer_iterator_next(rb, &iterator));
    CHECK(count == 5);

    /* Clone can read the same data */
    gsr_replay_buffer *clone = gsr_replay_buffer_clone(rb);
    CHECK(clone != NULL);
    iterator = (gsr_replay_buffer_iterator){0, 0};
    uint8_t *data = gsr_replay_buffer_iterator_get_packet_data(clone, iterator);
    CHECK(data != NULL && data[0] == 0);
    free(data);
    gsr_replay_buffer_destroy(clone);

    /* The .gsr files must be removed on clear and the directory on destroy */
    gsr_replay_buffer_clear(rb);
    gsr_replay_buffer_destroy(rb);
    struct stat st;
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    _rmdir(dir);

    /* Dispatcher: create via gsr_replay_buffer_create */
    gsr_replay_buffer *ram = gsr_replay_buffer_create(GSR_REPLAY_STORAGE_RAM, NULL, 0.0, 8);
    CHECK(ram != NULL);
    AVPacket *packet = make_packet(9, 8, true, 0);
    CHECK(gsr_replay_buffer_append(ram, packet, clock_get_monotonic_seconds()));
    av_packet_free(&packet);
    iterator = (gsr_replay_buffer_iterator){0, 0};
    AVPacket *stored = gsr_replay_buffer_iterator_get_packet(ram, iterator);
    CHECK(stored != NULL && stored->size == 8);
    gsr_replay_buffer_destroy(ram);
}

/* ------------------------------------------------------------------ JSON */

static void test_json(void) {
    printf("-- json helpers\n");

    char escaped[64];
    gsr_json_escape_string(escaped, sizeof(escaped), "a\"b\\c\n");
    CHECK(strcmp(escaped, "a\\\"b\\\\c\\n") == 0);

    char data[] = "{\"num\": 123, \"str\": \"hi\"}";
    sj_Reader reader = sj_reader(data, strlen(data));
    sj_Value obj = sj_read(&reader);
    CHECK(obj.type == SJ_OBJECT);

    sj_Value key, value;
    bool found_num = false, found_str = false;
    while(sj_iter_object(&reader, obj, &key, &value)) {
        if(gsr_json_string_equals(&key, "num")) {
            int64_t number = 0;
            CHECK(gsr_json_number_to_int64(&value, &number) && number == 123);
            found_num = true;
        } else if(gsr_json_string_equals(&key, "str")) {
            CHECK(gsr_json_string_equals(&value, "hi"));
            found_str = true;
        }
    }
    CHECK(found_num && found_str);

    /* Non-number must fail */
    int64_t number = 0;
    CHECK(!gsr_json_number_to_int64(&key, &number)); /* key is a string */
}

/* ------------------------------------------------------------ args parser */

typedef struct {
    int version_calls;
    int info_calls;
    int list_monitors_calls;
} handler_userdata;

static int handler_version(void *userdata) {
    ++((handler_userdata*)userdata)->version_calls;
    printf("    (version handler called)\n");
    return 0;
}

static int handler_info(void *userdata) {
    ++((handler_userdata*)userdata)->info_calls;
    printf("    (info handler called)\n");
    return 0;
}

static int handler_list_monitors(void *userdata) {
    ++((handler_userdata*)userdata)->list_monitors_calls;
    printf("    (list-monitors handler called)\n");
    return 0;
}

static const args_handlers test_handlers = {
    .version = handler_version,
    .info = handler_info,
    .list_audio_devices = NULL,
    .list_application_audio = NULL,
    .list_v4l2_devices = NULL,
    .list_capture_options = NULL,
    .list_monitors = handler_list_monitors,
};

static void test_args_parser(void) {
    printf("-- args parser\n");
    handler_userdata userdata = {0};

    /* A realistic full command line (replay mode) */
    char *argv[] = {
        "gpu-screen-recorder",
        "-w", "monitor",
        "-f", "60",
        "-q", "very_high",
        "-a", "default_output",
        "-a", "app:firefox",
        "-o", "replay_out",
        "-c", "mp4",
        "-k", "h264",
        "-ac", "aac",
        "-ab", "128",
        "-r", "10",
        "-replay-storage", "ram",
        "-cr", "full",
        "-tune", "quality",
        "-df", "yes",
        "-cursor", "no",
        "-bm", "qp",
        "-fm", "cfr",
        "-encoder", "gpu",
        "-pixfmt", "yuv420",
    };
    const int argc = sizeof(argv) / sizeof(argv[0]);

    args_parser parser;
    int command_exit_code = 0;
    args_parse_result result = args_parser_parse(&parser, argc, argv, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_OK);

    const gsr_recorder_settings *settings = &parser.settings;
    CHECK(settings->capture_source != NULL && strcmp(settings->capture_source, "monitor") == 0);
    CHECK(settings->fps == 60);
    CHECK(settings->video_quality == GSR_VIDEO_QUALITY_VERY_HIGH);
    CHECK(settings->audio_bitrate == 128000);
    CHECK(settings->container_format != NULL && strcmp(settings->container_format, "mp4") == 0);
    CHECK(settings->video_codec == GSR_VIDEO_CODEC_H264);
    CHECK(settings->audio_codec == GSR_AUDIO_CODEC_AAC);
    CHECK(settings->replay_buffer_size_secs == 12); /* 10 + (int64_t)(keyint 2.0 + 0.5) */
    CHECK(settings->is_replaying);
    CHECK(settings->replay_storage == GSR_REPLAY_STORAGE_RAM);
    CHECK(settings->color_range == GSR_COLOR_RANGE_FULL);
    CHECK(settings->tune == GSR_TUNE_QUALITY);
    CHECK(settings->date_folders);
    CHECK(!settings->record_cursor);
    CHECK(settings->bitrate_mode == GSR_BITRATE_MODE_QP);
    CHECK(settings->framerate_mode == GSR_FRAMERATE_MODE_CONSTANT);
    CHECK(settings->pixel_format == GSR_PIXEL_FORMAT_YUV420);
    CHECK(settings->video_encoder == GSR_VIDEO_ENCODER_HW_GPU);
    args_parser_deinit(&parser);

    /* Missing required argument (-w) */
    char *argv_missing[] = { "gpu-screen-recorder", "-f", "60", "-o", "out.mp4", "-c", "mp4" };
    result = args_parser_parse(&parser, (int)(sizeof(argv_missing)/sizeof(argv_missing[0])), argv_missing, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_ERROR);
    args_parser_deinit(&parser);

    /* Unknown option */
    char *argv_unknown[] = { "gpu-screen-recorder", "-w", "monitor", "-bogus", "x" };
    result = args_parser_parse(&parser, 5, argv_unknown, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_ERROR);
    args_parser_deinit(&parser);

    /* Invalid enum value */
    char *argv_badenum[] = { "gpu-screen-recorder", "-w", "monitor", "-k", "banana" };
    result = args_parser_parse(&parser, 5, argv_badenum, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_ERROR);
    args_parser_deinit(&parser);

    /* Invalid boolean value */
    char *argv_badbool[] = { "gpu-screen-recorder", "-w", "monitor", "-cursor", "maybe" };
    result = args_parser_parse(&parser, 5, argv_badbool, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_ERROR);
    args_parser_deinit(&parser);

    /* fps out of range */
    char *argv_badfps[] = { "gpu-screen-recorder", "-w", "monitor", "-f", "5000" };
    result = args_parser_parse(&parser, 5, argv_badfps, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_ERROR);
    args_parser_deinit(&parser);

    /* --version, --info and --list-monitors dispatch to the handlers */
    char *argv_version[] = { "gpu-screen-recorder", "--version" };
    result = args_parser_parse(&parser, 2, argv_version, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_COMMAND_HANDLED);
    CHECK(userdata.version_calls == 1);
    args_parser_deinit(&parser);

    char *argv_info[] = { "gpu-screen-recorder", "--info" };
    result = args_parser_parse(&parser, 2, argv_info, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_COMMAND_HANDLED);
    CHECK(userdata.info_calls == 1);
    args_parser_deinit(&parser);

    char *argv_monitors[] = { "gpu-screen-recorder", "--list-monitors" };
    result = args_parser_parse(&parser, 2, argv_monitors, &test_handlers, &userdata, &command_exit_code);
    CHECK(result == ARGS_PARSE_RESULT_COMMAND_HANDLED);
    CHECK(userdata.list_monitors_calls == 1);
    args_parser_deinit(&parser);
}

/* --------------------------------------------------------------- harness */

int main(void) {
    printf("gsr-core-test: portable engine core unit tests\n");

    test_utils();
    test_audio_input();
    test_recording_clock();
    test_replay_buffer_ram();
    test_replay_buffer_disk();
    test_json();
    test_args_parser();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
