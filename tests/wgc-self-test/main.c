/* tests/wgc-self-test/main.c — Phase 5 WGC capture self-test.
 *
 * Runs headless on CI (the runner's virtual display): creates the WGC
 * capture backend for the primary monitor, starts it, waits for a frame,
 * and validates the captured D3D11 texture + the recorder damage contract.
 *
 *   - WGC unavailable in this session (IsSupported() == false) -> SKIP,
 *     exit 0 (environment-limited; roadmap Phase 5, brief §64).
 *   - Any real capture failure -> FAIL, exit 1.
 *
 * Also probes the Option-B ANGLE interop (architecture §3.3):
 * EGL_ANGLE_d3d_texture_client_buffer imports the WGC D3D11 texture into a
 * GL context running on the SAME D3D11 device (EGL_ANGLE_device_d3d). This
 * segment is informational — it needs libEGL.dll (MSYS2 angleproject),
 * present in the ctest step but not on the plain runner — and never fails
 * the run; it reports whether the zero-copy import works.
 */
#include "display.h"
#include "capture.h"

#include "../../upstream/include/capture/capture.h"
#include "../../upstream/include/log.h"

#include <windows.h>
#include <d3d11.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

/* ---- ANGLE interop probe (informational) ---------------------------------
 * Passes the WGC D3D11 texture (not the device) as the EGL client buffer:
 * eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_D3D_TEXTURE_ANGLE,
 * (EGLClientBuffer)texture, ...) is the zero-copy import from
 * EGL_ANGLE_d3d_texture_client_buffer. */

/* EGL tokens used by the probe (declared locally so the binary has no link
   or compile dependency on ANGLE; everything is loaded dynamically). */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLImage;
typedef void *EGLDeviceEXT;
typedef void *EGLClientBuffer;
typedef intptr_t EGLint;
typedef intptr_t EGLAttrib;
typedef int (*EGLGetProcAddress_t)(const char *);
#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_DEVICE_EXT ((EGLDeviceEXT)0)
#define EGL_NONE 0x3038
#define EGL_NO_SURFACE ((void*)0)
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE 0x3209
#define EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D11_ANGLE 0x320B
#define EGL_D3D11_DEVICE_ANGLE 0x33A2
#define EGL_D3D_TEXTURE_ANGLE 0x33A3
#define EGL_EXTENSIONS 0x3055
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#define GL_NO_ERROR 0

typedef EGLDisplay (*eglGetPlatformDisplayEXT_t)(EGLint, void *, const EGLint *);
typedef EGLDeviceEXT (*eglCreateDeviceANGLE_t)(EGLint, void *, const EGLAttrib *);
typedef EGLint (*eglInitialize_t)(EGLDisplay, EGLint *, EGLint *);
typedef EGLint (*eglBindAPI_t)(EGLint);
typedef EGLint (*eglChooseConfig_t)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
typedef EGLContext (*eglCreateContext_t)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
typedef EGLint (*eglMakeCurrent_t)(EGLDisplay, void *, void *, EGLContext);
typedef const char *(*eglQueryString_t)(EGLDisplay, EGLint);
typedef EGLImage (*eglCreateImageKHR_t)(EGLDisplay, EGLContext, EGLint, EGLClientBuffer, const EGLint *);
typedef EGLint (*eglGetError_t)(void);
typedef void (*glEGLImageTargetTexture2DOES_t)(EGLint, EGLImage);
typedef void (*glGetError_t)(void);
typedef void (*glGetTexLevelParameteriv_t)(EGLint, EGLint, EGLint, EGLint *);
typedef EGLGetProcAddress_t (*eglGetProcAddress_fn)(const char *);

static void run_angle_probe(ID3D11Device *device, ID3D11Texture2D *texture) {
    printf("-- angle interop probe\n");

    HMODULE egl_module = LoadLibraryA("libEGL.dll");
    if(!egl_module) {
        printf("SKIP: libEGL.dll not loadable (ANGLE not installed; only in the MSYS2 ctest step)\n");
        return;
    }
    /* GLESv2 must be loadable too (same package) */
    HMODULE gles_module = LoadLibraryA("libGLESv2.dll");
    if(!gles_module) {
        printf("SKIP: libGLESv2.dll not loadable\n");
        return;
    }

    eglGetProcAddress_fn eglGetProcAddress = (eglGetProcAddress_fn)GetProcAddress(egl_module, "eglGetProcAddress");
    if(!eglGetProcAddress) {
        printf("SKIP: eglGetProcAddress missing\n");
        return;
    }
    eglGetPlatformDisplayEXT_t eglGetPlatformDisplayEXT = (eglGetPlatformDisplayEXT_t)GetProcAddress(egl_module, "eglGetPlatformDisplayEXT");
    eglCreateDeviceANGLE_t eglCreateDeviceANGLE = (eglCreateDeviceANGLE_t)GetProcAddress(egl_module, "eglCreateDeviceANGLE");
    eglInitialize_t eglInitialize = (eglInitialize_t)GetProcAddress(egl_module, "eglInitialize");
    eglBindAPI_t eglBindAPI = (eglBindAPI_t)GetProcAddress(egl_module, "eglBindAPI");
    eglChooseConfig_t eglChooseConfig = (eglChooseConfig_t)GetProcAddress(egl_module, "eglChooseConfig");
    eglCreateContext_t eglCreateContext = (eglCreateContext_t)GetProcAddress(egl_module, "eglCreateContext");
    eglMakeCurrent_t eglMakeCurrent = (eglMakeCurrent_t)GetProcAddress(egl_module, "eglMakeCurrent");
    eglQueryString_t eglQueryString = (eglQueryString_t)GetProcAddress(egl_module, "eglQueryString");
    eglGetError_t eglGetError = (eglGetError_t)GetProcAddress(egl_module, "eglGetError");
    eglCreateImageKHR_t eglCreateImageKHR = (eglCreateImageKHR_t)eglGetProcAddress("eglCreateImageKHR");
    glEGLImageTargetTexture2DOES_t glEGLImageTargetTexture2DOES = (glEGLImageTargetTexture2DOES_t)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    glGetError_t glGetError = (glGetError_t)eglGetProcAddress("glGetError");
    glGetTexLevelParameteriv_t glGetTexLevelParameteriv = (glGetTexLevelParameteriv_t)eglGetProcAddress("glGetTexLevelParameteriv");

    if(!eglGetPlatformDisplayEXT || !eglCreateDeviceANGLE || !eglInitialize || !eglBindAPI ||
       !eglChooseConfig || !eglCreateContext || !eglMakeCurrent || !eglQueryString ||
       !eglCreateImageKHR || !glEGLImageTargetTexture2DOES || !glGetError || !glGetTexLevelParameteriv) {
        printf("SKIP: required EGL/GLES entry points missing (old ANGLE build?)\n");
        return;
    }

    /* Run ANGLE on the SAME D3D11 device so the WGC texture is importable
       zero-copy (EGL_ANGLE_device_d3d: texture must come from the display's
       device). */
    EGLDeviceEXT egl_device = eglCreateDeviceANGLE(EGL_D3D11_DEVICE_ANGLE, (void*)device, NULL);
    if(egl_device == EGL_NO_DEVICE_EXT) {
        printf("INFO: eglCreateDeviceANGLE failed (0x%llx) — ANGLE device path unavailable\n", (unsigned long long)eglGetError());
        return;
    }
    const EGLint platform_attrs[] = {
        EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_D3D11_ANGLE,
        EGL_NONE
    };
    EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, (void*)egl_device, platform_attrs);
    if(display == EGL_NO_DISPLAY) {
        printf("INFO: eglGetPlatformDisplayEXT failed\n");
        return;
    }
    EGLint major = 0, minor = 0;
    if(!eglInitialize(display, &major, &minor)) {
        printf("INFO: eglInitialize failed\n");
        return;
    }
    printf("angle: EGL %d.%d on D3D11 device (vendor: %s)\n", (int)major, (int)minor, eglQueryString(display, 0x3053 /* EGL_VENDOR */));

    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    const bool has_client_buffer_ext = extensions && strstr(extensions, "EGL_ANGLE_d3d_texture_client_buffer") != NULL;
    const bool has_device_d3d = extensions && strstr(extensions, "EGL_ANGLE_device_d3d") != NULL;
    printf("angle: EGL_ANGLE_d3d_texture_client_buffer=%s EGL_ANGLE_device_d3d=%s\n",
        has_client_buffer_ext ? "yes" : "NO", has_device_d3d ? "yes" : "NO");
    if(!has_client_buffer_ext || !has_device_d3d) {
        printf("WARNING: ANGLE build lacks the D3D11-texture-import extensions; Option B needs an ANGLE with them enabled\n");
        return;
    }

    if(!eglBindAPI(EGL_OPENGL_ES_API))
        return;
    EGLConfig config = NULL;
    EGLint num_configs = 0;
    const EGLint config_attrs[] = { EGL_NONE };
    if(!eglChooseConfig(display, config_attrs, &config, 1, &num_configs) || num_configs == 0) {
        printf("INFO: eglChooseConfig failed\n");
        return;
    }
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if(!context) {
        printf("INFO: eglCreateContext failed\n");
        return;
    }
    if(!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        printf("INFO: eglMakeCurrent failed\n");
        return;
    }

    /* Import the WGC D3D11 texture as a GL_TEXTURE_2D. */
    EGLImage image = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_D3D_TEXTURE_ANGLE, (EGLClientBuffer)texture, NULL);
    printf("angle: eglCreateImageKHR returned %s (texture import %s)\n",
        image ? "an image" : "NULL", image ? "OK" : "FAILED");
    printf("angle: probe complete (informational)\n");
}

/* ---- WGC capture self-test ------------------------------------------------ */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("wgc-self-test: Phase 5 WGC capture self-test\n");

    if(!gsr_platform_capture_backend_available(GSR_CAPTURE_BACKEND_WGC)) {
        printf("SKIP: Windows Graphics Capture is not supported in this session (headless/service); exit 0\n");
        return 0;
    }
    printf("wgc: GraphicsCaptureSession::IsSupported() == true\n");

    /* Primary monitor via the Phase 4 enumeration. */
    gsr_platform_monitor *monitors = NULL;
    int monitor_count = 0;
    CHECK(gsr_platform_display_list_monitors(&monitors, &monitor_count));
    if(monitor_count < 1) {
        fprintf(stderr, "FAIL: no monitors enumerated\n");
        ++num_failures;
        free(monitors);
        return 1;
    }
    const gsr_platform_monitor *primary = &monitors[0];
    for(int i = 0; i < monitor_count; ++i) {
        if(monitors[i].is_primary) {
            primary = &monitors[i];
            break;
        }
    }
    printf("wgc: primary monitor = %s (%dx%d)\n", primary->name, primary->width, primary->height);

    void *hmon = gsr_platform_display_find_hmonitor(primary->name);
    CHECK(hmon != NULL);
    if(!hmon) {
        free(monitors);
        return 1;
    }

    gsr_platform_wgc_target target;
    memset(&target, 0, sizeof(target));
    target.kind = GSR_PLATFORM_WGC_TARGET_MONITOR;
    target.handle = hmon;
    snprintf(target.name, sizeof(target.name), "%s", primary->name);

    gsr_platform_wgc_options options;
    memset(&options, 0, sizeof(options));
    options.cursor = false;

    gsr_capture *cap = gsr_platform_capture_wgc_create(&target, &options);
    CHECK(cap != NULL);
    if(!cap) {
        free(monitors);
        return 1;
    }

    gsr_capture_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    CHECK(cap->start(cap, &metadata) == 0);
    if(num_failures > 0) {
        cap->destroy(cap);
        free(monitors);
        return 1;
    }
    CHECK(metadata.video_size.x > 0 && metadata.video_size.y > 0);
    printf("wgc: capture started, video_size=%dx%d\n", metadata.video_size.x, metadata.video_size.y);

    /* Poll for the first frame (WGC delivers an initial snapshot quickly). */
    bool got_frame = false;
    for(int attempt = 0; attempt < 200 && !got_frame; ++attempt) {
        cap->tick(cap);

        bool err = false;
        if(cap->should_stop(cap, &err)) {
            fprintf(stderr, "FAIL: capture stopped prematurely (err=%d)\n", err ? 1 : 0);
            ++num_failures;
            break;
        }

        if(cap->is_damaged(cap)) {
            /* recorder contract: clear_damage() precedes capture() */
            cap->clear_damage(cap);
            CHECK(!cap->is_damaged(cap));
            got_frame = true;
        } else {
            Sleep(50);
        }
    }
    if(!got_frame) {
        fprintf(stderr, "FAIL: no frame arrived within 10s\n");
        ++num_failures;
    }

    if(got_frame) {
        void *texture = NULL;
        int frame_w = 0, frame_h = 0;
        CHECK(gsr_platform_capture_wgc_get_frame(cap, &texture, &frame_w, &frame_h));
        CHECK(texture != NULL);
        CHECK(frame_w > 0 && frame_h > 0);
        printf("wgc: frame captured: %dx%d (texture %p)\n", frame_w, frame_h, texture);

        if(texture) {
            ID3D11Texture2D *tex = (ID3D11Texture2D*)texture;
            D3D11_TEXTURE2D_DESC desc;
            /* MinGW's d3d11.h only exposes COM vtable calls via the
               ID3D11Texture2D_* inline wrappers in C (member-style calls
               are C++-only). */
            ID3D11Texture2D_GetDesc(tex, &desc);
            CHECK((int)desc.Width == frame_w && (int)desc.Height == frame_h);
            CHECK(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
            printf("wgc: texture format = %u (BGRA8 expected), mips=%u\n", (unsigned)desc.Format, desc.MipLevels);

            /* The recorder's damage was cleared before capture(); capture()
               delivers the frame, so damage stays clear after. */
            CHECK(cap->capture(cap, &metadata, NULL) == 0);
            CHECK(!cap->is_damaged(cap));

            /* Informational Option-B ANGLE import probe. */
            ID3D11Device *device = NULL;
            ID3D11DeviceChild_GetDevice((ID3D11DeviceChild*)tex, &device);
            run_angle_probe(device, tex);
            if(device)
                ID3D11Device_Release(device);
        }
    }

    cap->destroy(cap);
    free(monitors);

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
