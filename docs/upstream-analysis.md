# Upstream Analysis — GPU Screen Recorder

**Phase 1 deliverable.** This document analyzes the upstream GPU Screen Recorder
project (engine, UI, GTK frontend, notification app) so that the Windows port can
preserve everything that is platform-independent and replace only the
Linux-specific platform layer.

## 1. Upstream repositories and revisions

The upstream project consists of four source repositories maintained by
dec05eba, plus build manifests. All are licensed **GPL-3.0-only**.

| Repository | Version | Revision | Purpose |
|---|---|---|---|
| [gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/) | 6.0.0 | r1467 / d31b698 | The recording engine (CLI daemon) |
| [gpu-screen-recorder-ui](https://git.dec05eba.com/gpu-screen-recorder-ui/) | 1.13.5 | r720 / edfc70d | ShadowPlay-style fullscreen overlay UI |
| [gpu-screen-recorder-gtk](https://git.dec05eba.com/gpu-screen-recorder-gtk/) | 5.8.0 | r513 / dade88d | GTK3 tray frontend (deprecated upstream) |
| [gpu-screen-recorder-notification](https://git.dec05eba.com/gpu-screen-recorder-notification/) | 1.3.4 | r110 / 8db3818 | ShadowPlay-style notification overlay |
| [flathub com.dec05eba.gpu_screen_recorder](https://github.com/flathub/com.dec05eba.gpu_screen_recorder) | — | — | Flatpak manifest; pins exact source snapshots and the full dependency set |

The exact source snapshots used for this analysis were obtained from the
official snapshot server (`https://dec05eba.com/snapshot/...`) at the revisions
pinned by the current flathub manifest, so all version numbers above are
authoritative.

Two additional libraries are consumed as build-time subprojects:

| Library | Version | Purpose |
|---|---|---|
| [mgl](https://repo.dec05eba.com/mgl) | 1.1.0 | Minimal Graphics Library (C): windowing (X11/Wayland), OpenGL/EGL context, input, fonts (pango), textures |
| mglpp | 1.1.0 | C++ wrapper around mgl used by gsr-ui and gsr-notify |

The full source of mgl and mglpp is **vendored inside the UI snapshot**
(`gpu-screen-recorder-ui/depends/mglpp/...`), which makes the UI build
self-contained on top of its platform dependencies.

## 2. Language and build systems

* **Engine:** C (GNU C11, `gnu11`), Meson + Ninja. Single binary plus two helpers.
* **UI:** C + C++17 (mglpp is C++17), Meson + Ninja. One main binary plus six helper tools.
* **GTK frontend:** C + C++ (GTK3), Meson.
* **Notification:** C++17 (mglpp), Meson.

The engine is deliberately plain C with function-pointer ("vtable") interfaces
at every platform seam. That is the single most important fact for portability:
**the Windows port can keep the entire engine core and swap the platform
implementations behind the existing interfaces.**

## 3. Engine architecture

### 3.1 Components

```
gpu-screen-recorder (CLI daemon)
├── src/args_parser.c        — command line parsing (-w, -f, -k, ...)
├── src/cli/main.c           — entry point, signal handlers, --info/--list-* commands
├── src/cli/ipc.c            — Unix-domain-socket JSON RPC server (-ipc option)
├── src/cli/commands.c       — --info / --list-* output (key|value format)
├── src/recorder/recorder.c  — main capture→render→encode loop (single video thread)
├── src/recorder/capture_setup.c  — builds capture sources from -w
├── src/recorder/video_codec.c   — codec selection/validation
├── src/recorder/codec_select.c  — picks encoder backend + fallbacks
├── src/recorder/audio_input.c   — parses -a audio sources (device:|app:)
├── src/recorder/audio_capture.c — audio capture threads + resampling
├── src/recorder/recording_clock.c — monotonic clock + pause handling
├── src/recorder/muxer.c         — AVFormatContext/stream setup
├── src/recorder/replay_save.c   — asynchronously writes replay buffer to file
├── src/recorder/screenshot.c    — one-shot screenshot path
├── src/replay_buffer/*.c        — RAM and disk replay buffers
├── src/capture/*.c              — capture backends (see 3.2)
├── src/encoder/encoder.c        — packet fan-out: muxer destinations + replay buffer
├── src/encoder/video/*.c        — encoder backends (see 3.3)
├── src/codec_query/*.c          — capability probing (nvenc/vaapi/vulkan)
├── src/window/*.c               — window/monitor enumeration + X11 events
├── src/egl.c                    — OpenGL (EGL/GLX) context + GL function table
├── src/shader.c, color_conversion.c — GLSL color conversion (RGB→YUV etc.)
├── src/window_texture.c         — X11 window → GL texture (composite)
├── src/cursor.c, damage.c       — X11 cursor capture, XDamage tracking
├── src/sound.c                  — PulseAudio device capture
├── src/pipewire_audio.c         — per-application audio capture
├── src/pipewire_video.c, dbus.c — xdg-desktop-portal screen cast
├── src/plugins.c                — plugin loading (shared libs, GL rendering)
├── src/image_writer.c           — JPEG (libturbojpeg) / PNG (stb) writing
├── src/utils.c, json.c, log.c, library_loader.c — portable helpers
└── protocol/                    — custom Wayland protocols (wayland-scanner output)
```

### 3.2 Capture backends (all behind `gsr_capture` vtable)

| File | Linux tech | Captures | Notes |
|---|---|---|---|
| `src/capture/kms.c` | DRM/KMS + EGL DMA-BUF | Monitor (AMD/Intel, and NVIDIA Wayland) | Needs root; split into `gsr-kms-server` helper |
| `src/capture/nvfbc.c` | NvFBC + X11 | Monitor / screen-direct (NVIDIA X11) | VRR/G-SYNC support |
| `src/capture/xcomposite.c` | XComposite + GL | Window (X11) | Window texture via composite |
| `src/capture/ximage.c` | XGetImage | Window (X11, fallback) | CPU readback |
| `src/capture/portal.c` | xdg-desktop-portal + PipeWire | Monitor/window (Wayland) | HDR not supported; session restore token |
| `src/capture/v4l2.c` | V4L2 + libdrm fourcc | Webcam | Optional overlay source |
| `src/window/window.c` | — | — | Monitor/window enumeration abstraction |

The `gsr_capture` interface (see `include/capture/capture.h`) is:
`start / on_event / tick / should_stop / capture / pre_capture /
uses_external_image / set_hdr_metadata / get_window_id / is_damaged /
clear_damage / destroy`, with `gsr_capture_metadata` carrying video size,
recording size, position, fps, alignment and flip. A Windows backend only has
to implement this interface.

### 3.3 Encoder backends (all behind `gsr_video_encoder` vtable)

| File | Backend | Notes |
|---|---|---|
| `src/encoder/video/nvenc.c` | NVENC via FFmpeg (`nvenc*`) | GPU |
| `src/encoder/video/vaapi.c` | VA-API via FFmpeg (`vaapi_*`) | AMD/Intel GPU |
| `src/encoder/video/vulkan.c` | Vulkan video encode | Experimental; avoids NVIDIA "p2 state" issue |
| `src/encoder/video/software.c` | CPU (libx264) | `-encoder cpu`, H264 only |

All backends feed `AVCodecContext` + `AVFrame` through FFmpeg's
`avcodec_send_frame`/`avcodec_receive_packet`. The video encoder interface is
`start / destroy / copy_textures_to_frame / get_textures` — i.e. the encoder
tells the renderer which GL textures to draw into, and then copies those
textures into an `AVFrame` (GPU-side for hardware encoders, readback for CPU).
On Windows this same interface can be implemented with D3D11 textures +
FFmpeg hardware frame mapping (see `docs/architecture.md`).

### 3.4 The recording pipeline (recorder.c)

Single-threaded video loop; audio runs on its own threads:

```
while running:
    process window events (X11) + cursor/damage events
    tick capture sources (damage check, should_stop check)
    if damaged and frame due:
        glClear -> capture source(s) into GL textures -> plugins draw
        -> gsr_egl_swap_buffers -> video encoder copy_textures_to_frame
        -> avcodec_send_frame -> receive packets
        -> encoder fans packets out to: recording destination(s) + replay buffer
    apply pause / replay-recording / replay-save requests (atomic flags)
    sleep until next frame (clock-based pacing)
```

* Control is **asynchronous**: signal handlers / IPC threads only write atomic
  flags (`running`, `toggle_pause`, `save_replay_seconds`, ...); the loop
  consumes them. This design ports cleanly to Windows.
* `gsr_recording_clock` is a monotonic clock (CLOCK_MONOTONIC) with pause
  support; frame PTS derive from it. Replaces cleanly with
  `QueryPerformanceCounter`-based clock.
* Pacing uses `clock_gettime` + `av_usleep`; Windows equivalent is a
  `QueryPerformanceCounter`/`Sleep` combination or a waitable timer.
* The encoder (`src/encoder/encoder.c`) supports **multiple recording
  destinations** from one capture+encode pipeline (regular recording while
  replay/streaming runs) — `GSR_MAX_RECORDING_DESTINATIONS 128`. This is pure
  FFmpeg logic, fully portable.

### 3.5 Replay buffer

* `gsr_replay_buffer` vtable with `RAM` and `disk` backends
  (`replay_buffer_ram.c`, `replay_buffer_disk.c`). Both store **encoded
  packets** (AVPacket), not raw frames — lightweight, and the buffer exists
  continuously before the save hotkey is pressed (true instant replay).
* RAM backend: ring of preallocated blocks sized from
  `calculate_estimated_replay_buffer_packets()`.
* Disk backend: `gsr-replay-*.gsr` temp files in the output directory, trimmed
  to the configured duration.
* Save path: `replay_save.c` clones the buffer on a worker thread and muxes to
  a final file (`Replay_YYYY-MM-DD_HH-MM-SS.<ext>`), reports the path via
  stdout + callback, and optionally runs the `-sc` script and restarts the
  buffer. **All of this is platform-independent** and is preserved as-is.

### 3.6 Audio

* `src/sound.c` — PulseAudio (mainloop API) device capture. Interface
  (`include/sound.h`): `sound_device_get_by_name` (with
  `default_output`/`default_input` that follow the system default),
  `sound_device_read_next_chunk`, `sound_device_flush`. Returns float32 or
  s16/s32 chunks with latency info. **This interface is the WASAPI seam.**
* `src/pipewire_audio.c` — per-application capture (`-a app:name`) by creating
  a virtual sink and linking streams; requires PipeWire.
* `src/recorder/audio_capture.c` — thread(s) reading devices, resampling via
  `libswresample`, optional `amix` filter graph when multiple sources share a
  track, feeding `avcodec_send_frame`. Mostly FFmpeg — portable.
* Audio codecs: Opus (default), AAC; FLAC disabled upstream due to desync
  issues (kept disabled for parity).
* A/V sync: audio PTS are offset by the codec's desired delay; the video clock
  is monotonic. No frame-count assumptions.

### 3.7 Screenshot

`src/recorder/screenshot.c` — one-shot run of the same capture pipeline
(capture → GL → readback) then JPEG (libturbojpeg, `image_writer.c`) or PNG
(stb_image_write). Detected by the output filename extension (.jpg/.png).

### 3.8 Streaming

`-o` can be a URL; FFmpeg muxing handles RTMP/RTSP/SRT/WHIP/HLS/etc. Streaming
is just the normal recorder with `is_livestream=true` (adds silent audio track
if no audio, forces no-audio-offset, avoids local file headers). Fully
portable once FFmpeg is built with the needed protocols.

### 3.9 CLI, signals, IPC

* CLI: `-w`, `-c`, `-s`, `-f`, `-cursor`, `-a`, `-ac`, `-ab`, `-k`, `-q`,
  `-bm`, `-fm`, `-cr`, `-tune`, `-keyint`, `-encoder`, `-fallback-cpu-encoding`,
  `-r`, `-replay-storage`, `-restart-replay-on-save`, `-df`, `-p`, `-sc`,
  `-ffmpeg-opts`, `-ffmpeg-video-opts`, `-ffmpeg-audio-opts`, `-gl-debug`,
  `-v`, `-low-power`, `-exclude-metadata`, `-write-first-frame-ts`, `-ipc`,
  `-o`, `-ro`, `-region`, `-restore-portal-session`,
  `-portal-session-token-filepath`; info commands `--help`, `--version`,
  `--info`, `--list-capture-options`, `--list-monitors`, `--list-v4l2-devices`,
  `--list-audio-devices`, `--list-application-audio`. Full reference in
  `gpu-screen-recorder.1` (vendored). **Windows port must keep all portable
  options; Linux-only ones must error clearly, never be silently ignored.**
* Signals: SIGINT/SIGTERM stop; SIGUSR1 save full replay; SIGUSR2 pause toggle;
  SIGRTMIN toggle replay-recording; SIGRTMIN+1..+6 save 10s/30s/1m/5m/10m/30m.
  Windows has no POSIX signals — replaced by named events + IPC (see
  `docs/architecture.md` §7).
* IPC (`src/cli/ipc.c`): optional Unix-domain socket, newline-terminated JSON
  requests/replies (`id`, `name`, `data`; reply `result: ok|error`, `data` =
  saved file path). Commands: `stop`, `save-replay [seconds]
  [restart-replay=bool]`, `toggle-pause`, `set-paused`, `toggle/start/stop
  -replay-recording`, `status`. `gsr-cli` is the reference client (vendored,
  ~330 lines). **Protocol preserved; transport becomes a named pipe.**
* `--info` and `--list-*` output uses a stable `key|value` / `name|WxH` line
  format that the UI parses (`GsrInfo.cpp`). **The Windows port must keep this
  exact format or update the ported UI parser in lockstep.**

### 3.10 Plugins

`plugin/plugin.h` — C ABI, `gsr_plugin_load/draw/destroy`-style API; plugins
render with OpenGL into the shared output framebuffer. On Windows the plugin
API can stay source-compatible, but the render context becomes the Windows GL
context (WGL/EGL), and plugins are `.dll` files instead of `.so`. Documented
as a parity consideration (see `docs/windows-port-parity.md`).

### 3.11 Configuration and paths

* No config file for the engine itself; everything is CLI. A `.env` file
  (`~/.config/gpu-screen-recorder/gpu-screen-recorder.env`) configures the
  optional systemd user service only.
* Portal session token: `~/.config/gpu-screen-recorder/restore_token`.
* NVIDIA perf profile: `~/.nv/nvidia-application-profiles-rc.d/10-gsr-...`
  (installed automatically on NVIDIA).
* NVIDIA suspend fix: `/usr/lib/modprobe.d/gsr-nvidia.conf` (system-wide
  install; not needed on Windows — the Windows driver handles suspend).
* Replays default to RAM; disk buffer temp files live next to the output dir.

### 3.12 Threading / process model

* One process (`gpu-screen-recorder`) + optional `gsr-kms-server` (root helper
  for DRM) + optional `gsr-cli` invocations.
* Video: single loop thread. Audio: capture threads. IPC: accept loop.
  Replay save: worker thread. All communication via atomics/mutexes.
  Bounded queues; no unbounded growth. This model maps 1:1 to Windows threads
  (no fork/exec beyond child process spawning for gsr-cli-style helpers).

## 4. UI architecture (gpu-screen-recorder-ui)

### 4.1 Overview

A **fullscreen overlay** in the style of NVIDIA ShadowPlay. Native
C++/C application rendering with OpenGL through the author's own GUI stack:

```
gsr-ui
├── mglpp (C++17 wrapper) — graphics/Text, Image, Texture, Shader, Sprite, VertexBuffer; window/Window, Keyboard; system/Clock, Utf8
└── mgl (C) — src/window/x11.c, src/window/wayland.c, src/graphics/backend/{glx,egl}.c,
              font rendering (pangoft2), stb_image, GL function table
```

It is **not** a web/Electron/GTK UI. All widgets are custom-drawn with
OpenGL: `src/gui/` contains Button, CheckBox, ComboBox, Entry, RadioButton,
List, Label, Image, Tooltip, Page/PageStack/StaticPage/ScrollablePage,
DropdownButton, FileChooser, SettingsPage, ScreenshotSettingsPage,
GlobalSettingsPage, GsrPage, Subsection, LineSeparator, CustomRendererWidget.

### 4.2 Behavior

* Launch actions: `launch-show`, `launch-hide` (default), `launch-hide-announce`,
  `launch-daemon`, `install-startup`. Second instance forwards to the running
  one via the RPC socket.
* `Left Alt+Z` shows/hides the overlay (configurable hotkey).
* Front page: Record / Replay / Stream / Screenshot buttons; replay save
  buttons (full / 1 min / 10 min); region and window capture modes; profiles;
  webcam overlay; settings gear.
* Settings pages mirror the engine's options: quality/bitrate/fps/codec/
  container, audio devices, hotkeys, save directories, language, startup,
  notification speed, tint color, encoder (gpu/cpu), color range, etc.
* `gsr-ui` **spawns `gpu-screen-recorder` as a child process** with a fully
  built command line (see `Overlay.cpp`), captures its stdout to learn saved
  file paths, and controls it with signals (SIGUSR1 save replay, SIGUSR2
  pause) and by killing the process on stop. On Windows the kill() shim
  terminates the child outright and there are no POSIX signals, so the port
  starts the engine with `-ipc gsr-record`/`gsr-replay`/`gsr-stream` and
  drives it through `gsr-cli` (save-replay [n], toggle-pause,
  toggle-replay-recording, stop) instead. It also spawns `gsr-notify` for
  notifications and `gsr-game-tracker` for the game-name list.
* The overlay background can show the focused window (X11 XComposite texture
  via `window_texture.c`).
* Config: `~/.config/gpu-screen-recorder/config_ui` — custom
  `key=value`-line format with `main.*` keys (config_file_version,
  show_hide_hotkey, tint_color, language, hotkeys_enable_option,
  notification_speed, exclude_metadata, ...) plus per-feature sections
  (record/replay/stream/screenshot configs incl. video_codec, fps, bitrate,
  save_directory, audio tracks, stream keys for twitch/youtube/rumble/kick/
  custom). Defaults are built-in.
* Translations: custom format, `translations/<locale>.txt`, template in
  `translations/template.txt`; shipped in the resources dir.

### 4.3 Platform-specific UI components (the port surface)

| Component | Linux implementation | Windows replacement |
|---|---|---|
| Windowing/input (mgl) | X11 + Wayland, EGL/GLX, pango fonts | Win32 window + OpenGL (WGL) backend; replace pango with DirectWrite/freetype |
| Global hotkeys | `gsr-global-hotkeys` (setuid helper; evdev grab + uinput virtual keyboard); joystick via /dev/input | `RegisterHotKey` / `RegisterRawInputDevices` in-process; XInput for controller |
| Cursor tracker (focused monitor) | X11 + DRM + Hyprland/Niri/Sway compositor-specific | Win32 cursor position + `MonitorFromPoint` / DXGI output enum |
| Region selector | X11 + Wayland overlay windows | Win32 transparent fullscreen overlay window |
| Desktop environment (game name detection) | X11 window titles; GNOME/KDE extensions/helpers (dbus, JS) | Win32 `GetForegroundWindow` + `GetWindowText` + process name; no shell extensions |
| Clipboard (screenshot "save to clipboard") | X11/Wayland clipboard | Win32 clipboard APIs |
| Notification | separate `gsr-notify` process | ported `gsr-notify` with Win32 backend |
| Startup | `~/.config/autostart/*.desktop` (XDG autostart) | Startup-folder shortcut / HKCU Run |
| Audio for UI sounds | libpulse-simple | Windows sound APIs / PlaySound |
| LED indicator (keyboard LED) | evdev LED ioctls via helper | Scroll-Lock LED trick or drop (documented) |
| RPC socket | abstract Unix socket "gsr-ui" | named pipe `\\.\pipe\gsr-ui` (same newline commands) |
| gsr-ui-cli | connects to the socket | same, named-pipe transport |

### 4.4 UI tools (separate executables)

* `gsr-global-hotkeys` — setuid C helper: grabs all keyboards (evdev),
  creates a virtual keyboard (`gsr-ui virtual keyboard`, uinput), forwards
  hotkey combos, handles joysticks, drives keyboard LEDs. Replaced on
  Windows by in-process `RegisterHotKey` (+ XInput).
* `gsr-ui-cli` — sends newline-terminated commands to the `gsr-ui` socket.
  Kept as a real binary on Windows (named-pipe client).
* `gsr-wayland-bridge`, `gsr-kwin-helper` (dbus JS), `gsr-gnome-helper`
  (extension), `gsr-game-tracker` — Wayland/desktop-shell glue; not needed on
  Windows (replaced by native Win32 equivalents where behavior matters).
* `gsr-ui` main binary also requires `gpu-screen-recorder` (engine) and
  `gsr-notify` at runtime.

## 5. GTK frontend (deprecated upstream)

* GTK3 + `ayatana-appindicator` tray icon (idle/recording/paused icons),
  settings window, X11 global shortcuts (`global_shortcuts.c`), spawns the
  engine and shows notifications.
* Upstream explicitly says this project is **no longer developed** and will be
  removed once the UI can run as a regular window. The Windows port should
  therefore treat `gpu-screen-recorder-gtk` as **not required**; the
  ShadowPlay-style UI is the primary interface. A Windows tray/startup story
  lives in the ported UI (settings → start on startup). Decision documented in
  `docs/windows-port-parity.md`.

## 6. Notification app (gsr-notify)

* C++17 + mglpp; a small always-on-top overlay window with icon + text,
  positioned on the cursor's monitor (X11) or focused monitor (Wayland).
  `--text`, `--timeout`, `--icon-color`, `--icon`, `--bg-color` args.
  Spawned by gsr-ui and by the gtk frontend.
* Ports as its own binary `gsr-notification.exe` with the same CLI and a Win32
  mgl backend (or a small Win32 renderer). See parity matrix.

## 7. Linux-specific dependency inventory

| Dependency | Used by | Windows replacement |
|---|---|---|
| X11 (Xlib, Xcomposite, Xrandr, Xfixes, Xdamage, Xext, Xi, Xcursor, Xrender) | engine (capture, window, cursor, damage), UI (mgl, cursor tracker, region selector, clipboard), gtk | Win32 + Windows Graphics Capture + DXGI |
| Wayland (client, egl, cursor, xkbcommon) + custom protocols | engine (portal, wayland window), UI (mgl, bridges), notification | n/a (removed) |
| DRM/KMS (libdrm) | engine kms capture, gsr-kms-server, UI cursor tracker | DXGI output enumeration |
| PipeWire + SPA | engine portal + app audio | WASAPI (+ Windows.Graphics.Capture for portal-equivalent capture) |
| PulseAudio | engine sound.c, UI sounds | WASAPI loopback/capture |
| libva / vaapi | engine vaapi encoder + codec query | n/a on Windows (AMD→AMF, Intel→QSV, NVIDIA→NVENC via FFmpeg) |
| libcap (setcap) | gsr-kms-server / gsr-global-hotkeys privilege helpers | n/a (no root model on Windows; WGC needs no elevation) |
| systemd | user service for replay-at-startup | Startup folder / HKCU Run (least invasive) |
| D-Bus | portal, kde/gnome helpers, notifications | n/a (Windows notifications / Win32) |
| evdev/uinput | gsr-global-hotkeys | RegisterHotKey / Raw Input |
| glibc/POSIX (signals, unix sockets, clock_gettime, unistd) | everywhere | Win32 equivalents (named events, named pipes, QPC, CRT) |
| GL/EGL/GLX (libglvnd) | engine render pipeline, mgl | WGL/EGL on Windows (or D3D11 for the engine encode path) |
| pangoft2 | mgl text | DirectWrite or freetype-based font atlas |

## 8. What can remain unchanged

* Everything under `src/recorder/` except `audio_input.c`'s device-name
  plumbing and `capture_setup.c`'s X11-specific bits (both small, isolated).
* `src/encoder/` (packet fan-out, replay buffer integration) — FFmpeg only.
* `src/replay_buffer/` — fully portable.
* `src/cli/commands.c` (info format), `src/args_parser.c` (option table),
  `src/json.c`, `src/log.c`, `src/image_writer.c`, `src/plugins.c` (with a
  Windows plugin-loading shim), `src/utils.c` (with path/fs shims),
  `src/ffmpeg_utils.c`, `src/recorder/recording_clock.c` (with a QPC clock
  shim), `src/library_loader.c` (already dynamic — `dlopen`→`LoadLibrary`).
* The entire FFmpeg encode/mux/stream pipeline.
* The UI's widget toolkit (mglpp) and all `src/gui/*` widgets.
* The UI's Overlay pages, settings logic, translations, config schema,
  themes, assets.

## 9. What needs a Windows implementation

* `src/capture/*` → `WindowsGraphicsCaptureBackend` (primary) +
  `DXGIDesktopDuplicationBackend` (fallback) + window capture via WGC item
  enumeration. Webcam via Media Foundation / DirectShow (optional).
* `src/encoder/video/vaapi.c` → AMF (AMD) / QSV (Intel) FFmpeg encoders;
  `nvenc.c` stays (FFmpeg `h264_nvenc` etc. work on Windows with d3d11va
  frames); `vulkan.c` → dropped or kept optional (decision in architecture
  doc); `software.c` stays.
* `src/sound.c` → WASAPI (loopback for `default_output`, capture for
  `default_input`, per-device, follow-system-default via `ERole`).
* `src/pipewire_audio.c` (app audio) → WASAPI session capture
  (`IAudioSessionManager2`) or stereo-mix approximation (documented
  limitation if exact per-app capture is impossible).
* `src/egl.c` + render pipeline → two options (architecture doc):
  (a) D3D11-native pipeline: WGC/DXGI → D3D11 texture → FFmpeg d3d11va
  hwframe → nvenc/amf/qsv; plugins re-implemented for D3D11; or
  (b) GL-on-Windows pipeline mirroring upstream (EGL/WGL): keep
  color_conversion.c/shader.c/plugins.c untouched.
* `src/window/*`, `src/cursor.c`, `src/damage.c` → Win32 + DXGI equivalents
  (monitor enumeration, focused window, cursor capture via WGC cursor API).
* `src/cli/ipc.c`, `tools/gsr-cli` → named-pipe transport, same JSON protocol.
* `src/cli/main.c` signals → named events + IPC-first design (see
  architecture doc §7).
* `kms/*`, `src/capture/portal.c`, `src/pipewire_video.c`, `src/dbus.c`,
  `src/wayland_host_bridge.c`, `src/kde_night_light.c`, `protocol/` → removed
  (replaced by Windows equivalents).
* UI: mgl Win32 backend, `GlobalHotkeys*`, `CursorTracker*`,
  `RegionSelector*`, `DesktopEnvironment*`, `Clipboard*`, `WaylandHostBridge`,
  `Hotplug`, `AudioPlayer`, `LedIndicator`, `window_texture.c`, the six helper
  tools (mostly dropped; `gsr-ui-cli` and hotkey handling kept).
* Notification: windowing → Win32 (via the same mgl backend or a small
  dedicated renderer).
* Installer + portable ZIP (new, Windows-only).
* systemd/X11-autostart startup → Windows startup folder / registry.

## 10. Key behavioral contracts to preserve

1. `--info` / `--list-*` stable `key|value` line format (UI parses it).
2. CLI option names and semantics from `gpu-screen-recorder.1`.
3. IPC JSON protocol (requests/replies, deferred replies for save operations,
   `gsr-cli` exit codes 0/1 and `running`/`not running` status).
4. Saved-file naming: `Replay_YYYY-MM-DD_HH-MM-SS.<ext>`, `Video_...`,
   `Screenshot_...` (verify exact templates in `replay_save.c` /
   `capture_setup.c` when implementing) + `-df` date folders.
5. Replay semantics: continuous encoded buffer; save N seconds / full;
   `-restart-replay-on-save`; RAM/disk storage; stdout reports the file path.
6. Recording while replaying/streaming (`-ro`, multiple recording
   destinations).
7. `-sc` script invocation with `(filepath, regular|replay|screenshot)`.
8. UI: Alt+Z overlay, front page actions, settings structure, hotkey config,
   `config_ui` schema, translations, notification behavior.
9. stdout = machine-readable (saved paths); stderr = logs.
10. Exit codes: distinct codes for distinct errors (per upstream TODO, partly
    implemented via `gsr_error_to_exit_code`).

## 11. Upstream TODO items relevant to the port

(From the engine and UI TODO files — these inform Windows-specific work and
should not be silently dropped.)

* Replay/recording save failures sometimes corrupt long mp4s (mitigate with
  `+faststart` / proper trailer handling; investigate on Windows).
* Support 2-pass, better quality scaling (lanczos), b-frames, ROI.
* VFR matching / damage tracking — Windows Graphics Capture delivers frames
  on its own cadence; damage detection needs a WGC-specific approach.
* Explicit sync and device-loss recovery (recreate capture on display
  mode/device change).
* HDR: capture HDR metadata via DXGI and write HEVC/AV1 HDR correctly;
  document tone mapping path for SDR outputs.
* `gsr-ui` systray support, recording timer, video trimming, webcam overlay.

## 12. Analysis conclusion

The upstream project is **unusually well structured for porting**: plain C
engine, vtable seams at every platform boundary, FFmpeg for all media
processing, a self-contained custom UI stack with a small platform layer
(mgl), and no framework lock-in. The Windows port is a *platform-backend
replacement project*, not a rewrite:

* ~70–80 % of engine code is expected to compile on Windows with only small
  portability shims (time, paths, threads, dynamic loading).
* The remaining 20–30 % is concentrated in ~15 files (capture backends,
  audio backends, IPC transport, egl/windowing, main.c signals).
* The UI requires a new mgl platform backend (Win32) and replacement of ~6
  small platform modules; all widgets, pages, settings, translations and
  assets carry over unchanged.
* The engine's IPC JSON protocol, `--info` format, CLI surface, replay
  semantics, file naming and stdout contract are the compatibility contract
  that makes the ported UI work against the ported engine with minimal
  UI-side changes.

See `docs/architecture.md` for the Windows replacement strategy,
`docs/implementation-roadmap.md` for the phase plan, and
`docs/windows-port-parity.md` for the feature-by-feature status matrix.
