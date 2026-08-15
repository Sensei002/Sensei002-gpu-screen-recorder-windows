/* libgen.h — shim for the Windows (MinGW-w64) build.
 *
 * MinGW-w64 does not ship <libgen.h>. The upstream engine only uses
 * dirname() (src/args_parser.c); basename() is provided for completeness.
 * Implementations live in gsr_win32_compat.c (also see gsr_win32_compat.h,
 * which is force-included into every translation unit).
 *
 * Windows port addition — see docs/upstream-porting-notes.md.
 */
#ifndef GSR_LIBGEN_H
#define GSR_LIBGEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Both functions may modify |path| in place (POSIX semantics). */
char *dirname(char *path);
char *basename(char *path);

#ifdef __cplusplus
}
#endif

#endif /* GSR_LIBGEN_H */
