/* tests/render-self-test/main.c — Phase 5b render pipeline self-test.
 *
 * Validates architecture §3.3 Option B (GL via ANGLE) end-to-end, headless:
 *
 *   gsr_egl_load (ANGLE on D3D11, WARP fallback)
 *     -> synthetic BGRA8 D3D11 texture
 *     -> EGL_ANGLE_d3d_texture_client_buffer import (gsr_platform_egl_*)
 *     -> upstream gsr_color_conversion_draw (unchanged upstream pipeline)
 *     -> readback + pixel validation (BGR swizzle, orientation, rotation)
 *
 * This is exactly the path the WGC backend's capture() takes
 * (gsr_capture_wgc.cpp) minus Windows.Graphics.Capture itself, which cannot
 * run on the Server-SKU CI runner (§3e). ANGLE is the MSYS2
 * mingw-w64-x86_64-angleproject package (libEGL.dll/libGLESv2.dll, on PATH
 * in the ctest step); on the plain runner where ANGLE is absent the test
 * SKIPs (exit 0) like the WGC self-test.
 */
#include "egl_win32.h"
#include "../../upstream/include/egl.h"
#include "../../upstream/include/color_conversion.h"
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

#define TEX_W 64
#define TEX_H 48

/* BGRA8 pixel helpers (matches WGC's B8G8R8A8_UNORM) */
static void fill_solid(unsigned char *pixels, int w, int h, unsigned char b, unsigned char g, unsigned char r) {
    for(int i = 0; i < w * h; ++i) {
        pixels[i*4 + 0] = b;
        pixels[i*4 + 1] = g;
        pixels[i*4 + 2] = r;
        pixels[i*4 + 3] = 255;
    }
}

/* left half red (BGRA {0,0,255,255}), right half blue (BGRA {255,0,0,255}) */
static void fill_split(unsigned char *pixels, int w, int h) {
    for(int y = 0; y < h; ++y) {
        for(int x = 0; x < w; ++x) {
            const int i = (y * w + x) * 4;
            if(x < w / 2) {
                pixels[i+0] = 0;   /* B */
                pixels[i+1] = 0;   /* G */
                pixels[i+2] = 255; /* R */
            } else {
                pixels[i+0] = 255; /* B */
                pixels[i+1] = 0;   /* G */
                pixels[i+2] = 0;   /* R */
            }
            pixels[i+3] = 255;
        }
    }
}

/* Replaces the contents of a D3D11 texture (the per-frame WGC equivalent). */
static void update_texture(ID3D11Device *device, ID3D11Texture2D *texture, const unsigned char *pixels, int w, int h) {
    (void)h;
    ID3D11DeviceContext *context = NULL;
    device->lpVtbl->GetImmediateContext(device, &context);
    if(!context)
        return;
    context->lpVtbl->UpdateSubresource(context, (ID3D11Resource*)texture, 0, NULL, pixels, (UINT)(w * 4), 0);
    context->lpVtbl->Release(context);
}

/* Creates a BGRA8 D3D11 texture on |device| initialized from |pixels|. */
static ID3D11Texture2D *create_texture(ID3D11Device *device, const unsigned char *pixels, int w, int h) {
    D3D11_TEXTURE2D_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data;
    memset(&data, 0, sizeof(data));
    data.pSysMem = pixels;
    data.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D *texture = NULL;
    if(FAILED(device->lpVtbl->CreateTexture2D(device, &desc, &data, &texture))) {
        fprintf(stderr, "FAIL: D3D11 CreateTexture2D failed\n");
        ++num_failures;
        return NULL;
    }
    return texture;
}

/* Creates the destination GL texture the color conversion renders into. */
static unsigned int create_destination_texture(gsr_egl *egl, int w, int h) {
    unsigned int texture_id = 0;
    egl->glGenTextures(1, &texture_id);
    egl->glBindTexture(GL_TEXTURE_2D, texture_id);
    egl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    egl->glBindTexture(GL_TEXTURE_2D, 0);
    return texture_id;
}

static void run_pipeline_test(gsr_egl *egl) {
    printf("-- render pipeline (D3D11 -> ANGLE -> color_conversion -> readback)\n");

    /* The egl owns the D3D11 device; the test texture must live on it so
       the import is on the same device (the zero-copy requirement). */
    ID3D11Device *device = (ID3D11Device*)gsr_platform_egl_get_d3d11_device(egl);
    CHECK(device != NULL);
    if(!device)
        return;

    unsigned char source[TEX_W * TEX_H * 4];

    /* Test 1 — BGR swizzle: a solid red BGRA8 texture must come back as
       pure red RGB after the pipeline's GSR_SOURCE_COLOR_BGR handling. */
    fill_solid(source, TEX_W, TEX_H, 0, 0, 255);
    ID3D11Texture2D *texture = create_texture(device, source, TEX_W, TEX_H);
    CHECK(texture != NULL);
    if(!texture) {
        device->lpVtbl->Release(device);
        return;
    }

    void *import = gsr_platform_egl_import_texture(egl, texture);
    CHECK(import != NULL);
    if(!import) {
        fprintf(stderr, "FAIL: EGL_ANGLE_d3d_texture_client_buffer import failed (ANGLE build missing the extension?)\n");
        texture->lpVtbl->Release(texture);
        device->lpVtbl->Release(device);
        return;
    }
    const unsigned int input_texture_id = gsr_platform_egl_texture_id(egl, import);
    CHECK(input_texture_id != 0);
    printf("render: imported D3D11 texture as GL_TEXTURE_2D %u\n", input_texture_id);

    const unsigned int dest_texture_id = create_destination_texture(egl, TEX_W, TEX_H);
    CHECK(dest_texture_id != 0);

    gsr_color_conversion color_conversion;
    gsr_color_conversion_params params;
    memset(&params, 0, sizeof(params));
    params.egl = egl;
    params.destination_color = GSR_DESTINATION_COLOR_RGB;
    params.destination_textures[0] = dest_texture_id;
    params.destination_textures_size[0] = (vec2i){TEX_W, TEX_H};
    params.num_destination_textures = 1;
    params.color_range = GSR_COLOR_RANGE_FULL;
    params.load_external_image_shader = false; /* WGC imports bind GL_TEXTURE_2D, not EXTERNAL_OES */
    CHECK(gsr_color_conversion_init(&color_conversion, &params) == 0);
    if(num_failures > 0) {
        texture->lpVtbl->Release(texture);
        device->lpVtbl->Release(device);
        return;
    }

    gsr_capture_metadata metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.video_size = (vec2i){TEX_W, TEX_H};
    metadata.recording_size = (vec2i){TEX_W, TEX_H};

    const vec2i full = {TEX_W, TEX_H};
    unsigned char readback[TEX_W * TEX_H * 4];

    /* Test 1: solid red, no rotation. */
    gsr_color_conversion_draw(&color_conversion, input_texture_id,
        (vec2i){0, 0}, full, (vec2i){0, 0}, full, full,
        GSR_ROT_0, GSR_FLIP_NONE, GSR_SOURCE_COLOR_BGR, false);
    gsr_color_conversion_read_destination_texture(&color_conversion, 0, 0, 0, TEX_W, TEX_H, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    {
        const unsigned char *p = &readback[(TEX_H / 2 * TEX_W + TEX_W / 2) * 4];
        printf("render: solid-red center pixel = {%u, %u, %u, %u}\n", p[0], p[1], p[2], p[3]);
        CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0); /* R=255 G=0 B=0 */
    }

    /* Test 2: left/right split, no rotation — orientation + swizzle. */
    fill_split(source, TEX_W, TEX_H);
    update_texture(device, texture, source, TEX_W, TEX_H);
    CHECK(gsr_platform_egl_update_texture(egl, import, texture)); /* same texture id: no-op */
    gsr_color_conversion_draw(&color_conversion, input_texture_id,
        (vec2i){0, 0}, full, (vec2i){0, 0}, full, full,
        GSR_ROT_0, GSR_FLIP_NONE, GSR_SOURCE_COLOR_BGR, false);
    gsr_color_conversion_read_destination_texture(&color_conversion, 0, 0, 0, TEX_W, TEX_H, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    {
        const unsigned char *left = &readback[(TEX_H / 2 * TEX_W + TEX_W / 4) * 4];
        const unsigned char *right = &readback[(TEX_H / 2 * TEX_W + 3 * TEX_W / 4) * 4];
        printf("render: split pixels left={%u,%u,%u} right={%u,%u,%u}\n", left[0], left[1], left[2], right[0], right[1], right[2]);
        CHECK(left[0] == 255 && left[1] == 0 && left[2] == 0);   /* left half red */
        CHECK(right[0] == 0 && right[1] == 0 && right[2] == 255); /* right half blue */
    }

    /* Test 3: same split with GSR_ROT_180 — halves must swap. */
    gsr_color_conversion_draw(&color_conversion, input_texture_id,
        (vec2i){0, 0}, full, (vec2i){0, 0}, full, full,
        GSR_ROT_180, GSR_FLIP_NONE, GSR_SOURCE_COLOR_BGR, false);
    gsr_color_conversion_read_destination_texture(&color_conversion, 0, 0, 0, TEX_W, TEX_H, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    {
        const unsigned char *left = &readback[(TEX_H / 2 * TEX_W + TEX_W / 4) * 4];
        const unsigned char *right = &readback[(TEX_H / 2 * TEX_W + 3 * TEX_W / 4) * 4];
        printf("render: ROT_180 split pixels left={%u,%u,%u} right={%u,%u,%u}\n", left[0], left[1], left[2], right[0], right[1], right[2]);
        CHECK(left[0] == 0 && left[1] == 0 && left[2] == 255);   /* left half now blue */
        CHECK(right[0] == 255 && right[1] == 0 && right[2] == 0); /* right half now red */
    }

    gsr_color_conversion_deinit(&color_conversion);
    gsr_platform_egl_destroy_imported_texture(egl, import);
    egl->glDeleteTextures(1, &dest_texture_id);
    texture->lpVtbl->Release(texture);
    device->lpVtbl->Release(device);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("render-self-test: Phase 5b ANGLE render pipeline self-test\n");

    gsr_egl egl;
    if(!gsr_egl_load(&egl, NULL, false, false)) {
        printf("SKIP: ANGLE (libEGL.dll/libGLESv2.dll) not loadable in this session; exit 0\n");
        return 0;
    }
    printf("render: GL_VENDOR=%s GL_RENDERER=%s\n",
        (const char*)egl.glGetString(GL_VENDOR), (const char*)egl.glGetString(GL_RENDERER));

    run_pipeline_test(&egl);

    gsr_egl_unload(&egl);

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
