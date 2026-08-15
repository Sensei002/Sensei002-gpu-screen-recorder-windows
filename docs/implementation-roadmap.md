# Implementation Roadmap

**Phase 1 deliverable.** The port proceeds in phases exactly as the project
brief defines. Every phase ends with a **CI green build/test in GitHub
Actions** — nothing is validated on a local machine. This document is the
living plan; each phase updates it with results.

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
      verbatim (`scripts/patches/`), LTO static build mirroring upstream,
      minus Linux-only backends (no vaapi/vulkan). Stamp-based + cached in CI.
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
      All three link `-static-libgcc -static-libwinpthread` so the `test` job
      re-runs them on a plain `windows-latest` runner without MSYS2.

**CI deliverables:** build job green (toolchain → FFmpeg → gsr_core → tests);
test job green (re-runs the static binaries); FFmpeg cached. Full recipe in
`docs/build-windows.md`.

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
   `process.h`, `display.h`, `notifications.h`, `startup.h`, `time.h`,
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

---

## Phase 5 — Capture: Windows Graphics Capture (primary)

**Goal:** real monitor + window capture via WGC, feeding the encoder pipeline.

Tasks:

1. D3D11 device + WGC `GraphicsCaptureSession` for monitor items and window
   items; cursor capture via WGC cursor APIs.
2. Render/encode pipeline decision (architecture §3.3):
   - Option A spike: D3D11 texture → FFmpeg d3d11va hwframe → nvenc/amf/qsv.
   - Option B spike: GL-on-Windows (EGL/WGL) with imported textures.
   - Choose, document, and implement the color-conversion path accordingly.
3. `gsr_capture_windows_graphics_capture.c` implements the `gsr_capture`
   vtable (start/capture/tick/should_stop/is_damaged/set_hdr_metadata/...).
4. Frame pacing + damage semantics: WGC delivers frames as the desktop
   changes; map to the recorder's damage/fps model (VFR default stays).
5. Device loss / resolution change: detect and recreate capture session;
   recorder `should_stop` on unrecoverable failure (no crash).
6. CI: compile the full production capture code (WGC/D3D11) on the runner;
   unit-test the pure logic (metadata, rotation, damage flags). Physical
   capture validation is documented as a hardware-limited item (brief §64) —
   provide a `--capture-self-test` mode that CI can run where a display is
   available, and manual validation instructions for real hardware.

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

## Phase 8 — AMD AMF

Tasks: AMF via FFmpeg (`h264_amf`/`hevc_amf`/`av1_amf`) over d3d11va;
capability detection via `AMFInit`; same option mapping; HDR metadata path;
unit tests as Phase 7.

---

## Phase 9 — Intel encoding (QSV)

Tasks: FFmpeg `*_qsv` encoders over d3d11va (`AV_HWDEVICE_TYPE_QSV`);
integrated + Arc detection; same option mapping; unit tests.

---

## Phase 10 — WASAPI audio

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

## Phase 11 — Replay

Tasks: replay is already portable; verify end-to-end on Windows:
RAM + disk buffers, save full/N-seconds, `-restart-replay-on-save`, `-df`,
`-replay-storage`, crash-safe disk buffer cleanup. Tests: buffer trim,
keyframe boundaries, save-path naming, temp-file cleanup on crash (simulated).

---

## Phase 12 — UI

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

## Phase 13 — Hotkeys + notifications + IPC integration

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

## Phase 14 — Startup + Windows integration

Tasks: startup option implementation; clean shutdown on session end/logoff;
file associations (optional, documented); `-sc` script execution on Windows
(`cmd /c`/direct exe); environment variable handling parity.

---

## Phase 15 — (was notifications/startup detail) fold into 13/14.

*(Phase numbering follows the brief's 20-phase list; notification/startup work
lives in Phases 13–14.)*

---

## Phase 16 — Installer + portable ZIP

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

## Phase 17 — GitHub Actions (single workflow, full pipeline)

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

## Phase 18 — Performance

Tasks: measure CPU/GPU overhead with documented methodology
(`docs/performance.md`); compare against OBS/NVIDIA/Game Bar where hardware
permits; verify no CPU readback on the hot path; optimize bounded queues,
replay buffer sizing; publish real measurements only.

---

## Phase 19 — Parity testing

Tasks: walk `docs/windows-port-parity.md` to FULL/PARTIAL per feature; fix
gaps; document every PARTIAL/NOT-POSSIBLE with a reason (brief §57: never fake
support).

---

## Phase 20 — Release packaging

Tasks: final validation gate (build→test→package→release all green),
release notes, installer + zip published. Acceptance checklist from the brief
§94 is the definition of done.

---

## CI hardware reality (brief §64 — honored, not hidden)

* GitHub-hosted Windows runners have a virtual display and (as of writing) no
  guaranteed physical GPU. NVENC/AMF/QSV/WGC-against-real-desktop,
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
| 8 | AMD AMF | pending |
| 9 | Intel encoding | pending |
| 10 | WASAPI | pending |
| 11 | Replay | pending |
| 12 | UI | pending |
| 13 | Hotkeys/notifications/IPC | pending |
| 14 | Startup/integration | pending |
| 15 | — (folded into 13–14) | — |
| 16 | Installer + portable zip | pending |
| 17 | GitHub Actions full pipeline | pending |
| 18 | Performance | pending |
| 19 | Parity testing | pending |
| 20 | Release packaging | pending |
