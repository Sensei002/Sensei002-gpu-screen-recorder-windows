# Building on Windows

Everything here is **executed by the GitHub Actions pipeline**
(`.github/workflows/windows-release.yml`) — you do not need a local build
environment. This document is the recipe so that the build is reproducible
locally and in CI from the same commands.

**Toolchain decision (Phase 2):** CMake + Ninja + **MinGW-w64 (MSYS2 MINGW64)**.

The upstream engine is GNU C11 (`gnu11`, `__attribute__`, `-Wshadow`) so GCC
is the natural compiler. MSVC would force invasive changes to shared upstream
code; MinGW-w64 compiles the upstream sources with only a small forced-include
shim. Upstream's Meson build is untouched (it remains the Linux build);
the Windows port is a separate CMake build tree.

## Prerequisites (MSYS2 MINGW64)

Install MSYS2 from <https://www.msys2.org/>, then in a **MINGW64** shell:

```sh
pacman -S --needed --noconfirm \
  mingw-w64-x86_64-gcc \
  mingw-w64-x86_64-binutils \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-nasm \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-make \
  mingw-w64-x86_64-python \
  mingw-w64-x86_64-curl \
  make \
  patch
```

(`nasm`, `make`, `patch`, `python`, `curl` are needed by the FFmpeg
provisioning script; `cmake`/`ninja`/`gcc`/`pkgconf` for the engine build.)

## 1. Provision FFmpeg (from source, pinned, verified)

```sh
./scripts/build-ffmpeg-windows.sh
```

This mirrors the upstream recipe (`extra/build_ffmpeg.sh` + the
`subprojects/*.wrap` pins of gpu-screen-recorder r1467) exactly:

| Library            | Version / revision            | Why                                   |
|--------------------|-------------------------------|---------------------------------------|
| FFmpeg             | 9.0                           | engine's media core (pinned upstream) |
| x264               | b35605ace3ddf7c1a5d67a2eb553f034aef41d55 | software H.264 (pinned upstream) |
| opus               | 1.6.1                         | audio codec (pinned upstream)         |
| mbedtls            | 3.6.7                         | TLS for streaming (pinned upstream)   |
| srt                | 1.5.6                         | SRT streaming (pinned upstream)       |
| nv-codec-headers   | n13.0.19.0                    | NVENC API headers (pinned upstream)   |

- Downloads are verified against the **same sha256 hashes** as upstream's
  `.wrap` files.
- The **two upstream FFmpeg patches** are applied verbatim
  (`scripts/patches/`):
  - `ffmpeg-nvenc-runtime-api-version.patch` — runtime NVENC API negotiation:
    one binary works from driver 471.41 (Kepler, GTX 9xx era) through current
    drivers. Critical for the brief's older-GPU requirement.
  - `ffmpeg-mbedtls-default-ca-certs.patch` — default CA store lookup for
    HTTPS streaming.
- Component list matches upstream's `--disable-everything` recipe (same
  encoders/muxers/protocols/filters/bsf) **minus the Linux-only backends**
  (no `vaapi`, no `vulkan`). NVENC (`h264_nvenc`, `hevc_nvenc`, `av1_nvenc`)
  is enabled. LTO static build, same as upstream.
- The build is **stamp-based and idempotent**: re-running with unchanged
  sources/compiler/args is a no-op, so CI caches the whole
  `build/ffmpeg-{prefix,sources,libs}` tree.

Result: `build/ffmpeg-prefix/lib/pkgconfig/*.pc` (static libs).

## 2. Configure and build the engine core

```sh
export PKG_CONFIG_PATH="$(cygpath -m "$PWD/build/ffmpeg-prefix/lib/pkgconfig");$(cygpath -m /mingw64/lib/pkgconfig)"
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPKG_CONFIG_EXECUTABLE="$(cygpath -m "$(command -v pkgconf.exe)")"
cmake --build build/cmake --parallel
```

Notes:

- Two different pkg-config worlds exist in MSYS2 (both matter):
  - The **FFmpeg provisioning script** runs `pkg-config` (MSYS2's wrapper
    that converts MSYS paths for the native pkgconf), so it exports the
    canonical **MSYS-style** `PKG_CONFIG_PATH` (`/path/lib/pkgconfig` joined
    with `:`).
  - **CMake** resolves the native `pkgconf.exe` (it cannot execute the shell
    wrapper), so the configure step passes it explicitly and gives it
    **Windows-style paths joined with `;`** (`cygpath -m`).
- The build compiles the *portable engine core* (`gsr_core`):
  16 upstream sources + the Windows portability shims. Capture/audio/IPC/
  windowing backends arrive in later phases behind the abstractions in
  `docs/architecture.md`.
- A portability shim (`platform/windows/gsr_win32_compat.h`) is
  force-included into every translation unit; it supplies only what the
  MinGW-w64 runtime lacks (`dlopen` family on LoadLibrary, `dirname`/
  `basename` + the `<libgen.h>`/`<dlfcn.h>` shims, `PATH_MAX`, `ssize_t`,
  `S_IS*`, clock fallback). `tests/compat-probe` reports which shims are
  active vs. natively provided.

## 3. Run the tests

```sh
ctest --test-dir build/cmake --output-on-failure
# or directly:
./build/cmake/ci-smoke.exe
./build/cmake/compat-probe.exe
./build/cmake/gsr-core-test.exe
```

`gsr-core-test` covers: CLI parsing (options, errors, `--version`/`--info`/
`--list-monitors` dispatch), audio input parsing (`-a` grammar, track names,
device validation), the recording clock (pause semantics), the replay buffers
(RAM ring wrap-around, keyframe find, clone, disk data read-back + cleanup),
JSON helpers, and the portable utils (including the LLP64 `strtoll` fix for
`gsr_string_to_int64`).

The three test executables link `-static-libgcc -static-libwinpthread`, so
they run on any Windows system without MSYS2 — that is how the workflow's
`test` job re-runs them from the build artifact.

## What CI does (and what it can't do)

See `docs/implementation-roadmap.md` ("CI hardware reality") and the workflow
file. GitHub-hosted runners have no guaranteed physical GPU: the *real*
capture/encoder backends are compiled (never a crippled CI build), all pure
logic is unit-tested, and hardware validation is documented separately
(`docs/troubleshooting-windows.md`, to be written).
