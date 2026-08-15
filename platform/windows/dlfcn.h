/* dlfcn.h — shim for the Windows (MinGW-w64) build.
 *
 * MinGW-w64 does not ship <dlfcn.h>. The upstream engine's
 * library_loader.c expects dlopen/dlsym/dlclose/dlerror. This port ships a
 * Win32 implementation in gsr_win32_compat.c (LoadLibrary/GetProcAddress);
 * the declarations below mirror the POSIX signatures and are also provided
 * by the force-included gsr_win32_compat.h, so they never conflict.
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#ifndef GSR_DLFCN_H
#define GSR_DLFCN_H

#include <stddef.h>

#ifndef RTLD_LAZY
#define RTLD_LAZY  1
#define RTLD_NOW   2
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 0x100
#endif

#ifdef __cplusplus
extern "C" {
#endif

void *dlopen(const char *filename, int flags);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif /* GSR_DLFCN_H */
