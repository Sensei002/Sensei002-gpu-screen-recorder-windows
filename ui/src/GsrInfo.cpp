#include "../include/GsrInfo.hpp"
#include "../include/Utils.hpp"
#include "../include/Process.hpp"

#include <optional>
#include <string.h>

namespace gsr {
    bool GsrVersion::operator>(const GsrVersion &other) const {
        return major > other.major || (major == other.major && minor > other.minor) || (major == other.major && minor == other.minor && patch > other.patch);
    }

    bool GsrVersion::operator>=(const GsrVersion &other) const {
        return major > other.major || (major == other.major && minor > other.minor) || (major == other.major && minor == other.minor && patch >= other.patch);
    }

    bool GsrVersion::operator<(const GsrVersion &other) const {
        return !operator>=(other);
    }

    bool GsrVersion::operator<=(const GsrVersion &other) const {
        return !operator>(other);
    }

    bool GsrVersion::operator==(const GsrVersion &other) const {
        return major == other.major && minor == other.minor && patch == other.patch;
    }

    bool GsrVersion::operator!=(const GsrVersion &other) const {
        return !operator==(other);
    }

    std::string GsrVersion::to_string() const {
        std::string result;
        if(major == 0 && minor == 0 && patch == 0)
            result = "Unknown";
        else
            result = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
        return result;
    }

    /* Windows CRLF robustness: the engine now writes binary-mode stdout
       (no \r\n), but tolerate a trailing '\r' from any other producer so
       exact-string comparisons ("vendor|nvidia", "h264_software", ...)
       never fail on Windows. */
    static std::string_view trim_cr(std::string_view line) {
        if(!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        return line;
    }

    /* Returns -1 on error */
    static int parse_u8(const char *str, int size) {
        if(size <= 0)
            return -1;

        int result = 0;
        for(int i = 0; i < size; ++i) {
            char c = str[i];
            if(c >= '0' && c <= '9') {
                result = result * 10 + (c - '0');
                if(result > 255)
                    return -1;
            } else {
                return -1;
            }
        }
        return result;
    }

    static GsrVersion parse_gsr_version(const std::string_view str) {
        GsrVersion result;
        uint8_t numbers[3];
        int number_index = 0;

        /* The Windows port reports "6.0.0-w1" (a suffix after the numeric
           triple). Stop at the first character that is not a digit or '.',
           then parse the leading numeric version only. */
        const size_t suffix_index = str.find_first_not_of("0123456789.");
        const std::string_view version_str = suffix_index == std::string_view::npos ? str : str.substr(0, suffix_index);

        size_t index = 0;
        while(true) {
            size_t next_index = version_str.find('.', index);
            if(next_index == std::string::npos)
                next_index = version_str.size();

            const int number = parse_u8(version_str.data() + index, next_index - index);
            if(number == -1) {
                fprintf(stderr, "Error: gpu-screen-recorder --info contains invalid gsr version: %.*s\n", (int)version_str.size(), version_str.data());
                return {0, 0, 0};
            }

            if(number_index >= 3) {
                fprintf(stderr, "Error: gpu-screen-recorder --info contains invalid gsr version: %.*s\n", (int)version_str.size(), version_str.data());
                return {0, 0, 0};
            }

            numbers[number_index] = number;
            ++number_index;
            index = next_index + 1;
            if(next_index == version_str.size())
                break;
        }

        result.major = numbers[0];
        result.minor = numbers[1];
        result.patch = numbers[2];
        return result;
    }

    static std::optional<KeyValue> parse_key_value(std::string_view line) {
        const size_t space_index = line.find('|');
        if(space_index == std::string_view::npos)
            return std::nullopt;
        return KeyValue{line.substr(0, space_index), line.substr(space_index + 1)};
    }

    static void parse_system_info_line(GsrInfo *gsr_info, std::string_view line) {
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return;

        if(key_value->key == "display_server") {
            if(key_value->value == "x11")
                gsr_info->system_info.display_server = DisplayServer::X11;
            else if(key_value->value == "wayland")
                gsr_info->system_info.display_server = DisplayServer::WAYLAND;
        } else if(key_value->key == "supports_app_audio") {
            gsr_info->system_info.supports_app_audio = key_value->value == "yes";
        } else if(key_value->key == "gsr_version") {
            gsr_info->system_info.gsr_version = parse_gsr_version(key_value->value);
        }
    }

    static void parse_gpu_info_line(GsrInfo *gsr_info, std::string_view line) {
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return;

        if(key_value->key == "vendor") {
            if(key_value->value == "amd")
                gsr_info->gpu_info.vendor = GpuVendor::AMD;
            else if(key_value->value == "intel")
                gsr_info->gpu_info.vendor = GpuVendor::INTEL;
            else if(key_value->value == "nvidia")
                gsr_info->gpu_info.vendor = GpuVendor::NVIDIA;
            else if(key_value->value == "broadcom")
                gsr_info->gpu_info.vendor = GpuVendor::BROADCOM;
            else if(key_value->value == "apple")
                gsr_info->gpu_info.vendor = GpuVendor::APPLE;
        } else if(key_value->key == "card_path") {
            gsr_info->gpu_info.card_path = key_value->value;
        }
    }

    static void parse_video_codecs_line(GsrInfo *gsr_info, std::string_view line) {
        if(line == "h264")
            gsr_info->supported_video_codecs.h264 = true;
        else if(line == "h264_software")
            gsr_info->supported_video_codecs.h264_software = true;
        else if(line == "hevc")
            gsr_info->supported_video_codecs.hevc = true;
        else if(line == "hevc_hdr")
            gsr_info->supported_video_codecs.hevc_hdr = true;
        else if(line == "hevc_10bit")
            gsr_info->supported_video_codecs.hevc_10bit = true;
        else if(line == "av1")
            gsr_info->supported_video_codecs.av1 = true;
        else if(line == "av1_hdr")
            gsr_info->supported_video_codecs.av1_hdr = true;
        else if(line == "av1_10bit")
            gsr_info->supported_video_codecs.av1_10bit = true;
        else if(line == "vp8")
            gsr_info->supported_video_codecs.vp8 = true;
        else if(line == "vp9")
            gsr_info->supported_video_codecs.vp9 = true;
        else if(line == "h264_vulkan")
            gsr_info->supported_video_codecs.h264_vulkan = true;
        else if(line == "hevc_vulkan")
            gsr_info->supported_video_codecs.hevc_vulkan = true;
        else if(line == "hevc_hdr_vulkan")
            gsr_info->supported_video_codecs.hevc_hdr_vulkan = true;
        else if(line == "hevc_10bit_vulkan")
            gsr_info->supported_video_codecs.hevc_10bit_vulkan = true;
        else if(line == "av1_vulkan")
            gsr_info->supported_video_codecs.av1_vulkan = true;
        else if(line == "av1_hdr_vulkan")
            gsr_info->supported_video_codecs.av1_hdr_vulkan = true;
        else if(line == "av1_10bit_vulkan")
            gsr_info->supported_video_codecs.av1_10bit_vulkan = true;
    }

    static void parse_image_formats_line(GsrInfo *gsr_info, std::string_view line) {
        if(line == "jpeg")
            gsr_info->supported_image_formats.jpeg = true;
        else if(line == "png")
            gsr_info->supported_image_formats.png = true;
    }

    enum class GsrInfoSection {
        UNKNOWN,
        SYSTEM_INFO,
        GPU_INFO,
        VIDEO_CODECS,
        IMAGE_FORMATS,
        CAPTURE_OPTIONS
    };

    GsrInfoExitStatus get_gpu_screen_recorder_info(GsrInfo *gsr_info) {
        *gsr_info = GsrInfo{};

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--info", nullptr };
        const int exit_status = exec_program_get_stdout(args, stdout_str);
        switch(exit_status) {
            case 0:  break;
            case 14: return GsrInfoExitStatus::BROKEN_DRIVERS;
            case 22: return GsrInfoExitStatus::OPENGL_FAILED;
            case 23: return GsrInfoExitStatus::NO_DRM_CARD;
            default: return GsrInfoExitStatus::FAILED_TO_RUN_COMMAND;
        }

        GsrInfoSection section = GsrInfoSection::UNKNOWN;
        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            line = trim_cr(line);
            if(starts_with(line, "section=")) {
                const std::string_view section_name = line.substr(8);
                if(section_name == "system_info")
                    section = GsrInfoSection::SYSTEM_INFO;
                else if(section_name == "gpu_info")
                    section = GsrInfoSection::GPU_INFO;
                else if(section_name == "video_codecs")
                    section = GsrInfoSection::VIDEO_CODECS;
                else if(section_name == "image_formats")
                    section = GsrInfoSection::IMAGE_FORMATS;
                else if(section_name == "capture_options")
                    section = GsrInfoSection::CAPTURE_OPTIONS;
                else
                    section = GsrInfoSection::UNKNOWN;
                return true;
            }

            switch(section) {
                case GsrInfoSection::UNKNOWN: {
                    break;
                }
                case GsrInfoSection::SYSTEM_INFO: {
                    parse_system_info_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::GPU_INFO: {
                    parse_gpu_info_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::VIDEO_CODECS: {
                    parse_video_codecs_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::IMAGE_FORMATS: {
                    parse_image_formats_line(gsr_info, line);
                    break;
                }
                case GsrInfoSection::CAPTURE_OPTIONS: {
                    // Intentionally ignore, get capture options with get_supported_capture_options instead
                    break;
                }
            }

            return true;
        });

        return GsrInfoExitStatus::OK;
    }

    static std::optional<AudioDevice> parse_audio_device_line(std::string_view line) {
        std::optional<AudioDevice> audio_device;
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return audio_device;

        audio_device = AudioDevice{std::string(key_value->key), std::string(key_value->value)};
        return audio_device;
    }

    std::vector<AudioDevice> get_audio_devices() {
        std::vector<AudioDevice> audio_devices;

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-audio-devices", nullptr };
        if(exec_program_get_stdout(args, stdout_str, false) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-audio-devices' failed\n");
            return audio_devices;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            std::optional<AudioDevice> audio_device = parse_audio_device_line(line);
            if(audio_device)
                audio_devices.push_back(std::move(audio_device.value()));
            return true;
        });

        return audio_devices;
    }

    std::vector<std::string> get_application_audio() {
        std::vector<std::string> application_audio;

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-application-audio", nullptr };
        if(exec_program_get_stdout(args, stdout_str) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-application-audio' failed\n");
            return application_audio;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            application_audio.emplace_back(line);
            return true;
        });

        return application_audio;
    }

    struct KeyValue3 {
        std::string_view value1;
        std::string_view value2;
        std::string_view value3;
    };

    static std::optional<KeyValue3> parse_3(std::string_view line) {
        const size_t space_index1 = line.find('|');
        if(space_index1 == std::string_view::npos)
            return std::nullopt;

        const size_t space_index2 = line.find('|', space_index1 + 1);
        if(space_index2 == std::string_view::npos)
            return std::nullopt;

        return KeyValue3{
            line.substr(0, space_index1),
            line.substr(space_index1 + 1, space_index2 - (space_index1 + 1)),
            line.substr(space_index2 + 1),
        };
    }

    static bool parse_camera_pixel_format(std::string_view line, GsrCameraPixelFormat &pixel_format) {
        if(line == "yuyv") {
            pixel_format = YUYV;
            return true;
        } else if(line == "mjpeg") {
            pixel_format = MJPEG;
            return true;
        } else {
            return false;
        }
    }

    static bool capture_option_line_to_camera(std::string_view line, std::string &path, GsrCameraSetup &camera_setup, GsrCameraPixelFormat &pixel_format) {
        const std::optional<KeyValue3> key_value3 = parse_3(line);
        if(!key_value3)
            return false;

        path = key_value3->value1;

        char value_buffer[256];
        snprintf(value_buffer, sizeof(value_buffer), "%.*s", (int)key_value3->value2.size(), key_value3->value2.data());
        if(sscanf(value_buffer, "%dx%d@%dhz", &camera_setup.resolution.x, &camera_setup.resolution.y, &camera_setup.fps) != 3)
            return false;

        if(!parse_camera_pixel_format(key_value3->value3, pixel_format))
            return false;

        return true;
    }

    static std::optional<GsrMonitor> capture_option_line_to_monitor(std::string_view line) {
        std::optional<GsrMonitor> monitor;
        const std::optional<KeyValue> key_value = parse_key_value(line);
        if(!key_value)
            return monitor;

        char value_buffer[256];
        snprintf(value_buffer, sizeof(value_buffer), "%.*s", (int)key_value->value.size(), key_value->value.data());

        monitor = GsrMonitor{std::string(key_value->key), mgl::vec2i{0, 0}};
        if(sscanf(value_buffer, "%dx%d", &monitor->size.x, &monitor->size.y) != 2)
            monitor->size = {0, 0};

        return monitor;
    }

    static GsrCamera* get_gsr_camera_by_path(std::vector<GsrCamera> &cameras, const std::string &path) {
        for(GsrCamera &camera : cameras) {
            if(camera.path == path)
                return &camera;
        }
        return nullptr;
    }

    static void parse_camera_line(std::string_view line, std::vector<GsrCamera> &cameras) {
        std::string camera_path;
        GsrCameraSetup camera_setup;
        GsrCameraPixelFormat pixel_format;
        if(!capture_option_line_to_camera(line, camera_path, camera_setup, pixel_format))
            return;

        GsrCamera *existing_camera = get_gsr_camera_by_path(cameras, camera_path);
        if(!existing_camera) {
            cameras.push_back(GsrCamera{camera_path, std::vector<GsrCameraSetup>{}, std::vector<GsrCameraSetup>{}});
            existing_camera = &cameras.back();
        }

        switch(pixel_format) {
            case YUYV:
                existing_camera->yuyv_setups.push_back(camera_setup);
                break;
            case MJPEG:
                existing_camera->mjpeg_setups.push_back(camera_setup);
                break;
        }
    }

    static void parse_capture_options_line(SupportedCaptureOptions &capture_options, std::string_view line) {
        if(line == "window") {
            capture_options.window = true;
        } else if(line == "region") {
            capture_options.region = true;
        } else if(line == "focused") {
            capture_options.focused = true;
        } else if(line == "portal") {
            capture_options.portal = true;
        } else if(!line.empty() && line[0] == '/') {
            parse_camera_line(line, capture_options.cameras);
        } else {
            std::optional<GsrMonitor> monitor = capture_option_line_to_monitor(line);
            if(monitor)
                capture_options.monitors.push_back(std::move(monitor.value()));
        }
    }

    static const char* gpu_vendor_to_string(GpuVendor vendor) {
        switch(vendor) {
            case GpuVendor::UNKNOWN:  return "unknown";
            case GpuVendor::AMD:      return "amd";
            case GpuVendor::INTEL:    return "intel";
            case GpuVendor::NVIDIA:   return "nvidia";
            case GpuVendor::BROADCOM: return "broadcom";
            case GpuVendor::APPLE: return "apple";
        }
        return "unknown";
    }

    SupportedCaptureOptions get_supported_capture_options(const GsrInfo &gsr_info) {
        SupportedCaptureOptions capture_options;

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-capture-options", gsr_info.gpu_info.card_path.c_str(), gpu_vendor_to_string(gsr_info.gpu_info.vendor), nullptr };
        if(exec_program_get_stdout(args, stdout_str) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-capture-options' failed\n");
            return capture_options;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            parse_capture_options_line(capture_options, trim_cr(line));
            return true;
        });

        return capture_options;
    }

    std::vector<GsrCamera> get_v4l2_devices() {
        std::vector<GsrCamera> cameras;

        std::string stdout_str;
        const char *args[] = { "gpu-screen-recorder", "--list-v4l2-devices", nullptr };
        if(exec_program_get_stdout(args, stdout_str) != 0) {
            fprintf(stderr, "error: 'gpu-screen-recorder --list-v4l2-devices' failed\n");
            return cameras;
        }

        string_split_char(stdout_str, '\n', [&](std::string_view line) {
            parse_camera_line(trim_cr(line), cameras);
            return true;
        });

        return cameras;
    }
}
