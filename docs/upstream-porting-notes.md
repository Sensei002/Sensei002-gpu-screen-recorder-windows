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
  (Phase 10).
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
