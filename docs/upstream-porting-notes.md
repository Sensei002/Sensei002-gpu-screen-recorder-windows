# Upstream Porting Notes

This document tracks the relationship between this Windows port and the
upstream GPU Screen Recorder project, so the port stays maintainable against
future upstream releases.

## 1. Upstream revision pins

Source snapshots were taken from the official upstream snapshot server at the
revisions pinned by the current flathub manifest
(`reference/flathub/com.dec05eba.gpu_screen_recorder.yml`):

| Upstream repo | Version | Revision | Snapshot date | Vendored at |
|---|---|---|---|---|
| gpu-screen-recorder | 6.0.0 | r1467 / d31b698 | 2026-08-09 | `src/`, `include/`, `kms/`, `plugin/`, `protocol/`, `external/` (adapted) |
| gpu-screen-recorder-ui | 1.13.5 | r720 / edfc70d | 2026-08-13 | `ui/` |
| gpu-screen-recorder-gtk | 5.8.0 | r513 / dade88d | 2026-08-04 | `gtk/` (reference only; not built — see parity matrix) |
| gpu-screen-recorder-notification | 1.3.4 | r110 / 8db3818 | 2026-07-21 | `notify/` |
| mgl / mglpp | 1.1.0 / 1.1.0 | vendored inside ui snapshot | — | `ui/depends/` |

The raw unmodified snapshots are kept in `reference/` (gitignored) as the
comparison baseline for sync/merge work.

## 2. Sync strategy

* **Keep common code identical.** Platform-independent engine code
  (recorder, encoder, replay buffer, json, log, utils, args_parser, FFmpeg
  pipeline) and UI code (widgets, pages, config, translations, assets) are
  copied from upstream **unchanged**, except for small, clearly-marked
  portability shims.
* **Fork model.** The vendored trees are treated as a downstream fork of the
  pinned revisions. When a new upstream revision ships, the sync procedure is:
  1. Fetch the new snapshot into `reference/`.
  2. Diff common code; apply upstream changes to the vendored trees.
  3. Re-run the abstraction mapping (any new Linux calls land behind
     `platform/` interfaces).
  4. Bump the revision pins in this table and in `docs/windows-port-parity.md`.
  5. Build + test in CI.
* **Minimize diff surface.** Changes to common files should be limited to:
  (a) include adjustments for Windows headers, (b) `platform/` calls replacing
  direct Linux calls, (c) `#ifdef _WIN32` only where an abstraction is
  genuinely not worth it (kept to a documented minimum — brief §34).

## 3. Modified / added / removed file ledger

*Maintained as the port progresses. Convention:*

| Status | Path (in this repo) | Upstream origin | Note |
|---|---|---|---|
| vendored | `src/recorder/*` | gpu-screen-recorder | portable; expected unchanged |
| vendored | `src/encoder/*`, `src/replay_buffer/*` | gpu-screen-recorder | portable; expected unchanged |
| modified | `src/egl.c` → replaced by `platform/windows/render_*.c` | gpu-screen-recorder | render backend swap (§3.3 architecture) |
| removed | `src/capture/{kms,portal,nvfbc,xcomposite,ximage,v4l2}.c` from Windows build | gpu-screen-recorder | replaced by WGC/DXGI backends |
| added | `src/capture/windows_graphics_capture.c`, `src/capture/dxgi_duplication.c` | — | new |
| removed | `src/sound.c` (pulse) from Windows build | gpu-screen-recorder | replaced by `platform/windows/audio_wasapi.c` |
| removed | `src/pipewire_audio.c`, `src/pipewire_video.c`, `src/dbus.c`, `src/kde_night_light.c`, `src/wayland_host_bridge.c`, `kms/`, `protocol/` | gpu-screen-recorder | Linux-only |
| modified | `src/cli/ipc.c` transport | gpu-screen-recorder | unix socket → named pipe, same JSON protocol |
| modified | `src/cli/main.c` control surface | gpu-screen-recorder | signals → named events + IPC |
| added | `platform/include/*`, `platform/windows/*` | — | new abstraction layer |
| modified | `ui/depends/mgl/src/window/win32.c` (new) | mgl | new backend; x11/wayland kept for reference |
| modified | `ui/src/GlobalHotkeys/*`, `CursorTracker/*`, `RegionSelector/*`, `DesktopEnvironment/*`, `Clipboard/*`, `AudioPlayer.cpp`, `Hotplug.cpp`, `LedIndicator.cpp`, `Rpc.cpp`, `WindowUtils.cpp` | gpu-screen-recorder-ui | Windows implementations |
| removed | `ui/tools/{gsr-global-hotkeys,gsr-wayland-bridge,gsr-kwin-helper,gsr-gnome-helper,gsr-game-tracker}` from Windows build | gpu-screen-recorder-ui | Linux-only helpers |
| added | `gsr-notification.exe` | gpu-screen-recorder-notification | renamed from `gsr-notify` for the Windows build (see parity matrix name note) |

## 3b. Phase 2 ledger additions

**Vendored engine** (Phase 2): the full gpu-screen-recorder source tree is
vendored at `upstream/` (engine 6.0.0, r1467). Files are **unmodified except**:

| Status | Path | Note |
|---|---|---|
| modified | `upstream/include/egl.h` | X11 types (`XID`, `Window`, `Display`, `Bool`) are typedef'd directly under `#if defined(_WIN32)`; the `#else` branch keeps the original `#include <X11/...>` for Linux. Windows port modification notice added. |
| modified | `upstream/include/recorder/windowing.h` | `#include <X11/Xlib.h>` guarded to `#else` (Display comes from egl.h on Windows). |

**Added (Windows port):**

| Path | Note |
|---|---|
| `platform/windows/gsr_win32_compat.h` | Force-included shim (`-include`): `dlopen` family on LoadLibrary/GetProcAddress, `clock_gettime` fallback (only when MinGW lacks it), `PATH_MAX`, `ssize_t`, `S_IS*`, `dirname`/`basename`, `RTLD_*`; includes `<time.h>`/`<sys/types.h>`/`<sys/stat.h>` *before* the feature checks so native symbols are never shadowed. |
| `platform/windows/gsr_win32_compat.c` | Implementations (dl*, dirname/basename with `\\`-aware separators). |
| `platform/windows/gsr_utils_win32.c` | Windows implementation of the portable `utils.h` subset, behavior-identical to `src/utils.c` (QPC clock, RtlGenRandom, `create_directory_recursive` accepting both separators, string/date/geometry helpers, `gsr_array_ensure_capacity`). Includes the temporary `gsr_window_get_display_server` placeholder (removed when the Phase 5 windowing backend lands). |
| `platform/windows/libgen.h`, `platform/windows/dlfcn.h` | Header shims for MinGW-w64 (ships neither). |
| `scripts/build-ffmpeg-windows.sh` | FFmpeg stack provisioning (see `docs/build-windows.md`). |
| `scripts/patches/ffmpeg-nvenc-runtime-api-version.patch`, `scripts/patches/ffmpeg-mbedtls-default-ca-certs.patch` | Upstream's two FFmpeg patches, copied verbatim and applied. |
| `tests/compat-probe/`, `tests/gsr-core-test/` | CI tests (see roadmap Phase 2). |

**Known Windows-specific differences discovered in Phase 2:**

* **LLP64:** `long` is 32-bit on Windows, so `gsr_string_to_int64` uses
  `strtoll` (upstream relies on LP64 where `long` == 64 bits).
* **`/dev/stdout` / piped output:** `file_is_pipe_or_char_device` has no
  Windows equivalent yet; the `-o` default for non-replay mode is a
  documented TODO until the output/IPC work lands.
* **`remove()` on directories** (replay buffer disk cleanup): relies on
  UCRT semantics; verified in CI via the disk replay buffer test.

## 3c. Phase 2 CI validation lessons (Windows build knowledge)

Hard-won findings from getting the Phase 2 pipeline green (13 CI runs).
These are *Windows-specific build facts*, not behavioral differences —
future phases and upstream syncs should assume them:

* **Two pkg-config worlds in MSYS2.** The provisioning script runs MSYS2's
  `pkg-config` wrapper, which needs the canonical **MSYS-style**
  `PKG_CONFIG_PATH` (`/path/lib/pkgconfig`, `:`-joined). CMake finds the
  **native `pkgconf.exe`** (it cannot execute the shell wrapper), which needs
  **Windows-style paths joined with `;`** (`cygpath -m`). Mixing the two is
  the #1 silent failure.
* **cmake-generated `.pc` files need Windows fixes before ffmpeg's configure
  can consume them** (`normalize_pkgconfig_files()` in
  `scripts/build-ffmpeg-windows.sh`, run unconditionally before the ffmpeg
  build — stamp/cache hits skip the per-lib build steps, so cached prefixes
  get normalized too):
  - `srt.pc` / `haisrt.pc`: SRT's cmake writes `Requires.private: mbedtls`
    and absolute-path `.a` entries into `Libs.private`; on Windows this
    breaks both `pkg-config --exists "srt >= 1.3.0"` and the bfd static
    link. Normalize to `-l` flags and drop `Requires.private` (the same fix
    as mpv-winbuild-cmake issue #467 / PR #468).
  - `mbedtls.pc` / `mbedx509.pc` / `mbedcrypto.pc`: the static archives
    reference Windows system libs the generated `.pc` files omit
    (`BCryptGenRandom` → `-lbcrypt`, `inet_pton` → `-lws2_32`). Append
    `Libs.private: -lbcrypt -lws2_32` (what the official MSYS2 mbedtls
    package ships). Without it, ffmpeg's configure link test fails with
    `undefined reference to BCryptGenRandom / __imp_inet_pton`.
* **mbedtls is built WITHOUT LTO** — a deliberate divergence from upstream's
  LTO-everywhere recipe. Slim-LTO static archives make GNU ld/bfd on Windows
  report `cannot find -lmbedcrypto` during ffmpeg's configure link tests even
  though the archive is present in the `-L` dir (the LTO plugin claim path
  fails). Plain archives link everywhere; mbedtls is ~1 MB, so the size cost
  is negligible. x264/opus/srt/ffmpeg keep LTO.
* **GCC 16 dropped the `-static-libwinpthread` driver flag** (MSYS2
  mingw-w64 gcc 16.1.0). Link winpthread statically with
  `-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic` so executables run on machines
  without the MSYS2 runtime DLLs (what the workflow's `test` job relies on).
  Related: **strip `-lpthread` from FFmpeg's pkg-config flags** — srt.pc's
  `Libs.private` contributes `-lpthread`, which resolves to the winpthread
  *DLL import library* (`libpthread.dll.a`). Linking the static
  `libwinpthread.a` as well is usually harmless, but as soon as one more
  FFmpeg member references a pthread symbol only the static lib provides,
  both libraries end up in the link and every pthread symbol is a
  `multiple definition` error (hit when `muxer.o` joined the build and
  pulled deeper FFmpeg members). The fix in `CMakeLists.txt` filters
  `-lpthread` out of the FFmpeg flags so the static archive is the sole
  provider.
* **`-static-libgcc` is not enough: also pass `-static-libstdc++`.**
  MinGW links libstdc++ dynamically by default. The engine core itself is
  C-only, but as soon as an executable pulls C++ code (SRT is C++; a test
  that touches `muxer.o` pulls deeper FFmpeg members that reference srt)
  the exe gets a `libstdc++-6.dll` dependency and dies with a silent
  missing-DLL failure on the plain runner (`Run tests directly` — ~6s of
  no output before exit). The symptom is easy to miss because binaries that
  don't reference C++ symbols run fine. Keep both flags on every
  distributable executable.
* **The recording clock's pause is retroactive, not frozen.** Upstream
  contract (recorder.c): `gsr_recording_clock_get_time()` returns *absolute*
  monotonic time and keeps advancing while paused; the paused interval is
  subtracted from the clock's offset when unpausing. The engine reads the
  clock only while not paused (frames are not captured during a pause), so a
  unit test asserting "time freezes while paused" is wrong.
* **CI cache key = source pins + patches, not the build script.** The
  script's per-library stamps rebuild only what changed, so diagnostic
  iterations restore the built libs and re-run only the failing step (~2 min)
  instead of a full ~8 min rebuild. Source pins live in
  `scripts/ffmpeg-sources.sh` precisely so the cache key can track them
  independently of build-script changes.
* **A header literally named `time.h` on the compiler's `-I` path shadows
  the system `<time.h>`.** Every `#include <time.h>` (including the one the
  compat shim force-includes into every translation unit) resolves to the
  `-I` copy first, silently stripping `time()`/`localtime()`/`strftime()`
  and the `_timespec64` machinery winpthreads' `pthread.h` depends on. The
  port's own time header is therefore `platform/include/gsr_time.h`, and no
  other port header is named after a system header.
* **Upstream headers that drag in X11/KMS/DRM compile on Windows via stub
  headers, not by editing upstream.** `recorder/muxer.h` →
  `capture_setup.h` pulls in `cursor.h` (XEvent), `kms/kms_shared.h`
  (`<drm_mode.h>` — not even present on Windows) and `<X11/Xlib.h>`. The
  port provides `platform/windows/stubs/X11/Xlib.h` (empty: the X11 types
  come from egl.h's `_WIN32` branch) and `platform/windows/stubs/drm_mode.h`
  (a dummy `struct hdr_output_metadata`), plus an opaque `XEvent` typedef in
  the compat shim, so the *real* upstream `muxer.c` compiles and its naming
  contract is tested against upstream code rather than a reimplementation.
  These stubs will be needed again whenever more of the recorder pipeline
  (recorder.c etc.) joins the Windows build.

## 3d. Phase 4 CI validation lessons (DXGI display enumeration)

* **The mode list gives the NATIVE panel size; desktop coordinates are
  post-rotation.** `DXGI_OUTPUT_DESC.DesktopCoordinates` reflects the
  rotated layout (a portrait panel reports width < height), while
  `GetDisplayModeList` returns modes in the panel's native orientation.
  To match upstream's `--list-monitors` semantics (native size stored in
  the struct, swapped at print time for 90/270 — exactly the Wayland
  `output_monitor_info` path), store the native size (largest-area mode)
  and apply the rotation in the formatter, not in the struct.
* **HDR detection is a color-space check on `IDXGIOutput6::GetDesc1`**
  (`ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`). Requires
  `_WIN32_WINNT >= 0x0A00`; the compat shim now pins `WINVER`/
  `_WIN32_WINNT`/`NTDDI_VERSION` to the Win10 level (additive — it only
  widens what the system headers declare).
* **Per-monitor DPI needs `shcore` + `shellscalingapi.h`**
  (`GetDpiForMonitor`, `MDT_EFFECTIVE_DPI`), declared only when
  `NTDDI_VERSION >= NTDDI_WINBLUE`; fall back to 96 on failure. This is
  why `dxgi` and `shcore` joined `gsr_core`'s link libraries — neither is
  in CMake's default MinGW system-lib set (`user32`/`gdi32`/... are).
* **COM from plain C uses `lpVtbl` calls.** MinGW-w64's `dxgi.h` doesn't
  provide the C++-style wrappers in C, so every call is
  `factory->lpVtbl->EnumAdapters1(factory, ...)` with manual
  `Release()`. Two-pass enumeration (count attached-to-desktop outputs,
  then fill) keeps the alloc size exact; zero attached monitors is
  *not* an error (disconnected RDP/headless) — the caller decides.
* **The friendly name comes from `EnumDisplayDevices`, not DXGI.**
  First call enumerates adapters (`\\.\DISPLAYn`); a second call with
  that device name yields the monitor, whose `DeviceString` is the EDID
  friendly name. DXGI has no friendly name API.
* **CI runner virtualization.** The `windows-2025` runner's virtual
  display enumerates via DXGI like any real monitor (Microsoft Basic
  Display Adapter, vendor id 0x1414), so the headless smoke test asserts
  `count >= 1`, sane fields, and exactly one primary — never exact
  resolutions or names (tolerating 1+ virtual monitors).
* **Not every DXGI IID is a linkable symbol in mingw-w64.**
  `libdxgi.a` exports the older IIDs (e.g. `IID_IDXGIFactory1`) as data
  exports, but `IID_IDXGIOutput6` (Win10-era, needed for the HDR
  `GetDesc1` color-space check) is missing — `undefined reference to
  IID_IDXGIOutput6`. Define the GUID locally (`static const GUID` with the
  header's bytes: `068346e8-aaec-4b84-add7-137f513f77a1`) instead of
  pulling `IID_...` from the import library.
* **Backslash literals in tests.** Windows device names (`\\.\DISPLAY1`)
  are painful to spell in C string literals and easy to get wrong in
  generated patches; the tests build them at runtime from the structs
  (lowercase a copy for case-insensitivity, mutate the last digit for a
  no-match case) instead of hardcoding the escape sequences.

## 3e. Phase 5 CI validation lessons (WGC capture, C++/WinRT)

* **C++/WinRT is a first-class MSYS2 package** (`mingw-w64-x86_64-cppwinrt`,
  arch-qualified name — the packages.msys2.org API shows archless names, the
  pacman repo uses `mingw-w64-x86_64-*`). The projected headers install to
  `/mingw64/include/winrt` and compile cleanly with g++ — but they
  **hard-require C++20**: `winrt/base.h` #errors "C++/WinRT requires
  coroutine support" under C++17, so `CMAKE_CXX_STANDARD 20` is mandatory
  (GCC 16 supports coroutines).
  `project(... C CXX)`; the global `-std=gnu11` compile option must be
  scoped to C with a generator expression
  (`$<$<COMPILE_LANGUAGE:C>:-std=gnu11>`) or it fights the C++ standard
  flag.
* **The desktop-interop interfaces are NOT in the C++/WinRT projection.**
  `IGraphicsCaptureItemInterop` and `IDirect3DDxgiInterfaceAccess` live in
  the Windows SDK's `windows.graphics.capture.interop.h` /
  `windows.graphics.directx.direct3d11.interop.h`, which MinGW-w64 does not
  ship (and which are not projected by the MSYS2 cppwinrt package). Declare
  them locally with `__declspec(uuid(...))` using the documented IIDs
  (verified against Microsoft Learn + the projection headers):
  `IGraphicsCaptureItemInterop = {3628e81b-3cac-4c60-b7f4-23ce0e0c3356}`,
  `IDirect3DDxgiInterfaceAccess = {a9b3d012-3df2-4ee3-b8d1-8695f457d3c1}`.
* **`CreateDirect3D11DeviceFromDXGIDevice` needs no import lib.** It is a
  free function exported by `Windows.Graphics.DirectX.Direct3D11.dll`;
  `LoadLibraryW` + `GetProcAddress` avoids the header/import-lib entirely.
* **WinRT activation links against `libwindowsapp.a`, not runtimeobject.**
  `RoGetActivationFactory` / `RoInitialize` are exported by mingw-w64's
  `libwindowsapp.a` (the crt package); `libruntimeobject.a` and
  `libonecore*.a` also contain the symbol, so add `windowsapp` to the link
  and keep it there.
* **The shared C API header needs `extern "C"` guards.** `capture.h` is
  included by both the pure-C engine/tests and the C++ TU. Without
  `#ifdef __cplusplus extern "C" { ... }`, the C++ TU sees C++-linkage
  declarations and references mangled symbols for functions defined as C
  (the helpers) or with `extern "C"` in the .cpp (the backend).
* **The recorder calls `clear_damage()` BEFORE `capture()`**
  (recorder.c `recorder_capture_and_encode_frame`), so a backend's
  `capture()` must NOT gate on its damage flag — it would drop every frame.
  `capture()` delivers the latest frame when one exists and returns -1 only
  when there is none; `is_damaged()`/`clear_damage()` are the recorder's
  pacing gate. (The first implementation gated capture on the damage state
  and would have recorded nothing — the self-test caught the contract
  mismatch before wiring.)
* **The `-include` compat shim is C++-safe** (has `extern "C"` around the
  dl* declarations), which is required because CMake force-includes it into
  every TU including the .cpp.
* **WGC frame delivery is pull-based, not event-driven.**
  `Direct3D11CaptureFramePool::TryGetNextFrame` in `tick()` (draining up to
  N frames and keeping the newest) matches the recorder's tick model with
  no DispatcherQueue. The session must stay alive while the frame is
  referenced (WGC reuses pool buffers), so the latest `Direct3D11CaptureFrame`
  is held in the backend state.
* **The `--capture-self-test` contract:** `GraphicsCaptureSession::IsSupported()
  == false` → SKIP/exit 0 (environment-limited, brief §64); a real capture
  failure → FAIL/exit 1. The ANGLE interop probe inside is informational
  (needs `libEGL.dll` from `mingw-w64-x86_64-angleproject`, present in the
  MSYS2 ctest step but not on the plain runner) and never fails the run.
* **`IsSupported()` is NOT sufficient — the interop runtime must be
  probed too.** On GitHub Actions runners (Windows Server SKU),
  `GraphicsCaptureSession::IsSupported()` returns **true** while
  `Windows.Graphics.DirectX.Direct3D11.dll` is **absent from System32**
  (`GetLastError 0x2`, file ABSENT) — the client-only interop component
  isn't shipped on Server. `gsr_platform_capture_backend_available()`
  therefore ANDs the session check with a probe that actually loads the
  DLL (plain name, then the full System32 path, logging the failure
  reason), and the self-test SKIPs cleanly in that case. End-to-end WGC
  capture (start → frame → texture) is validated MANUALLY on a
  Win10/11 desktop; CI covers compile, pure logic, and the graceful-SKIP
  path.

## 3f. Phase 5b CI validation lessons (ANGLE GL render backend)

* **The upstream `gsr_egl` loader is Linux-only in three hard ways**:
  `dlopen("libEGL.so.1")`-style library names, an X11 native display for
  `eglGetDisplay`, and `gsr_egl_proc_load_egl()` **hard-requiring the Mesa
  DMABUF export extensions** (`eglExportDMABUFImageQueryMESA`/`MESA`) which
  ANGLE does not implement. The DMABUF exports have **zero call sites** in
  upstream (dead requirements) — the Windows loader simply does not load
  them. The seam is `#ifdef _WIN32` inside `gsr_egl_load`/`gsr_egl_unload`
  in `upstream/src/egl.c`, delegating to `platform/windows/gsr_egl_win32.c`.
  The unused Linux static helpers in egl.c are eliminated by -O3, so they
  need no `#ifdef`.
* **ANGLE is a first-class MSYS2 package** (`mingw-w64-x86_64-angleproject`,
  arch-qualified name — same API-vs-repo naming trap as §3e): `libEGL.dll`
  + `libGLESv2.dll` land in `/mingw64/bin` (on PATH in the MSYS2 shell), so
  the loader uses the existing `dlopen`/`dlsym` shim with the plain DLL
  names and needs no import libs.
* **The ANGLE display is created on an explicit device, not a native
  display**: `eglCreateDeviceANGLE(EGL_D3D11_DEVICE_ANGLE, device, NULL)`
  then `eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, NULL)`
  (EGL_ANGLE_device_d3d + EGL_EXT_platform_device — the only path this
  ANGLE version implements for a caller-supplied device; the
  `EGL_PLATFORM_ANGLE_EGL_HANDLE_ANGLE` attribute form is not handled).
  The D3D11 device is created by the loader (hardware, WARP fallback) and
  shared with capture backends via the Windows-only `d3d11_device` fields
  added to the `gsr_egl` struct — the WGC frame pool MUST run on the SAME
  device for a zero-copy `EGL_D3D_TEXTURE_ANGLE` import.
* **`EGL_BAD_ATTRIBUTE (0x3004)` on display creation was two stale
  constants, not a missing backend.** MSYS2's ANGLE DOES compile the D3D11
  backend (check: `libGLESv2.dll` imports `d3dcompiler_47.dll`, the D3D11
  HLSL compiler). The failures were: (1) `EGL_D3D11_DEVICE_ANGLE` is
  **0x33A1** (EGL_ANGLE_device_d3d spec: D3D9=0x33A0, D3D11=0x33A1) but a
  hand-rolled 0x33A2 was used — `Device::CreateDevice` compares against
  0x33A1, matches nothing, and returns EGL_BAD_ATTRIBUTE; and (2) passing
  the raw `ID3D11Device*` as the `EGL_PLATFORM_ANGLE_ANGLE` native display
  is invalid on Windows (`isValidNativeDisplay` calls `WindowFromDC`), so
  only the EGLDeviceEXT route works. Lesson: verify every hand-rolled
  ANGLE constant against the shipped `eglext_angle.h`/spec before
  debugging runtime errors — same trap as the DXGI IIDs in §3d.
* **The context is surfaceless** (EGL 1.5: `eglMakeCurrent(display,
  EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)`) — no window surface, no Win32
  window dependency. This is what lets the render pipeline run headless on
  CI (WARP) and keeps `gsr_window` out of the capture path.
* **`EGL_D3D11_TEXTURE_ANGLE` images bind as `GL_TEXTURE_2D`, NOT
  `GL_TEXTURE_EXTERNAL_OES`.** Upstream's external-image shader variants
  (used for DMABUF captures) bind EXTERNAL_OES; passing `external_texture`
  = true to `gsr_color_conversion_draw` would therefore sample an image
  that is not an EXTERNAL_OES texture. The WGC backend imports into
  `GL_TEXTURE_2D` and passes `external_texture=false`, which uses the
  regular sampler2D shaders — correct on ANGLE and still zero-copy.
* **The eglCreateImage target for a D3D11 texture is
  `EGL_D3D11_TEXTURE_ANGLE` (0x3484), NOT `EGL_D3D_TEXTURE_ANGLE`
  (0x33A3).** `ValidateCreateImage` accepts 0x3484 as an image target;
  0x33A3 is only a *client-buffer type* for `eglCreatePbufferFromClientBuffer`
  and is rejected with `EGL_BAD_PARAMETER` ("invalid target: 0x%X") — the
  symptom is the import failing at `eglCreateImage` even though both
  `EGL_ANGLE_d3d_texture_client_buffer` and `EGL_ANGLE_image_d3d11_texture`
  are advertised. Same stale-constant trap as `EGL_D3D11_DEVICE_ANGLE`
  above: read the shipped `eglext_angle.h` before writing extension code.
* **Imported textures sample BLACK until sampler state is set.** A freshly
  generated GL texture defaults to `GL_TEXTURE_MIN_FILTER =
  NEAREST_MIPMAP_LINEAR`, and a mip-less EGL-image texture with that
  filter is INCOMPLETE — every sample returns (0,0,0) with no GL error
  (the draw "succeeds"). The import helper sets `GL_LINEAR`/`GL_LINEAR`
  + `GL_CLAMP_TO_EDGE`, matching upstream's kms.c convention for input
  textures. Symptom on CI: the render-self-test passed the import check
  but every readback pixel was black.
* **ANGLE's GL_VENDOR is "Google Inc. (…GPU vendor…)"** — upstream's
  `gl_get_gpu_info` string checks still work on real GPUs, but on a
  software adapter (WARP / Microsoft Basic Render Driver, DXGI vendor
  0x1414) it returns false and upstream `gsr_egl_load` would FAIL. The
  Windows build adds `GSR_GPU_VENDOR_UNKNOWN` to the vendor enum, a
  DXGI-adapter VendorId fallback in the Windows `gl_get_gpu_info` (the
  upstream one lives in the X11/DRM utils.c which is not compiled), and
  default cases in the two vendor switches (`cli/commands.c`,
  `recorder/codec_select.c`) — honest "unknown/software" reporting instead
  of a hard failure.
* **The `gsr_egl` struct grows Windows-only fields** (`d3d11_device`,
  `d3d11_device_context`, `egl_angle_device`) and the ANGLE platform
  constants (`EGL_D3D_TEXTURE_ANGLE` etc.) under the existing `_WIN32`
  block in `upstream/include/egl.h` — same documented-patch approach as the
  X11-type shims.
* **`render-self-test` validates the whole Option-B path headless**: the
  loader, the import, and the UNCHANGED upstream `color_conversion.c`/
  `shader.c` now compile into `gsr_core` and run on WARP in the ctest
  step. The test checks the BGR swizzle (BGRA8 → RGB), draw orientation,
  and `GSR_ROT_180`. This is the same code path `wgc_capture()` takes,
  minus WGC itself.

## 3g. Phase 6 CI validation lessons (DXGI Desktop Duplication)

* **Desktop Duplication is the OPPOSITE of WGC on rotation.** WGC
  delivers pre-rotated content (a rotated monitor's frame is already
  upright). `IDXGIOutputDuplication::AcquireNextFrame` returns an
  **un-rotated surface** in the native panel orientation, with the desktop
  image rotated *within* it — a portrait 768x1024 monitor at 90° yields a
  1024x768 surface (per the desktop-dup-api docs). So the DD backend's
  `capture()` must pass the monitor's rotation to
  `gsr_color_conversion_draw` and use the **rotated (effective) size** as
  `source_size` — exactly the upstream KMS monitor pattern (`capture_size`
  rotated, `texture_size` native). `video_size` is the rotated size.
  The rotation mapping is `DXGI_MODE_ROTATION` minus one (IDENTITY=1 →
  GSR_ROT_0, ROTATE90=2 → GSR_ROT_90, ...) since GSR_ROT_90 is a 90°
  clockwise rotation, same as DXGI's.
* **The DD surface format is ALWAYS `DXGI_FORMAT_B8G8R8A8_UNORM`**
  regardless of the display mode — no HDR through this backend
  (`set_hdr_metadata` returns false even for an HDR target), and the draw
  always uses `GSR_SOURCE_COLOR_BGR`.
* **`AcquireNextFrame`/`ReleaseFrame` are a strict pair.**
  `ReleaseFrame` must be called before the next `AcquireNextFrame`, and
  the frame's `IDXGIResource` stays valid only until then. The backend
  releases the previous frame at the top of `tick()` before acquiring,
  and `DXGI_ERROR_WAIT_TIMEOUT` (desktop unchanged) is a normal "no new
  frame" — not an error. `DXGI_ERROR_ACCESS_LOST` (resolution change,
  session lock, secure desktop) means re-create the duplication, not stop.
* **`DuplicateOutput` requires the device on the SAME adapter as the
  output** — a WARP device or a device on another adapter fails with
  `DXGI_ERROR_UNSUPPORTED`. The shared-ANGLE-device path (Phase 5b) works
  on a real GPU; the standalone probe creates a hardware device first and
  falls back to WARP (which then fails honestly at DuplicateOutput).
* **DD is pure C — no C++/WinRT needed** (unlike WGC): it is a DXGI/D3D11
  COM interface, so `gsr_capture_dxgi.c` compiles with the plain C
  toolchain. This also makes DD potentially available on Server SKUs
  where WGC is not (WGC needs the WinRT interop runtime DLL that Server
  SKUs lack; DD needs only DXGI 1.2+, which the runner's Basic Display
  Adapter provides) — the `gsr_platform_capture_dxgi_available()` probe
  (hardware device + `DuplicateOutput` on the primary monitor) is what
  `gsr_platform_capture_backend_available` uses for the DXGI branch, and
  `dxgi-self-test` exercises a REAL capture path on CI when it succeeds.
* **`DuplicateOutput`'s device parameter is `IUnknown*`** — C has no
  implicit upcast, so every call site needs an explicit `(IUnknown*)`
  cast; the first CI build failed with `-Wincompatible-pointer-types` on
  all three call sites (this is a C-only issue; the C++ WGC backend gets
  the upcast for free).
* **`upstream/src/capture/capture.c` (the `gsr_capture_*` vtable
  wrappers) is not in the Windows build by default** — it must be added
  to the CMake source list. It is pure C with no X11 deps (only
  `capture/capture.h` + `<assert.h>`), and the wrappers are the public
  API the engine and self-tests call (the dxgi-self-test hit undefined
  references until it was added).
* **Phase 2 stubs that duplicate upstream wrapper names must be removed
  when the real wrapper is introduced.** `gsr_utils_win32.c` carried a
  `gsr_capture_set_hdr_metadata` stub (kept muxer.c linkable before the
  capture backends existed); once `capture.c` was built the two collided
  at link time (`multiple definition`). The upstream wrapper dispatches
  to the backend's `set_hdr_metadata`, which the Phase 5/6 backends
  implement — strictly better than the always-false stub.
* **IID lesson (repeat of §3d)**: `IID_IDXGIOutput1` and
  `IID_ID3D11Texture2D` are declared as local GUID constants rather than
  referencing mingw-w64's linkable symbols — the same DXGI IID trap.

## 4. Known behavioral differences (kept up to date per phase)

See `docs/windows-port-parity.md` for the full matrix. Summary of *inherent*
differences (not fixable, by design):

* No root/KMS helper (`gsr-kms-server`) — Windows Graphics Capture needs no
  elevation. Monitor capture requires no password prompt on Windows.
* No POSIX signals — control is via named events + named-pipe IPC (same
  semantics, plus `gsr-cli` becomes the primary remote-control path).
* No XDG autostart / systemd — startup via HKCU Run / Startup folder.
* X11-only features (`-w screen-direct`, `-w focused` on X11, `-fm content`
  on X11) map to Windows equivalents where possible or are reported with a
  clear error (never silently ignored — brief §32).
* App-audio (`-a app:name`) depends on the WASAPI feasibility result
  (Phase 8).
* Per-window capture uses WGC window items (no XComposite texture path).
* The UI's game-name detection uses the foreground window/process on Windows
  (no GNOME/KDE extensions).
* mgl text rendering uses DirectWrite/freetype instead of pangoft2 (visual
  parity target: same font metrics and layout where feasible).

## 5. Versioning

* The Windows port keeps its own version stream (brief §80): format
  `<port-version>` derived from the upstream engine version, e.g.
  `6.0.0-w1` → tag `v6.0.0-w1`. Installer/zip filenames are fixed
  (`GPU-Screen-Recorder-Windows-x64-Setup.exe`, `...-Portable.zip`) with the
  version in release metadata, per brief §79.

## 3h. Phase 7 CI validation lessons (end-to-end recorder)

* **recorder.c needs no Windows surgery** — its real X11 surface is one
  `gsr_window_get_display_server` call plus the `<X11/Xlib.h>` include and
  a `DefaultRootWindow()` macro (dead code on Windows: the X11 cursor
  display is always NULL). The damage/cursor systems it drives are
  X11-only, so the Windows build replaces them with no-op stubs
  (`gsr_recorder_win32.c`): `gsr_damage_init` returns false, which keeps
  `use_damage_tracking` off and the recorder's damage OR-gate fed by the
  capture backends' own `is_damaged()`/`clear_damage()`.
* **The capture_setup seam is the whole game.** `capture_setup.c` builds
  X11/KMS/NVFBC/V4L2 captures; the Windows twin
  (`gsr_capture_setup_win32.c`) implements the same header API over
  WGC/DXGI: monitor -> `gsr_platform_capture_select_backend()` (WGC
  preferred, DXGI fallback), window -> WGC window target, region/focused/
  portal/v4l2 -> honest `GSR_ERROR_UNSUPPORTED`. Cursor/damage deps are
  no-ops (the backends draw the cursor natively).
* **`uses_external_image` must be false for the D3D11-import backends.**
  Phase 5b imports the WGC/DXGI texture as a plain `GL_TEXTURE_2D` and
  draws with `external_texture=false` — so the backends must report
  `uses_external_image=false`, or the recorder loads the external-image
  (OES) shader unnecessarily (Phase 5b shipped this as `true` by mistake;
  corrected here, where the recorder first consumes the value).
* **`gl_create_texture` is missing on Windows** — it lives in the
  X11/DRM `utils.c` which is not built. Straight GL; reimplemented in the
  recorder shim. The software encoder's `copy_textures_to_frame` reads via
  `gsr_color_conversion_read_destination_texture` = `glReadPixels`
  (ANGLE-safe; Phase 5b's `glGetTexImage` was a red herring).
* **The codec-query/encoder objects are all Linux-side.** `codec_select.c`
  references `gsr_video_encoder_{vaapi,vulkan,nvenc}_create` and the
  `gsr_get_supported_video_codecs_*` queries, none of which exist on
  Windows — guarded with `#ifdef _WIN32` (software path short-circuits to
  libx264; NVENC probing is Phase 7 milestone B). LTO cannot drop the
  unreachable branches, so the guards are required even with `-flto`.
* **`gsr_capture_set_hdr_metadata` is the upstream wrapper now** — the
  Phase 2 always-false stub in `gsr_utils_win32.c` collided with the real
  wrapper once `capture.c` was built (see §3g, same lesson).
* **The GL context is bound to the thread that made it current.** The
  recorder (and every GL call it makes — texture import, color-conversion
  draw, readback) must run on the thread that loaded the egl. The first
  self-test ran `gsr_recorder_run` on a pthread and every call silently
  failed (`glGenTextures` returned 0, so the DD frame import failed with
  NO EGL error — the symptom was a valid-looking recording of black
  frames). Upstream's CLI runs the recorder on the main thread; the
  self-test now stops it from a timer thread via `gsr_recorder_stop`
  (an atomic store, safe cross-thread).
* **The ffmpeg build has no demuxers.** The script configures
  `--disable-everything` with `--enable-muxer` only, so libavformat can
  mux but cannot open a file for reading — the recorder produced a valid
  mkv that `avformat_open_input` rejected. Validation (and any future
  file-reading feature) needs `--enable-demuxer=matroska`. The configure
  args are part of the per-lib stamp, so changing them rebuilds only
  ffmpeg (~2 min) even with the cached prefix.
* **`audio_capture.c` needs the `sound_device_*` API stubbed.** The
  upstream `sound.c` (PulseAudio/PipeWire) is not built, but the
  audio_capture object that recorder.c pulls in references it. Stubs that
  return "unavailable" are fine: the recorder with zero audio tracks never
  calls them. Phase 8 (WASAPI) replaces the stubs.
* **`gsr_window` is an opaque forward-decl in egl.h but a full vtable
  struct in window.h** — allocate/`memset` it only where window.h is
  included, or "storage size isn't known" errors (the win32 egl loader
  only stores the pointer, so a zeroed instance is safe).

## 3i. Phase 8 CI validation lessons (WASAPI audio)

* **The CI runner has NO audio endpoints at all.** `get_pulseaudio_inputs`
  reports 0 active devices, and a diagnostic added to it shows 0 render + 0
  capture endpoints *including disabled/unplugged* — the GitHub runner is
  genuinely without an audio stack. The live WASAPI capture path therefore
  cannot be exercised in CI (same situation as WGC in Phase 5). The
  conversion math is proven instead: the pure pipeline (mix-format decode,
  stereo downmix, 44.1k→48k linear resample, S16/S32/F32 quantize, ring
  push) was extracted behind `audio_wasapi_internal.h` and is driven with
  synthetic data by `tests/audio-conv-test` (52 checks). recorder-self-test
  probes the default output (open + read one chunk) and only then adds the
  `-a default_output` track, so a machine without audio records video-only
  instead of failing.
* **`sound_device_read_next_chunk`'s timeout is load-bearing.** The engine's
  audio thread is blocked inside it when the recording stops; the timeout
  (and the -1 return = "no audio, fill silence") is what lets the thread
  wake and see `running=false`, so `sound_device_close` can join it without
  deadlocking. Never block indefinitely.
* **Chunks must be exactly `period_frame_size` frames.** The engine's
  `swr_convert` consumes exactly `frame_size` frames per call and the A/V
  sync counts whole chunks — the device must deliver the requested period
  size, not whatever the OS hands out (WASAPI shared-mode packets are
  ~10 ms = 480 frames; a ring buffer accumulates them into 1024-frame
  periods).
* **The device delivers the *codec's* format, not WASAPI's.**
  `audio_codec_context_get_audio_format` maps AAC→S32, flac→S32,
  opus→F32/S16 at GSR_AUDIO_SAMPLE_RATE (48 kHz) stereo. Shared-mode WASAPI
  is opened with the endpoint's *mix format* (F32/48k/stereo on modern
  Windows → the conversion is a pass-through) and converted in software;
  the 16/24-bit, mono/surround, and ≠48 kHz mix-format cases are handled
  with a documented linear resampler.
* **The MMDevice/audio-client IIDs are not defined by mingw-w64.**
  `IID_IAudioClient`, `IID_IAudioCaptureClient`, `CLSID_MMDeviceEnumerator`,
  `IID_IMMDeviceEnumerator` are declared in the headers but no import
  library defines them, so taking their address fails to link (the same
  class of problem as the DXGI IIDs in §3g, but for audio — `libole32.a`
  does not carry them). Define them locally with external linkage.
  `DEVICE_STATE_ALL` is likewise missing from mingw's `mmdeviceapi.h`
  (only the individual states exist); define `0x0000000F`.
* **S32 quantization has a float trap.** `2147483647` is NOT representable
  as a float — it rounds to 2³¹, so `(int32_t)(1.0f * 2147483647.0f)` is
  undefined behavior (wraps to INT32_MIN on x86): full-scale positive audio
  becomes garbage. Scale by 2ᴺ (libswresample's convention, which the
  engine feeds downstream) and saturate in integer space against the exact
  float constants; the same protects int16 (`1.0 * 32768.0f` overflows).
* **COM is per-thread.** The open path and the WASAPI capture thread each
  call `CoInitializeEx(COINIT_MULTITHREADED)`; balance S_OK/S_FALSE with
  `CoUninitialize`, but not `RPC_E_CHANGED_MODE`. The ring buffer needs no
  locking between producer and consumer beyond the SRWLOCK/condvar used for
  the read path.
* **The engine never frees the read buffer.** `read_next_chunk` hands back
  a pointer that is consumed and discarded — reuse a device-owned buffer
  (like upstream's ringbuffer read pointer), do not malloc per call or the
  recorder leaks ~190 small buffers/sec.
* **The recorder opens devices at create time** (from
  `gsr_audio_track_init_device_inputs`) but the audio thread starts later;
  `sound_device_flush` discards the pre-recording ring content so stale
  audio never enters the file. Default-device change tracking (the upstream
  "auto-switch" comment) and WASAPI session enumeration for `-a app:name`
  are documented as not-yet-implemented (roadmap Phase 8, remaining).

## 3k. Phase 8 (milestone B) CI validation lessons (listing, sync, device-change)

* **Check for an existing definition before implementing a
  declared-but-unimplemented header function.** `gsr_platform_audio_format_device_line`
  was declared in platform/include/audio.h with a "Phase 8" comment, and
  the Phase 8A work implemented the enumeration but NOT the formatter —
  so I implemented the formatter too and got a multiple-definition link
  error: the pure formatter had quietly landed in
  `gsr_platform_win32.c` (Phase 3). `grep -rln` the symbol across
  `platform/` before writing a header-declared function.
* **The session-enumeration IIDs are NOT what common web copies say.**
  `IID_IAudioSessionControl2` is `bfb7ff88-7239-4fc9-...` (NOT
  `-6799-4fa9-`), and `IID_IAudioSessionEnumerator` is
  `e2f5bb11-0570-40ca-acdd-3aa01277dee8` (NOT `-3aa47b1f-`). Use the
  WIDL-generated mingw-w64 header values (audiopolicy.h) — a wrong IID
  makes `GetService`/`QueryInterface` fail E_NOINTERFACE and the
  enumeration silently return nothing. Same mingw situation as the
  mmdevice IIDs: declared, defined by no import library → define locally.
* **Per-app audio is infeasible with WASAPI — document it, don't fake it.**
  Loopback capture is endpoint-wide; there is no public API to capture a
  single app's session (that requires an APO or virtual device). The
  engine already degrades honestly: the `GSR_APP_AUDIO` code path is
  upstream's pipewire build, so on Windows `-a app:NAME` returns
  `GSR_ERROR_UNSUPPORTED` at track setup. What WASAPI DOES offer is
  session ENUMERATION (`IAudioSessionManager2`), which powers
  `--list-application-audio` (display name, pid, state — the Volume Mixer
  view). The parse surface (app:/app-inverse: → APPLICATION tracks) is
  upstream code, already built, and pinned by tests.
* **A partial period legitimately stays in the ring.** 144000 frames
  (3 s @48 kHz) is 140 whole 1024-frame periods + 640 frames; the
  consumer reads whole periods only, so the harness must assert
  `consumed + remainder == fed` (nothing lost), NOT `consumed == fed`.
  The engine discards the partial remainder on stop — by design.
* **libFLAC's frame size is build-dependent** (4608 with this ffmpeg's
  libFLAC, not 1024 and not a fixed 4096). The device must deliver
  whatever the opened codec context reports; don't hardcode it.
* **The IMMNotificationClient C vtable is easy to get wrong.** The
  callback layout (OnDeviceStateChanged/OnDeviceAdded/OnDeviceRemoved/
  OnDefaultDeviceChanged/OnPropertyValueChanged after the IUnknown trio)
  comes straight from mingw's WIDL mmdeviceapi.h — implement it with
  `lpVtbl` like the rest of the port, keep the callbacks to Interlocked
  flags only (they fire on an MMDevice-owned thread), and unregister on
  the SAME enumerator instance that registered. The default-device
  auto-switch re-resolves and re-opens on the capture thread, throttled
  to ≤1/s and self-healing on failure (silence + retry, the same contract
  as the AUDCLNT_E_DEVICE_INVALIDATED path).
* **The ffmpeg cache fix is proven end-to-end.** With the v3 key the
  restore hits, the stamp check makes the build a no-op, and the save
  correctly SKIPs on a restore hit — a green run that used to take
  ~12 min now spends ~2 min in the ffmpeg stage (the coverage rebuild is
  the remaining long pole).

## 3j. Phase 7 (milestone B) CI validation lessons (NVENC d3d11va)

* **Upstream's GL+CUDA nvenc encoder has no Windows equivalent to copy.**
  There is no CUDA-GL interop in this port, so the same
  `gsr_video_encoder` contract is met with d3d11va: the color conversion
  renders into the same 2 GL textures as the software encoder,
  `glReadPixels` fills a persistent system-memory NV12/P010 frame, and
  `av_hwframe_transfer_data` (DIRECTION_TO) uploads it into a D3D11 hw
  frame allocated from a hw-frames context built on the SAME device ANGLE
  uses (`gsr_platform_egl_get_d3d11_device`, Phase 5b). The encoder reads
  `AV_PIX_FMT_D3D11` and the codec context's `hw_frames_ctx`; the d3d11va
  equivalent of upstream's `cuMemcpy2DAsync` is the hwframe transfer, and
  the `frame` the recorder passes in is reused as the persistent hw frame
  (allocated once in `start` via `av_hwframe_get_buffer`).
* **A d3d11va device context is hand-built, not opened by name.**
  `av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA)` + set
  `AVD3D11VADeviceContext.device` (AddRef'd — FFmpeg releases it on free)
  + `av_hwdevice_ctx_init`, then `av_hwframe_ctx_alloc` with
  `format=AV_PIX_FMT_D3D11`, `sw_format=NV12`/`P010LE`, then
  `av_hwframe_ctx_init`. There is no d3d11va equivalent of
  `av_hwdevice_ctx_create` with a device name; pass the device in.
* **Probing must be honest and real.** `gsr_get_supported_video_codecs_nvenc`
  creates a hardware D3D11 device, requires the DXGI adapter description to
  contain "NVIDIA", maps it to a generation (pure table,
  `gsr_nvenc_internal.h`, headless-tested), pre-filters codecs the
  generation cannot have, then ACTUALLY opens each encoder (`avcodec_open2`
  with a 128x128 d3d11va hw-frames context, `highbitdepth=1` for 10-bit).
  On the CI runner (Basic Display Adapter) it returns false, which drives
  the existing `-fallback-cpu-encoding` path — the same honest-degradation
  contract upstream has for unknown vendors.
* **C has one namespace for typedefs and functions.** The accessor
  `gsr_nvenc_generation_caps()` collided with the typedef
  `gsr_nvenc_generation_caps` — "redeclared as different kind of symbol".
  Rename one (the function became `gsr_nvenc_get_generation_caps`); this
  cascaded into a wall of downstream errors that looked like a missing
  include.
* **Adapter-description substring matching is a trap.** "Quadro RTX 4000"
  is Turing but contains "rtx 40" (Ada); "Quadro RTX A6000" is Ampere;
  "RTX 4000 Ada Generation" is Ada; "adapter" contains "ada". Match the
  professional naming FIRST (rtx a-series, quadro rtx) before the consumer
  "rtx N0" series, and never use a short generic substring like "ada" —
  unmatched cards must fall through to UNKNOWN (probe everything, the
  honest outcome) rather than guess.
* **`actions/cache/save` errors when the key already exists.** Every run
  re-wrote the deterministic ffmpeg cache key (`if: always()` save), so
  after any successful save the next run's save failed with "cache entry
  already exists". Save only when the restore missed. See the workflow's
  ffmpeg cache comment for the second half of that fix (drop
  `ffmpeg-sources` from the payload — hundreds of MB of re-downloadable
  tarball trees — which is what made saves time out).
* **`GL_UNSIGNED_SHORT` was missing from the port's GL constants.** The
  10-bit readback path uses it; egl.h defines the GL constant block for
  the port and only had `GL_UNSIGNED_BYTE`. Added `0x1403` alongside.
* **The recorder runs the encoder on the GL thread.** The encoder's
  `start`/`copy_textures_to_frame` call GL; running the recorder on a
  pthread while GL is current on the main thread makes every GL call
  silently fail (texture 0 → black frames that "validate"). Same lesson
  as recorder-self-test (§3h): GL is thread-bound, keep it on the thread
  that loaded egl.

## 3l. Phase 9 CI validation lessons (replay save, crash cleanup)

* **The MinGW CRT defaults open/creat/read/write to TEXT mode, and that
  silently corrupts binary packet I/O.** A text-mode `read()` stops at the
  first Ctrl-Z (0x1A) byte, and a text-mode `write()` translates '\n' to
  "\r\n". Real x264 packet data contains 0x1A, so the replay save thread's
  first read failed ("failed to read data from file") and every save
  reported failure. `gsr-core-test` never caught it because its synthetic
  packets use a single-byte fill (0x00-0x04) that contains no 0x1A — the
  first REAL packet data exposed it. The .gsr files are now opened with
  `O_BINARY` (`GSR_REPLAY_OPEN_BINARY` in replay_buffer_disk.c, 0 on
  POSIX where binary is the default). Lesson: any upstream code doing
  POSIX file I/O on binary data needs `O_BINARY` on this port; text-mode
  test data can hide it.
* **POSIX remove() cannot remove a DIRECTORY on Windows.** `_unlink` only
  removes files, so `gsr_replay_buffer_disk_destroy`'s `remove(session_dir)`
  silently leaked the session directory after every disk-buffer session
  (the files were gone, the empty dir stayed). Use `RemoveDirectoryA` on
  Windows (available everywhere — the compat header force-includes
  windows.h). `gsr-core-test` missed it because it only checks that the
  BASE replay directory still exists.
* **Windows _unlink fails on a file that is still open (sharing
  violation); POSIX unlink() does not.** `gsr_replay_buffer_disk_clear`
  removed the files while the current file's write fd was still open, so
  the current `Replay_N.gsr` leaked and the directory removal then failed
  too. Close `storage_fd` BEFORE the file loop. This is an upstream
  ordering assumption that only manifests on Windows.
* **Crash-safe cleanup is free because session dirs are timestamped.** The
  disk buffer names each session's dir `gsr-replay-<timestamp>.gsr` and
  removes it on a clean exit, so any OTHER `gsr-replay-*.gsr` dir in the
  replay directory is by construction a crashed session's leftover.
  `gsr_platform_replay_cleanup_stale_directories` sweeps those at the next
  `gsr_replay_buffer_disk_create` (skipping the current session's name,
  pattern-guarded so unrelated dirs survive) — the same assumption makes
  multi-instance-on-one-directory a documented non-goal.
* **The recorder's keyint setting is in SECONDS, not frames.** x264's
  keyint = `settings.keyint * fps` (keyint=10 at 10fps produced
  `keyint=100` = a keyframe every 10s), so a 7s recording had exactly one
  keyframe and the post-restart save found none. Use `keyint=1.0` for a
  1s cadence in tests that must always find a keyframe.
* **Simulating the disk buffer's 256MB rollover in a unit test requires
  BOTH halves of the real code's behavior**: close `storage_fd` AND reset
  `storage_num_bytes_written` to 0 (the append path resets it at the
  boundary). Forgetting the reset leaves the next file's packets indexed
  from a stale byte offset, so reads return the wrong data or hit EOF.
* **`gsr_replay_buffer_destroy` frees the buffer struct** (unlike
  `destroy_at_exit`), so dereferencing `rb->replay_directory` (or any
  field) after destroy is a use-after-free. Copy paths out before
  destroying.
* **Saved replay durations are pts-derived and short on slow runners.**
  The recorder's pts runs ~1.2s behind wall time during its startup burst
  on the CI runner, so duration assertions on saved replays must be loose
  (real content > 0.3s); the robust proof of `-restart-replay-on-save` is
  the post-restart save being clearly SHORTER than the pre-restart FULL
  save, not exact durations.

## 3m. Phase 10 (milestone A) CI validation lessons (mgl Win32 backend, WGL)

The vendored UI (`ui/`, mglpp r720) needed a Win32 window backend + GL
loader before any UI code could build. Five CI-only bugs, all now fixed:

* **Microsoft's opengl32.dll exports only the GL 1.1 core.** Everything
  from `glBlendFuncSeparate` (1.4) to VBOs (1.5) and shaders (2.0) must be
  resolved via `wglGetProcAddress` — and that requires a CURRENT context,
  which does not exist at `mgl_init` time. The first attempt hard-failed
  the loader on every non-1.1 "required" symbol; the fix resolves them
  lazily (`mgl_gl_load_windows_extensions`) right after the WGL context is
  made current, filling NULL slots only.
* **`mgl_graphics_make_context_current` segfaulted through a NULL
  `glBlendFuncSeparate` on GDI Generic** (GL 1.1, CI's software GL) — and
  the crash happened INSIDE `mgl_window_create`, so the test's FAIL line
  was never written: the only symptom was a bare `SegFault` with zero test
  output. Guard extension-dependent state setup behind NULL checks; a
  missing extension is normal on GDI Generic, not fatal.
* **Ordering matters: the extension load must happen before the GL state
  setup that uses the extensions.** The first version loaded extensions
  AFTER `make_context_current` returned, i.e. after the state setup that
  needed `glBlendFuncSeparate`. Moving the load into the success path
  fixed both the ordering and made the win32.c hook redundant.
* **A zeroed `mgl_window_create_params` requests `MGL_GRAPHICS_API_EGL`**
  (EGL is enum value 0), which the Win32 backend correctly rejects —
  "EGL is not supported on Windows yet". Tests must set
  `params.graphics_api = MGL_GRAPHICS_API_WGL` explicitly.
* **Two test-expectation traps.** (1) Showing a hidden window queues a
  real `WM_SIZE` (the pre-show size), so a synthetic `WM_SIZE` test must
  drain events first or the stale resize is polled. (2) `mgl_window.size`
  is the CLIENT rect — frame chrome excluded — so subclassed-window size
  checks must compare against `GetClientRect`, not the outer window rect.
* **`sys/mman.h` does not exist on MinGW.** `fileutils.c` got a
  `MapViewOfFile` implementation under `#ifdef _WIN32` (with `O_BINARY` —
  the §3l text-mode lesson applies to its `read` path too), and the
  Linux-only mmap helper is compiled out.
* **The `-static-libwinpthread` driver flag is C++-only.** The Phase 2
  lesson again, re-confirmed when linking the C mgl test — the static
  runtimes are handled by the core's link flags, tests just need
  `-static-libgcc`.

## 3n. Phase 10 (milestone B) CI validation lessons (mgl text, pangoft2)

The mgl text pipeline (text.c/text_edit.c/font_atlas.c) compiled and ran on
Windows with almost no code change — the milestone was mostly build/runtime
plumbing. The lessons:

* **pangoft2 needs no platform code.** `pango_ft2_font_map_new()` +
  freetype glyph rasterization is the Linux path, and it works as-is on
  MinGW — pango's Win32 font maps (GDI/DirectWrite) are NOT needed, which
  keeps shaping/layout/fallback behavior identical to upstream. The only
  text.c change was `mgl_text_get_default_font_name`: GSettings schemas
  don't exist on Windows (returns false → empty font name), so return
  "Segoe UI" under `#ifdef _WIN32`.
* **MSYS2's fontconfig has the builder's sysconfdir baked in.**
  `C:\msys64\etc\fonts` (from the MSYS2 build environment) does not exist
  on GitHub runners — without help, FcInit finds no config and no fonts,
  and pango layouts measure 0. Fix: a repo `tests/fonts.conf` with
  `<dir>C:/Windows/Fonts</dir>` + `FONTCONFIG_PATH` set on every test
  invocation (ctest, direct pwsh runs, the coverage ctest, and the
  plain-runner `test` job). fontconfig's windows-font source would also
  work but the explicit dir is deterministic.
* **MSYS2 pango is shared-DLL only (no static archives),** which broke the
  project's "static-linked exe runs on the plain runner" property. Fix: an
  `ldd`-based bundling step copies the transitive `/mingw64/bin` DLLs next
  to the exe before the artifact upload; Windows resolves DLLs from the
  exe's directory, so the plain-runner test job works unchanged. This is
  the same DLL set the Phase 13/17 installer must bundle — the eventual
  UI binary will use the same step.
* **The font atlas asserts `context->current_window`** (mgl_font_atlas_init
  calls glGenTextures). The x11 backend sets it; the new Win32 backend
  didn't, so it now mirrors x11.c (set after setup, clear in deinit when
  it matches).
* **`mgl_text_set_string` takes (text, text_len)** — `-1` auto-computes;
  calling it with 2 args is a compile error caught by CI's first build.
* **Coverage denominator grows with vendored code** (13,863 lines now) —
  the pango sources are instrumented but mostly untested headless, so the
  percentage dips; the milestone's real proof is the runtime checks (font
  found, layout size > 0, atlas glyphs rasterized, fallback resolved).

## 3o. Phase 10 (milestone C) CI validation lessons (full gsr-ui port)

The remaining Phase 10 work — the whole UI app — landed in one milestone:
RPC→named pipes + gsr-ui-cli, the six Win32 platform modules, the
Process/Utils/WindowUtils ports, the Overlay.cpp/main.cpp X11 guards, and
the full 163-target build with ui-rpc-test/ui-module-test. The lessons, in
the order CI found them:

* **windows.h's bare `ERROR` macro collides with `NotificationLevel::ERROR`**
  (and would with any `ERROR` enumerator). `#undef ERROR` at the top of the
  header (like Rpc.hpp) is the fix. Do NOT use
  `#pragma push_macro("ERROR")`/`pop_macro` around the enum: pop_macro
  restores the macro for the rest of the TU, so every later
  `NotificationLevel::ERROR` use expands to `NotificationLevel::0`.
* **`stat.st_mtim` is Linux-only.** Windows `struct stat` has `st_mtime`
  (already `time_t`) — guard or use the portable field.
* **`strcasecmp` needs `<strings.h>` on MinGW** (declared there, not in
  string.h).
* **The X11 guard pattern has two traps.** (1) A function whose body is
  `#ifdef _WIN32 return; #else ...body... #endif` must NOT put its closing
  brace inside the `#else` — on Windows the enclosing namespace never
  closes ("expected '}' at end of input"). Keep one closing brace after
  `#endif`. (2) A `#ifndef _WIN32` that swallows the function's final
  `return; }` leaves the function unclosed on Windows and cascades into
  the next function ("qualified-id in declaration before '(' token").
  Always add an explicit `#else (void)x; return false; #endif`.
* **Keep `Display *x11_dpy` as a member on Windows (NULL).** Many
  notification/monitor helpers take it as an opaque handle and check
  `if(x11_dpy)` internally; with the member present they compile and
  behave correctly (skip X11 paths) with zero per-call-site guards.
* **X11-only files that are excluded from the Windows build still leave
  link holes.** `window_texture.c` (XGetImage/XRender) isn't built, so
  `window_texture_deinit` was undefined — guard the two call sites in
  Overlay.cpp (the member is never initialized on Windows).
* **Named-pipe server: `FILE_FLAG_FIRST_PIPE_INSTANCE` is for the FIRST
  instance only.** CreateNamedPipe fails with ERROR_ACCESS_DENIED for the
  second instance created with it — the re-armed listen pipe after the
  first accepted client silently failed, and the next client spun on
  `ERROR_PIPE_BUSY` forever. Drop the flag (or pass it once).
* **Named-pipe server: close the pipe BEFORE freeing its OVERLAPPED.**
  Closing the pipe handle cancels the pending ConnectNamedPipe, and the
  cancellation writes the final status into the OVERLAPPED. `~Rpc` freed
  `ov` first, so the cancel wrote into freed memory — 0xc0000374 heap
  corruption detected at process exit, silent otherwise.
* **Named-pipe clients must bound their `ERROR_PIPE_BUSY` retry loop.**
  `WaitNamedPipeA`-and-`continue` spins forever if the server never
  accepts; bound to ~5s then fail (Rpc::open and gsr-ui-cli both).
* **ctest's default per-test timeout is 1500s** — a hung test silently ate
  the whole runner. `ctest --timeout 120` turns a hang into a 2-minute
  failure naming the test.
* **MinGW has no libasan** (`-fsanitize=address` fails to link: cannot
  find -lasan) — heap bugs have to be found by reasoning about Windows
  semantics, not ASan, unless a custom toolchain is installed.
* **The `-static-libgcc` link flag on the C++ UI tests is fine**, but any
  future static/dynamic CRT mixing will resurface as exit-code-only
  crashes (0xc0000374 family) — check heap-adjacent ordering bugs first.

## 3p. Phase 10 (remaining) CI validation lessons (overlay behavior, golden render)

* **Overlay window styles are testable headless via GetWindowLongPtr.** The
  UI's `show()` treatment maps to queryable Win32 style bits: click-through
  → `WS_EX_LAYERED|WS_EX_TRANSPARENT`, taskbar-hide →
  `WS_EX_TOOLWINDOW`, alpha → `WS_EX_LAYERED` (via `support_alpha`).
  `ui-module-test` asserts them on a real hidden mgl window, so the
  overlay behavior milestone needs no running engine.
* **Always-on-top is NOT in the ex-style bits.** `WS_EX_TOPMOST` passed to
  `CreateWindowEx`/`SetWindowPos` is translated into z-order placement and
  never readable from `GetWindowLongPtr`. Verify it behaviorally: create a
  second window after the first, `make_window_sticky` the first, then walk
  `GetWindow(hwnd_b, GW_HWNDPREV)` and assert the sticky window is reached.
* **mgl owns `GWLP_USERDATA`** (its `mgl_window*`), so a test that needs to
  map HWND→its own state must use `SetPropA`/`GetPropA` window properties,
  not the userdata slot.
* **A golden render test can be self-bootstrapping on CI.** `ui-golden-test`
  renders the real settings page (theme textures + fontconfig text + widget
  draw + WGL swap) headless on the runner's GDI Generic software GL, writes
  `ui-golden-render.ppm` to the CWD, and: updates the committed golden when
  `GSR_GOLDEN_UPDATE=1`; passes with a warning when the golden is missing
  (first run — the artifact render is then committed as the golden); and
  otherwise compares with a per-pixel tolerance (>= 99.5% within ±4/channel)
  to absorb fontconfig/antialiasing drift between runner images.
* **Read the framebuffer BEFORE `SwapBuffers`.** With WGL double buffering
  the back buffer is only defined until the swap; `glReadPixels` before
  `mgl_window_display()` and flip rows (GL is bottom-up, PPM top-down).
* **Teardown order in a GL golden test matters.** The mgl window (and the
  widgets holding theme-texture references) must be destroyed before
  `deinit_theme()`, and the window before `mgl_deinit()` — scope the whole
  render in a block and tear down outside it.
* **Subprocess probes degrade gracefully on CI.** `get_audio_devices` /
  `get_supported_capture_options` spawn `gpu-screen-recorder` (not on PATH
  in the test job) — `spawn_program` returns -1 and the page renders with
  empty lists, which is deterministic enough for a golden baseline.

## 3q. Phase 11 CI validation lessons (engine binary + named-pipe IPC)

* **The engine executable is a real target now.** `gpu-screen-recorder.exe`
  (platform/windows/gsr_main_win32.c) mirrors upstream `src/cli/main.c`:
  the `-ipc` handlers, deferred-request completion, screenshot path, args
  validation, `-sc`. The Windows differences are: no POSIX signals
  (SetConsoleCtrlHandler for Ctrl+C/close/logoff; SIGINT/SIGTERM via
  signal() still work on MinGW, SIGUSR1/SIGRTMIN don't exist), no
  display-server env (no DISPLAY/WAYLAND_DISPLAY), no /proc, no geteuid
  check, no mallopt, no install_cuda_no_stable_perf_limit. The NVIDIA env
  vars upstream sets are set with `_putenv_s`; the LIBVA/vblank unsets are
  VAAPI-only and skipped. App audio (`-a app:*`) is rejected with exit 2
  (GSR_APP_AUDIO is not built on Windows). `windowing` is the
  ANGLE-on-D3D11 loader; `card_path_found` means "GL is usable".
* **Named pipes are the IPC transport, and the deferred-request semantics
  are byte-identical to upstream.** Request/reply JSON, the error strings
  ("unknown request name '%s'", "a replay is already being saved",
  "option -r is required to save a replay"), and the deferred replies
  (stop/save-replay/stop-replay-recording answered only by
  gsr_ipc_complete_request) all match `src/cli/ipc.c`. `engine-ipc-test`
  proves the round-trips in-process (server + client API), then spawns the
  real engine with `-ipc` and drives it through the real `gsr-cli.exe`.
* **Four named-pipe traps, all caught in review before CI:**
  1. **Never signal the shutdown event for a completed deferred request** —
     the loop treats it as quit, so the IPC server would die the first time
     a replay finished saving. A completed request wakes the thread via a
     separate auto-reset wakeup event.
  2. **Free the consumed listen OVERLAPPED before re-arming.** The accept
     path replaced `self->listen_overlapped` without freeing the old one
     (handle + struct leak per connection). Free it after
     GetOverlappedResult, before creating the fresh listen instance.
  3. **A synchronous ReadFile completion carries data the event never
     signals.** If a fast client's bytes arrive before the OVERLAPPED goes
     pending, ReadFile returns TRUE with bytes already in the buffer;
     treating that as "disconnected" (or ignoring it) loses the request.
     Process the bytes and re-arm immediately.
  4. **A HANDLE is 64-bit; the deferred-request client token must be
     intptr_t, not int.** Upstream's `client_fd` is a Unix socket fd and
     fits an int; truncating a pipe HANDLE to int corrupts the lookup when
     the deferred reply is sent (the fix is in `cli/ipc.h`, shared header).
* **Engine smoke commands need ANGLE, so they run in the MSYS2 step.**
  `--info`, `--list-capture-options` and `--list-monitors` load EGL
  (gsr_windowing_load_egl) and exit 22 without ANGLE — matching upstream's
  exit code when the card isn't found. The workflow runs them only where
  libEGL.dll is on PATH; `--version`/`gsr-cli -h` run everywhere.
* **The live engine test must pass `-fallback-cpu-encoding yes`.** CI's
  runner has no NVIDIA GPU (Basic Display Adapter/WARP), and the engine
  defaults to the GPU encoder; without the fallback the recording fails
  exactly like recorder-self-test's would. With it, the engine records the
  primary monitor with libx264 and the stop request saves the file — the
  same path recorder-self-test exercises.
* **`-w monitor` is not a literal on Windows.** The capture setup resolves
  real monitor names (`\\.\DISPLAY1`) or the primary alias `screen`; the
  test resolves the primary monitor via gsr_platform_display_list_monitors
  before spawning the engine.

## 3r. Phase 12 startup/logoff lessons (HKCU Run autostart + clean shutdown)

* **HKCU Run must store the full exe path, not a bare command.** The first
  Windows `set_xdg_autostart` wrote `"gsr-ui" launch-daemon`, which only
  launches if `gsr-ui` is on PATH — true for an installer but not a portable
  ZIP. The value is now `"<full path to gsr-ui.exe>" launch-daemon` via
  `GetModuleFileNameA`, so autostart works from any install location. The
  UI's value name is `gpu-screen-recorder-ui` under
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
* **The UI is a console-subsystem app, which is a feature here.** gsr-ui is
  built with `int main` (no `-mwindows`), so it has a console attached and
  Windows delivers `CTRL_LOGOFF`/`CTRL_SHUTDOWN`/`CTRL_CLOSE` to a
  `SetConsoleCtrlHandler`. That's the clean-shutdown path on session end:
  the UI exits its main loop (overlay tears down) and the separately-spawned
  engine gets its own `CTRL_LOGOFF` via gsr_main_win32.c and saves the
  recording. Two consequences: (1) hide the console window at startup
  (`ShowWindow(GetConsoleWindow(), SW_HIDE)`) so autostart at logon doesn't
  flash a black box — but keep the console attached so the handler still
  fires; (2) a GUI-subsystem build (`-mwindows`) would lose console events
  and need WM_QUERYENDSESSION/WM_ENDSESSION handling instead.
* **Registry round-trip is testable through the real UI code path.**
  `ui-module-test` calls `gsr::set_xdg_autostart`/`is_xdg_autostart_enabled`
  (Utils.cpp) — set → verify the value contains the exe path +
  `launch-daemon` → unset → verify gone — saving and restoring the prior
  state so a developer machine with autostart enabled isn't clobbered.
* **File associations are out of scope (documented).** Recordings are
  standard mp4/mkv; the UI opens the save folder rather than registering
  extensions, matching upstream's behavior (no proprietary format needs an
  association).

## 3s. Phase 13 installer/branding lessons (Inno Setup + logo embedding)

* **Inno Setup 6 via chocolatey beats NSIS for this project.** The package
  job runs on a plain `windows-latest` runner (no MSYS2), and `choco install
  innosetup -y` is a single command that ISCC.exe is available immediately.
  NSIS would require adding `mingw-w64-x86_64-nsis` to the MSYS2 install
  list and running the package job under MSYS2, adding ~2 minutes of
  provisioning. Inno's `PrivilegesRequired=lowest` handles per-user installs
  natively (installs to `%LOCALAPPDATA%\Programs\`, no admin); NSIS needs
  manual registry work for the same result. The installer script is generated
  at CI time by `build-package.ps1` (absolute paths embedded) to avoid ISPP
  path-quoting pitfalls.
* **windres `-D` quoting is fragile — hardcode version in the .rc instead.**
  The first attempt passed `-DGSR_VERSION_STR=\"6.0.0-w1\"` via
  `RC_FLAGS`; windres receives literal backslashes in the define value,
  producing a mangled version string. The fix: hardcode `GSR_VERSION_STR`
  and `GSR_VERSION_NUM` directly in `packaging/gsr.rc` with a "keep in sync
  with CMakeLists.txt" comment. RC_FLAGS now only carries `-Ipackaging/`.
  (GCC's driver unescapes `\"` correctly; windres does not guarantee the
  same behavior.)
* **windres resource NAME matters: the version block must be named `1`.**
  The first build embedded the icon (from the same `.rc`) fine but Windows
  reported the version info as absent: `ExtractIconEx` found the icon,
  `EnumResourceTypes` listed RT_VERSION (16), yet `GetFileVersionInfoSize`
  failed with error 1813. Root cause: the block was declared as
  `VS_VERSION_INFO VERSIONINFO` *without* `#include <winver.h>`, so windres
  kept the literal string `"VS_VERSION_INFO"` as the resource name instead
  of the numeric ID 1 that the version API looks up
  (`FindResource(MAKEINTRESOURCE(VS_VERSION_INFO)=1, RT_VERSION)`). The
  block silently embeds under the wrong name. Fix: declare it `1 VERSIONINFO`
  directly (or `#include <winver.h>` to expand the macro). `build-package.ps1`
  validates this with `Get-Item(...).VersionInfo.ProductName`.
* **PNG-compressed ICO entries are fine for Windows 10/11.** The icon
  generator (`make_icon.ps1`) writes 7 sizes (16–256) all as embedded PNGs
  rather than the traditional mixed BMP+PNG format. Windows 10 and 11
  (the port's only supported targets) render PNG entries at every size
  correctly. This avoids the complex BMP DIB construction (BITMAPINFOHEADER
  + bottom-up XOR data + AND mask) in pure PowerShell.
* **ANGLE DLLs must ship next to the engine in both ZIP and installer.**
  The engine dlopens `libEGL.dll`/`libGLESv2.dll` at capture time
  (`gsr_egl_win32.c`); without them it fails at runtime. The build job's
  ANGLE bundle step copies them from `/mingw64/bin/` (MSYS2 package
  `angleproject`) next to `gpu-screen-recorder.exe`; the artifact glob
  `build/cmake/*.dll` includes them. The package job hard-fails if either
  DLL is missing from the staged layout.
* **`ExtractIconEx` is the right icon-existence probe.** Calling
  `ExtractIconEx(path, -1, null, null, 0)` returns the count of icon
  resources in the PE — 0 means the exe has no `.ico` embedded. This is
  cheaper and more reliable than parsing the PE resource tree in PowerShell,
  and avoids the weakness of `ExtractAssociatedIcon` (which returns a
  generic application icon for exes with no icon resource, making a positive
  assertion meaningless).
* **`$ErrorActionPreference = "Stop"` breaks `2>&1` on native commands.**
  In PowerShell 5.1, capturing stderr via `2>&1` with EAP Stop turns each
  stderr line into a terminating `ErrorRecord`. The `Assert-Runs` validation
  function suspends EAP for the native invocation scope (`try/finally`),
  checks `$LASTEXITCODE` immediately, and only throws on a non-zero exit.
  Without this guard, any engine output to stderr (log messages, warnings)
  would abort the entire package build.
