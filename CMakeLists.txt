# =============================================================================
# GPU Screen Recorder — Windows Port — build system (Phase 2)
#
# Builds the *portable* core of the upstream engine on Windows with
# MinGW-w64 (GCC) + Ninja. Only files that are platform-independent are
# compiled here; platform backends (capture, audio, IPC, windowing) land in
# later phases behind the abstractions documented in docs/architecture.md.
#
# Toolchain: MinGW-w64 (MSYS2 MINGW64 environment), see docs/build-windows.md.
# FFmpeg:   built from source by scripts/build-ffmpeg-windows.sh (mirrors the
#           upstream static-ffmpeg recipe); located via pkg-config.
# =============================================================================
cmake_minimum_required(VERSION 3.25)
# Phase 5: the WGC capture backend (gsr_capture_wgc.cpp) is C++/WinRT.
project(gpu_screen_recorder_windows C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# MSYS2's cppwinrt headers hard-require C++20 (winrt/base.h #errors on
# missing coroutine support), so the backend compiles as C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MINGW)
    message(FATAL_ERROR
        "The Windows port currently requires the MinGW-w64 (GCC) toolchain. "
        "Use the MSYS2 MINGW64 environment (see docs/build-windows.md).")
endif()

# Match upstream compiler flags where meaningful (meson: -Wshadow, warning_level=2).
# -std=gnu11 is scoped to C: CMAKE_CXX_STANDARD 20 provides -std=gnu++20 for
# the Phase 5 C++/WinRT backend (a global -std would fight it).
add_compile_options($<$<COMPILE_LANGUAGE:C>:-std=gnu11> -Wshadow -Wall -Wextra)

# The engine version string (upstream meson passes -DGSR_VERSION="...").
add_compile_definitions(GSR_VERSION="6.0.0-w1")

# Optional gcov coverage instrumentation (Phase 3 CI deliverable: a
# coverage report artifact). Enabled in a separate build dir by the
# workflow; never in the release build.
option(GSR_ENABLE_COVERAGE "Build with gcov coverage instrumentation" OFF)
if(GSR_ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# Force-include the Windows portability shim into every translation unit.
add_compile_options("-include" "${CMAKE_SOURCE_DIR}/platform/windows/gsr_win32_compat.h")

# ---------------------------------------------------------------------------
# FFmpeg via pkg-config (static build from scripts/build-ffmpeg-windows.sh).
# The upstream engine links: libavformat libavcodec libavutil libswresample
# libavfilter.
# ---------------------------------------------------------------------------
# MSYS2 may ship pkg-config as a shell-script wrapper (not discoverable by
# CMake's find_program on Windows); fall back to pkgconf.exe directly.
find_program(PKG_CONFIG_EXECUTABLE pkg-config)
if(NOT PKG_CONFIG_EXECUTABLE)
    find_program(PKG_CONFIG_EXECUTABLE pkgconf)
endif()
if(NOT PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR
        "pkg-config/pkgconf not found. Install mingw-w64-x86_64-pkgconf in the "
        "MSYS2 MINGW64 environment (see docs/build-windows.md).")
endif()

set(GSR_FFMPEG_LIBS libavformat libavcodec libavutil libswresample libavfilter)
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --cflags ${GSR_FFMPEG_LIBS}
    OUTPUT_VARIABLE GSR_FFMPEG_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GSR_PKG_RESULT)
if(NOT GSR_PKG_RESULT EQUAL 0)
    message(FATAL_ERROR
        "pkg-config could not find the FFmpeg libraries. Build them first with "
        "scripts/build-ffmpeg-windows.sh and set PKG_CONFIG_PATH accordingly "
        "(see docs/build-windows.md).")
endif()
execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs ${GSR_FFMPEG_LIBS}
    OUTPUT_VARIABLE GSR_FFMPEG_LIBS_FLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(GSR_FFMPEG_CFLAGS_LIST UNIX_COMMAND "${GSR_FFMPEG_CFLAGS}")
separate_arguments(GSR_FFMPEG_LIBS_FLAGS_LIST UNIX_COMMAND "${GSR_FFMPEG_LIBS_FLAGS}")

# srt.pc's Libs.private contributes MinGW runtime flags that resolve to
# DLL import libraries: -lpthread (libpthread.dll.a), -lstdc++
# (libstdc++-6.dll), -lgcc_s (libgcc_s_seh-1.dll), -latomic
# (libatomic-1.dll). Two problems:
#   1. -lpthread next to our static winpthread gives `multiple definition
#      of pthread_*` once a member of both ends up in the link.
#   2. The others only become DLL dependencies when a member is pulled;
#      binaries that pull deep FFmpeg members (via muxer.o, which touches
#      SRT/C++) then fail on the plain runner with a silent missing-DLL
#      crash. Filter all four; they are provided statically by the
#      runtime block below and by -static-libgcc/-static-libstdc++ on the
#      executable targets.
set(GSR_FILTERED_RUNTIME_LIBS "-lpthread" "-lstdc++" "-lgcc_s" "-latomic")
set(GSR_FFMPEG_LIBS_FLAGS_FILTERED "")
foreach(flag IN LISTS GSR_FFMPEG_LIBS_FLAGS_LIST)
    if(NOT flag IN_LIST GSR_FILTERED_RUNTIME_LIBS)
        list(APPEND GSR_FFMPEG_LIBS_FLAGS_FILTERED "${flag}")
    endif()
endforeach()
set(GSR_FFMPEG_LIBS_FLAGS_LIST ${GSR_FFMPEG_LIBS_FLAGS_FILTERED})

# ---------------------------------------------------------------------------
# gsr_core — the portable engine core (upstream sources + Windows shims).
# ---------------------------------------------------------------------------
set(GSR_CORE_SOURCES
    upstream/src/defs.c
    upstream/src/json.c
    upstream/src/log.c
    upstream/src/args_parser.c
    upstream/src/library_loader.c
    upstream/src/ffmpeg_utils.c
    upstream/src/encoder/encoder.c
    upstream/src/replay_buffer/replay_buffer.c
    upstream/src/replay_buffer/replay_buffer_ram.c
    upstream/src/replay_buffer/replay_buffer_disk.c
    upstream/src/recorder/audio_codec.c
    upstream/src/recorder/video_codec.c
    upstream/src/recorder/recording_clock.c
    upstream/src/recorder/audio_capture.c
    upstream/src/recorder/audio_input.c
    upstream/src/recorder/capture_source.c
    # recorder/muxer.c: the real upstream save-filepath naming function
    # (gsr_create_new_recording_filepath_from_timestamp) that the
    # platform filesystem layer delegates to.
    upstream/src/recorder/muxer.c
    # Phase 2 shims.
    platform/windows/gsr_win32_compat.c
    platform/windows/gsr_utils_win32.c
    # Phase 3 platform layer (pure-logic backends + small helpers).
    platform/windows/gsr_filesystem_win32.c
    platform/windows/gsr_ipc_protocol.c
    platform/windows/gsr_config_win32.c
    platform/windows/gsr_codec_caps_win32.c
    platform/windows/gsr_display_win32.c
    platform/windows/gsr_platform_win32.c
    # Phase 5: WGC capture backend — pure logic helpers (C, headless-tested)
    # plus the C++/WinRT backend itself (extern "C" surface in capture.h).
    platform/windows/gsr_capture_wgc_helpers.c
    platform/windows/gsr_capture_wgc.cpp
)

add_library(gsr_core STATIC ${GSR_CORE_SOURCES})
# platform/windows is on the include path so that the <libgen.h> and
# <dlfcn.h> shims (MinGW-w64 ships neither) resolve for the upstream sources.
target_include_directories(gsr_core PUBLIC
    ${CMAKE_SOURCE_DIR}/platform/include   # the port's own interfaces (Phase 3)
    ${CMAKE_SOURCE_DIR}/upstream/include
    ${CMAKE_SOURCE_DIR}/platform/windows
    # Stub X11/DRM headers so upstream headers that reference X11/KMS types
    # (cursor.h, capture_setup.h) compile on Windows; see
    # docs/upstream-porting-notes.md §3c.
    ${CMAKE_SOURCE_DIR}/platform/windows/stubs)
target_compile_options(gsr_core PUBLIC ${GSR_FFMPEG_CFLAGS_LIST})
target_link_libraries(gsr_core PUBLIC
    m
    ${GSR_FFMPEG_LIBS_FLAGS_LIST}
    advapi32 # SystemFunction036 (RtlGenRandom) for generate_random_characters
    shell32  # SHGetKnownFolderPath (gsr_platform_get_videos_dir)
    ole32    # CoTaskMemFree (gsr_platform_get_videos_dir)
    dxgi     # Phase 4 monitor enumeration (CreateDXGIFactory1, IDXGIOutput6)
    shcore   # Phase 4 per-monitor DPI (GetDpiForMonitor)
    d3d11        # Phase 5 WGC backend (D3D11CreateDevice, ID3D11Texture2D)
    windowsapp   # Phase 5 WGC backend (RoGetActivationFactory/RoInitialize,
                 # exported by mingw-w64's libwindowsapp.a)
    # MinGW runtime libs, statically (the executables must run without the
    # MSYS2 runtime DLLs, which the workflow's `test` job relies on):
    #   - winpthreads: MSYS2's gcc 16 dropped the -static-libwinpthread
    #     driver flag, so force the static archive at the linker level
    #     (encoder.c uses pthread mutexes).
    #   - atomic: pulled by deep FFmpeg members; link the static archive so
    #     no libatomic-1.dll is needed.
    #   - stdc++: libsrt.a is C++; -static-libstdc++ is a no-op when linking
    #     with the C driver (cc), so force the static archive here too or
    #     the exe either fails to link or needs libstdc++-6.dll.
    "-Wl,-Bstatic"
    "-lwinpthread"
    "-latomic"
    "-lstdc++"
    "-Wl,-Bdynamic")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
# Phase 1 CI toolchain smoke test (compiler/OS info).
add_executable(ci-smoke tests/ci-smoke/main.c)

# Probe which POSIX symbols the MinGW-w64 runtime provides (CI diagnostics).
add_executable(compat-probe tests/compat-probe/main.c)
target_link_libraries(compat-probe PRIVATE gsr_core)

# Core unit tests: CLI parsing, audio input parsing, recording clock,
# replay buffer (RAM + disk), JSON.
add_executable(gsr-core-test tests/gsr-core-test/main.c)
target_link_libraries(gsr-core-test PRIVATE gsr_core)

# Phase 3 platform-layer unit tests: filesystem naming/sanitization, the
# IPC protocol codec, config round-trip, codec capability logic, and the
# --list-monitors/--info output formats.
add_executable(platform-test tests/platform-test/main.c)
target_link_libraries(platform-test PRIVATE gsr_core)

# Phase 5 WGC capture self-test: runs a real WGC capture of the primary
# monitor on CI's virtual display and validates the damage/clear/capture
# contract plus the D3D11 texture. SKIPs (exit 0) when WGC is unavailable
# in the session; FAILs on any real capture failure. The ANGLE interop
# probe inside is informational (needs libEGL.dll, only in the MSYS2
# ctest step).
add_executable(wgc-self-test tests/wgc-self-test/main.c)
target_link_libraries(wgc-self-test PRIVATE gsr_core)

# Link the test executables against the static MinGW runtime so they can be
# re-run by the workflow's `test` job without the MSYS2 environment.
# -static-libgcc works with the C driver; the C++ runtime (libstdc++.a),
# winpthread and atomic are already linked statically via the gsr_core
# PUBLIC link flags above (-static-libstdc++/-static-libwinpthread are
# driver options that do nothing when linking with cc).
foreach(target ci-smoke compat-probe gsr-core-test platform-test wgc-self-test)
    set_target_properties(${target} PROPERTIES
        LINK_FLAGS "-static-libgcc")
endforeach()

enable_testing()
add_test(NAME ci-smoke COMMAND ci-smoke)
add_test(NAME gsr-core-test COMMAND gsr-core-test)
add_test(NAME platform-test COMMAND platform-test)
add_test(NAME wgc-self-test COMMAND wgc-self-test)
