/* platform/include/process.h — child-process interfaces for the Windows port.
 *
 * Phase 3 deliverable (headers only). Implemented in Phase 12
 * (platform/windows/process.c, CreateProcess with stdout-pipe capture).
 *
 * Upstream spawns child processes (the UI spawns the engine and parses its
 * stdout; -sc scripts run on save) with fork/exec-style helpers
 * (exec_program). The Windows equivalent must capture stdout the same way
 * because the UI's engine contract is stdout = machine-readable saved
 * paths (docs/upstream-analysis.md §10.9).
 */
#ifndef GSR_PLATFORM_PROCESS_H
#define GSR_PLATFORM_PROCESS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct gsr_platform_process gsr_platform_process;

/* Spawns |command_line| (full command line, Win32 quoting rules) and
 * captures its stdout when |capture_stdout|. Returns NULL on failure.
 * The child is started suspended-free and inherits no console. */
gsr_platform_process *gsr_platform_process_spawn(const char *command_line, bool capture_stdout);

/* Reads one line (up to size-1 bytes, NUL-terminated) from the child's
 * stdout. Returns the number of bytes read (excluding NUL), 0 at EOF, -1
 * on failure or when stdout was not captured. */
int gsr_platform_process_read_stdout_line(gsr_platform_process *process, char *buf, size_t size);

/* Returns the child's exit code, or -1 while it is still running. */
int gsr_platform_process_get_exit_code(gsr_platform_process *process);

/* Waits for the child to exit (|timeout_ms|, -1 = forever). Returns the
 * exit code, or -1 on timeout. */
int gsr_platform_process_wait(gsr_platform_process *process, int timeout_ms);

/* Terminates the child (like upstream killing the engine on stop). */
void gsr_platform_process_terminate(gsr_platform_process *process);

/* Frees the handle. */
void gsr_platform_process_destroy(gsr_platform_process *process);

#endif /* GSR_PLATFORM_PROCESS_H */
