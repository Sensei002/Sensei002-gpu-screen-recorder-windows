#pragma once

#include <string>
#include <vector>
#include <stdint.h>

#include <mglpp/system/vec.hpp>

namespace gsr {
    struct SupportedVideoCodecs {
        bool h264 = false;
        bool h264_software = false;
        bool hevc = false;
        bool hevc_hdr = false;
        bool hevc_10bit = false;
        bool av1 = false;
        bool av1_hdr = false;
        bool av1_10bit = false;
        bool vp8 = false;
        bool vp9 = false;
        bool h264_vulkan = false;
        bool hevc_vulkan = false;
        bool hevc_hdr_vulkan = false;
        bool hevc_10bit_vulkan = false;
        bool av1_vulkan = false;
        bool av1_hdr_vulkan = false;
        bool av1_10bit_vulkan = false;
    };

    struct SupportedImageFormats {
        bool jpeg = false;
        bool png = false;
    };

    enum GsrCameraPixelFormat {
        YUYV,
        MJPEG
    };

    struct GsrMonitor {
        std::string name;
        mgl::vec2i size;
    };

    struct GsrCameraSetup {
        mgl::vec2i resolution;
        int fps;
        //GsrCameraPixelFormat pixel_format;
    };

    struct GsrCamera {
        std::string path;
        std::vector<GsrCameraSetup> yuyv_setups;
        std::vector<GsrCameraSetup> mjpeg_setups;
    };

    struct GsrVersion {
        uint8_t major = 0;
        uint8_t minor = 0;
        uint8_t patch = 0;

        bool operator>(const GsrVersion &other) const;
        bool operator>=(const GsrVersion &other) const;
        bool operator<(const GsrVersion &other) const;
        bool operator<=(const GsrVersion &other) const;
        bool operator==(const GsrVersion &other) const;
        bool operator!=(const GsrVersion &other) const;

        std::string to_string() const;
    };

    struct SupportedCaptureOptions {
        bool window = false;
        bool region = false;
        bool focused = false;
        bool portal = false;
        std::vector<GsrMonitor> monitors;
        std::vector<GsrCamera> cameras;
    };

    enum class DisplayServer {
        UNKNOWN,
        X11,
        WAYLAND
    };

    struct SystemInfo {
        DisplayServer display_server = DisplayServer::UNKNOWN;
        bool supports_app_audio = false;
        GsrVersion gsr_version;
    };

    enum class GpuVendor {
        UNKNOWN,
        AMD,
        INTEL,
        NVIDIA,
        BROADCOM,
        APPLE
    };

    struct GpuInfo {
        GpuVendor vendor = GpuVendor::UNKNOWN;
        std::string card_path;
    };

    struct GsrInfo {
        SystemInfo system_info;
        GpuInfo gpu_info;
        SupportedVideoCodecs supported_video_codecs;
        SupportedImageFormats supported_image_formats;
    };

    enum class GsrInfoExitStatus {
        OK,
        BROKEN_DRIVERS,
        FAILED_TO_RUN_COMMAND,
        OPENGL_FAILED,
        NO_DRM_CARD
    };

    struct AudioDevice {
        std::string name;
        std::string description;
    };

    GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info);

    std::vector<AudioDevice> get_audio_devices();
    std::vector<std::string> get_application_audio();
    SupportedCaptureOptions get_supported_capture_options(const GsrInfo &gsr_info);
    std::vector<GsrCamera> get_v4l2_devices();
}