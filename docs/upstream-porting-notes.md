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
* **`EGL_D3D_TEXTURE_ANGLE` images bind as `GL_TEXTURE_2D`, NOT
  `GL_TEXTURE_EXTERNAL_OES`.** Upstream's external-image shader variants
  (used for DMABUF captures) bind EXTERNAL_OES; passing `external_texture`
  = true to `gsr_color_conversion_draw` would therefore sample an image
  that is not an EXTERNAL_OES texture. The WGC backend imports into
  `GL_TEXTURE_2D` and passes `external_texture=false`, which uses the
  regular sampler2D shaders — correct on ANGLE and still zero-copy.
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
