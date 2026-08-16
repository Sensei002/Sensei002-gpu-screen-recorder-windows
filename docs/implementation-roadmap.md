# Implementation Roadmap

**Phase 1 deliverable.** The port proceeds in phases exactly as the project
brief defines. Every phase ends with a **CI green build/test in GitHub
Actions** — nothing is validated on a local machine. This document is the
living plan; each phase updates it with results.

**Phase numbering:** the brief's 20-phase list minus the two dropped phases
(AMD AMF, Intel QSV — NVIDIA-only scope decision, see the note after Phase 7);
all later phases are renumbered accordingly.

**Development model (from the brief):** `edit → commit → push → GitHub Actions
(build → test → package → validate → release)`. All compilation, testing,
packaging and releasing happens in CI. The user's machine is for editing and
reviewing only.

---

## Phase 1 — Upstream analysis ✅ (complete)

Deliverables (all in this repository):

- [x] `docs/upstream-analysis.md` — full analysis of engine, UI, GTK frontend,
      notification app, dependencies, Linux-specific surface, port seams.
- [x] `docs/architecture.md` — Windows port architecture.
- [x] `docs/implementation-roadmap.md` — this document.
- [x] `docs/licensing.md`, `docs/upstream-porting-notes.md`.
- [x] `docs/windows-port-parity.md` — feature matrix (draft).
- [x] `README.md`.
- [x] Repo scaffolding: `platform/` layout, `.github/workflows/windows-release.yml`
      skeleton with a toolchain smoke test, `tests/ci-smoke/`.

Upstream revisions analyzed: engine 6.0.0 (r1467/d31b698), UI 1.13.5
(r720/edfc70d), GTK 5.8.0 (r513/dade88d), notification 1.3.4 (r110/8db3818).

---

## Phase 2 — Windows build infrastructure ✅ (complete)

**Goal:** a reproducible Windows x64 build of *something real* in CI: vendored
engine sources compiling with the chosen toolchain, plus FFmpeg provisioning.

**Outcome — toolchain decision: CMake + Ninja + MinGW-w64 (MSYS2 MINGW64).**
Upstream is GNU C11 (gnu11, `__attribute__((format))`, `-Wshadow`), so GCC is
the natural compiler; MSVC would need invasive changes to shared code. CMake
is the build driver (upstream's Meson stays untouched for the Linux build;
the Windows port is a separate build tree). `windows-release.yml` runs
`msys2/setup-msys2` (MINGW64) and builds everything in CI.

Deliverables:

- [x] **Vendored engine** in `upstream/` (gpu-screen-recorder 6.0.0, r1467)
      — source + headers + `external/` vendored libs, unmodified except two
      documented de-X11 header patches (`egl.h`, `recorder/windowing.h`) that
      keep the X11 types under `#else` on Linux.
- [x] **Portability shims** (`platform/windows/`):
      - `gsr_win32_compat.h/.c` — force-included via CMake `-include`;
        `dlopen/dlsym/dlclose/dlerror` on LoadLibrary/GetProcAddress,
        `clock_gettime` fallback (only when MinGW lacks it — MSYS2 provides
        it natively), `PATH_MAX`/`ssize_t`/`S_IS*`, `dirname`/`basename`
        (Windows separator aware), `<libgen.h>` and `<dlfcn.h>` shims.
      - `gsr_utils_win32.c` — Windows implementation of the *portable*
        `utils.h` subset with behavior identical to upstream (QPC clock,
        RtlGenRandom, directory creation, string/date helpers); includes the
        `gsr_window_get_display_server` placeholder replaced by the real
        windowing backend in Phase 5.
- [x] **FFmpeg provisioning** (`scripts/build-ffmpeg-windows.sh`): builds the
      upstream-pinned stack from source — ffmpeg 9.0, x264, opus 1.6.1,
      mbedtls 3.6.7, srt 1.5.6, nv-codec-headers n13.0.19.0 — with
      sha256-verified downloads, the two upstream ffmpeg patches applied
      verbatim (`scripts/patches/`), LTO static build mirroring upstream
      **except mbedtls** (built without LTO — a Windows/bfd limitation, see
      porting notes §3c), minus Linux-only backends (no vaapi/vulkan).
      Stamp-based + cached in CI.
- [x] **`gsr_core` static library** (CMake): 16 portable upstream sources
      (args_parser, json, log, utils, encoder, replay buffer RAM+disk,
      audio_input, audio_codec, video_codec, recording_clock, audio_capture,
      capture_source, ffmpeg_utils, library_loader, defs) + the Windows
      shims, linked against the static FFmpeg via pkg-config.
- [x] **Tests** (`tests/`):
      - `gsr-core-test` — CLI parsing (`args_parser` incl. `--version`/
        `--info`/`--list-monitors` dispatch, error cases), audio input
        parsing (`-a` grammar, track names, device validation),
        recording clock (pause semantics), replay buffer RAM + disk
        (wrap-around, keyframe find, clone, data read-back, cleanup),
        JSON helpers, portable utils (incl. the LLP64 `strtoll` int64 fix).
      - `compat-probe` — CI diagnostics: which shims are active vs. natively
        provided by MinGW-w64 (clock_gettime, PATH_MAX, ssize_t, S_IS*,
        dlopen, dirname, strcasecmp).
      - `ci-smoke` — Phase 1 toolchain smoke test.
      All three link `-static-libgcc` + winpthread statically
      (`-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic`; GCC 16 dropped
      `-static-libwinpthread`) so the `test` job re-runs them on a plain
      `windows-latest` runner without MSYS2.

**CI deliverables:** build job green (toolchain → FFmpeg → gsr_core → tests);
test job green (re-runs the static binaries); FFmpeg cached. Full recipe in
`docs/build-windows.md`.

**Validated green in CI (run 13):** FFmpeg stack from source, engine-core
build, 140 unit-test checks, and the plain-runner re-run all pass. The
hard-won Windows build knowledge from the 13-run grind (pkg-config path
styles, `.pc` normalization, mbedtls LTO exception, GCC 16's dropped
`-static-libwinpthread`, the recording clock's retroactive pause semantics)
is documented in `docs/upstream-porting-notes.md` §3c.

**Windows-specific behavior differences found in this phase** (documented in
docs/upstream-porting-notes.md): `long` is 32-bit on Windows (LLP64) so
`gsr_string_to_int64` uses `strtoll`; `/dev/stdout` output and
`file_is_pipe_or_char_device` have no Windows equivalent yet (piped output
lands with the Phase 5+ capture/IPC work).

---

## Phase 3 — Platform abstraction + test harness

**Goal:** the port's own platform layer takes shape, with a real automated
test harness so every later phase can be verified in CI without hardware.

Tasks:

1. Define `platform/include/` interfaces (headers only):
   `capture.h`, `audio.h`, `hotkeys.h`, `ipc.h`, `filesystem.h`,
   `process.h`, `display.h`, `notifications.h`, `startup.h`, `gsr_time.h`,
   `thread.h`.
2. Map every upstream caller to these interfaces (from upstream-analysis §9).
3. Test harness (`tests/`):
   - CLI parsing tests (options, `--info` format golden files).
   - Config serialization tests (config_ui schema round-trip).
   - Replay buffer tests (RAM + disk: append/trim/keyframe-find/clone).
   - Recording-clock/timestamp tests (QPC shim, pause semantics).
   - JSON/IPC protocol tests (request/reply framing, deferred replies,
     `gsr-cli` command→JSON mapping).
   - Filename/path tests (Windows-invalid chars, `-df` date folders, Unicode).
   - Encoder capability logic tests (codec table vs. capability flags —
     pure logic, no GPU needed).
   - Use a lightweight framework or plain asserts + a runner script; must run
     headless on `windows-latest`.

**CI deliverables:** `test` job green on every push; coverage report artifact.

**Status: COMPLETE.** All tasks delivered and validated green in CI:

- `platform/include/` interface headers (13 total — the 11 above plus
  `config.h` and `codec_caps.h`), documented with a caller map in
  `docs/platform-interfaces.md`.
- `platform/windows/` backends: `gsr_filesystem_win32.c`,
  `gsr_ipc_protocol.c`, `gsr_config_win32.c`, `gsr_codec_caps_win32.c`,
  `gsr_display_win32.c`, `gsr_platform_win32.c`; real upstream `muxer.c`
  added to `gsr_core` (with `gsr_capture_set_hdr_metadata` stub).
- `tests/platform-test/main.c` (filesystem, IPC codec, config round-trip,
  codec caps, display/info format) plus the existing ci-smoke and
  gsr-core-test suites; all green via ctest and on the plain runner.
- Coverage job: `GSR_ENABLE_COVERAGE` builds with `--coverage`;
  gcovr runs under the mingw64 python with a Windows-style `--root`,
  producing a real report artifact — currently **42.3% lines (1382/3270)**.
- Lessons from the grind (header shadowing, X11/DRM stub headers,
  `-lpthread`/`-lstdc++`/`-lgcc_s`/`-latomic` static linkage,
  MSYS-vs-mingw64 python path styles) are in
  `docs/upstream-porting-notes.md` §3c/§3d.

---

## Phase 4 — Monitor / display enumeration

**Goal:** `--list-monitors`, `--list-capture-options`, and the UI's monitor
picker work on Windows.

Tasks:

1. DXGI + `GetMonitorInfoW` enumeration: name, position, size, refresh rate,
   rotation, DPI, HDR state, adapter/vendor.
2. Implement `platform/windows/display.c` + wire into
   `src/cli/commands.c` monitor listing (same `name|WxH` output).
3. Map upstream monitor names (e.g. `DP-1`) to Windows monitor IDs in the
   capture source parser (accept both `\\.\DISPLAY1` and friendly aliases).
4. Tests: monitor-list golden format; name mapping; rotation handling.

**CI deliverables:** `--list-monitors`-style output verified headless
(runner has a virtual display; CI may need to tolerate 1+ virtual monitors).

**Status: COMPLETE.** DXGI + `GetMonitorInfoW` enumeration landed in
`platform/windows/gsr_display_win32.c` (the format helpers from Phase 3
live in the same file):

- Per-monitor: Win32 device name + EDID friendly name, virtual-screen
  position, **native** panel resolution (largest mode in the mode list),
  refresh rate, rotation, per-monitor DPI (`GetDpiForMonitor`),
  primary flag, HDR10 state (`IDXGIOutput6::GetDesc1` color space), and
  adapter vendor/name (`DXGI_ADAPTER_DESC1`).
- The `--list-monitors` line prints the **effective** (post-rotation) size,
  matching upstream's Wayland `output_monitor_info` semantics (native size
  stored, swapped at print time for 90/270).
- `gsr_platform_display_find_monitor` maps a `-w` monitor argument
  (`\\.\DISPLAY1` or a friendly alias, case-insensitive) to a monitor;
  upstream-style DRM names (`DP-1`) have no Windows equivalent and return
  -1 unless a device/friendly name matches. The Phase 5/6 capture backends
  call this to resolve `-w` monitor targets to an `HMONITOR`.
- Tests: deterministic logic tests (name mapping, rotation/effective size,
  vendor ids) plus a headless enumeration smoke test that asserts the
  runner's virtual display yields >= 1 monitor with sane fields and exactly
  one primary.
- CI: `platform-test` exercises all of the above; the `compat` header now
  targets the Windows 10 API level (`WINVER`/`_WIN32_WINNT`/`NTDDI_VERSION`
  = 0x0A00) for `dxgi1_6.h`/`shellscalingapi.h`; `dxgi` + `shcore` joined
  the core's link libraries. DXGI lessons are in
  `docs/upstream-porting-notes.md` §3d.

---

## Phase 5 — Capture: Windows Graphics Capture (primary)

**Goal:** real monitor + window capture via WGC, feeding the encoder pipeline.

**Status: backend + self-test SHIPPED and CI-green; render path + cursor
land in the next capture increment (Phase 5b / Phase 6 work).** See
`docs/upstream-porting-notes.md` §3e for the CI lessons.

Tasks:

1. ✅ D3D11 device + WGC `GraphicsCaptureSession` for monitor items (window
   items use the same `CreateForWindow` interop path — implemented, not yet
   exercised on CI); cursor capture via WGC cursor APIs (pending — the
   `IsCursorCaptureEnabled(false)` opt-out is wired, the default-on path
   needs no code).
2. ✅ Render/encode pipeline decision (architecture §3.3): **Option B — GL
   via ANGLE** (user-approved spike decision): ANGLE's
   `EGL_ANGLE_d3d_texture_client_buffer` imports the WGC D3D11 texture into
   a GL context on the SAME D3D11 device (`EGL_ANGLE_device_d3d`) —
   zero-copy, and the upstream color-conversion pipeline stays unchanged.
   The self-test probes the import on CI where ANGLE is present.
3. ✅ `gsr_capture_wgc.cpp` implements the `gsr_capture` vtable
   (start/tick/should_stop/capture/is_damaged/clear_damage/set_hdr_metadata
   /uses_external_image/destroy); C API in `platform/include/capture.h`;
   pure logic headless-tested in `gsr_capture_wgc_helpers.c`.
4. ✅ Frame pacing + damage semantics: WGC delivers frames as the desktop
   changes; `tick()` drains the pool (TryGetNextFrame, newest frame wins)
   into the recorder's damage/clear/capture model.
5. 🔲 Device loss / resolution change: `GetDeviceRemovedReason` is checked
   in tick (hard stop on removal); session recreation on mode change is
   pending.
6. ✅ CI: full production capture code (WGC/D3D11/C++/WinRT) compiles on the
   runner; pure logic unit-tested headless; `wgc-self-test` binary runs a
   real WGC capture of the primary monitor where a display exists and
   SKIPs (exit 0) where it doesn't; the ANGLE import probe runs
   informationally in the ctest step. Manual validation instructions for
   real hardware: see the self-test's output contract.

**CI deliverables:** full production build includes WGC; `test` job green.

---

## Phase 6 — DXGI Desktop Duplication fallback

Tasks:

1. `gsr_capture_dxgi_duplication.c` implementing the same vtable
   (monitor-only; no window capture — windows fall back to WGC item or are
   rejected with a clear error).
2. Automatic selection: try WGC first, fall back to DXGI on failure
   (documented). `--info` reports the active capture backend.
3. Tests: backend-selection logic (pure), golden `--info` lines.

---

## Phase 7 — NVIDIA NVENC

Tasks:

1. FFmpeg nvenc encode path over d3d11va frames (`h264_nvenc`, `hevc_nvenc`,
   `av1_nvenc`), replacing/alongside upstream's GL+CUDA path.
2. Capability probing (`src/codec_query/` Windows impl): driver version,
   codec/profile/resolution limits per GPU generation (GTX 9xx/10xx, RTX
   20xx/30xx/40xx/50xx); hide unsupported options (e.g. AV1 on pre-RTX).
3. Map upstream `-tune`, `-keyint`, `-bm qp/vbr/cbr`, `-q`, `-cr`, presets,
   `-ffmpeg-video-opts` onto nvenc options (same semantics).
4. Tests: capability-table logic; option mapping; `--info` output.

**CI note:** NVENC hardware is not guaranteed on the runner; capability logic
is unit-tested, and a real-GPU validation checklist is documented
(`docs/troubleshooting-windows.md`).

---

> **Scope decision (user, 2026-08-15): NVIDIA-only port.** The brief's
> phases 8 (AMD AMF) and 9 (Intel QSV) are intentionally dropped; later
> phases are renumbered (so WASAPI is Phase 8 here). Hardware encoding is
> NVIDIA NVENC only; non-NVIDIA machines fall back to software encoding
> (`-encoder cpu`, libx264 — upstream behavior, brief §57: never fake
> support). Capture stays GPU-agnostic (WGC/DXGI are adapter-neutral); only
> *encoding* is NVIDIA-only. Recorded in `docs/windows-port-parity.md` and
> `docs/architecture.md`.

---

## Phase 8 — WASAPI audio

Tasks:

1. `platform/windows/audio_wasapi.c` implementing `sound_device_*`:
   loopback (default output), capture (default input), device-by-name,
   default-device change tracking, latency reporting.
2. Per-app audio spike: WASAPI session enumeration for `-a app:name`;
   feasibility decision + documented fallback.
3. A/V sync validation harness (timestamps from QPC-based clock; no
   frame-count assumptions): tests for chunk delivery, resampling
   (libswresample), codec delay offsets (opus/aac).
4. `--list-audio-devices` / `--list-application-audio` Windows output.
5. Device-change error handling (upstream TODO parity).

---

## Phase 9 — Replay

Tasks: replay is already portable; verify end-to-end on Windows:
RAM + disk buffers, save full/N-seconds, `-restart-replay-on-save`, `-df`,
`-replay-storage`, crash-safe disk buffer cleanup. Tests: buffer trim,
keyframe boundaries, save-path naming, temp-file cleanup on crash (simulated).

---

## Phase 10 — UI

Tasks:

1. mgl Win32 backend (window, input, WGL/EGL context) — the big item.
2. Replace UI platform modules (architecture §4.2): GlobalHotkeys
   (RegisterHotKey), CursorTracker, RegionSelector, DesktopEnvironment,
   Clipboard, AudioPlayer; drop WaylandHostBridge/Hotplug/LedIndicator.
3. `Rpc.cpp` → named pipe; `gsr-ui-cli.exe`; single-instance semantics.
4. Overlay behavior on Windows (fullscreen/topmost/focus/per-monitor).
5. Startup option (HKCU Run / Startup folder); tray-icon decision.
6. Translations + assets verification; config_ui path mapping.
7. CI: full UI build + headless smoke test (window creation offscreen where
   possible); screenshot golden tests of the settings pages (render to
   texture, no real display needed).

---

## Phase 11 — Hotkeys + notifications + IPC integration

Tasks:

1. Engine control events + named events (architecture §7); gsr-cli.exe
   end-to-end against a running engine in CI (spawn engine with
   `-ipc`, run commands, assert replies) — headless-friendly.
2. gsr-notify port: `gsr-notification.exe` with the same CLI; CI smoke test.
3. UI↔engine integration in CI: launch UI (daemon mode) → `gsr-ui-cli
   toggle-show` → assert RPC reply; engine child-process contract tests.
4. Windows notifications only where they don't distort upstream UX (default:
   the ShadowPlay-style overlay notifications via gsr-notification.exe).

---

## Phase 12 — Startup + Windows integration

Tasks: startup option implementation; clean shutdown on session end/logoff;
file associations (optional, documented); `-sc` script execution on Windows
(`cmd /c`/direct exe); environment variable handling parity.

---

## Phase 13 — Installer + portable ZIP

Tasks:

1. Installer tech decision (NSIS vs Inno Setup vs WiX) via CI spike; produce
   `GPU-Screen-Recorder-Windows-x64-Setup.exe`: per-user install, Start Menu
   shortcut, optional desktop shortcut, optional startup, uninstaller,
   config preservation, bundled DLLs.
2. Portable build `GPU-Screen-Recorder-Windows-x64-Portable.zip` from the
   same binaries.
3. Validation in CI: extract zip, check expected executables + DLLs + license,
   run `--help`/`--version`/`--info`.

---

## Phase 14 — GitHub Actions (single workflow, full pipeline)

Tasks: complete `windows-release.yml`:

```
build (checkout → provision deps → configure → compile → upload artifact)
  ↓
test (download artifact → unit/integration tests → fail on error)
  ↓
package (installer + zip + validation → upload release artifacts)
  ↓
release (create GitHub Release → upload EXE + ZIP → publish)
```

Release policy: documented in the workflow header — push to `main` runs
build+test+package; a release is published only when the workflow is
triggered with a version tag (e.g. `v0.1.0-windows`) or a workflow_dispatch
with a version — *never* on every trivial commit (brief §84). Release notes
auto-generated with version, upstream revision, commit SHA, Windows-specific
changes, limitations, hardware notes, attribution.

---

## Phase 15 — Performance

Tasks: measure CPU/GPU overhead with documented methodology
(`docs/performance.md`); compare against OBS/NVIDIA/Game Bar where hardware
permits; verify no CPU readback on the hot path; optimize bounded queues,
replay buffer sizing; publish real measurements only.

---

## Phase 16 — Parity testing

Tasks: walk `docs/windows-port-parity.md` to FULL/PARTIAL per feature; fix
gaps; document every PARTIAL/NOT-POSSIBLE with a reason (brief §57: never fake
support).

---

## Phase 17 — Release packaging

Tasks: final validation gate (build→test→package→release all green),
release notes, installer + zip published. Acceptance checklist from the brief
§94 is the definition of done.

---

## CI hardware reality (brief §64 — honored, not hidden)

* GitHub-hosted Windows runners have a virtual display and (as of writing) no
  guaranteed physical GPU. NVENC/WGC-against-real-desktop,
  multi-monitor, HDR and high-refresh capture **cannot be claimed as CI-tested**.
* Mitigations: (1) the production build always compiles the *real* capture and
  encoder backends (no crippled CI build); (2) all pure logic (capability
  tables, protocol, config, replay, timestamps, option mapping) is unit-tested
  in CI; (3) a `--capture-self-test` mode and documented hardware validation
  checklist cover real-GPU verification; (4) release notes state exactly what
  CI validated vs. what requires hardware validation.

## Phase status

| Phase | Title | Status |
|---|---|---|
| 1 | Upstream analysis | ✅ complete |
| 2 | Windows build infrastructure | ✅ complete (MinGW-w64 chosen, FFmpeg from source, core builds) |
| 3 | Platform abstraction + tests | ⏳ next |
| 4 | Monitor enumeration | pending |
| 5 | Windows Graphics Capture | pending |
| 6 | DXGI fallback | pending |
| 7 | NVIDIA NVENC | pending |
| 8 | WASAPI | pending |
| 9 | Replay | pending |
| 10 | UI | pending |
| 11 | Hotkeys/notifications/IPC | pending |
| 12 | Startup/integration | pending |
| 13 | Installer + portable zip | pending |
| 14 | GitHub Actions full pipeline | pending |
| 15 | Performance | pending |
| 16 | Parity testing | pending |
| 17 | Release packaging | pending |
