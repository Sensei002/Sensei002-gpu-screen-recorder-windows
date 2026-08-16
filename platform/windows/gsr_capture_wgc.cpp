/* gsr_capture_wgc.cpp — Windows Graphics Capture (WGC) capture backend.
 *
 * Phase 5 deliverable. Implements the upstream gsr_capture vtable
 * (upstream/include/capture/capture.h) for monitor/window capture via
 * Windows.Graphics.Capture, delivering the captured D3D11 texture to the GL
 * (ANGLE) pipeline per docs/architecture.md §3.3 Option B (imported
 * zero-copy via EGL_ANGLE_d3d_texture_client_buffer).
 *
 * Written in C++ with C++/WinRT (MSYS2 package mingw-w64-x86_64-cppwinrt).
 * The public API is extern "C" (platform/include/capture.h) so the pure-C
 * engine and tests never see C++/WinRT.
 *
 * The desktop-interop interfaces IGraphicsCaptureItemInterop and
 * IDirect3DDxgiInterfaceAccess are NOT part of the C++/WinRT projection
 * (they live in the Windows SDK's desktop-interop headers, which MinGW-w64
 * does not ship), so they are declared here with their documented IIDs —
 * see docs/upstream-porting-notes.md §3e. Frame delivery is polled
 * (TryGetNextFrame in tick()) rather than event-driven, which needs no
 * DispatcherQueue and matches the recorder's tick() model.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef WINVER
#define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
#include <windows.h>
#include <roapi.h>       /* RoGetActivationFactory */
#include <d3d11.h>
#include <dxgi.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <new>

#include "../../platform/include/capture.h"
#include "../../platform/include/egl_win32.h"

/* The upstream headers below declare C functions (gsr_color_conversion_draw,
   scale_keep_aspect_ratio, gsr_capture_get_target_position) without
   extern "C" guards; wrap them so this C++ TU links the C definitions. */
extern "C" {
#include "../../upstream/include/capture/capture.h"
#include "../../upstream/include/vec2.h"
#include "../../upstream/include/utils.h"
}

/* upstream's log.h has no extern "C" guard; gsr_log is defined as C in
   log.c, so the include must be wrapped or the C++ TU links a mangled
   gsr_log. Nothing else in this TU's include chain pulls log.h. */
extern "C" {
#include "../../upstream/include/log.h"
}

/* ---- desktop-interop interfaces (not in the C++/WinRT projection) -------- */

/* IGraphicsCaptureItemInterop — windows.graphics.capture.interop.h.
 * IID 3628e81b-3cac-4c60-b7f4-23ce0e0c3356 (documented; verified against
 * the Microsoft WGC samples and the cppwinrt metadata). */
struct __declspec(uuid("3628e81b-3cac-4c60-b7f4-23ce0e0c3356")) IGraphicsCaptureItemInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND appWindow, REFIID riid, void **result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID riid, void **result) = 0;
};

/* IDirect3DDxgiInterfaceAccess — windows.graphics.directx.direct3d11.interop.h.
 * IID a9b3d012-3df2-4ee3-b8d1-8695f457d3c1 (documented; verified against
 * Microsoft Learn and the Windows SDK metadata). */
struct __declspec(uuid("a9b3d012-3df2-4ee3-b8d1-8695f457d3c1")) IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void **p) = 0;
};

/* The interop IIDs as plain GUID constants. winrt::guid_of<T>() relies on
 * __mingw_uuidof, which GCC cannot resolve for these locally-declared
 * interfaces ("used before its definition" — a known MinGW quirk), so the
 * code below passes explicit GUID values instead of guid_of. */
static const GUID GSR_IID_IGraphicsCaptureItemInterop = {0x3628e81b, 0x3cac, 0x4c60, {0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56}};
static const GUID GSR_IID_IDirect3DDxgiInterfaceAccess = {0xa9b3d012, 0x3df2, 0x4ee3, {0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1}};

/* CreateDirect3D11DeviceFromDXGIDevice — free function exported by
 * Windows.Graphics.DirectX.Direct3D11.dll (no import lib/header needed). */
typedef HRESULT(WINAPI *CreateDirect3D11DeviceFromDXGIDevice_t)(IDXGIDevice *dxgiDevice, void **graphicsDevice);

/* Loads Windows.Graphics.DirectX.Direct3D11.dll and returns the
 * CreateDirect3D11DeviceFromDXGIDevice export, or NULL. Tries the plain
 * name first (client Windows finds it in System32), then the full
 * System32 path (some Server SKUs / environments fail the bare-name
 * lookup even though the file is present). Logs the failure reason. */
static CreateDirect3D11DeviceFromDXGIDevice_t load_create_d3d11_device(void) {
    HMODULE module = LoadLibraryW(L"Windows.Graphics.DirectX.Direct3D11.dll");
    if(!module) {
        wchar_t sysdir[MAX_PATH];
        if(GetSystemDirectoryW(sysdir, MAX_PATH) != 0) {
            wchar_t full_path[MAX_PATH];
            if(swprintf(full_path, MAX_PATH, L"%ls\\Windows.Graphics.DirectX.Direct3D11.dll", sysdir) > 0)
                module = LoadLibraryW(full_path);
        }
        if(!module)
            gsr_log(GSR_LOG_LEVEL_ERROR,
                "gsr_capture_wgc: cannot load Windows.Graphics.DirectX.Direct3D11.dll (GetLastError=0x%08lx; System32 file %s)",
                (unsigned long)GetLastError(),
                GetFileAttributesW(L"C:\\Windows\\System32\\Windows.Graphics.DirectX.Direct3D11.dll") != INVALID_FILE_ATTRIBUTES ? "present" : "ABSENT");
    }
    if(!module)
        return NULL;
    auto proc = (CreateDirect3D11DeviceFromDXGIDevice_t)GetProcAddress(module, "CreateDirect3D11DeviceFromDXGIDevice");
    if(!proc)
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: CreateDirect3D11DeviceFromDXGIDevice export missing");
    return proc;
}

/* ---- documented interface IIDs (layout-identical GUIDs) ------------------ */

/* IID_IDXGIDevice — 54ec77fa-1377-44e6-8c32-88fd5f44c84c */
static const GUID GSR_IID_IDXGIDevice = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
/* IID_ID3D11Texture2D — 6f15aaf2-d208-4e89-9ab4-489535d34f9c */
static const GUID GSR_IID_ID3D11Texture2D = {0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}};
/* IID of the Windows.Graphics.Capture.GraphicsCaptureItem runtimeclass —
 * 79c3f95b-31f7-4ec2-a464-632ef5d30760 (from the cppwinrt projection). */
static const GUID GSR_IID_GraphicsCaptureItem = {0x79c3f95b, 0x31f7, 0x4ec2, {0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60}};

/* ---- backend state ------------------------------------------------------- */

typedef struct gsr_capture_wgc {
    gsr_platform_wgc_target target;
    gsr_platform_wgc_options options;
    gsr_platform_wgc_damage damage;

    bool started;
    bool should_stop;
    bool stop_is_error;
    bool is_hdr;

    winrt::com_ptr<ID3D11Device> device;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice d3d_device{nullptr};

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};

    /* Latest frame: the frame object must stay alive while its texture is
       referenced (WGC reuses the pool's buffers once the frame is gone). */
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame latest_frame{nullptr};
    winrt::com_ptr<ID3D11Texture2D> latest_texture;
    int frame_width;
    int frame_height;

    /* Phase 5b: the imported GL texture (EGL_ANGLE_d3d_texture_client_buffer)
       drawn into the color conversion by capture(). NULL until capture()
       runs with a GL pipeline. */
    void *import_handle;
    gsr_egl *egl;
} gsr_capture_wgc;

/* ---- vtable implementations ---------------------------------------------- */

static int wgc_start(gsr_capture *cap, gsr_capture_metadata *capture_metadata) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    if(self->started)
        return 0;

    try {
        /* 1. D3D11 device. Phase 5b: when a GL pipeline (ANGLE egl) is
           configured, the WGC frame pool MUST run on the SAME device ANGLE
           runs on, or the EGL_ANGLE_d3d_texture_client_buffer import is a
           copy (or fails) — share it. Standalone (self-test without egl):
           hardware first, WARP fallback. */
        self->egl = self->options.egl;
        HRESULT hr = S_OK;
        if(self->egl && self->egl->d3d11_device) {
            self->device.copy_from((ID3D11Device*)self->egl->d3d11_device);
            gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_wgc_start: using the shared ANGLE D3D11 device");
        } else {
            static const D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
            };
            const UINT create_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; /* ANGLE interop */
            D3D_FEATURE_LEVEL got_level = D3D_FEATURE_LEVEL_10_0;
            winrt::com_ptr<ID3D11Device> device;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_flags,
                levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, device.put(), &got_level, nullptr);
            if(FAILED(hr)) {
                gsr_log(GSR_LOG_LEVEL_INFO, "gsr_capture_wgc_start: hardware device unavailable (0x%08lx), using WARP", (unsigned long)hr);
                hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, create_flags,
                    levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, device.put(), &got_level, nullptr);
            }
            if(FAILED(hr)) {
                gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: D3D11CreateDevice failed (0x%08lx)", (unsigned long)hr);
                return -1;
            }
            self->device = device;
        }

        /* 2. Wrap the D3D11 device as a WinRT IDirect3DDevice for the frame
           pool (Windows.Graphics.DirectX.Direct3D11.dll ships with Win10+;
           the free function needs no import lib or header). */
        winrt::com_ptr<IDXGIDevice> dxgi_device;
        hr = self->device->QueryInterface(GSR_IID_IDXGIDevice, dxgi_device.put_void());
        if(FAILED(hr)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: device has no IDXGIDevice (0x%08lx)", (unsigned long)hr);
            return -1;
        }
        auto create_d3d11_device = load_create_d3d11_device();
        if(!create_d3d11_device) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: Windows.Graphics.DirectX.Direct3D11 interop unavailable (missing runtime on this SKU?)");
            return -1;
        }
        void *raw_winrt_device = nullptr;
        hr = create_d3d11_device(dxgi_device.get(), &raw_winrt_device);
        if(FAILED(hr)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: CreateDirect3D11DeviceFromDXGIDevice failed (0x%08lx)", (unsigned long)hr);
            return -1;
        }
        /* Wrap the raw ABI pointer into the projected type, taking
           ownership (this MSYS2 cppwinrt has no attach_abi<T>(void*)). */
        self->d3d_device = winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice{ raw_winrt_device, winrt::take_ownership_from_abi };

        /* 3. Capture item: monitor or window via the interop factory. */
        winrt::com_ptr<IGraphicsCaptureItemInterop> interop;
        winrt::hstring factory_name{ L"Windows.Graphics.Capture.GraphicsCaptureItem" };
        hr = RoGetActivationFactory(static_cast<HSTRING>(winrt::get_abi(factory_name)),
            GSR_IID_IGraphicsCaptureItemInterop, interop.put_void());
        if(FAILED(hr)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: no WGC interop factory (0x%08lx)", (unsigned long)hr);
            return -1;
        }
        if(self->target.kind == GSR_PLATFORM_WGC_TARGET_MONITOR)
            hr = interop->CreateForMonitor((HMONITOR)self->target.handle, GSR_IID_GraphicsCaptureItem, winrt::put_abi(self->item));
        else
            hr = interop->CreateForWindow((HWND)self->target.handle, GSR_IID_GraphicsCaptureItem, winrt::put_abi(self->item));
        if(FAILED(hr)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: CreateFor%s failed (0x%08lx)",
                self->target.kind == GSR_PLATFORM_WGC_TARGET_MONITOR ? "Monitor" : "Window", (unsigned long)hr);
            return -1;
        }

        /* 4. Frame pool + session (2 buffers; BGRA8 = upstream BGR source).
           Note: this projection names the enum DirectXPixelFormat and the
           item size type SizeInt32 (the older Direct3DPixelFormat /
           Foundation::Size names are gone). */
        const winrt::Windows::Graphics::SizeInt32 item_size = self->item.Size();
        self->frame_pool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::Create(
            self->d3d_device,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2, item_size);
        self->session = self->frame_pool.CreateCaptureSession(self->item);
        if(!self->options.cursor) {
            /* Cursor capture is on by default; only touch the property when
               disabling (older builds lack it — ignore failures). */
            try { self->session.IsCursorCaptureEnabled(false); } catch(...) { }
        }
        self->session.StartCapture();

        /* 5. Metadata. WGC delivers the content as displayed: a rotated
           monitor's frame is already rotated, so the effective size is the
           item size and the color-conversion draw rotation is ROT_0 (the
           monitor's physical rotation is still reported via
           gsr_platform_wgc_rotation_from_monitor for the metadata). */
        capture_metadata->video_size = (vec2i){(int)item_size.Width, (int)item_size.Height};
        self->started = true;
        return 0;
    } catch(const winrt::hresult_error &e) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_start: %s (0x%08lx)",
            winrt::to_string(e.message()).c_str(), (unsigned long)e.code().value);
        return -1;
    }
}

static void wgc_tick(gsr_capture *cap) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    if(!self->started || self->should_stop)
        return;

    /* Device loss: report a hard error so the recorder stops cleanly. */
    if(self->device) {
        const HRESULT removed = self->device->GetDeviceRemovedReason();
        if(FAILED(removed)) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_tick: D3D11 device removed (0x%08lx)", (unsigned long)removed);
            self->should_stop = true;
            self->stop_is_error = true;
            return;
        }
    }

    try {
        /* Drain the pool, keeping the newest frame (the recorder ticks often;
           a later tick picks up anything newer). */
        for(int i = 0; i < 4; ++i) {
            auto frame = self->frame_pool.TryGetNextFrame();
            if(!frame)
                break;

            /* Unwrap the WinRT surface to the raw D3D11 texture. */
            winrt::com_ptr<IDirect3DDxgiInterfaceAccess> access;
            frame.Surface().as(winrt::guid(GSR_IID_IDirect3DDxgiInterfaceAccess), access.put_void());
            winrt::com_ptr<ID3D11Texture2D> texture;
            access->GetInterface(GSR_IID_ID3D11Texture2D, texture.put_void());

            D3D11_TEXTURE2D_DESC desc;
            texture->GetDesc(&desc);

            self->latest_frame = frame;
            self->latest_texture = texture;
            self->frame_width = (int)desc.Width;
            self->frame_height = (int)desc.Height;
            gsr_platform_wgc_damage_on_frame(&self->damage);
        }
    } catch(const winrt::hresult_error &e) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_tick: %s (0x%08lx)",
            winrt::to_string(e.message()).c_str(), (unsigned long)e.code().value);
    }
}

static bool wgc_should_stop(gsr_capture *cap, bool *err) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    if(err)
        *err = self->stop_is_error;
    return self->should_stop;
}

static int wgc_capture(gsr_capture *cap, gsr_capture_metadata *capture_metadata, gsr_color_conversion *color_conversion) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;

    /* The recorder calls clear_damage() BEFORE capture() (recorder.c:
       recorder_capture_and_encode_frame), so capture() must not gate on
       the damage flag — it delivers the latest frame whenever one exists.
       Returns -1 only when no frame has arrived yet. */
    if(!self->latest_texture)
        return -1;

    /* Phase 5b: import the WGC texture into GL (EGL_ANGLE_d3d_texture_client_buffer,
       zero-copy on the shared device) and draw it into the color conversion.
       When no GL pipeline is configured (standalone self-test), the frame is
       exposed through gsr_platform_capture_wgc_get_frame instead. */
    gsr_egl *egl = color_conversion ? color_conversion->params.egl : NULL;
    if(!egl)
        return 0;

    if(!self->import_handle) {
        self->import_handle = gsr_platform_egl_import_texture(egl, self->latest_texture.get());
        if(!self->import_handle) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_capture: failed to import WGC texture into GL");
            return -1;
        }
    } else if(!gsr_platform_egl_update_texture(egl, self->import_handle, self->latest_texture.get())) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_capture_wgc_capture: failed to update imported GL texture");
        return -1;
    }

    const unsigned int texture_id = gsr_platform_egl_texture_id(egl, self->import_handle);
    const vec2i frame_size = {self->frame_width, self->frame_height};
    vec2i recording_size = capture_metadata->recording_size;
    if(recording_size.x <= 0 || recording_size.y <= 0)
        recording_size = capture_metadata->video_size;
    const vec2i output_size = scale_keep_aspect_ratio(frame_size, recording_size);
    const vec2i target_pos = gsr_capture_get_target_position(output_size, capture_metadata);

    /* WGC delivers rotated content already rotated, so the draw rotation is
       GSR_ROT_0 (see wgc_start). The D3D11 texture is imported as a
       GL_TEXTURE_2D, so external_texture=false (the external shader variants
       bind GL_TEXTURE_EXTERNAL_OES, which this import is not — the ANGLE
       client-buffer image is a GL_TEXTURE_2D sibling). Source is BGRA8 =
       GSR_SOURCE_COLOR_BGR. */
    gsr_color_conversion_draw(color_conversion, texture_id,
        target_pos, output_size,
        (vec2i){0, 0}, frame_size, frame_size,
        GSR_ROT_0, capture_metadata->flip, GSR_SOURCE_COLOR_BGR, false);
    return 0;
}

static bool wgc_uses_external_image(gsr_capture *cap) {
    (void)cap;
    /* The D3D11 texture imports as a regular GL_TEXTURE_2D (Phase 5b), so
       the color conversion must NOT load the external-image (OES) shader
       and capture() draws with external_texture=false. */
    return false;
}

static bool wgc_set_hdr_metadata(gsr_capture *cap, AVMasteringDisplayMetadata *mastering_display_metadata, AVContentLightMetadata *light_metadata) {
    (void)mastering_display_metadata;
    (void)light_metadata;
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    /* Real mastering/light values land with the Phase 7 HDR probe; the
       flag alone is what the encoder needs to know HDR is active. */
    return self->is_hdr;
}

static bool wgc_is_damaged(gsr_capture *cap) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    return gsr_platform_wgc_damage_is_damaged(&self->damage);
}

static void wgc_clear_damage(gsr_capture *cap) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    gsr_platform_wgc_damage_consume(&self->damage);
}

static void wgc_destroy(gsr_capture *cap) {
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    if(self) {
        try {
            if(self->session)
                self->session.Close();
            if(self->frame_pool)
                self->frame_pool.Close();
        } catch(...) {
            /* Best-effort teardown */
        }
        if(self->import_handle && self->egl) {
            gsr_platform_egl_destroy_imported_texture(self->egl, self->import_handle);
            self->import_handle = NULL;
        }
        delete self;
        cap->priv = NULL;
    }
    free(cap);
}

/* ---- extern "C" API (platform/include/capture.h) ------------------------- */

extern "C" gsr_capture *gsr_platform_capture_wgc_create(const gsr_platform_wgc_target *target, const gsr_platform_wgc_options *options) {
    if(!target) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_platform_capture_wgc_create: target is NULL");
        return NULL;
    }

    static bool apartment_initialized = false;
    if(!apartment_initialized) {
        winrt::init_apartment(); /* RoInitialize(RO_INIT_SINGLETHREADED) */
        apartment_initialized = true;
    }

    gsr_capture *cap = (gsr_capture*)calloc(1, sizeof(gsr_capture));
    if(!cap)
        return NULL;

    gsr_capture_wgc *self = new (std::nothrow) gsr_capture_wgc();
    if(!self) {
        free(cap);
        return NULL;
    }
    self->target = *target;
    if(options)
        self->options = *options;
    self->is_hdr = options ? options->hdr : false;
    gsr_platform_wgc_damage_init(&self->damage);

    *cap = (gsr_capture){
        .start = wgc_start,
        .tick = wgc_tick,
        .should_stop = wgc_should_stop,
        .capture = wgc_capture,
        .uses_external_image = wgc_uses_external_image,
        .set_hdr_metadata = wgc_set_hdr_metadata,
        .is_damaged = wgc_is_damaged,
        .clear_damage = wgc_clear_damage,
        .destroy = wgc_destroy,
        .priv = self,
    };
    return cap;
}

extern "C" bool gsr_platform_capture_wgc_get_frame(gsr_capture *cap, void **out_texture, int *width, int *height) {
    if(!cap || !cap->priv)
        return false;
    gsr_capture_wgc *self = (gsr_capture_wgc*)cap->priv;
    if(!self->latest_texture)
        return false;
    if(out_texture)
        *out_texture = self->latest_texture.get();
    if(width)
        *width = self->frame_width;
    if(height)
        *height = self->frame_height;
    return true;
}

extern "C" bool gsr_platform_capture_backend_available(gsr_capture_backend_type backend) {
    if(backend == GSR_CAPTURE_BACKEND_WGC) {
        try {
            /* WGC needs BOTH the session API (IsSupported) and the
               Direct3D11 interop runtime that wraps our D3D11 device into
               an IDirect3DDevice for the frame pool. A Server SKU can
               report IsSupported()==true yet lack the interop DLL — treat
               that as unavailable so callers can skip instead of failing. */
            return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported()
                && load_create_d3d11_device() != NULL;
        } catch(...) {
            return false;
        }
    }
    if(backend == GSR_CAPTURE_BACKEND_DXGI_DUPLICATION)
        return gsr_platform_capture_dxgi_available();
    return false;
}
