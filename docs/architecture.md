# Windows Port Architecture

**Phase 1 deliverable; updated through Phase 2.** This document defines how the upstream GPU Screen
Recorder project is ported to native Windows x64. It follows the core rule from
the project brief:

> PRESERVE EVERYTHING THAT IS PLATFORM-INDEPENDENT.
> REPLACE ONLY WHAT IS PLATFORM-SPECIFIC.

Read `docs/upstream-analysis.md` first for the upstream architecture this
document builds on.

## 1. Port strategy

* Vendor the upstream engine + UI source trees into this repository (GPL-3.0,
  attribution preserved — see `docs/licensing.md`).
* Keep upstream files byte-identical where possible; add platform abstraction
  headers; add `platform/windows/...` backends; add the minimal `#ifdef`
  plumbing at the existing vtable seams.
* The engine keeps its FFmpeg-based encode/mux/replay/stream pipeline and its
  CLI/IPC contracts (see `docs/upstream-analysis.md` §10). Only the platform
  layer changes.
* The UI keeps the mglpp widget stack, all pages/settings/translations/assets,
  and the engine-control contract (spawn engine, parse stdout, control via
  IPC). Only mgl's windowing backend and the ~6 small platform modules change.

## 2. Repository layout

```
gpu-screen-recorder-windows/
├── .github/workflows/windows-release.yml   # single CI/CD workflow (build→test→package→release)
├── CMakeLists.txt           # Windows build (gsr_core + tests)
├── upstream/                # vendored engine (gpu-screen-recorder 6.0.0 r1467)
│   ├── src/, include/, external/   # unmodified except documented de-X11 header patches
├── platform/
│   ├── include/             # platform abstraction interfaces (owned by this port; Phase 3)
│   ├── linux/               # (reference; not built on Windows)
│   └── windows/             # Windows backends + portability shims (Phase 2: gsr_win32_compat,
│                            #   gsr_utils_win32, libgen.h/dlfcn.h shims; capture/audio/etc. later)
├── scripts/
│   ├── build-ffmpeg-windows.sh   # pinned FFmpeg stack from source (mirrors upstream recipe)
│   └── patches/                  # upstream's two FFmpeg patches, applied verbatim
├── tests/                   # ci-smoke, compat-probe, gsr-core-test (see roadmap)
└── docs/                    # this documentation set
```

Later phases add `ui/` (gpu-screen-recorder-ui + vendored mglpp/mgl), `notify/`
(gsr-notification), `tools/` (gsr-cli, gsr-ui-cli), and `installer/`.

Decision notes:

* **One repo, one workflow.** Engine + UI + notify live in one repository so
  the single `windows-release.yml` can build everything and produce one
  installer/zip, per the project brief. Upstream revision pins are recorded in
  `docs/upstream-porting-notes.md`.
* **`reference/`** (the raw upstream snapshots used for analysis) is
  gitignored; the vendored source under `upstream/` is committed.
* **Build system (decided in Phase 2):** CMake + Ninja + **MinGW-w64**
  (MSYS2 MINGW64). Upstream is GNU C11 (`gnu11`, `__attribute__`,
  `-Wshadow`), so GCC is the natural compiler; MSVC would force invasive
  changes to shared code. FFmpeg is built once from pinned sources by
  `scripts/build-ffmpeg-windows.sh` (mirrors upstream's static recipe) and
  cached by CI. See `docs/build-windows.md`.

## 3. The engine port

### 3.1 Abstractions (already exist upstream — reuse them)

| Upstream interface | Windows implementation |
|---|---|
| `gsr_capture` (vtable) | `gsr_capture_windows_graphics_capture` (primary), `gsr_capture_dxgi_duplication` (fallback), window capture via WGC items |
| `gsr_video_encoder` (vtable) | `gsr_video_encoder_nvenc` (via FFmpeg `*_nvenc` + d3d11va), `gsr_video_encoder_software` — NVIDIA-only port (AMD/Intel dropped, 2026-08-15) |
| `gsr_replay_buffer` (vtable) | **unchanged** (RAM/disk encoded-packet buffers) |
| `sound_device_*` (`include/sound.h`) | WASAPI implementation |
| `gsr_windowing` / `gsr_window` | Win32 window + event pump (replaces X11 window; EGL replaced per §3.3) |
| `gsr_cli`/IPC JSON protocol | named-pipe transport, identical JSON messages |
| signals (SIGUSR1/2, SIGRTMIN*) | named events + a tiny control-pipe; IPC-first (§7) |
| `gsr_egl` GL table | WGL/EGL-on-Windows GL table (if GL pipeline retained) or D3D11 (§3.3) |

### 3.2 Capture architecture (Windows)

```
CaptureBackend (gsr_capture)
├── WindowsGraphicsCaptureBackend      # primary: Windows.Graphics.Capture (WGC)
│     monitor item  → D3D11 texture (shared)
│     window item   → D3D11 texture
│     cursor        → WGC cursor API (shape/size/position)
├── DXGIDesktopDuplicationBackend      # fallback (e.g. WGC unavailable / OBS-style legacy)
└── (future)                          # Webcam via Media Foundation (optional overlay)
```

* WGC frames arrive on a `IDirect3DDuplication`-like texture; the capture
  backend copies/rotates into the pipeline's input D3D11 texture. No CPU
  readback on the hot path.
* Monitor enumeration: DXGI `EnumAdapters/EnumOutputs` + `GetMonitorInfoW` for
  names (`\\.\DISPLAY1` etc.), resolutions, refresh rates, rotation, HDR state.
  `--list-monitors` / `--list-capture-options` map onto this.
* Multi-monitor / mixed DPI: per-output coordinates via `GetDpiForMonitor`
  (per-monitor-v2 DPI awareness); WGC items are physical-pixel based.
* HDR: query output color space; write mastering/light metadata into HEVC/AV1
  HDR streams via the existing `gsr_capture_set_hdr_metadata` hook; document
  SDR tonemapping behavior when the capture is HDR but the encoder is SDR.
* Device loss / display mode change: WGC `IsSupported` re-check + recreate
  capture on failure, mirroring upstream's crash-recovery TODO.

### 3.3 Rendering / encode path decision

**DECIDED (Phase 5 spike, user-approved): Option B — GL via ANGLE.**

* **Option A (rejected): D3D11-native.** WGC/DXGI → D3D11 texture →
  FFmpeg `d3d11va` hwframe (`av_hwframe_ctx` with `AV_HWDEVICE_TYPE_D3D11VA`)
  → `h264_nvenc` / `hevc_nvenc` / `av1_nvenc` / `libx264`. Color conversion and
  scaling are done with D3D11 shaders (re-implement `color_conversion.c`
  against D3D11) or with GPU-side FFmpeg filters. This removes GL entirely
  from the engine hot path and is the closest Windows analogue of upstream's
  "GPU-only" pipeline. Plugin rendering (upstream draws with GL) is the main
  casualty: plugins get a D3D11 rendering context via the same plugin.h ABI,
  or GL is kept only for plugins (§3.4).
* **Option B (chosen): GL-on-Windows via ANGLE.** Provide an EGL backend via
  ANGLE so upstream's `egl.c`/`shader.c`/`color_conversion.c`/`plugins.c`
  run unchanged; import WGC's D3D11 capture textures into GL with
  `EGL_ANGLE_d3d_texture_client_buffer` + `EGL_ANGLE_device_d3d`, running
  ANGLE on the SAME D3D11 device WGC uses — zero-copy, no staging copy.
  Rationale (Phase 5 spike): (a) the entire upstream color-conversion and
  plugin render path stays byte-for-byte identical — the lowest-risk path to
  parity; (b) `mingw-w64-x86_64-angleproject` is a first-class MSYS2
  package, so the dependency is CI-installable; (c) the extension spec
  (chromium.googlesource.com/angle/...) confirms `eglCreateImageKHR` with
  target `EGL_D3D11_TEXTURE_ANGLE` wraps a D3D11 texture as a `GL_TEXTURE_2D`
  sibling with no copy when the display shares the device. Trade-offs
  accepted: ANGLE runtime dependency (bundled with the installer) and the
  D3D11/GL interop layer is ANGLE-specific.

**Validation status (Phase 5b, 2026-08-16):** the ANGLE render backend is
SHIPPED and CI-green. `gsr_egl_load` on Windows runs ANGLE on a shared
D3D11 device (hardware → WARP) with a surfaceless ES3 context; the WGC
backend imports its D3D11 texture with `EGL_D3D11_TEXTURE_ANGLE` and draws
through the unchanged upstream `gsr_color_conversion`. `render-self-test`
validates the full path headless on WARP (synthetic texture → import →
draw → readback; BGR swizzle, orientation, rotation). End-to-end
WGC→encode still needs a real Win10/11 desktop (the Server-SKU CI runner
lacks the WGC interop DLL, §3e) — see `docs/implementation-roadmap.md`
Phase 5b.

Either way the encoder input is an `AVFrame` in a hardware pixel format; the
existing `gsr_video_encoder` interface is preserved.

### 3.4 Plugins

Keep `plugin.h` source-compatible. On Windows plugins are `.dll`s; the plugin
render context matches the pipeline's GL/D3D11 choice (§3.3). Documented in
the parity matrix as PARTIAL (rendering API may differ in backend).

### 3.5 Audio (WASAPI)

* `default_output` → WASAPI **loopback** on the current default render
  endpoint, following default-device changes via `MMNotificationClient`.
* `default_input` → WASAPI capture on the default input endpoint.
* `device:name` → WASAPI endpoint by device ID/name; `--list-audio-devices`
  enumerates endpoints (name + description, same `name|description` format).
* `app:name` → WASAPI session enumeration
  (`IAudioSessionManager2`/`IAudioSessionControl`) to capture a specific
  process's audio, or an audio-session group; if a faithful equivalent is
  impossible, provide "all desktop audio minus named app" where feasible and
  document the limitation. (Investigated in Phase 8 with a spike.)
* Sample format conversion (F32/S16/S32) and resampling happen via
  `libswresample` exactly as upstream does; `sound_device_read_next_chunk`
  keeps its contract (chunk + latency in seconds).
* Audio device change → error/recovery path per upstream's TODO.

### 3.6 IPC and control

* Unix-domain socket → **named pipe** `\\.\pipe\gsr-<pid>` (name includes pid
  so multiple instances don't collide; `gsr-cli` discovers the live instance
  the same way the UI does). Newline-terminated JSON messages byte-identical
  to upstream's protocol (requests/replies, deferred replies for
  stop/save-replay, `status` → `running`/`not running`).
* `gsr-cli.exe` keeps its CLI (`-ipc <pipe> <command> [args]`) and exit codes.
* Signals → the engine registers a set of named events
  (`Global\gsr-<pid>-stop`, `-save-replay`, `-pause`, `-save-replay-<n>s`, ...)
  plus a poll of the control pipe inside the main loop, so the UI/gsr-ui-cli
  can trigger actions without POSIX signals. All control still funnels into
  the same atomic flags the recorder loop already consumes — zero changes to
  `recorder.c` control semantics.
* UI→engine integration stays: UI spawns `gpu-screen-recorder.exe` with the
  same argument construction, reads stdout for saved paths, controls via
  gsr-cli-style IPC + events instead of signals.

### 3.7 Paths and filesystem

```
%LOCALAPPDATA%\gpu-screen-recorder\        # runtime/temp (replay disk buffer temp, session state)
%APPDATA%\gpu-screen-recorder\             # config (config_ui, restore_token equivalent)
%USERPROFILE%\Videos\                      # default save directory (matches upstream ~/Videos)
%APPDATA%\Microsoft\Windows\Start Menu\Programs\  # shortcuts (installer)
```

A `platform/filesystem` abstraction maps upstream path helpers; no Linux paths
are hard-coded. Unicode: all file APIs use wide-char (`wchar_t`) variants;
filename sanitation for Windows-invalid characters added to the filename
generation paths.

### 3.8 Threading, clocks, sleeps

* `clock_gettime(CLOCK_MONOTONIC)` → `QueryPerformanceCounter`.
* `av_usleep`/`usleep` → `Sleep`/waitable timers (loop is unchanged otherwise).
* pthreads → `std::thread` shim or Win32 threads (upstream already uses
  `stdatomic`); a `platform/thread` header provides the small set of used
  primitives (mutex, thread, cond). No redesign of the threading model:
  video loop thread, audio capture threads, IPC accept loop, replay-save
  worker.

### 3.9 Child processes

* `exec_program`-style spawning in the UI and `-sc` script execution → Win32
  `CreateProcess` with stdout-pipe capture (the UI already parses stdout).
* `-sc` scripts: Windows `.bat`/`.cmd`/`.ps1`/exe — first arg is the saved
  file path, second the type (`regular|replay|screenshot`). Documented.
* `gsr-kms-server` (root helper) → **not needed**: WGC requires no elevation.
  Removed. (See parity matrix.)

## 4. The UI port

### 4.1 mgl Windows backend

The only *required* UI-layer port is a new mgl platform backend:

* `mgl/src/window/win32.c` — window creation (borderless fullscreen overlay,
  topmost, click-through states), message pump, input translation
  (WM_KEYDOWN/WM_CHAR → mgl key codes, WM_MOUSEMOVE/… → mouse, WM_MOUSEWHEEL,
  focus/activate events), per-monitor DPI awareness.
* `mgl/src/graphics/backend/wgl.c` (or egl-on-windows) — GL 3.3+ context,
  swap interval, GL function loading (wglGetProcAddress / gl3w-style loader).
* Fonts: replace pangoft2 with DirectWrite (or a freetype atlas) behind mgl's
  `font_atlas/glyph_map` interface so `Text`/`TextEdit` widgets are untouched.
* stb_image and the rest of mgl's graphics stack are platform-free and stay.

### 4.2 UI platform modules (replacement table)

| Module | Windows replacement |
|---|---|
| `GlobalHotkeys/*` | `RegisterHotKey` (VK+modifiers) + `RegisterRawInputDevices`; joystick via XInput |
| `CursorTracker/*` | `GetCursorPos` + `MonitorFromPoint`; focused-window via `GetForegroundWindow` |
| `RegionSelector/*` | transparent Win32 overlay window (WGC region capture) |
| `DesktopEnvironment/*` | `GetForegroundWindow`/`GetWindowTextW` + process name for game-name folders; drop GNOME/KDE helpers |
| `Clipboard/*` | Win32 clipboard (CF_DIB / CF_HDROP) — works while unfocused on Windows |
| `WaylandHostBridge` / `Hotplug` / `LedIndicator` | removed (or LED via Scroll-Lock hack, documented) |
| `AudioPlayer` | Windows sound (PlaySound/MMDevice) |
| `window_texture.c` (overlay background) | WGC window-item preview texture or dropped (documented) |
| RPC (`Rpc.cpp`) | named pipe, same newline commands |
| `gsr-ui-cli` | named-pipe client binary |

### 4.3 Startup / tray

* "Start program on system startup?" → HKCU `Run` key or Startup-folder
  shortcut (least invasive, no admin). Replaces XDG autostart.
* Tray: upstream UI has a TODO for systray and the gtk frontend (deprecated)
  owns the tray today. The port ships a minimal tray icon in the UI
  (idle/recording/paused) only if it doesn't distort the upstream UX;
  otherwise documented as WINDOWS-SPECIFIC addition. Decision in Phase 10.

### 4.4 Overlay behavior on Windows

* Fullscreen borderless overlay window on the target monitor, topmost,
  keyboard focus, `WS_EX_TOPMOST`/`WS_EX_NOACTIVATE` tricks as needed; hide on
  Alt+Z; per-monitor placement matches upstream's "show on cursor monitor".
* Capture-background (window texture) via WGC preview; failure → plain tinted
  background (never silently record the wrong target).

## 5. Encoders: capability detection

* Reuse `src/codec_query/` shape: a Windows `codec_query` implementation that
  probes FFmpeg's available encoders + GPU APIs:
  * NVIDIA: `nvEncodeAPI`/FFmpeg nvenc support + driver version; expose
    H264/HEVC/AV1 per capability; older GPUs get graceful degradation
    (no AV1 on pre-RTX; HEVC limits on GTX 9xx/10xx documented).
  * Software: `libx264` (+ libx265/libsvtav1 only if upstream parity requires;
    upstream currently restricts `-encoder cpu` to H264 — keep that).
* Only expose codecs the current GPU actually supports (`--info` output stays
  `key|value`). Never fake support (brief §57).
* HDR: expose `hevc_hdr`/`av1_hdr`/`_10bit` only when the pipeline can deliver
  real 10-bit capture + metadata; otherwise hide them and document.

## 6. Streaming

Keep everything: `-o <url>` with FFmpeg protocols (RTMP/SRT/WHIP/HLS).
Requires the Windows FFmpeg build to include the same muxers/protocols as the
upstream static build (see roadmap Phase 2 FFmpeg provisioning).

## 7. Control surface summary (compatibility contract)

| Upstream mechanism | Windows mechanism | Notes |
|---|---|---|
| SIGINT/SIGTERM | WM_CLOSE / named event + `gsr-cli stop` | UI sends stop via IPC |
| SIGUSR1 / SIGRTMIN+n (save replay) | named events + `gsr-cli save-replay [n]` | same semantics |
| SIGUSR2 (pause) | named event + `gsr-cli set-paused` | same semantics |
| SIGRTMIN (replay-recording) | named event + `gsr-cli start/stop-replay-recording` | same semantics |
| Unix socket IPC | named pipe | same JSON protocol |
| `gsr-cli` | `gsr-cli.exe` | same CLI/exit codes |
| `gsr-ui-cli` | `gsr-ui-cli.exe` | same commands |

The engine will additionally keep a `--help`/`--version`/`--info` identical to
upstream (with Windows-specific sections documented).

## 8. Performance goals (brief §38)

* Capture → GPU texture → hardware encode with **no CPU readback** in the hot
  path (Option A pipeline). CPU only orchestrates.
* Bounded queues; the recorder loop already paces via the recording clock.
* Measure in CI where possible (CPU% of smoke recording on runner hardware
  where a GPU exists is not guaranteed — see roadmap §“CI hardware limits”);
  document methodology in `docs/performance.md` (Phase 15).

## 9. Non-goals (explicit)

* No Electron/WebView/React; no telemetry; no accounts; no OBS dependency.
* No kernel drivers; no DLL injection; no anti-cheat bypass; no admin
  requirement for normal operation (WGC is a user-mode API).
* No redesign of the UI or the CLI; no renaming of components beyond
  `.exe`/`.dll` extensions and `gsr-notification.exe` (upstream's notification
  binary is `gsr-notify`; see `docs/windows-port-parity.md` for the name
  rationale).

## 10. Open decisions (to be resolved in CI spikes, tracked in the roadmap)

1. ~~Build system/toolchain~~ — **decided Phase 2: CMake + Ninja + MinGW-w64**.
2. ~~FFmpeg provisioning~~ — **decided Phase 2: compile-from-source in CI**, pinned
   to upstream's versions, cached.
3. Render/encode pipeline: Option A (D3D11-native) vs Option B (GL-on-Windows)
   (Phase 5).
4. Per-app audio feasibility on WASAPI (Phase 8).
5. Installer tech: Inno Setup 6 via chocolatey (decided in Phase 13; produces a per-user install with Start Menu/desktop shortcuts, optional HKCU autostart, and uninstaller).
6. Tray icon scope (Phase 10).
7. Webcam capture scope (Media Foundation) — parity or documented limitation
   (Phase 5/11).
