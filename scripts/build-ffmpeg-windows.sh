#!/usr/bin/env bash
# =============================================================================
# scripts/build-ffmpeg-windows.sh
#
# Builds the pinned FFmpeg stack used by the Windows port, from source, as
# static libraries, inside an MSYS2 MINGW64 environment.
#
# This mirrors the upstream recipe exactly (extra/build_ffmpeg.sh +
# subprojects/*.wrap in gpu-screen-recorder r1467 / 6.0.0), including:
#   * the same pinned source versions and hashes,
#   * the same --disable-everything component list (minus Linux-only
#     backends: no libva/vaapi and no vulkan),
#   * the same LTO static build,
#   * the two upstream ffmpeg patches (runtime NVENC API negotiation and
#     mbedtls default CA certificates), applied verbatim.
#
# Pinned sources (identical to upstream's .wrap files):
#   ffmpeg            9.0
#   x264              b35605ace3ddf7c1a5d67a2eb553f034aef41d55
#   opus              1.6.1
#   mbedtls           3.6.7
#   srt               1.5.6
#   nv-codec-headers  n13.0.19.0
#
# Usage (in an MSYS2 MINGW64 shell):
#   ./scripts/build-ffmpeg-windows.sh
#
# Environment variables:
#   FFMPEG_PREFIX  install prefix          (default: $PWD/build/ffmpeg-prefix)
#   FFMPEG_SOURCES source download dir     (default: $PWD/build/ffmpeg-sources)
#   CC             C compiler              (default: gcc, the MINGW64 gcc)
#   CXX            C++ compiler            (default: g++, for srt)
#   JOBS           parallel jobs           (default: nproc)
#   FFMPEG_LTO     0 to disable LTO        (default: 1, mirrors upstream)
#   BUILD_LIBS     space separated subset of
#                  "nv-codec-headers x264 opus mbedtls srt ffmpeg"
#                  (default: all of them)
#
# The build is stamp-based and idempotent: re-running with the same sources,
# compiler and arguments is a no-op (mirrors upstream's stamp file logic), so
# the whole build/ffmpeg directory can be cached by CI.
# =============================================================================
set -eu

PREFIX="${FFMPEG_PREFIX:-$PWD/build/ffmpeg-prefix}"
SOURCES_DIR="${FFMPEG_SOURCES:-$PWD/build/ffmpeg-sources}"
CC="${CC:-gcc}"
CXX="${CXX:-g++}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
LTO_ENABLED="${FFMPEG_LTO:-1}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PATCH_DIR="$SCRIPT_DIR/patches"

export CC
# MSYS2 ships pkg-config as a wrapper that converts MSYS-style paths for the
# native pkgconf.exe underneath it, so the canonical MSYS2 form (MSYS paths
# joined with ':') is what works here. The diagnostic right before the ffmpeg
# build confirms it resolves libopus/libx264/libsrt/mbedtls.
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:/mingw64/lib/pkgconfig"

mkdir -p "$PREFIX" "$SOURCES_DIR"

require_program() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 is required to build the FFmpeg stack from source" >&2
        exit 1
    fi
}

require_program pkg-config
require_program make
require_program nasm
require_program cmake
require_program curl
require_program tar
require_program sha256sum
require_program patch
require_program cygpath

PYTHON="$(command -v python3 || command -v python || true)"
if [ -z "$PYTHON" ]; then
    echo "error: python3/python is required to build mbedtls from source" >&2
    exit 1
fi

# ---- pinned sources: "name|url1;url2;...|sha256" -----------------------
# Hashes are the exact source_hash values from the upstream .wrap files.
# Multiple URLs are tried in order (primary first); a download is only
# accepted when its sha256 matches, so any mirror is safe to use.
NVENC_HEADERS="nv-codec-headers|https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.0.19.0.tar.gz|86d15d1a7c0ac73a0eafdfc57bebfeba7da8264595bf531cf4d8db1c22940116"
X264="x264|https://code.videolan.org/videolan/x264/-/archive/b35605ace3ddf7c1a5d67a2eb553f034aef41d55/x264-b35605ace3ddf7c1a5d67a2eb553f034aef41d55.tar.bz2|6eeb82934e69fd51e043bd8c5b0d152839638d1ce7aa4eea65a3fedcf83ff224"
OPUS="opus|https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz;https://archive.mozilla.org/pub/opus/opus-1.6.1.tar.gz|6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1"
MBEDTLS="mbedtls|https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2|a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
SRT="srt|https://github.com/Haivision/srt/archive/refs/tags/v1.5.6.tar.gz|2c4980c2c4cfd142d21b829d939dc51db9c6628af5967fff62fd7290769569c7"
FFMPEG="ffmpeg|https://ffmpeg.org/releases/ffmpeg-9.0.tar.xz;https://mirrors.ustc.edu.cn/ffmpeg/ffmpeg-9.0.tar.xz;https://mirrors.tuna.tsinghua.edu.cn/ffmpeg/ffmpeg-9.0.tar.xz|7f607a00dd0d28a729d5a4811205812eef01cf6ef6155025febb6f36a9062d52"

# ---- download + verify + extract ---------------------------------------
# ffmpeg.org and other release hosts are known to reset connections
# intermittently, so every attempt gets aggressive curl retries and each
# source has mirror fallbacks. The sha256 check decides what is accepted.
fetch_source() {
    local name="$1" urls="$2" sha256="$3"
    local archive="$SOURCES_DIR/$name.tar"
    local tmp_archive="$archive.tmp"

    if [ -f "$archive" ] && [ "$(sha256sum "$archive" | cut -d' ' -f1)" = "$sha256" ]; then
        echo "== $name: source already present and verified"
        return 0
    fi

    local ok=0
    local IFS=';'
    for url in $urls; do
        echo "== $name: downloading $url"
        if curl -fSL --connect-timeout 30 --max-time 300 \
            --retry 5 --retry-all-errors --retry-delay 3 \
            -o "$tmp_archive" "$url"; then
            local actual_sha
            actual_sha="$(sha256sum "$tmp_archive" | cut -d' ' -f1)"
            if [ "$actual_sha" = "$sha256" ]; then
                ok=1
                break
            else
                echo "warning: sha256 mismatch from $url (expected $sha256, got $actual_sha); trying next mirror" >&2
            fi
        else
            echo "warning: download failed from $url; trying next mirror" >&2
        fi
    done
    unset IFS

    if [ "$ok" != "1" ]; then
        echo "error: could not download $name from any mirror (expected sha256 $sha256)" >&2
        exit 1
    fi
    mv "$tmp_archive" "$archive"

    echo "== $name: extracting"
    rm -rf "$SOURCES_DIR/$name-src"
    mkdir -p "$SOURCES_DIR/$name-src"
    tar -xf "$archive" -C "$SOURCES_DIR/$name-src" --strip-components=1
}

# The stamp skips rebuilding a library when nothing changed, exactly like the
# upstream extra/build_ffmpeg.sh.
stamp_matches() {
    local stamp_file="$1" stamp="$2"
    [ -f "$stamp_file" ] && [ "$(cat "$stamp_file")" = "$stamp" ]
}

write_stamp() {
    local stamp_file="$1" stamp="$2"
    printf '%s' "$stamp" > "$stamp_file"
}

build_lib() {
    local name="$1"
    local source_dir="$SOURCES_DIR/$name-src"
    local build_dir="$PWD/build/ffmpeg-libs/$name"
    local log_file="$build_dir/build.log"
    local stamp_file="$PREFIX/$name.stamp"

    local args=""
    case "$name" in
        nv-codec-headers) args="PREFIX=$PREFIX" ;;
        x264)             args="--prefix=$PREFIX --enable-static --enable-pic --enable-lto --disable-cli --disable-opencl" ;;
        opus)             args="--prefix=$PREFIX --disable-shared --enable-static --with-pic --disable-doc --disable-extra-programs" ;;
        mbedtls)          args="-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON" ;;
        srt)              args="-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DENABLE_APPS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_TESTING=OFF -DENABLE_UNITTESTS=OFF -DUSE_ENCLIB=mbedtls -DMBEDTLS_PREFIX=$PREFIX -DSSL_REQUIRED_MODULES=mbedtls" ;;
        ffmpeg)           args="$(ffmpeg_configure_args)" ;;
    esac

    local stamp="$source_dir|$CC|$args"
    if stamp_matches "$stamp_file" "$stamp"; then
        echo "== $name: up to date (stamp matches), skipping"
        return 0
    fi

    mkdir -p "$build_dir"
    echo "== $name: building (see $log_file for details)"

    (
        cd "$build_dir"
        case "$name" in
            nv-codec-headers)
                make -C "$source_dir" PREFIX="$PREFIX" install
                ;;
            x264|opus|ffmpeg)
                "$source_dir/configure" $args
                make -j"$JOBS"
                make install
                ;;
            mbedtls)
                "$PYTHON" "$source_dir/scripts/config.py" \
                    -f "$source_dir/include/mbedtls/mbedtls_config.h" set MBEDTLS_SSL_DTLS_SRTP
                cmake -S "$source_dir" -B "$build_dir" $args
                cmake --build "$build_dir" -j"$JOBS"
                cmake --install "$build_dir"
                ;;
            srt)
                cmake -S "$source_dir" -B "$build_dir" $args
                cmake --build "$build_dir" -j"$JOBS"
                cmake --install "$build_dir"
                ;;
        esac
    ) >"$log_file" 2>&1 || {
        echo "error: failed to build $name, see $log_file for details:" >&2
        tail -n 30 "$log_file" >&2
        exit 1
    }

    write_stamp "$stamp_file" "$stamp"
}

# ---- ffmpeg configure arguments ----------------------------------------
ffmpeg_configure_args() {
    local encoders="aac,flac,libx264,libopus,h264_nvenc,hevc_nvenc,av1_nvenc"
    local muxers="mp4,mov,matroska,webm,flv,mpegts,hls,whip"
    local protocols="file,pipe,tcp,udp,http,https,tls,rtmp,rtmps,libsrt"
    local filters="abuffer,abuffersink,amix,aresample,aformat,anull"

    local lto_arg=""
    if [ "$LTO_ENABLED" = "1" ]; then
        lto_arg="--enable-lto"
    fi

    echo --prefix="$PREFIX" \
        --cc="$CC" \
        --target-os=mingw32 \
        --arch=x86_64 \
        --pkg-config-flags=--static \
        --disable-debug \
        --disable-everything \
        --disable-autodetect \
        --disable-programs \
        --disable-doc \
        --disable-avdevice \
        --disable-swscale \
        --disable-shared \
        --enable-static \
        --enable-pic \
        --enable-gpl \
        --enable-version3 \
        --enable-network \
        --enable-w32threads \
        $lto_arg \
        --enable-encoder="$encoders" \
        --enable-muxer="$muxers" \
        --enable-protocol="$protocols" \
        --enable-filter="$filters" \
        --enable-bsf=extract_extradata \
        --enable-libx264 \
        --enable-libopus \
        --enable-libsrt \
        --enable-mbedtls \
        --enable-ffnvcodec \
        --enable-nvenc \
        --enable-cuda
}

# ---- apply the upstream ffmpeg patches --------------------------------
# The meson wrap applies these via diff_files; we apply them the same way,
# once, before the stamp is written.
apply_ffmpeg_patches() {
    local source_dir="$SOURCES_DIR/ffmpeg-src"
    local marker="$source_dir/.gsr-windows-patches-applied"
    if [ -f "$marker" ]; then
        return 0
    fi
    echo "== ffmpeg: applying upstream patches"
    patch -d "$source_dir" -p1 < "$PATCH_DIR/ffmpeg-nvenc-runtime-api-version.patch"
    patch -d "$source_dir" -p1 < "$PATCH_DIR/ffmpeg-mbedtls-default-ca-certs.patch"
    touch "$marker"
}

# ---- build everything ---------------------------------------------------
BUILD_LIBS="${BUILD_LIBS:-nv-codec-headers x264 opus mbedtls srt ffmpeg}"

echo "== FFmpeg stack build"
echo "   prefix:    $PREFIX"
echo "   sources:   $SOURCES_DIR"
echo "   compiler:  $CC (CXX: $CXX)"
echo "   jobs:      $JOBS"
echo "   lto:       $LTO_ENABLED"
echo "   libraries: $BUILD_LIBS"

# fetch + extract each source first (downloads can be parallelized/retried)
declare -A SOURCES=(
    [nv-codec-headers]="$NVENC_HEADERS"
    [x264]="$X264"
    [opus]="$OPUS"
    [mbedtls]="$MBEDTLS"
    [srt]="$SRT"
    [ffmpeg]="$FFMPEG"
)
for lib in $BUILD_LIBS; do
    IFS='|' read -r name urls sha256 <<< "${SOURCES[$lib]}"
    fetch_source "$name" "$urls" "$sha256"
done

if [[ "$BUILD_LIBS" == *ffmpeg* ]]; then
    apply_ffmpeg_patches

    echo "== ffmpeg: pkg-config diagnostics"
    echo "   pkg-config: $(command -v pkg-config)"
    echo "   PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
    echo "   prefix pkgconfig dir:"
    ls -la "$PREFIX/lib/pkgconfig" 2>&1
    echo "   --modversion:"
    pkg-config --modversion libopus libx264 libsrt mbedtls 2>&1 || true
fi

for lib in $BUILD_LIBS; do
    build_lib "$lib"
done

echo "== done. FFmpeg stack installed in $PREFIX"
