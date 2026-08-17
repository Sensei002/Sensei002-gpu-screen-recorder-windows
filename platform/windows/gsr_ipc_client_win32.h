/* gsr_ipc_client_win32.h — private client API of the Windows named-pipe IPC
 * transport (gsr_ipc_win32.c), used by gsr-cli.exe (Phase 11). Mirrors the
 * upstream Unix-socket client behavior: connect, send one request, read the
 * reply line, disconnect.
 */
#ifndef GSR_IPC_CLIENT_WIN32_H
#define GSR_IPC_CLIENT_WIN32_H

#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

/* Connects to \\.\pipe\<name> (the name may be passed with or without the
   prefix). Returns INVALID_HANDLE_VALUE when the engine isn't listening.
   |reply_timeout_seconds| > 0 sets the pipe read timeout; 0 waits forever
   (used for stop/save-replay, which only reply when the file is saved). */
HANDLE gsr_platform_ipc_client_connect(const char *socket_filepath, int reply_timeout_seconds);

void gsr_platform_ipc_client_disconnect(HANDLE pipe);

/* Writes the whole request; returns false on error. */
bool gsr_platform_ipc_client_send_all(HANDLE pipe, const char *data, size_t size);

/* Reads until a newline; |reply_size| excludes the newline. Returns false
   on error/timeout/disconnect. */
bool gsr_platform_ipc_client_receive_reply(HANDLE pipe, char *reply, size_t reply_capacity, size_t *reply_size);

#endif /* GSR_IPC_CLIENT_WIN32_H */
