/* platform/include/filesystem.h — filesystem interfaces for the Windows port.
 *
 * Phase 3 deliverable. Implementation: platform/windows/gsr_filesystem_win32.c.
 *
 * Why this exists: upstream's portable code (utils.h, recorder/muxer.c) works
 * with POSIX-style paths and '/' separators and never has to think about
 * Windows filename rules. The Windows port must keep the *naming contract*
 * byte-identical (Replay_YYYY-MM-DD_HH-MM-SS.mp4, -df date folders) while
 * making the names actually valid on Windows. This interface:
 *
 *   - sanitizes user-supplied names (Windows-invalid characters, reserved
 *     device names, trailing dots/spaces);
 *   - joins path components with the platform separator;
 *   - converts between UTF-8 (the engine's string type) and UTF-16 (the
 *     Win32 file API type);
 *   - resolves the default save directory (Videos);
 *   - builds save filepaths through the *real* upstream function
 *     gsr_create_new_recording_filepath_from_timestamp (recorder/muxer.c),
 *     so the naming contract is tested against upstream code, not a copy.
 */
#ifndef GSR_PLATFORM_FILESYSTEM_H
#define GSR_PLATFORM_FILESYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/* Sanitizes |name| so it is a valid Windows file name (without directory
 * components):
 *
 *   - replaces the Windows-invalid characters  < > : " / \ | ? *  and
 *     control characters (0x00-0x1F) with '_';
 *   - trims trailing dots and spaces (Win32 strips them and CreateFile
 *     refuses to open such names);
 *   - prefixes '_' when the base name (case-insensitive, extension
 *     ignored) is a reserved device name: CON, PRN, AUX, NUL, COM1..COM9,
 *     LPT1..LPT9.
 *
 * Writes at most |out_size| bytes into |out| (always NUL-terminated when
 * out_size > 0). Returns false when the result is empty (e.g. |name| was
 * all-invalid), in which case |out| holds an empty string.
 */
bool gsr_platform_path_sanitize_filename(const char *name, char *out, size_t out_size);

/* Joins two path components with the platform separator ('\\' on Windows,
 * but '/' is also accepted by all Win32 APIs, so the port uses '/'
 * internally for consistency with the engine). Handles missing/duplicate
 * separators. Returns false when the result does not fit.
 */
bool gsr_platform_path_join(const char *a, const char *b, char *out, size_t out_size);

/* UTF-8 <-> UTF-16 conversion helpers (the engine is UTF-8; Win32 file
 * APIs are UTF-16). Returns false on invalid input or when the output
 * buffer is too small. |out| is always NUL-terminated on success.
 */
bool gsr_platform_utf8_to_wide(const char *utf8, wchar_t *out, size_t out_chars);
bool gsr_platform_wide_to_utf8(const wchar_t *wide, char *out, size_t out_size);

/* Resolves the default save directory: the user's Videos folder
 * (SHGetKnownFolderPath FOLDERID_Videos), falling back to %USERPROFILE%
 * when it cannot be resolved. UTF-8. Returns false when neither exists.
 */
bool gsr_platform_get_videos_dir(char *out, size_t out_size);

/* Builds a save filepath exactly like the engine does:
 *   <directory>/<prefix>_<YYYY-MM-DD_HH-MM-SS>.<ext>
 * or, when |date_folders|, <directory>/<YYYY-MM-DD>/<prefix>_<HH-MM-SS>.<ext>
 * (creating the date folder). Thin wrapper over the upstream function
 * gsr_create_new_recording_filepath_from_timestamp (recorder/muxer.c) so
 * the Windows port and upstream produce identical names. |filepath| must
 * hold at least 260 bytes.
 */
bool gsr_platform_create_recording_filepath(char *filepath, size_t filepath_size, const char *directory, const char *filename_prefix, const char *file_extension, bool date_folders);

/* Phase 9: crash-safe disk replay-buffer cleanup.
 *
 * The disk replay buffer (upstream/src/replay_buffer/replay_buffer_disk.c)
 * names each session's working directory `gsr-replay-<timestamp>.gsr`
 * inside the replay directory and removes it on a clean exit. A crashed
 * session leaves it (and the Replay_*.gsr files inside) behind forever.
 * This helper sweeps every such directory inside |replay_directory| EXCEPT
 * |current_session_dirname| (the directory name of the session that is
 * about to start), so each new session cleans up the leftovers of previous
 * crashed ones. |current_session_dirname| may be NULL to sweep everything.
 * Directories that do not match the `gsr-replay-*.gsr` pattern are never
 * touched. Returns 0 on success (including when nothing matched).
 */
int gsr_platform_replay_cleanup_stale_directories(const char *replay_directory, const char *current_session_dirname);

#endif /* GSR_PLATFORM_FILESYSTEM_H */
