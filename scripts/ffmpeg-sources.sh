# =============================================================================
# scripts/ffmpeg-sources.sh
#
# Pinned sources for the FFmpeg stack build, in "name|url1;url2;...|sha256"
# form. Hashes are the exact source_hash values from the upstream .wrap
# files. Multiple URLs are tried in order (primary first); a download is
# only accepted when its sha256 matches, so any mirror is safe to use.
#
# This file is `source`d by scripts/build-ffmpeg-windows.sh and is part of
# the CI cache key (scripts/ffmpeg-sources.sh + scripts/patches/*.patch),
# so changing a pin invalidates the cache while script-only changes reuse
# the built libraries.
# =============================================================================
NVENC_HEADERS="nv-codec-headers|https://github.com/FFmpeg/nv-codec-headers/archive/refs/tags/n13.0.19.0.tar.gz|86d15d1a7c0ac73a0eafdfc57bebfeba7da8264595bf531cf4d8db1c22940116"
X264="x264|https://code.videolan.org/videolan/x264/-/archive/b35605ace3ddf7c1a5d67a2eb553f034aef41d55/x264-b35605ace3ddf7c1a5d67a2eb553f034aef41d55.tar.bz2|6eeb82934e69fd51e043bd8c5b0d152839638d1ce7aa4eea65a3fedcf83ff224"
OPUS="opus|https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz;https://archive.mozilla.org/pub/opus/opus-1.6.1.tar.gz|6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1"
MBEDTLS="mbedtls|https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2|a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
SRT="srt|https://github.com/Haivision/srt/archive/refs/tags/v1.5.6.tar.gz|2c4980c2c4cfd142d21b829d939dc51db9c6628af5967fff62fd7290769569c7"
FFMPEG="ffmpeg|https://ffmpeg.org/releases/ffmpeg-9.0.tar.xz;https://mirrors.ustc.edu.cn/ffmpeg/ffmpeg-9.0.tar.xz;https://mirrors.tuna.tsinghua.edu.cn/ffmpeg/ffmpeg-9.0.tar.xz|7f607a00dd0d28a729d5a4811205812eef01cf6ef6155025febb6f36a9062d52"
