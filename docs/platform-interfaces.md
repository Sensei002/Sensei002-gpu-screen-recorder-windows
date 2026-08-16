# Platform Interfaces (Phase 3 deliverable)

**Phase 3 deliverable.** The port's own platform layer lives in
`platform/include/` (interfaces) with implementations in
`platform/windows/`. It exists because the upstream engine and UI are
*portable* where they sit behind their own vtables (`gsr_capture`,
`gsr_video_encoder`, `sound_device_*`) — but the Windows port needs
additional, port-owned abstractions the upstream code never had (Windows
filename rules, named-pipe IPC, monitor enumeration, hotkeys, startup,
config storage). The rule from the brief holds: **preserve everything
platform-independent; replace only what is platform-specific.**

Each interface below is mapped to its upstream callers (what it replaces or
serves), its Windows implementation, and the phase that implements it.

## Interface map

| Interface (`platform/include/`) | Purpose | Upstream callers / replaced code | Windows implementation | Phase |
|---|---|---|---|---|
| `gsr_time.h` | ns-resolution monotonic clock, wall-clock ms | engine `utils.h` clock (seconds only), UI timers | `gsr_platform_win32.c` (QPC) | 3 ✅ |
| `filesystem.h` | filename sanitization, path join, UTF-8↔UTF-16, Videos dir, save-filepath naming | `recorder/muxer.c` `gsr_create_new_recording_filepath_from_timestamp` (delegated to, not replaced), UI save-dir settings | `gsr_filesystem_win32.c` | 3 ✅ |
| `display.h` | monitor model + `--list-monitors`/`--info` output format | `cli/commands.c` (`--list-monitors`, `--info` key\|value lines), UI monitor picker | `gsr_display_win32.c` (format + DXGI/GetMonitorInfoW enumeration, Phase 4); `gsr_platform_display_find_monitor` resolves `-w` monitor names for the Phase 5/6 capture backends | 3 ✅ format, 4 ✅ enumeration |
| `capture.h` | capture backend identity + auto-selection (WGC / DXGI) + the WGC backend's C API and pure logic | engine `gsr_capture` vtable (kept unchanged) — this header is the *selection* layer + the WGC backend's extern "C" surface | `gsr_platform_win32.c` (select/name); `gsr_capture_wgc.cpp` (WGC backend, C++/WinRT, extern "C" API) + `gsr_capture_wgc_helpers.c` (pure logic) | 3 ✅ logic, 5 ✅ WGC backend + self-test, 6 probes |
| `audio.h` | WASAPI endpoint model + `--list-audio-devices` line format | engine `sound.h` `sound_device_*` (kept unchanged) — this header is the *enumeration* layer | `gsr_platform_win32.c` (line format) now; enumeration in `audio_wasapi.c` | 3 ✅ format, 8 enumeration |
| `hotkeys.h` | global hotkey registration | UI `GlobalHotkeys/*` (X11/joystick) | `hotkeys.c` (RegisterHotKey) | 11 |
| `ipc.h` | IPC protocol codec + named-pipe transport | engine `cli/ipc.c` (Unix socket), `cli/commands.c` request handling, `tools/gsr-cli` | `gsr_ipc_protocol.c` (codec, byte-identical wire format) now; pipe transport in `ipc.c` | 3 ✅ codec, 11 transport |
| `process.h` | child-process spawn with stdout capture | UI engine spawn (parses stdout), `-sc` scripts, `exec_program`-style helpers | `process.c` (CreateProcess) | 12 |
| `notifications.h` | notification display | UI notifications, `gsr-notify` binary | `gsr-notification.exe` | 11 |
| `startup.h` | autostart at logon | UI XDG autostart | `startup.c` (HKCU Run key) | 12 |
| `thread.h` | thread naming (threading itself = winpthreads, no shim needed) | engine pthreads / UI std::thread (unchanged) | `gsr_platform_win32.c` (SetThreadDescription) | 3 ✅ |
| `config.h` | schema-driven config in upstream's config_ui key=value format | UI `config_ui` file (custom key=value-line format, upstream-analysis §4.2) | `gsr_config_win32.c` (machinery + provisional schema) | 3 ✅ machinery, 10 full schema |
| `codec_caps.h` | codec capability decision logic (`-k` options, HDR gating, `-encoder` fallback) | engine `codec_query/` probe data (`gsr_supported_video_codecs` struct — kept unchanged), `--info` codec line | `gsr_codec_caps_win32.c` | 3 ✅ logic, 7 probe |

## How the upstream caller map (§9 of upstream-analysis) lands

| Upstream surface | Windows plan |
|---|---|
| `src/capture/*` (portal, xcomposite, ximage, kms, nvfbc, v4l2) | `gsr_capture` vtable unchanged; backends are `capture.h`-selected WGC (Phase 5, shipped: `gsr_capture_wgc.cpp`) / DXGI duplication (Phase 6) |
| `src/codec_query/vaapi.c`, `vulkan.c` | dropped (NVIDIA-only scope); `codec_caps.h` decides from the `gsr_supported_video_codecs` probe |
| `src/sound.c`, `src/pipewire_audio.c` | `sound.h` interface unchanged; WASAPI backend (Phase 8) + `audio.h` enumeration |
| `src/cli/ipc.c`, `tools/gsr-cli` | `ipc.h` codec (byte-identical JSON) + named-pipe transport (Phase 11) |
| `src/cli/main.c` signals | named events + IPC-first control (architecture §7, Phase 11) |
| `src/window/*`, `src/cursor.c`, `src/damage.c` | Win32/DXGI equivalents (Phases 4–6) |
| UI `GlobalHotkeys/CursorTracker/RegionSelector/DesktopEnvironment/Clipboard/AudioPlayer` | `hotkeys.h` + Win32 equivalents (Phases 10–11) |
| UI `config_ui` | `config.h` (same key=value format) |
| UI engine spawn / `-sc` scripts | `process.h` (Phase 12) |
| XDG autostart | `startup.h` (Phase 12) |
| `gsr-notify` | `gsr-notification.exe` (Phase 11) |

## Implementation notes

- **Wire-format parity is tested, not assumed.** The IPC codec
  (`gsr_ipc_protocol.c`) reproduces upstream's exact request/reply JSON
  templates and deferred-request state machine; `tests/platform-test`
  asserts the produced bytes against the upstream format strings.
- **Naming parity is tested against upstream code.** `filesystem.h`'s
  save-filepath builder delegates to the *real*
  `gsr_create_new_recording_filepath_from_timestamp` (recorder/muxer.c,
  compiled into `gsr_core`), so `Replay_YYYY-MM-DD_HH-MM-SS.mp4` and `-df`
  date folders are exercised as upstream implements them.
- **Provisional schema.** `config.h`'s UI schema covers the keys documented
  in upstream-analysis §4.2 with provisional defaults; Phase 10 replaces it
  with the complete upstream `config_ui` option set. The machinery
  (round-trip, validation, forward-compatible unknown keys) is what Phase 3
  tests.
- **Codec decisions are pure logic.** `codec_caps.h` turns the Phase 7 NVENC
  probe's `gsr_supported_video_codecs` into UI/`--info` decisions; every
  combination is unit-tested without hardware (brief §64).
- **Header-only for later phases.** `hotkeys.h`, `process.h`,
  `notifications.h`, `startup.h` declare the interfaces now; their
  implementations land with the phases that use them.
