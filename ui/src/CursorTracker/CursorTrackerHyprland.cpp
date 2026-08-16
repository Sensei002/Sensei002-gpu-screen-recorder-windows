#include "../../include/CursorTracker/CursorTrackerHyprland.hpp"
#include "../../include/Process.hpp"

namespace gsr {
    // static
    bool CursorTrackerHyprland::is_supported() {
        const char *args[] = { "hyprctl", "-j", "activeworkspace", nullptr };
        std::string output;
        return exec_program_on_host_get_stdout(args, output, false) == 0;
    }

    CursorTrackerHyprland::CursorTrackerHyprland() {

    }

    std::optional<CursorInfo> CursorTrackerHyprland::get_latest_cursor_info() {
        const char *args[] = { "hyprctl", "-j", "activeworkspace", nullptr };
        std::string output;
        if(exec_program_on_host_get_stdout(args, output, false) != 0)
            return std::nullopt;

        size_t start_index = output.find("\"monitor\"");
        if(start_index == std::string::npos)
            return std::nullopt;

        start_index += 9;
        start_index = output.find('"', start_index);
        if(start_index == std::string::npos)
            return std::nullopt;

        start_index += 1;
        const size_t end_index = output.find('"', start_index);
        if(end_index == std::string::npos)
            return std::nullopt;

        return CursorInfo{ mgl::vec2i{0, 0}, output.substr(start_index, end_index - start_index) };
    }
}