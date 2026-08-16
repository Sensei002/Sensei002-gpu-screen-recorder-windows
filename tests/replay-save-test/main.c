/* tests/replay-save-test/main.c — Phase 9: disk replay-buffer behaviors.
 *
 * The replay save path itself (gsr_replay_save_start/thread + the saved
 * Replay_*.mkv validation) is exercised end-to-end by recorder-self-test's
 * replay pass. This test covers the pieces that need no camera/GL:
 *
 *   1. Disk buffer trim: the file the buffer is currently writing rolls
 *      over at 256MB (storage_fd closes, next append opens a new
 *      Replay_N.gsr); once a second file exists, time-based trimming
 *      removes the oldest file and its packets. The test forces the
 *      rollover by closing the storage fd directly (the 256MB arithmetic
 *      is trivial), then drives the real time-trim path with tiny data
 *      and verifies file removal on disk.
 *   2. Keyframe boundaries: find_keyframe must keep working as files are
 *      trimmed away (the keyframe the save starts from can live in a
 *      later file), and must report not-found after the last keyframe.
 *   3. Simulated crash cleanup: a crashed session leaves its
 *      gsr-replay-<timestamp>.gsr directory (and Replay_*.gsr files)
 *      behind; the next session's disk-buffer create sweeps stale ones
 *      (gsr_platform_replay_cleanup_stale_directories, hooked into
 *      gsr_replay_buffer_disk_create) while preserving the current
 *      session's own directory and any non-matching directory.
 *
 * Windows port addition — see docs/upstream-porting-notes.md §3l.
 */
#include "utils.h"
#include "replay_buffer/replay_buffer.h"
#include "replay_buffer/replay_buffer_disk.h"
#include "defs.h"
#include "../../platform/include/filesystem.h" /* the sweep helper */

#include <libavcodec/avcodec.h>
#include <libavutil/mem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <direct.h> /* _rmdir */
#include <unistd.h> /* close */

static int num_failures = 0;
static int num_checks = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

static AVPacket* make_packet(int index, size_t size, bool keyframe, int stream_index) {
    AVPacket *packet = av_packet_alloc();
    av_new_packet(packet, (int)size);
    memset(packet->data, (unsigned char)(index & 0xFF), size);
    packet->stream_index = stream_index;
    if(keyframe)
        packet->flags |= AV_PKT_FLAG_KEY;
    return packet;
}

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ------------------------------------------------------------------ trim */

static void test_disk_trim_and_keyframes(void) {
    printf("-- replay buffer (disk): trim + keyframe boundaries\n");

    const char *base = "test-replay-trim";
    gsr_replay_buffer *rb = gsr_replay_buffer_disk_create(base, 4.0);
    CHECK(rb != NULL);
    gsr_replay_buffer_disk *disk = (gsr_replay_buffer_disk*)rb;

    /* File 1: two packets; the first is a keyframe. */
    AVPacket *p = make_packet(0, 32, true, 0);
    CHECK(gsr_replay_buffer_append(rb, p, 100.0));
    av_packet_free(&p);
    p = make_packet(1, 32, false, 0);
    CHECK(gsr_replay_buffer_append(rb, p, 100.1));
    av_packet_free(&p);
    CHECK(disk->num_files == 1);
    CHECK(disk->files[0]->id == 0);

    /* Force the 256MB rollover exactly as gsr_replay_buffer_disk_append
       does when the current file fills up: close the storage fd. The next
       append then creates file 2. */
    close(disk->storage_fd);
    disk->storage_fd = 0;

    /* File 2: 100 packets at 0.1s spacing starting at t=200.0; keyframes
       at packet indices 50 (t=205.0) and 80 (t=208.0). The first packet is
       deliberately NOT a keyframe so the keyframe search must scan. */
    for(int i = 0; i < 100; ++i) {
        const bool keyframe = i == 50 || i == 80;
        p = make_packet(i, 32, keyframe, 0);
        CHECK(gsr_replay_buffer_append(rb, p, 200.0 + i * 0.1));
        av_packet_free(&p);

        /* While both files exist, a keyframe search from the start must
           find file 1's keyframe (packet 0 of file 0). */
        if(i == 1) {
            CHECK(disk->num_files == 2);
            const gsr_replay_buffer_iterator found = gsr_replay_buffer_find_keyframe(rb, (gsr_replay_buffer_iterator){0, 0}, 0, false);
            CHECK(found.file_index == 0 && found.packet_index == 0);
        }
    }

    /* Time-based trim: once t - file2.start (200.0) >= 4.0 the oldest file
       is removed, so after appending through t=209.9 only file 2 remains. */
    CHECK(disk->num_files == 1);
    CHECK(disk->files[0]->id == 1);

    /* The removed file is gone from disk; the surviving one is present. */
    char removed_file[PATH_MAX];
    snprintf(removed_file, sizeof(removed_file), "%s/Replay_0.gsr", disk->replay_directory);
    CHECK(!file_exists(removed_file));
    char surviving[PATH_MAX];
    snprintf(surviving, sizeof(surviving), "%s/Replay_1.gsr", disk->replay_directory);
    CHECK(file_exists(surviving));

    /* Keyframe search across the trimmed buffer finds file 2's first
       keyframe (packet 50) ... */
    gsr_replay_buffer_iterator found = gsr_replay_buffer_find_keyframe(rb, (gsr_replay_buffer_iterator){0, 0}, 0, false);
    CHECK(found.file_index == 0 && found.packet_index == 50);

    /* ... and reports not-found after the last keyframe (packet 80). */
    found = gsr_replay_buffer_find_keyframe(rb, (gsr_replay_buffer_iterator){81, 0}, 0, false);
    CHECK(found.packet_index == (size_t)-1);

    /* Full iteration over the surviving file: all 100 packets present in
       order, and the data reads back from the .gsr file. */
    int count = 0;
    gsr_replay_buffer_iterator iterator = {0, 0};
    do {
        AVPacket *packet = gsr_replay_buffer_iterator_get_packet(rb, iterator);
        CHECK(packet != NULL && packet->size == 32);
        uint8_t *data = gsr_replay_buffer_iterator_get_packet_data(rb, iterator);
        CHECK(data != NULL && data[0] == (unsigned char)(count & 0xFF));
        free(data);
        ++count;
    } while(gsr_replay_buffer_iterator_next(rb, &iterator));
    CHECK(count == 100);

    gsr_replay_buffer_destroy(rb);
    CHECK(!file_exists(disk->replay_directory));
    _rmdir(base);
}

/* ------------------------------------------------------- crash cleanup */

static void test_crash_cleanup(void) {
    printf("-- replay buffer (disk): simulated crash cleanup\n");

    const char *base = "test-replay-crash";
    char base_buf[PATH_MAX];
    snprintf(base_buf, sizeof(base_buf), "%s", base);
    CHECK(create_directory_recursive(base_buf) == 0);

    /* A crashed session's leftovers: a gsr-replay-*.gsr directory with a
       Replay_0.gsr inside (closed handles — a real crash leaves none). */
    char stale[PATH_MAX];
    snprintf(stale, sizeof(stale), "%s/gsr-replay-2000-01-01_00-00-00.gsr", base);
    char stale_buf[PATH_MAX];
    snprintf(stale_buf, sizeof(stale_buf), "%s", stale);
    CHECK(create_directory_recursive(stale_buf) == 0);
    char stale_file[PATH_MAX];
    snprintf(stale_file, sizeof(stale_file), "%s/Replay_0.gsr", stale);
    FILE *f = fopen(stale_file, "wb");
    CHECK(f != NULL);
    if(f) {
        fwrite("junk", 1, 4, f);
        fclose(f);
    }
    CHECK(file_exists(stale));

    /* A directory that must NOT be swept (does not match the pattern). */
    char keep[PATH_MAX];
    snprintf(keep, sizeof(keep), "%s/not-a-replay-dir", base);
    char keep_buf[PATH_MAX];
    snprintf(keep_buf, sizeof(keep_buf), "%s", keep);
    CHECK(create_directory_recursive(keep_buf) == 0);

    /* The next session's disk-buffer create triggers the sweep: the stale
       session directory is removed, the unrelated directory survives. */
    gsr_replay_buffer *rb = gsr_replay_buffer_disk_create(base, 4.0);
    CHECK(rb != NULL);
    gsr_replay_buffer_disk *disk = (gsr_replay_buffer_disk*)rb;

    CHECK(!file_exists(stale));
    struct stat st;
    CHECK(stat(keep, &st) == 0 && S_ISDIR(st.st_mode));

    /* The current session's own directory is created on first append and
       removed on destroy (the clean-exit path that makes the sweep's
       "everything else is stale" assumption sound). */
    const char *own = disk->replay_directory;
    CHECK(!file_exists(own)); /* created lazily */
    AVPacket *p = make_packet(7, 32, true, 0);
    CHECK(gsr_replay_buffer_append(rb, p, 100.0));
    av_packet_free(&p);
    CHECK(stat(own, &st) == 0 && S_ISDIR(st.st_mode));

    gsr_replay_buffer_destroy(rb);
    CHECK(!file_exists(own));

    /* Only the unrelated directory remains; clean it up. */
    _rmdir(keep);
    _rmdir(base);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("replay-save-test: Phase 9 (disk replay buffer: trim, keyframes, crash cleanup)\n");

    test_disk_trim_and_keyframes();
    test_crash_cleanup();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    if(num_failures > 0) {
        printf("FAIL\n");
        return 1;
    }
    printf("PASS\n");
    return 0;
}
