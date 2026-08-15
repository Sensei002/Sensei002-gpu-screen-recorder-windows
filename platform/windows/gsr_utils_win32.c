/* gsr_utils_win32.c — Windows implementation of the *portable* subset of the
 * upstream utils.h interface (upstream/src/utils.c mixes these with X11/DRM/
 * OpenGL code that the Windows build replaces with its own backends).
 *
 * Behavior of each function matches upstream/src/utils.c exactly; only the
 * platform bits differ (QPC clock, RtlGenRandom, Win32 directory creation).
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#include "../../upstream/include/utils.h"
#include "../../upstream/include/log.h"
#include "../../upstream/include/window/window.h"
/* gsr_capture_get_target_position() needs the full gsr_capture_metadata
   definition and the GSR_CAPTURE_ALIGN_* enums; utils.h only forward-
   declares the typedef. Same include set as upstream's src/utils.c. */
#include "../../upstream/include/capture/capture.h"

#include <windows.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <direct.h>
#include <errno.h>
#include <assert.h>

/* ---- time -------------------------------------------------------------- */
double clock_get_monotonic_seconds(void) {
    static double qpc_frequency = 0.0;
    if(qpc_frequency == 0.0) {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        qpc_frequency = (double)frequency.QuadPart;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / qpc_frequency;
}

/* ---- random ------------------------------------------------------------ */
/* RtlGenRandom (SystemFunction036) is the classic non-deprecated way to get
   cryptographically random bytes without bcrypt.dll. Declared manually since
   it has no header. Returns BOOLEAN (nonzero = success). */
#ifndef NTAPI
#define NTAPI __stdcall
#endif
extern BOOLEAN NTAPI SystemFunction036(PVOID pbBuffer, ULONG dwLength);

bool generate_random_characters(char *buffer, int buffer_size, const char *alphabet, size_t alphabet_size) {
    if(!SystemFunction036(buffer, (ULONG)buffer_size)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "failed to get random bytes");
        return false;
    }

    for(int i = 0; i < buffer_size; ++i) {
        unsigned char c = *(unsigned char*)&buffer[i];
        buffer[i] = alphabet[c % alphabet_size];
    }

    return true;
}

bool generate_random_characters_standard_alphabet(char *buffer, int buffer_size) {
    return generate_random_characters(buffer, buffer_size, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", 62);
}

/* ---- filesystem -------------------------------------------------------- */
int create_directory_recursive(char *path) {
    /* Same algorithm as upstream, but accepts both '/' and '\\' separators
       (Windows paths use '\\'; the upstream code only handles '/'). */
    int path_len = strlen(path);
    char *p = path;
    char *end = path + path_len;
    for(;;) {
        char *slash_p = strchr(p, '/');
        char *backslash_p = strchr(p, '\\');
        char *sep = slash_p;
        if(!sep || (backslash_p && backslash_p < sep))
            sep = backslash_p;

        // Skips first separator, we don't want to try and create the root
        if(sep == path) {
            ++p;
            continue;
        }

        if(!sep)
            sep = end;

        char prev_char = *sep;
        *sep = '\0';
        int err = _mkdir(path);
        *sep = prev_char;

        if(err == -1 && errno != EEXIST)
            return err;

        if(sep == end)
            break;
        else
            p = sep + 1;
    }
    return 0;
}

/* ---- geometry ---------------------------------------------------------- */
vec2i scale_keep_aspect_ratio(vec2i from, vec2i to) {
    if(from.x == 0 || from.y == 0)
        return (vec2i){0, 0};

    const double height_to_width_ratio = (double)from.y / (double)from.x;
    from.x = to.x;
    from.y = from.x * height_to_width_ratio;

    if(from.y > to.y) {
        const double width_height_ratio = (double)from.x / (double)from.y;
        from.y = to.y;
        from.x = from.y * width_height_ratio;
    }

    /* An extreme aspect ratio can cause a dimension to truncate to 0, which the video encoder cant handle */
    if(from.x < 1)
        from.x = 1;
    if(from.y < 1)
        from.y = 1;

    return from;
}

vec2i gsr_capture_get_target_position(vec2i output_size, gsr_capture_metadata *capture_metadata) {
    vec2i target_pos = {0, 0};

    switch(capture_metadata->halign) {
        case GSR_CAPTURE_ALIGN_START:
            break;
        case GSR_CAPTURE_ALIGN_CENTER:
            target_pos.x = capture_metadata->video_size.x/2 - output_size.x/2;
            break;
        case GSR_CAPTURE_ALIGN_END:
            target_pos.x = capture_metadata->video_size.x - output_size.x;
            break;
    }

    switch(capture_metadata->valign) {
        case GSR_CAPTURE_ALIGN_START:
            break;
        case GSR_CAPTURE_ALIGN_CENTER:
            target_pos.y = capture_metadata->video_size.y/2 - output_size.y/2;
            break;
        case GSR_CAPTURE_ALIGN_END:
            target_pos.y = capture_metadata->video_size.y - output_size.y;
            break;
    }

    target_pos.x += capture_metadata->position.x;
    target_pos.y += capture_metadata->position.y;
    return target_pos;
}

/* ---- strings ----------------------------------------------------------- */
void gsr_string_split(const char *str, char delimiter, gsr_string_split_callback callback, void *userdata) {
    const size_t str_len = strlen(str);
    size_t index = 0;
    while(index < str_len) {
        const char *end = strchr(str + index, delimiter);
        const size_t end_index = end ? (size_t)(end - str) : str_len;

        if(!callback(str + index, end_index - index, userdata))
            break;

        index = end_index + 1;
    }
}

bool gsr_string_starts_with(const char *str, size_t str_size, const char *substr) {
    const size_t substr_len = strlen(substr);
    return str_size >= substr_len && memcmp(str, substr, substr_len) == 0;
}

bool gsr_string_ends_with(const char *str, const char *substr) {
    const size_t str_len = strlen(str);
    const size_t substr_len = strlen(substr);
    return str_len >= substr_len && memcmp(str + str_len - substr_len, substr, substr_len) == 0;
}

static bool string_to_long(const char *str, size_t size, long *number) {
    char number_str[32];
    snprintf(number_str, sizeof(number_str), "%.*s", (int)size, str);

    errno = 0;
    *number = strtol(number_str, NULL, 0);
    return errno == 0;
}

bool gsr_string_to_int(const char *str, size_t size, int *number) {
    long value = 0;
    if(!string_to_long(str, size, &value))
        return false;
    *number = value;
    return true;
}

bool gsr_string_to_int64(const char *str, size_t size, int64_t *number) {
    /* Windows is LLP64: `long` is only 32 bits, so strtoll is required for
       int64 values (upstream relies on LP64 where long == 64 bits). */
    char number_str[32];
    snprintf(number_str, sizeof(number_str), "%.*s", (int)size, str);

    errno = 0;
    *number = strtoll(number_str, NULL, 0);
    return errno == 0;
}

bool gsr_array_ensure_capacity(void **array, size_t num_items, size_t *capacity_items, size_t item_size) {
    if(num_items + 1 >= *capacity_items) {
        size_t new_capacity_items = *capacity_items * 2;
        if(new_capacity_items == 0)
            new_capacity_items = 32;

        void *new_data = realloc(*array, new_capacity_items * item_size);
        if(!new_data) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_array_ensure_capacity: failed to reallocate memory");
            return false;
        }

        *array = new_data;
        *capacity_items = new_capacity_items;
    }
    return true;
}

/* ---- date/time strings -------------------------------------------------- */
void gsr_get_date_str(char *str, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(str, size - 1, "%Y-%m-%d_%H-%M-%S", t);
}

void gsr_get_date_only_str(char *str, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(str, size - 1, "%Y-%m-%d", t);
}

void gsr_get_time_only_str(char *str, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(str, size - 1, "%H-%M-%S", t);
}

/* ---- display server placeholder ----------------------------------------
 * args_parser.c references gsr_window_get_display_server() (from
 * window/window.c, which is not built yet). This stub is replaced by the
 * real Windows windowing backend in Phase 5. */
gsr_display_server gsr_window_get_display_server(const gsr_window *self) {
    (void)self;
    return GSR_DISPLAY_SERVER_X11;
}
