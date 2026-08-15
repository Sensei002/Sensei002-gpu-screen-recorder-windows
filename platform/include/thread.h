/* platform/include/thread.h — threading interfaces for the Windows port.
 *
 * Phase 3 deliverable.
 *
 * Threading model: the engine uses pthreads (mutex/cond/thread) and the
 * UI uses C++ std::thread. On Windows both are provided by winpthreads
 * (linked statically since Phase 2), so no shim layer is needed for the
 * primitives themselves — this header only carries the small extras the
 * port needs on Windows: naming threads for debugging. The engine's
 * existing threading code (video loop thread, audio capture threads, IPC
 * accept loop, replay-save worker) is unchanged.
 */
#ifndef GSR_PLATFORM_THREAD_H
#define GSR_PLATFORM_THREAD_H

/* Sets the current thread's name (SetThreadDescription); visible in the
 * debugger and in crash dumps. No-op when the API is unavailable. */
void gsr_platform_thread_set_current_name(const char *name);

#endif /* GSR_PLATFORM_THREAD_H */
