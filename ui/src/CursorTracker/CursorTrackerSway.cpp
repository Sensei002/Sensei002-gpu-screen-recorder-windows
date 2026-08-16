#include "../../include/CursorTracker/CursorTrackerSway.hpp"
#include "../../include/Process.hpp"
#include "../../include/Utils.hpp"

namespace gsr {
    // static
    bool CursorTrackerSway::is_supported() {
        const char *args[] = { "swaymsg", "-p", "-t", "get_outputs", nullptr };
        std::string output;
        return exec_program_on_host_get_stdout(args, output, false) == 0;
    }

    CursorTrackerSway::CursorTrackerSway() {

    }

    std::optional<CursorInfo> CursorTrackerSway::get_latest_cursor_info() {
        const char *args[] = { "swaymsg", "-p", "-t", "get_outputs", nullptr };
        std::string output;
        if(exec_program_on_host_get_stdout(args, output, false) != 0)
            return std::nullopt;

        std::string focused_monitor_name;
        string_split_char(output, '\n', [&focused_monitor_name](std::string_view line) {
            if(starts_with(line, "Output ") && ends_with(line, "(focused)")) {
                const size_t start_index = 7;
                const size_t end_index = line.find(' ', start_index);
                if(end_index != std::string_view::npos)
                    focused_monitor_name = line.substr(start_index, end_index - start_index);
                return false;
            }
            return true;
        });

        return CursorInfo{ mgl::vec2i{0, 0}, std::move(focused_monitor_name) };
    }
}