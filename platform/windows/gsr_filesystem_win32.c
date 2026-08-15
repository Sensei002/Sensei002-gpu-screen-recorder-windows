/* gsr_filesystem_win32.c — Windows implementation of platform/include/filesystem.h.
 *
 * Phase 3 deliverable. The save-filepath builder delegates to the real
 * upstream function (recorder/muxer.c) so the Windows port produces
 * byte-identical names; everything else here is the Windows filename
 * handling the upstream code never had to do.
 */
#include "../../platform/include/filesystem.h"

#include "../../upstream/include/recorder/muxer.h"

#include <windows.h>
#include <shlobj.h>

#include <stdio.h>
#include <string.h>

/* ---- Windows filename sanitization -------------------------------------- */

static bool name_is_reserved_device(const char *base, size_t base_len) {
    /* Win32 reserves these names case-insensitively with ANY extension
       (CON.txt is invalid too). */
    static const char *const reserved[] = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
    };

    for(size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        const size_t len = strlen(reserved[i]);
        if(base_len == len && strncasecmp(base, reserved[i], len) == 0)
            return true;
    }
    return false;
}

bool gsr_platform_path_sanitize_filename(const char *name, char *out, size_t out_size) {
    if(!out || out_size == 0)
        return false;
    out[0] = '\0';
    if(!name)
        return false;

    /* Sanitizing is per-byte safe for UTF-8: every invalid character is
       ASCII, and no byte of a multi-byte UTF-8 sequence is < 0x80. */
    char buf[260];
    const size_t name_len = strlen(name);
    const size_t len = name_len < sizeof(buf) - 1 ? name_len : sizeof(buf) - 1;

    size_t write = 0;
    for(size_t i = 0; i < len; ++i) {
        const unsigned char c = (unsigned char)name[i];
        if(c < 0x20 || strchr("<>:\"/\\|?*", c))
            buf[write++] = '_';
        else
            buf[write++] = (char)c;
    }

    /* Win32 strips trailing dots and spaces; such names are unopenable. */
    while(write > 0 && (buf[write - 1] == '.' || buf[write - 1] == ' '))
        --write;

    /* Reserved device names get an underscore prefix so the file is still
       creatable and clearly distinguishable. */
    size_t base_len = 0;
    while(base_len < write && buf[base_len] != '.')
        ++base_len;
    if(name_is_reserved_device(buf, base_len)) {
        memmove(buf + 1, buf, write);
        buf[0] = '_';
        ++write;
    }

    if(write == 0) {
        out[0] = '\0';
        return false;
    }

    buf[write] = '\0';
    snprintf(out, out_size, "%s", buf);
    return true;
}

/* ---- path helpers -------------------------------------------------------- */

bool gsr_platform_path_join(const char *a, const char *b, char *out, size_t out_size) {
    if(!a || !b || !out || out_size == 0)
        return false;

    const size_t a_len = strlen(a);
    const size_t b_len = strlen(b);
    const bool a_has_sep = a_len > 0 && (a[a_len - 1] == '/' || a[a_len - 1] == '\\');
    const bool b_has_sep = b_len > 0 && (b[0] == '/' || b[0] == '\\');
    const bool need_sep = !a_has_sep && !b_has_sep && a_len > 0 && b_len > 0;

    if(a_len + (need_sep ? 1 : 0) + b_len + 1 > out_size)
        return false;

    snprintf(out, out_size, "%s%s%s", a, need_sep ? "/" : "", b);
    return true;
}

/* ---- UTF-8 <-> UTF-16 ---------------------------------------------------- */

bool gsr_platform_utf8_to_wide(const char *utf8, wchar_t *out, size_t out_chars) {
    if(!utf8 || !out || out_chars == 0)
        return false;

    const int len = (int)strlen(utf8);
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, len, NULL, 0);
    if(needed <= 0 || (size_t)needed >= out_chars)
        return false;

    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, len, out, needed);
    out[needed] = L'\0';
    return true;
}

bool gsr_platform_wide_to_utf8(const wchar_t *wide, char *out, size_t out_size) {
    if(!wide || !out || out_size == 0)
        return false;

    const int len = (int)wcslen(wide);
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, len, NULL, 0, NULL, NULL);
    if(needed <= 0 || (size_t)needed >= out_size)
        return false;

    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, len, out, needed, NULL, NULL);
    out[needed] = '\0';
    return true;
}

/* ---- default save directory ---------------------------------------------- */

bool gsr_platform_get_videos_dir(char *out, size_t out_size) {
    if(!out || out_size == 0)
        return false;

    wchar_t *videos = NULL;
    if(SHGetKnownFolderPath(&FOLDERID_Videos, KF_FLAG_DEFAULT, NULL, &videos) == S_OK && videos) {
        const bool ok = gsr_platform_wide_to_utf8(videos, out, out_size);
        CoTaskMemFree(videos);
        if(ok)
            return true;
    }

    /* Fall back to %USERPROFILE% (Videos normally lives under it). */
    char profile[1024];
    const DWORD profile_len = GetEnvironmentVariableA("USERPROFILE", profile, sizeof(profile));
    if(profile_len > 0 && profile_len < sizeof(profile)) {
        snprintf(out, out_size, "%s", profile);
        return true;
    }

    out[0] = '\0';
    return false;
}

/* ---- save filepath (upstream naming contract) ---------------------------- */

bool gsr_platform_create_recording_filepath(char *filepath, size_t filepath_size, const char *directory, const char *filename_prefix, const char *file_extension, bool date_folders) {
    /* The real upstream function: identical naming, -df handling and
       directory creation — tested as-is (see tests/platform-test). */
    return gsr_create_new_recording_filepath_from_timestamp(filepath, filepath_size, directory, filename_prefix, file_extension, date_folders);
}
