/* platform/include/egl_win32.h — Windows ANGLE EGL backend surface (Phase 5b).
 *
 * The upstream gsr_egl struct (upstream/include/egl.h) is the GL function
 * table; on Windows it is filled by gsr_egl_load_win32()
 * (platform/windows/gsr_egl_win32.c), which runs ANGLE on a D3D11 device
 * (architecture §3.3 Option B). This header exposes the Windows-specific
 * pieces beyond the loader itself:
 *
 *   - the shared D3D11 device accessor (capture backends must create their
 *     frame pools on the SAME device ANGLE uses, for a zero-copy import);
 *   - the D3D11-texture -> GL_TEXTURE_2D import ("imported texture" object
 *     with an opaque handle) implemented with
 *     EGL_ANGLE_d3d_texture_client_buffer + EGL_ANGLE_device_d3d.
 *
 * Pure C; used by the C++/WinRT WGC backend and the C render self-test.
 */
#ifndef GSR_EGL_WIN32_H
#define GSR_EGL_WIN32_H

#include <stdbool.h>

/* The functions below are defined in C (platform/windows/gsr_egl_win32.c)
   and called from the C++/WinRT WGC backend, so they need C linkage. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct gsr_egl gsr_egl;

/* The ID3D11Device* (AddRef'd, caller releases) that the ANGLE display runs
 * on. NULL if the egl was not loaded. Capture backends that need the WinRT
 * IDirect3DDevice wrapper (WGC frame pool) must use this device. */
void *gsr_platform_egl_get_d3d11_device(gsr_egl *egl);

/* ---- D3D11 texture import (EGL_ANGLE_d3d_texture_client_buffer) ----------
 * An "imported texture" is a heap object owning one GL_TEXTURE_2D plus the
 * EGLImage that backs it. The GL texture id is stable across rebinds, so a
 * capture backend creates it once and calls gsr_platform_egl_update_texture
 * each frame with the new D3D11 texture; the image is destroyed and
 * recreated, the texture redefined — no per-frame GL object churn.
 *
 * |texture| is an ID3D11Texture2D* on the SAME device the egl runs on
 * (see gsr_platform_egl_get_d3d11_device). Returns a nonzero handle or 0. */

void *gsr_platform_egl_import_texture(gsr_egl *egl, void *texture);
/* Rebind |handle| to a new D3D11 texture. No-op if |texture| is the same
   texture as the last import. Returns false on failure (handle stays
   bound to its previous texture). */
bool gsr_platform_egl_update_texture(gsr_egl *egl, void *handle, void *texture);
/* The GL_TEXTURE_2D id to pass to gsr_color_conversion_draw(). */
unsigned int gsr_platform_egl_texture_id(gsr_egl *egl, void *handle);
void gsr_platform_egl_destroy_imported_texture(gsr_egl *egl, void *handle);

#ifdef __cplusplus
}
#endif

#endif /* GSR_EGL_WIN32_H */
