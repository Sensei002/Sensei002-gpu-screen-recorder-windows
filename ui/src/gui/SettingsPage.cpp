#include "../../include/gui/SettingsPage.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/CustomRendererWidget.hpp"
#include "../../include/gui/Image.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Theme.hpp"
#include "../../include/GsrInfo.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Translation.hpp"

#include <cctype>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>

#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <charconv>

namespace gsr {
    static const char *custom_app_audio_tag = "[custom]";

    enum class AudioTrackType {
        DEVICE,
        APPLICATION,
        APPLICATION_CUSTOM
    };

    static const char* settings_page_type_to_title_text(SettingsPage::Type type) {
        switch(type) {
            case SettingsPage::Type::REPLAY: return TR("Instant Replay");
            case SettingsPage::Type::RECORD: return TR("Record");
            case SettingsPage::Type::STREAM: return TR("Livestream");
        }
        return "";
    }

    template <typename T>
    static T sv_to_int(std::string_view str) {
        T result = 0;
        std::from_chars(str.data(), str.data() + str.size(), result);
        return result;
    }

    static bool supports_vulkan_video_encoding(const SupportedVideoCodecs &supported_video_codecs) {
        return supported_video_codecs.h264_vulkan
            || supported_video_codecs.hevc_vulkan
            || supported_video_codecs.hevc_hdr_vulkan
            || supported_video_codecs.hevc_10bit_vulkan
            || supported_video_codecs.av1_vulkan
            || supported_video_codecs.av1_hdr_vulkan
            || supported_video_codecs.av1_10bit_vulkan;
    }

    static std::optional<std::string> webcam_get_name(std::string_view webcam_path) {
        std::optional<std::string> v4l2_device_name;

        if(!starts_with(webcam_path, "/dev/video"))
            return v4l2_device_name;

        const std::string_view path_filename = webcam_path.substr(5);

        char v4l2_device_name_path[64];
        snprintf(v4l2_device_name_path, sizeof(v4l2_device_name_path), "/sys/class/video4linux/%.*s/name", (int)path_filename.size(), path_filename.data());

        const int fd = open(v4l2_device_name_path, O_RDONLY);
        if(fd < 0)
            return v4l2_device_name;

        char webcam_name_buf[256];
        const ssize_t bytes_read = read(fd, webcam_name_buf, sizeof(webcam_name_buf));
        if(bytes_read > 0) {
            v4l2_device_name = std::string(webcam_name_buf, bytes_read);
            if(v4l2_device_name->back() == '\n')
                v4l2_device_name->pop_back();
            v4l2_device_name.value() += " (" + std::string(webcam_path) + ")";
        }

        close(fd);
        return v4l2_device_name;
    }

    SettingsPage::SettingsPage(Type type, const GsrInfo *gsr_info, Config &config, PageStack *page_stack, bool supports_window_title) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        type(type),
        config(config),
        gsr_info(gsr_info),
        page_stack(page_stack),
        supports_window_title(supports_window_title)
    {
        audio_devices = get_audio_devices();
        application_audio = get_application_audio();
        capture_options = get_supported_capture_options(*gsr_info);

        auto content_page = std::make_unique<GsrPage>(settings_page_type_to_title_text(type), TR("Settings"));
        content_page->add_button(TR("Back"), "back", get_color_theme().page_bg_color);
        content_page->on_click = [page_stack](const std::string &id) {
            if(id == "back")
                page_stack->pop();
        };
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
        add_page_specific_widgets();
        load();
    }

    std::unique_ptr<RadioButton> SettingsPage::create_view_radio_button() {
        auto view_radio_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        view_radio_button->add_item(TR("Simple view"), "simple");
        view_radio_button->add_item(TR("Advanced view"), "advanced");
        view_radio_button->set_horizontal_alignment(Widget::Alignment::CENTER);
        view_radio_button_ptr = view_radio_button.get();
        return view_radio_button;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_record_area_box() {
        auto record_area_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        // TODO: Show options not supported but disable them
        if(capture_options.window)
            record_area_box->add_item(TR("Window"), "window");
        if(capture_options.focused)
            record_area_box->add_item(TR("Follow focused window"), "focused");
        if(capture_options.region)
            record_area_box->add_item(TR("Region"), "region");
        if(!capture_options.monitors.empty())
            record_area_box->add_item(TR("Focused monitor"), "focused_monitor");
        for(const auto &monitor : capture_options.monitors) {
            char name[256];
            snprintf(name, sizeof(name), TR("Monitor %s (%dx%d)"), monitor.name.c_str(), monitor.size.x, monitor.size.y);
            record_area_box->add_item(name, monitor.name);
        }
        if(capture_options.portal)
            record_area_box->add_item(TR("Desktop portal"), "portal");
        record_area_box_ptr = record_area_box.get();
        return record_area_box;
    }

    std::unique_ptr<Widget> SettingsPage::create_record_area() {
        auto record_area_list = std::make_unique<List>(List::Orientation::VERTICAL);
        record_area_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Capture source:"), get_color_theme().text_color));
        record_area_list->add_widget(create_record_area_box());
        return record_area_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_area_width_entry() {
        auto area_width_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1920", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        area_width_entry->set_number_mode(true, 1, 1 << 15);
        area_width_entry_ptr = area_width_entry.get();
        return area_width_entry;
    }

    std::unique_ptr<Entry> SettingsPage::create_area_height_entry() {
        auto area_height_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1080", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        area_height_entry->set_number_mode(true, 1, 1 << 15);
        area_height_entry_ptr = area_height_entry.get();
        return area_height_entry;
    }

    std::unique_ptr<List> SettingsPage::create_area_size() {
        auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        area_size_params_list->add_widget(create_area_width_entry());
        area_size_params_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), "x", get_color_theme().text_color));
        area_size_params_list->add_widget(create_area_height_entry());
        return area_size_params_list;
    }

    std::unique_ptr<List> SettingsPage::create_area_size_section() {
        auto area_size_list = std::make_unique<List>(List::Orientation::VERTICAL);
        area_size_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Area size:"), get_color_theme().text_color));
        area_size_list->add_widget(create_area_size());
        area_size_list_ptr = area_size_list.get();
        return area_size_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_video_width_entry() {
        auto video_width_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1920", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        video_width_entry->set_number_mode(true, 1, 1 << 15);
        video_width_entry_ptr = video_width_entry.get();
        return video_width_entry;
    }

    std::unique_ptr<Entry> SettingsPage::create_video_height_entry() {
        auto video_height_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1080", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        video_height_entry->set_number_mode(true, 1, 1 << 15);
        video_height_entry_ptr = video_height_entry.get();
        return video_height_entry;
    }

    std::unique_ptr<List> SettingsPage::create_video_resolution() {
        auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        area_size_params_list->add_widget(create_video_width_entry());
        area_size_params_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), "x", get_color_theme().text_color));
        area_size_params_list->add_widget(create_video_height_entry());
        return area_size_params_list;
    }

    std::unique_ptr<List> SettingsPage::create_video_resolution_section() {
        auto video_resolution_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_resolution_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video resolution limit:"), get_color_theme().text_color));
        video_resolution_list->add_widget(create_video_resolution());
        video_resolution_list_ptr = video_resolution_list.get();
        return video_resolution_list;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_restore_portal_session_checkbox() {
        auto restore_portal_session_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Restore portal session"));
        restore_portal_session_checkbox->set_checked(true);
        restore_portal_session_checkbox_ptr = restore_portal_session_checkbox.get();
        return restore_portal_session_checkbox;
    }

    std::unique_ptr<List> SettingsPage::create_restore_portal_session_section() {
        auto restore_portal_session_list = std::make_unique<List>(List::Orientation::VERTICAL);
        restore_portal_session_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), " ", get_color_theme().text_color));
        restore_portal_session_list->add_widget(create_restore_portal_session_checkbox());
        restore_portal_session_list_ptr = restore_portal_session_list.get();
        return restore_portal_session_list;
    }

    std::unique_ptr<Widget> SettingsPage::create_change_video_resolution_section() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Change video resolution"));
        change_video_resolution_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_capture_target_section() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);

        auto capture_target_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        capture_target_list->add_widget(create_record_area());
        capture_target_list->add_widget(create_area_size_section());
        capture_target_list->add_widget(create_video_resolution_section());
        capture_target_list->add_widget(create_restore_portal_session_section());

        ll->add_widget(std::move(capture_target_list));
        ll->add_widget(create_change_video_resolution_section());

        return std::make_unique<Subsection>(TR("Capture"), std::move(ll), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<List> SettingsPage::create_webcam_sources() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);
        ll->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Webcam source:"), get_color_theme().text_color));

        auto combobox = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        combobox->add_item(TR("None"), "");
        for(const GsrCamera &camera : capture_options.cameras) {
            const std::string camera_name = webcam_get_name(camera.path).value_or(camera.path);
            combobox->add_item(camera_name, camera.path);
        }
        webcam_sources_box_ptr = combobox.get();

        webcam_sources_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            selected_camera = std::nullopt;
            selected_camera_setup = std::nullopt;
            webcam_video_format_box_ptr->clear_items();
            if(id == "") {
                webcam_body_list_ptr->set_visible(false);
                return;
            }

            auto it = std::find_if(capture_options.cameras.begin(), capture_options.cameras.end(), [&](const GsrCamera &camera) {
                return camera.path == id;
            });
            if(it == capture_options.cameras.end())
                return;

            webcam_body_list_ptr->set_visible(true);
            webcam_video_format_box_ptr->add_item(TR("Auto (recommended)"), "auto");

            if(!it->yuyv_setups.empty())
                webcam_video_format_box_ptr->add_item(TR("YUYV (Fastest)"), "yuyv");

            if(!it->mjpeg_setups.empty())
                webcam_video_format_box_ptr->add_item(TR("Motion-JPEG (Slower)"), "mjpeg");

            const std::string prev_webcam_video_format = get_current_record_options().webcam_video_format;
            webcam_video_format_box_ptr->set_selected_item(webcam_video_format_box_ptr->get_selected_id());
            webcam_video_format_box_ptr->set_selected_item(prev_webcam_video_format);
            selected_camera = *it;
        };

        ll->add_widget(std::move(combobox));
        return ll;
    }

    std::unique_ptr<List> SettingsPage::create_webcam_video_setups() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);
        ll->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video setup:"), get_color_theme().text_color));

        auto combobox = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        webcam_video_setup_box_ptr = combobox.get();

        webcam_video_setup_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            int camera_width = 0;
            int camera_height = 0;
            int camera_fps = 0;

            char id_str[256];
            snprintf(id_str, sizeof(id_str), "%.*s", (int)id.size(), id.data());
            sscanf(id_str, "%dx%d@%dhz", &camera_width, &camera_height, &camera_fps);

            RecordOptions &current_record_options = get_current_record_options();
            current_record_options.webcam_camera_width = camera_width;
            current_record_options.webcam_camera_height = camera_height;
            current_record_options.webcam_camera_fps = camera_fps;

            selected_camera_setup = GsrCameraSetup{mgl::vec2i{camera_width, camera_height}, camera_fps};
        };

        ll->add_widget(std::move(combobox));
        return ll;
    }

    static std::vector<GsrCameraSetup> sort_camera_setup(const std::vector<GsrCameraSetup> &setups) {
        auto result = setups;
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            const uint64_t area_a = (uint64_t)a.resolution.x * (uint64_t)a.resolution.y;
            const uint64_t area_b = (uint64_t)b.resolution.x * (uint64_t)b.resolution.y;
            if(area_a != area_b)
                return area_a > area_b;
            return a.fps > b.fps;
        });
        return result;
    }

    std::unique_ptr<List> SettingsPage::create_webcam_video_format() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);
        ll->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video format:"), get_color_theme().text_color));

        auto combobox = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        webcam_video_format_box_ptr = combobox.get();

        webcam_video_format_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            get_current_record_options().webcam_video_format = id;

            auto it = std::find_if(capture_options.cameras.begin(), capture_options.cameras.end(), [&](const GsrCamera &camera) {
                return camera.path == webcam_sources_box_ptr->get_selected_id();
            });
            if(it == capture_options.cameras.end())
                return;

            webcam_video_setup_box_ptr->clear_items();
            if(id == "yuyv") {
                const auto setups = sort_camera_setup(it->yuyv_setups);
                for(const auto &setup : setups) {
                    char setup_str[256];
                    snprintf(setup_str, sizeof(setup_str), "%dx%d@%dhz", setup.resolution.x, setup.resolution.y, setup.fps);
                    webcam_video_setup_box_ptr->add_item(setup_str, setup_str, false);
                }
            } else if(id == "mjpeg") {
                const auto setups = sort_camera_setup(it->mjpeg_setups);
                for(const auto &setup : setups) {
                    char setup_str[256];
                    snprintf(setup_str, sizeof(setup_str), "%dx%d@%dhz", setup.resolution.x, setup.resolution.y, setup.fps);
                    webcam_video_setup_box_ptr->add_item(setup_str, setup_str, false);
                }
            } else if(id == "auto") {
                auto setups = it->yuyv_setups;
                setups.insert(setups.end(), it->mjpeg_setups.begin(), it->mjpeg_setups.end());
                setups = sort_camera_setup(setups);
                setups.erase(std::unique(setups.begin(), setups.end(), [](const auto &a, const auto &b) {
                    return a.resolution.x == b.resolution.x && a.resolution.y == b.resolution.y && a.fps == b.fps;
                }), setups.end());

                for(const auto &setup : setups) {
                    char setup_str[256];
                    snprintf(setup_str, sizeof(setup_str), "%dx%d@%dhz", setup.resolution.x, setup.resolution.y, setup.fps);
                    webcam_video_setup_box_ptr->add_item(setup_str, setup_str, false);
                }
            }

            const RecordOptions &current_record_options = get_current_record_options();
            char webcam_setup_str[256];
            snprintf(webcam_setup_str, sizeof(webcam_setup_str), "%dx%d@%dhz", current_record_options.webcam_camera_width, current_record_options.webcam_camera_height, current_record_options.webcam_camera_fps);
            webcam_video_setup_box_ptr->set_selected_item(webcam_video_setup_box_ptr->get_selected_id());
            webcam_video_setup_box_ptr->set_selected_item(webcam_setup_str);
        };

        ll->add_widget(std::move(combobox));
        return ll;
    }

    std::unique_ptr<List> SettingsPage::create_webcam_video_setup_list() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        list->add_widget(create_webcam_video_format());
        list->add_widget(create_webcam_video_setups());
        return list;
    }

    std::unique_ptr<Widget> SettingsPage::create_webcam_location_widget() {
        const float camera_screen_width = std::min(400.0f, (float)settings_scrollable_page_ptr->get_inner_size().x * 0.90f);
        camera_screen_size = mgl::vec2f(camera_screen_width, camera_screen_width * 0.5625);

        const float screen_border = 2.0f;
        const mgl::vec2f screen_border_size(screen_border, screen_border);
        screen_inner_size = mgl::vec2f(camera_screen_size - screen_border_size*2.0f);

        const mgl::vec2f bounding_box_size(30.0f, 30.0f);

        auto camera_location_widget = std::make_unique<CustomRendererWidget>(camera_screen_size);
        camera_location_widget->draw_handler = [this, screen_border_size, screen_border](mgl::Window &window, mgl::vec2f pos, mgl::vec2f size) {
            if(!selected_camera.has_value() || !selected_camera_setup.has_value())
                return;

            pos = pos.floor();
            size = size.floor();
            const mgl::vec2i mouse_pos = window.get_mouse_position();
            const mgl::vec2f webcam_box_min_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), screen_inner_size * 0.2f);

            if(moving_webcam_box) {
                webcam_box_pos = mouse_pos.to_vec2f() - screen_border_size - webcam_box_grab_offset - pos;
            } else if(webcam_resize_corner == WebcamBoxResizeCorner::BOTTOM_RIGHT) {
                const mgl::vec2f mouse_diff = mouse_pos.to_vec2f() - webcam_resize_start_pos;
                webcam_box_size = webcam_box_size_resize_start + mouse_diff;
            }

            webcam_box_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), webcam_box_size);

            if(webcam_box_pos.x < 0.0f)
                webcam_box_pos.x = 0.0f;
            else if(webcam_box_pos.x + webcam_box_size.x > screen_inner_size.x)
                webcam_box_pos.x = screen_inner_size.x - webcam_box_size.x;

            if(webcam_box_pos.y < 0.0f)
                webcam_box_pos.y = 0.0f;
            else if(webcam_box_pos.y + webcam_box_size.y > screen_inner_size.y)
                webcam_box_pos.y = screen_inner_size.y - webcam_box_size.y;

            if(webcam_box_size.x < webcam_box_min_size.x)
                webcam_box_size.x = webcam_box_min_size.x;
            else if(webcam_box_pos.x + webcam_box_size.x > screen_inner_size.x)
                webcam_box_size.x = screen_inner_size.x - webcam_box_pos.x;

            //webcam_box_size = clamp_keep_aspect_ratio(selected_camera->size.to_vec2f(), webcam_box_size);

            if(webcam_box_size.y < webcam_box_min_size.y)
                webcam_box_size.y = webcam_box_min_size.y;
            else if(webcam_box_pos.y + webcam_box_size.y > screen_inner_size.y)
                webcam_box_size.y = screen_inner_size.y - webcam_box_pos.y;

            webcam_box_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), webcam_box_size);

            {
                draw_rectangle_outline(window, pos, size, mgl::Color(255, 0, 0, 255), screen_border);
                mgl::Text screen_text(TR("Screen"), get_theme().camera_setup_font_desc.c_str());
                screen_text.set_position((pos + size * 0.5f - screen_text.get_bounds().size * 0.5f).floor());
                window.draw(screen_text);
            }

            {
                webcam_box_drawn_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), webcam_box_size);
                webcam_box_drawn_pos = (pos + screen_border_size + webcam_box_pos).floor();

                draw_rectangle_outline(window, webcam_box_drawn_pos, webcam_box_drawn_size, mgl::Color(0, 255, 0, 255), screen_border);

                // mgl::Rectangle resize_area(webcam_box_drawn_pos + webcam_box_drawn_size - bounding_box_size*0.5f - screen_border_size*0.5f, bounding_box_size);
                // resize_area.set_color(mgl::Color(0, 0, 255, 255));
                // window.draw(resize_area);

                mgl::Text webcam_text(TR("Webcam"), get_theme().camera_setup_font_desc.c_str());
                webcam_text.set_position((webcam_box_drawn_pos + webcam_box_drawn_size * 0.5f - webcam_text.get_bounds().size * 0.5f).floor());
                window.draw(webcam_text);
            }
        };

        camera_location_widget->event_handler = [this, screen_border_size, bounding_box_size](mgl::Event &event, mgl::Window&, mgl::vec2f, mgl::vec2f) {
            switch(event.type) {
                case mgl::Event::MouseButtonPressed: {
                    if(event.mouse_button.button == mgl::Mouse::Left && webcam_resize_corner == WebcamBoxResizeCorner::NONE) {
                        const mgl::vec2f mouse_button_pos(event.mouse_button.x, event.mouse_button.y);
                        if(mgl::FloatRect(webcam_box_drawn_pos, webcam_box_drawn_size).contains(mouse_button_pos)) {
                            moving_webcam_box = true;
                            webcam_box_grab_offset = mouse_button_pos - webcam_box_drawn_pos;
                        } else {
                            moving_webcam_box = false;
                        }
                    } else if(event.mouse_button.button == mgl::Mouse::Right && !moving_webcam_box) {
                        const mgl::vec2f mouse_button_pos(event.mouse_button.x, event.mouse_button.y);
                        webcam_resize_start_pos = mouse_button_pos;
                        webcam_box_pos_resize_start = webcam_box_pos;
                        webcam_box_size_resize_start = webcam_box_size;
                        webcam_box_grab_offset = mouse_button_pos - webcam_box_drawn_pos;

                        /*if(mgl::FloatRect(webcam_box_drawn_pos - bounding_box_size*0.5f, bounding_box_size).contains(mouse_button_pos)) {
                            webcam_resize_corner = WebcamBoxResizeCorner::TOP_LEFT;
                            fprintf(stderr, "top left\n");
                        } else if(mgl::FloatRect(webcam_box_drawn_pos + mgl::vec2f(webcam_box_drawn_size.x, 0.0f) - bounding_box_size*0.5f, bounding_box_size).contains(mouse_button_pos)) {
                            webcam_resize_corner = WebcamBoxResizeCorner::TOP_RIGHT;
                            fprintf(stderr, "top right\n");
                        } else if(mgl::FloatRect(webcam_box_drawn_pos + mgl::vec2f(0.0f, webcam_box_drawn_size.y) - bounding_box_size*0.5f, bounding_box_size).contains(mouse_button_pos)) {
                            webcam_resize_corner = WebcamBoxResizeCorner::BOTTOM_LEFT;
                            fprintf(stderr, "bottom left\n");
                        } else */if(mgl::FloatRect(webcam_box_drawn_pos + webcam_box_drawn_size - bounding_box_size*0.5f - screen_border_size*0.5f, bounding_box_size).contains(mouse_button_pos)) {
                            webcam_resize_corner = WebcamBoxResizeCorner::BOTTOM_RIGHT;
                        } else {
                            webcam_resize_corner = WebcamBoxResizeCorner::NONE;
                        }
                    }
                    break;
                }
                case mgl::Event::MouseButtonReleased: {
                    if(event.mouse_button.button == mgl::Mouse::Left && webcam_resize_corner == WebcamBoxResizeCorner::NONE) {
                        moving_webcam_box = false;
                    } else if(event.mouse_button.button == mgl::Mouse::Right && !moving_webcam_box) {
                        webcam_resize_corner = WebcamBoxResizeCorner::NONE;
                    }
                    break;
                }
                default: {
                    break;
                }
            }
            return true;
        };

        return camera_location_widget;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_flip_camera_checkbox() {
        auto flip_camera_horizontally_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Flip camera horizontally"));
        flip_camera_horizontally_checkbox_ptr = flip_camera_horizontally_checkbox.get();
        return flip_camera_horizontally_checkbox;
    }

    std::unique_ptr<List> SettingsPage::create_webcam_body() {
        auto body_list = std::make_unique<List>(List::Orientation::VERTICAL);
        webcam_body_list_ptr = body_list.get();
        body_list->set_visible(false);
        body_list->add_widget(create_webcam_location_widget());
        body_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("* Right click in the bottom right corner to resize the webcam"), get_color_theme().text_color));
        body_list->add_widget(create_flip_camera_checkbox());
        body_list->add_widget(create_webcam_video_setup_list());
        return body_list;
    }

    std::unique_ptr<Widget> SettingsPage::create_webcam_section() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);
        ll->add_widget(create_webcam_sources());
        ll->add_widget(create_webcam_body());
        return std::make_unique<Subsection>(TR("Webcam"), std::move(ll), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    static bool audio_device_is_output(const std::string &audio_device_id) {
        return audio_device_id == "default_output" || ends_with(audio_device_id, ".monitor");
    }

    std::unique_ptr<ComboBox> SettingsPage::create_audio_device_selection_combobox(AudioDeviceType device_type) {
        auto audio_device_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        for(const auto &audio_device : audio_devices) {
            const bool device_is_output = audio_device_is_output(audio_device.name);
            if((device_type == AudioDeviceType::OUTPUT && device_is_output) || (device_type == AudioDeviceType::INPUT && !device_is_output)) {
                std::string description = audio_device.description;
                if(starts_with(description, "Monitor of "))
                    description.erase(0, 11);
                audio_device_box->add_item(description, audio_device.name);
            }
        }
        return audio_device_box;
    }

    static void set_application_audio_options_visible(Subsection *audio_track_subsection, bool visible, const GsrInfo &gsr_info) {
        if(!gsr_info.system_info.supports_app_audio)
            visible = false;

        List *audio_track_items_list = dynamic_cast<List*>(audio_track_subsection->get_inner_widget());

        List *buttons_list = dynamic_cast<List*>(audio_track_items_list->get_child_widget_by_index(1));
        Button *add_application_audio_button = dynamic_cast<Button*>(buttons_list->get_child_widget_by_index(2));
        add_application_audio_button->set_visible(visible);

        CheckBox *invert_app_audio_checkbox = dynamic_cast<CheckBox*>(audio_track_items_list->get_child_widget_by_index(3));
        invert_app_audio_checkbox->set_visible(visible);

        List *custom_audio_track_name_list_ptr = dynamic_cast<List*>(audio_track_items_list->get_child_widget_by_index(5));
        custom_audio_track_name_list_ptr->set_visible(visible);
    }

    static void set_application_audio_options_visible(List *audio_track_section_list_ptr, bool visible, const GsrInfo &gsr_info) {
        audio_track_section_list_ptr->for_each_child_widget([visible, &gsr_info](std::unique_ptr<Widget> &widget) {
            Subsection *audio_track_subsection = dynamic_cast<Subsection*>(widget.get());
            set_application_audio_options_visible(audio_track_subsection, visible, gsr_info);
            return true;
        });
    }

    std::unique_ptr<Button> SettingsPage::create_remove_audio_device_button(List *audio_input_list_ptr, List *audio_device_list_ptr) {
        auto remove_audio_track_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 0));
        remove_audio_track_button->set_icon(&get_theme().trash_texture);
        remove_audio_track_button->set_icon_padding_scale(0.75f);
        remove_audio_track_button->on_click = [this, audio_input_list_ptr, audio_device_list_ptr]() {
            audio_input_list_ptr->remove_widget(audio_device_list_ptr);
            update_application_audio_warning_visibility();
        };
        return remove_audio_track_button;
    }

    std::unique_ptr<List> SettingsPage::create_audio_device(AudioDeviceType device_type, List *audio_input_list_ptr) {
        auto audio_device_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        audio_device_list->userdata = (void*)(uintptr_t)AudioTrackType::DEVICE;
        audio_device_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), device_type == AudioDeviceType::OUTPUT ? TR("Output device:") : TR("Input device:   "), get_color_theme().text_color));
        audio_device_list->add_widget(create_audio_device_selection_combobox(device_type));
        audio_device_list->add_widget(create_remove_audio_device_button(audio_input_list_ptr, audio_device_list.get()));
        return audio_device_list;
    }

    std::unique_ptr<Button> SettingsPage::create_add_audio_track_button() {
        auto button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Add audio track"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        button->on_click = [this]() {
            audio_track_section_list_ptr->add_widget(create_audio_track_section(audio_section_ptr));
        };
        button->set_visible(type != Type::STREAM);
        return button;
    }

    void SettingsPage::update_application_audio_warning_visibility() {
        audio_track_section_list_ptr->for_each_child_widget([](std::unique_ptr<Widget> &child_widget) {
            Subsection *audio_subsection = dynamic_cast<Subsection*>(child_widget.get());
            List *audio_track_section_items_list_ptr = dynamic_cast<List*>(audio_subsection->get_inner_widget());
            List *audio_input_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(2));
            CheckBox *application_audio_invert_checkbox_ptr = dynamic_cast<CheckBox*>(audio_track_section_items_list_ptr->get_child_widget_by_index(3));
            List *application_audio_warning_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(4));

            int num_output_devices = 0;
            int num_application_audio = 0;

            audio_input_list_ptr->for_each_child_widget([&num_output_devices, &num_application_audio](std::unique_ptr<Widget> &child_widget){
                List *audio_track_line = dynamic_cast<List*>(child_widget.get());
                const AudioTrackType audio_track_type = (AudioTrackType)(uintptr_t)audio_track_line->userdata;
                switch(audio_track_type) {
                    case AudioTrackType::DEVICE: {
                        Label *label = dynamic_cast<Label*>(audio_track_line->get_child_widget_by_index(0));
                        const bool is_output_device = starts_with(label->get_text(), TR("Output device"));
                        if(is_output_device)
                            num_output_devices++;
                        break;
                    }
                    case AudioTrackType::APPLICATION:
                    case AudioTrackType::APPLICATION_CUSTOM: {
                        num_application_audio++;
                        break;
                    }
                }
                return true;
            });

            application_audio_warning_list_ptr->set_visible(num_output_devices > 0 && (num_application_audio > 0 || application_audio_invert_checkbox_ptr->is_checked()));
            return true;
        });
    }

    std::unique_ptr<Button> SettingsPage::create_add_audio_output_device_button(List *audio_input_list_ptr) {
        auto button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Add output device"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        button->on_click = [this, audio_input_list_ptr]() {
            audio_devices = get_audio_devices();
            audio_input_list_ptr->add_widget(create_audio_device(AudioDeviceType::OUTPUT, audio_input_list_ptr));
            update_application_audio_warning_visibility();
        };
        return button;
    }

    std::unique_ptr<Button> SettingsPage::create_add_audio_input_device_button(List *audio_input_list_ptr) {
        auto button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Add input device"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        button->on_click = [this, audio_input_list_ptr]() {
            audio_devices = get_audio_devices();
            audio_input_list_ptr->add_widget(create_audio_device(AudioDeviceType::INPUT, audio_input_list_ptr));
        };
        return button;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_application_audio_selection_combobox(List *application_audio_row) {
        auto audio_device_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        ComboBox *audio_device_box_ptr = audio_device_box.get();
        for(const auto &app_audio : application_audio) {
            audio_device_box->add_item(app_audio, app_audio);
        }
        audio_device_box->add_item(TR("Custom..."), custom_app_audio_tag);

        audio_device_box->on_selection_changed = [application_audio_row, audio_device_box_ptr](std::string_view, std::string_view id) {
            if(id == custom_app_audio_tag) {
                application_audio_row->userdata = (void*)(uintptr_t)AudioTrackType::APPLICATION_CUSTOM;
                auto custom_app_audio_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", (int)(2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 10.0f));
                application_audio_row->replace_widget(audio_device_box_ptr, std::move(custom_app_audio_entry));
            }
        };

        return audio_device_box;
    }

    std::unique_ptr<List> SettingsPage::create_application_audio(List *audio_input_list_ptr) {
        auto application_audio_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        application_audio_list->userdata = (void*)(uintptr_t)AudioTrackType::APPLICATION;
        application_audio_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Application:     "), get_color_theme().text_color));
        application_audio_list->add_widget(create_application_audio_selection_combobox(application_audio_list.get()));
        application_audio_list->add_widget(create_remove_audio_device_button(audio_input_list_ptr, application_audio_list.get()));
        return application_audio_list;
    }

    std::unique_ptr<List> SettingsPage::create_custom_application_audio(List *audio_input_list_ptr) {
        auto application_audio_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        application_audio_list->userdata = (void*)(uintptr_t)AudioTrackType::APPLICATION_CUSTOM;
        application_audio_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Application:     "), get_color_theme().text_color));
        application_audio_list->add_widget(std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", (int)(2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 10.0f)));
        application_audio_list->add_widget(create_remove_audio_device_button(audio_input_list_ptr, application_audio_list.get()));
        return application_audio_list;
    }

    std::unique_ptr<Button> SettingsPage::create_add_application_audio_button(List *audio_input_list_ptr) {
        auto add_audio_track_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), TR("Add application audio"), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        add_audio_track_button->on_click = [this, audio_input_list_ptr]() {
            application_audio = get_application_audio();
            if(application_audio.empty())
                audio_input_list_ptr->add_widget(create_custom_application_audio(audio_input_list_ptr));
            else
                audio_input_list_ptr->add_widget(create_application_audio(audio_input_list_ptr));

            update_application_audio_warning_visibility();
        };
        return add_audio_track_button;
    }

    std::unique_ptr<List> SettingsPage::create_add_audio_buttons(List *audio_input_list_ptr) {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->add_widget(create_add_audio_output_device_button(audio_input_list_ptr));
        list->add_widget(create_add_audio_input_device_button(audio_input_list_ptr));
        list->add_widget(create_add_application_audio_button(audio_input_list_ptr));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_audio_input_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        //list->add_widget(create_audio_device(list.get())); // Add default_output by default
        return list;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_application_audio_invert_checkbox() {
        auto application_audio_invert_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Record audio from all applications except the selected ones"));
        application_audio_invert_checkbox->set_checked(false);
        application_audio_invert_checkbox->on_changed = [this](bool) {
            update_application_audio_warning_visibility();
        };
        return application_audio_invert_checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_application_audio_warning() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        list->set_spacing(0.005f);
        list->set_visible(false);

        const int font_character_size = 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str());
        list->add_widget(std::make_unique<Image>(&get_theme().warning_texture, mgl::vec2f(font_character_size, font_character_size), Image::ScaleBehavior::SCALE));
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Recording output devices and application audio may record all output audio, which is likely not what you want to do. Remove the output devices."), get_color_theme().text_color);
        label->set_wrap_width(label->get_font_size() * 55);
        list->add_widget(std::move(label));

        return list;
    }

    static void update_audio_track_titles(List *audio_track_section_list_ptr) {
        int index = 0;
        audio_track_section_list_ptr->for_each_child_widget([&index](std::unique_ptr<Widget> &widget) {
            char audio_track_name[32];
            snprintf(audio_track_name, sizeof(audio_track_name), TR("Audio track #%d"), 1 + index);
            ++index;

            Subsection *subsection = dynamic_cast<Subsection*>(widget.get());
            List *subesection_items = dynamic_cast<List*>(subsection->get_inner_widget());
            Label *audio_track_title = dynamic_cast<Label*>(dynamic_cast<List*>(subesection_items->get_child_widget_by_index(0))->get_child_widget_by_index(0));
            audio_track_title->set_text(audio_track_name);
            return true;
        });
    }

    std::unique_ptr<List> SettingsPage::create_audio_track_title_and_remove(Subsection *audio_track_subsection, const char *title) {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->add_widget(std::make_unique<Label>(get_theme().title_font_desc.c_str(), title, get_color_theme().text_color));

        auto remove_track_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 0));
        remove_track_button->set_icon(&get_theme().trash_texture);
        remove_track_button->set_icon_padding_scale(0.75f);
        remove_track_button->on_click = [this, audio_track_subsection]() {
            audio_track_section_list_ptr->remove_widget(audio_track_subsection);
            update_audio_track_titles(audio_track_section_list_ptr);
        };
        list->add_widget(std::move(remove_track_button));
        list->set_visible(type != Type::STREAM);
        return list;
    }

    std::unique_ptr<Subsection> SettingsPage::create_audio_track_section(Widget *parent_widget) {
        char audio_track_name[32];
        int child_index = 1 + (int)audio_track_section_list_ptr->get_num_children();
        snprintf(audio_track_name, sizeof(audio_track_name), TR("Audio track #%d"), child_index);

        auto audio_input_section = create_audio_input_section();
        List *audio_input_section_ptr = audio_input_section.get();

        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        List *list_ptr = list.get();
        auto subsection = std::make_unique<Subsection>("", std::move(std::move(list)), mgl::vec2f(parent_widget->get_inner_size().x, 0.0f));
        subsection->set_bg_color(mgl::Color(35, 40, 44));

        list_ptr->add_widget(create_audio_track_title_and_remove(subsection.get(), audio_track_name));
        list_ptr->add_widget(create_add_audio_buttons(audio_input_section_ptr));
        list_ptr->add_widget(std::move(audio_input_section));
        list_ptr->add_widget(create_application_audio_invert_checkbox());
        list_ptr->add_widget(create_application_audio_warning());
        list_ptr->add_widget(create_audio_track_custom_name_input());

        set_application_audio_options_visible(subsection.get(), view_radio_button_ptr->get_selected_id() == "advanced", *gsr_info);
        return subsection;
    }

    std::unique_ptr<List> SettingsPage::create_audio_track_custom_name_input() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Track title:"), get_color_theme().text_color));

        auto track_name_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", (int)(2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20));
        track_name_entry->on_changed = [entry = track_name_entry.get()](std::string_view text) {
            std::string sanitized;
            bool modified = false;
            sanitized.reserve(text.size());
            for(char c : text){
                if(c == '|' || c == '\n')
                    modified = true;
                else
                    sanitized.push_back(c);
            }
            if(modified)
                entry->set_text(sanitized);
        };
        list->add_widget(std::move(track_name_entry));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_audio_track_section_list() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_track_section_list_ptr = list.get();
        return list;
    }

    std::unique_ptr<Widget> SettingsPage::create_audio_section() {
        auto audio_device_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        List *audio_device_section_list_ptr = audio_device_section_list.get();

        auto subsection = std::make_unique<Subsection>(TR("Audio"), std::move(audio_device_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
        audio_section_ptr = subsection.get();
        audio_device_section_list_ptr->add_widget(create_add_audio_track_button());
        audio_device_section_list_ptr->add_widget(create_audio_track_section_list());
        audio_device_section_list_ptr->add_widget(create_audio_codec());
        return subsection;
    }

    std::unique_ptr<List> SettingsPage::create_video_quality_box() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video quality:"), get_color_theme().text_color));

        auto video_quality_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->add_item(TR("Constant bitrate (Recommended)"), "custom");
        else
            video_quality_box->add_item(TR("Constant bitrate"), "custom");
        video_quality_box->add_item(TR("Medium"), "medium");
        video_quality_box->add_item(TR("High"), "high");
        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->add_item(TR("Very high"), "very_high");
        else
            video_quality_box->add_item(TR("Very high (Recommended)"), "very_high");
        video_quality_box->add_item(TR("Ultra"), "ultra");

        if(type == Type::REPLAY || type == Type::STREAM)
            video_quality_box->set_selected_item("custom");
        else
            video_quality_box->set_selected_item("very_high");

        video_quality_box_ptr = video_quality_box.get();
        list->add_widget(std::move(video_quality_box));

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_video_bitrate_entry() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        auto video_bitrate_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "8000", (int)(2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 5));
        video_bitrate_entry->set_number_mode(true, 1, 500000);
        video_bitrate_entry_ptr = video_bitrate_entry.get();
        list->add_widget(std::move(video_bitrate_entry));

        if(type == Type::STREAM) {
            auto size_mb_label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "", get_color_theme().text_color);
            Label *size_mb_label_ptr = size_mb_label.get();
            list->add_widget(std::move(size_mb_label));

            video_bitrate_entry_ptr->on_changed = [size_mb_label_ptr](std::string_view text) {
                const double video_bitrate_mbits_per_seconds = (double)sv_to_int<int64_t>(text) / 1024.0;
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.2fMbps", video_bitrate_mbits_per_seconds);
                size_mb_label_ptr->set_text(buffer);
            };
        }

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_video_bitrate() {
        auto video_bitrate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_bitrate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video bitrate (Kbps):"), get_color_theme().text_color));
        video_bitrate_list->add_widget(create_video_bitrate_entry());
        video_bitrate_list_ptr = video_bitrate_list.get();
        return video_bitrate_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_color_range_box() {
        auto color_range_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        color_range_box->add_item(TR("Limited"), "limited");
        color_range_box->add_item(TR("Full"), "full");
        color_range_box_ptr = color_range_box.get();
        return color_range_box;
    }

    std::unique_ptr<List> SettingsPage::create_color_range() {
        auto color_range_list = std::make_unique<List>(List::Orientation::VERTICAL);
        color_range_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Color range:"), get_color_theme().text_color));
        color_range_list->add_widget(create_color_range_box());
        color_range_list_ptr = color_range_list.get();
        return color_range_list;
    }

    std::unique_ptr<List> SettingsPage::create_video_quality_section() {
        auto quality_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        quality_list->add_widget(create_video_quality_box());
        quality_list->add_widget(create_video_bitrate());
        quality_list->add_widget(create_color_range());
        return quality_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_video_codec_box() {
        auto video_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        // TODO: Show options not supported but disable them.
        // TODO: Show error if no encoders are supported.
        // TODO: Show warning (once) if only software encoder is available.
        video_codec_box->add_item(TR("Auto (Recommended)"), "auto");
        if(gsr_info->supported_video_codecs.h264)
            video_codec_box->add_item(TR("H264"), "h264");
        if(gsr_info->supported_video_codecs.hevc)
            video_codec_box->add_item(TR("HEVC"), "hevc");
        if(gsr_info->supported_video_codecs.hevc_10bit)
            video_codec_box->add_item(TR("HEVC (10 bit, reduces banding)"), "hevc_10bit");
        if(gsr_info->supported_video_codecs.hevc_hdr)
            video_codec_box->add_item(TR("HEVC (HDR)"), "hevc_hdr");
        if(gsr_info->supported_video_codecs.av1)
            video_codec_box->add_item(TR("AV1"), "av1");
        if(gsr_info->supported_video_codecs.av1_10bit)
            video_codec_box->add_item(TR("AV1 (10 bit, reduces banding)"), "av1_10bit");
        if(gsr_info->supported_video_codecs.av1_hdr)
            video_codec_box->add_item(TR("AV1 (HDR)"), "av1_hdr");
        if(gsr_info->supported_video_codecs.vp8)
            video_codec_box->add_item(TR("VP8"), "vp8");
        if(gsr_info->supported_video_codecs.vp9)
            video_codec_box->add_item(TR("VP9"), "vp9");
        if(gsr_info->supported_video_codecs.h264_software)
            video_codec_box->add_item(TR("H264 Software Encoder (Slow, not recommended)"), "h264_software");
        video_codec_box_ptr = video_codec_box.get();
        return video_codec_box;
    }

    std::unique_ptr<List> SettingsPage::create_video_codec() {
        auto video_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Video codec:"), get_color_theme().text_color));
        video_codec_list->add_widget(create_video_codec_box());
        video_codec_ptr = video_codec_list.get();
        return video_codec_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_audio_codec_box() {
        auto audio_codec_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        audio_codec_box->add_item(TR("Opus (Recommended)"), "opus");
        audio_codec_box->add_item(TR("AAC"), "aac");
        audio_codec_box_ptr = audio_codec_box.get();
        return audio_codec_box;
    }

    std::unique_ptr<List> SettingsPage::create_audio_codec() {
        auto audio_codec_list = std::make_unique<List>(List::Orientation::VERTICAL);
        audio_codec_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Audio codec:"), get_color_theme().text_color));
        audio_codec_list->add_widget(create_audio_codec_box());
        audio_codec_ptr = audio_codec_list.get();
        return audio_codec_list;
    }

    std::unique_ptr<Entry> SettingsPage::create_framerate_entry() {
        auto framerate_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "60", (int)(2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 3));
        framerate_entry->set_number_mode(true, 1, 500);
        framerate_entry_ptr = framerate_entry.get();
        return framerate_entry;
    }

    std::unique_ptr<List> SettingsPage::create_framerate() {
        auto framerate_list = std::make_unique<List>(List::Orientation::VERTICAL);
        framerate_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Frame rate:"), get_color_theme().text_color));
        framerate_list->add_widget(create_framerate_entry());
        return framerate_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_framerate_mode_box() {
        auto framerate_mode_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        framerate_mode_box->add_item(TR("Auto (Recommended)"), "auto");
        framerate_mode_box->add_item(TR("Constant"), "cfr");
        framerate_mode_box->add_item(TR("Variable"), "vfr");
        if(gsr_info->system_info.display_server == DisplayServer::X11)
            framerate_mode_box->add_item(TR("Sync to content"), "content");
        else
            framerate_mode_box->add_item(TR("Sync to content (Only X11 or desktop portal capture)"), "content");
        framerate_mode_box_ptr = framerate_mode_box.get();
        return framerate_mode_box;
    }

    std::unique_ptr<List> SettingsPage::create_framerate_mode() {
        auto framerate_mode_list = std::make_unique<List>(List::Orientation::VERTICAL);
        framerate_mode_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Frame rate mode:"), get_color_theme().text_color));
        framerate_mode_list->add_widget(create_framerate_mode_box());
        framerate_mode_list_ptr = framerate_mode_list.get();
        return framerate_mode_list;
    }

    std::unique_ptr<List> SettingsPage::create_framerate_section() {
        auto framerate_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        framerate_info_list->add_widget(create_framerate());
        framerate_info_list->add_widget(create_framerate_mode());
        return framerate_info_list;
    }
    
    std::unique_ptr<Widget> SettingsPage::create_record_cursor_section() {
        auto record_cursor_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Record cursor"));
        record_cursor_checkbox->set_checked(true);
        record_cursor_checkbox_ptr = record_cursor_checkbox.get();
        return record_cursor_checkbox;
    }

    std::unique_ptr<Widget> SettingsPage::create_enable_vulkan_video_encoding_section() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        vulkan_video_list_ptr = list.get();

        auto enable_vulkan_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Enable vulkan video encoding (experimental)"));
        enable_vulkan_checkbox_ptr = enable_vulkan_checkbox.get();

        list->add_widget(std::move(enable_vulkan_checkbox));

        auto info = std::make_unique<Image>(&get_theme().question_mark_texture, enable_vulkan_checkbox_ptr->get_size(), Image::ScaleBehavior::SCALE);
        info->set_tooltip_text(
            TR("Use vulkan video encoding instead of VAAPI/NVENC.\n"
               "Enabling this may result in better game performance while recording, especially on NVIDIA.\n"
               "Note that this option is experimental. There may be GPU driver issues that causes issues when this is enabled.")
        );
        Image *info_ptr = info.get();
        info->on_mouse_move = [info_ptr](bool inside) {
            if(inside)
                set_current_tooltip(info_ptr);
            else
                remove_as_current_tooltip(info_ptr);
        };
        list->add_widget(std::move(info));

        return list;
    }

    std::unique_ptr<Widget> SettingsPage::create_video_section() {
        auto video_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        video_section_list->add_widget(create_video_quality_section());
        video_section_list->add_widget(create_video_codec());
        video_section_list->add_widget(create_framerate_section());
        video_section_list->add_widget(create_record_cursor_section());
        video_section_list->add_widget(create_enable_vulkan_video_encoding_section());
        return std::make_unique<Subsection>(TR("Video"), std::move(video_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> SettingsPage::create_settings() {
        auto page_list = std::make_unique<List>(List::Orientation::VERTICAL);
        page_list->set_spacing(0.018f);
        page_list->add_widget(create_view_radio_button());
        auto scrollable_page = std::make_unique<ScrollablePage>(content_page_ptr->get_inner_size() - mgl::vec2f(0.0f, page_list->get_size().y + 0.018f * get_theme().window_height));
        settings_scrollable_page_ptr = scrollable_page.get();
        page_list->add_widget(std::move(scrollable_page));

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_capture_target_section());
        settings_list->add_widget(create_webcam_section());
        settings_list->add_widget(create_audio_section());
        settings_list->add_widget(create_video_section());
        settings_list_ptr = settings_list.get();
        settings_scrollable_page_ptr->add_widget(std::move(settings_list));
        return page_list;
    }

    void SettingsPage::add_widgets() {
        content_page_ptr->add_widget(create_settings());

        record_area_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            const bool focused_selected = id == "focused";
            const bool portal_selected = id == "portal";
            area_size_list_ptr->set_visible(focused_selected);
            video_resolution_list_ptr->set_visible(!focused_selected && change_video_resolution_checkbox_ptr->is_checked());
            change_video_resolution_checkbox_ptr->set_visible(!focused_selected);
            restore_portal_session_list_ptr->set_visible(portal_selected);
            return true;
        };

        change_video_resolution_checkbox_ptr->on_changed = [this](bool checked) {
            const bool focused_selected = record_area_box_ptr->get_selected_id() == "focused";
            video_resolution_list_ptr->set_visible(!focused_selected && checked);
        };

        video_quality_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            const bool custom_selected = id == "custom";
            video_bitrate_list_ptr->set_visible(custom_selected);

            if(estimated_file_size_ptr)
                estimated_file_size_ptr->set_visible(custom_selected);

            return true;
        };
        video_quality_box_ptr->on_selection_changed("", video_quality_box_ptr->get_selected_id());

        if(!capture_options.monitors.empty())
            record_area_box_ptr->set_selected_item("focused_monitor");
        else if(capture_options.portal)
            record_area_box_ptr->set_selected_item("portal");
        else if(capture_options.window)
            record_area_box_ptr->set_selected_item("window");
        else
            record_area_box_ptr->on_selection_changed("", "");
    }

    void SettingsPage::add_page_specific_widgets() {
        switch(type) {
            case Type::REPLAY:
                add_replay_widgets();
                break;
            case Type::RECORD:
                add_record_widgets();
                break;
            case Type::STREAM:
                add_stream_widgets();
                break;
        }
    }

    std::unique_ptr<List> SettingsPage::create_save_directory(const char *label) {
        auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
        save_directory_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), label, get_color_theme().text_color));
        auto save_directory_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), get_videos_dir().c_str(), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
        save_directory_button_ptr = save_directory_button.get();
        save_directory_button->on_click = [this]() {
            auto select_directory_page = std::make_unique<GsrPage>(TR("File"), TR("Settings"));
            select_directory_page->add_button(TR("Save"), "save", get_color_theme().tint_color);
            select_directory_page->add_button(TR("Cancel"), "cancel", get_color_theme().page_bg_color);

            auto file_chooser = std::make_unique<FileChooser>(save_directory_button_ptr->get_text(), select_directory_page->get_inner_size());
            FileChooser *file_chooser_ptr = file_chooser.get();
            select_directory_page->add_widget(std::move(file_chooser));

            select_directory_page->on_click = [this, file_chooser_ptr](const std::string &id) {
                if(id == "save") {
                    save_directory_button_ptr->set_text(file_chooser_ptr->get_current_directory());
                    page_stack->pop();
                } else if(id == "cancel") {
                    page_stack->pop();
                }
            };

            page_stack->push(std::move(select_directory_page));
        };
        save_directory_list->add_widget(std::move(save_directory_button));
        return save_directory_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_container_box() {
        auto container_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        container_box->add_item("mp4", "mp4");
        container_box->add_item("mkv", "matroska");
        container_box->add_item("flv", "flv");
        container_box->add_item("mov", "mov");
        container_box_ptr = container_box.get();
        return container_box;
    }

    std::unique_ptr<List> SettingsPage::create_container_section() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Container:"), get_color_theme().text_color));
        container_list->add_widget(create_container_box());
        return container_list;
    }

    std::unique_ptr<List> SettingsPage::create_replay_time_entry() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);

        auto replay_time_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "60", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        replay_time_entry->set_number_mode(true, 1, 86400);
        replay_time_entry_ptr = replay_time_entry.get();
        list->add_widget(std::move(replay_time_entry));

        auto replay_time_label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "00h:00m:00s", get_color_theme().text_color);
        replay_time_label_ptr = replay_time_label.get();
        list->add_widget(std::move(replay_time_label));

        return list;
    }

    std::unique_ptr<List> SettingsPage::create_replay_time() {
        auto replay_time_list = std::make_unique<List>(List::Orientation::VERTICAL);
        replay_time_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Replay duration in seconds:"), get_color_theme().text_color));
        replay_time_list->add_widget(create_replay_time_entry());
        return replay_time_list;
    }

    std::unique_ptr<List> SettingsPage::create_replay_storage() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Where should temporary replay data be stored?"), get_color_theme().text_color));
        auto replay_storage_button = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::HORIZONTAL);
        replay_storage_button_ptr = replay_storage_button.get();
        replay_storage_button->add_item(TR("RAM"), "ram");
        replay_storage_button->add_item(TR("Disk (Not recommended on SSDs)"), "disk");

        replay_storage_button->on_selection_changed = [this](std::string_view, std::string_view id) {
            update_estimated_replay_file_size(id);
            return true;
        };

        list->add_widget(std::move(replay_storage_button));
        return list;
    }

    std::unique_ptr<RadioButton> SettingsPage::create_start_replay_automatically() {
        auto radiobutton = std::make_unique<RadioButton>(get_theme().body_font_desc.c_str(), RadioButton::Orientation::VERTICAL);
        radiobutton->add_item(TR("Don't turn on replay automatically"), "dont_turn_on_automatically");
        radiobutton->add_item(TR("Turn on replay when this program starts"), "turn_on_at_system_startup");
        radiobutton->add_item(TR("Turn on replay when starting a game"), "turn_on_at_game_launch");
        turn_on_replay_automatically_mode_ptr = radiobutton.get();
        return radiobutton;
    }

    std::unique_ptr<Widget> SettingsPage::create_start_replay_automatically_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(create_start_replay_automatically());

        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Only turn on replay if a power supply is connected"));
        replay_power_supply_checkbox_ptr = checkbox.get();
        list->add_widget(std::move(checkbox));

        return list;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_save_replay_in_game_folder() {
        char text[256];
        snprintf(text, sizeof(text), TR("Save video in a folder based on the games name%s"), supports_window_title ? "" : TR(" (X11 applications only)"));
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), text);
        save_replay_in_game_folder_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_restart_replay_on_save() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Restart replay on save"));
        restart_replay_on_save = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Label> SettingsPage::create_estimated_replay_file_size() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "Estimated video max file size in RAM: 57.60MB", get_color_theme().text_color);
        estimated_file_size_ptr = label.get();
        return label;
    }

    void SettingsPage::update_estimated_replay_file_size(std::string_view replay_storage_type) {
        const int64_t replay_time_seconds = sv_to_int<int64_t>(replay_time_entry_ptr->get_text());
        const int64_t video_bitrate_kbps = sv_to_int<int64_t>(video_bitrate_entry_ptr->get_text());
        const double video_filesize_mb = ((double)replay_time_seconds * (double)video_bitrate_kbps) / 8192.0;

        char buffer[256];
        snprintf(buffer, sizeof(buffer), TR("Estimated video max file size %s: %.2fMB.\nChange video bitrate or replay duration to change file size."), replay_storage_type == "ram" ? TR("in RAM") : TR("on disk"), video_filesize_mb);
        estimated_file_size_ptr->set_text(buffer);
    }

    void SettingsPage::update_replay_time_text() {
        int seconds = sv_to_int<int>(replay_time_entry_ptr->get_text());

        const int hours = seconds / 60 / 60;
        seconds -= (hours * 60 * 60);

        const int minutes = seconds / 60;
        seconds -= (minutes * 60);

        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%02dh:%02dm:%02ds", hours, minutes, seconds);
        replay_time_label_ptr->set_text(buffer);
    }

    void SettingsPage::view_changed(bool advanced_view) {
        color_range_list_ptr->set_visible(advanced_view);
        audio_codec_ptr->set_visible(advanced_view);
        video_codec_ptr->set_visible(advanced_view);
        framerate_mode_list_ptr->set_visible(advanced_view);
        vulkan_video_list_ptr->set_visible(advanced_view && supports_vulkan_video_encoding(gsr_info->supported_video_codecs));
        set_application_audio_options_visible(audio_track_section_list_ptr, advanced_view, *gsr_info);
        settings_scrollable_page_ptr->reset_scroll();
    }

    RecordOptions& SettingsPage::get_current_record_options() {
        switch(type) {
            default:
                assert(false);
            case Type::REPLAY: return config.replay_config.record_options;
            case Type::RECORD: return config.record_config.record_options;
            case Type::STREAM: return config.streaming_config.record_options;
        }
    }

    std::unique_ptr<CheckBox> SettingsPage::create_led_indicator() {
        char label_str[256];
        label_str[0] = '\0';
        switch(type) {
            case Type::REPLAY:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show replay status with scroll lock LED"));
                break;
            case Type::RECORD:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show recording status with scroll lock LED"));
                break;
            case Type::STREAM:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show streaming status with scroll lock LED"));
                break;
        }

        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), label_str);
        checkbox->set_checked(false);
        led_indicator_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<CheckBox> SettingsPage::create_notifications() {
        char label_str[256];
        label_str[0] = '\0';
        switch(type) {
            case Type::REPLAY:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show replay notifications"));
                break;
            case Type::RECORD:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show recording notifications"));
                break;
            case Type::STREAM:
                snprintf(label_str, sizeof(label_str), "%s", TR("Show streaming notifications"));
                break;
        }

        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), label_str);
        checkbox->set_checked(true);
        show_notification_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<List> SettingsPage::create_indicator() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(create_notifications());
        list->add_widget(create_led_indicator());
        return list;
    }

    std::unique_ptr<Widget> SettingsPage::create_low_power_mode() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        list->set_visible(gsr_info->gpu_info.vendor == GpuVendor::AMD);

        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Record in low-power mode"));
        low_power_mode_checkbox_ptr = checkbox.get();

        list->add_widget(std::move(checkbox));

        auto info = std::make_unique<Image>(&get_theme().question_mark_texture, low_power_mode_checkbox_ptr->get_size(), Image::ScaleBehavior::SCALE);
        info->set_tooltip_text(
            TR("Do not force the GPU to go into high performance mode when recording.\n"
               "May affect recording performance, especially when playing a video at the same time.\n"
               "If enabled then it's recommended to use sync to content frame rate mode to reduce power usage when idle.")
        );
        Image *info_ptr = info.get();
        info->on_mouse_move = [info_ptr](bool inside) {
            if(inside)
                set_current_tooltip(info_ptr);
            else
                remove_as_current_tooltip(info_ptr);
        };
        list->add_widget(std::move(info));

        return list;
    }

    void SettingsPage::add_replay_widgets() {
        auto file_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto file_info_data_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        file_info_data_list->add_widget(create_save_directory(TR("Directory to save replays:")));
        file_info_data_list->add_widget(create_container_section());
        file_info_data_list->add_widget(create_replay_time());
        file_info_list->add_widget(std::move(file_info_data_list));
        file_info_list->add_widget(create_estimated_replay_file_size());
        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("File info"), std::move(file_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto general_list = std::make_unique<List>(List::Orientation::VERTICAL);
        general_list->add_widget(create_replay_storage());
        general_list->add_widget(create_save_replay_in_game_folder());
        general_list->add_widget(create_restart_replay_on_save());
        general_list->add_widget(create_low_power_mode());

        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("General"), std::move(general_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));
        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("Replay indicator"), create_indicator(), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));
        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("Autostart"), create_start_replay_automatically_section(), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        view_radio_button_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            view_changed(id == "advanced");
            return true;
        };
        view_radio_button_ptr->on_selection_changed(TR("Simple"), "simple");

        replay_time_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_replay_file_size(replay_storage_button_ptr->get_selected_id());
            update_replay_time_text();
        };

        video_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_replay_file_size(replay_storage_button_ptr->get_selected_id());
        };
    }

    std::unique_ptr<CheckBox> SettingsPage::create_save_recording_in_game_folder() {
        char text[256];
        snprintf(text, sizeof(text), TR("Save video in a folder based on the games name%s"), supports_window_title ? "" : TR(" (X11 applications only)"));
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), text);
        save_recording_in_game_folder_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Label> SettingsPage::create_estimated_record_file_size() {
        auto label = std::make_unique<Label>(get_theme().body_font_desc.c_str(), "Estimated video file size per minute (excluding audio): 345.60MB", get_color_theme().text_color);
        estimated_file_size_ptr = label.get();
        return label;
    }

    void SettingsPage::update_estimated_record_file_size() {
        const int64_t video_bitrate_bps = sv_to_int<int64_t>(video_bitrate_entry_ptr->get_text()) * 1000LL / 8LL;
        const double video_filesize_mb_per_minute = (60.0 * (double)video_bitrate_bps) / 1000.0 / 1000.0 * 1.024;

        char buffer[512];
        snprintf(buffer, sizeof(buffer), TR("Estimated video file size per minute (excluding audio): %.2fMB"), video_filesize_mb_per_minute);
        estimated_file_size_ptr->set_text(buffer);
    }

    void SettingsPage::add_record_widgets() {
        auto file_info_list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto file_info_data_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        file_info_data_list->add_widget(create_save_directory(TR("Directory to save videos:")));
        file_info_data_list->add_widget(create_container_section());
        file_info_list->add_widget(std::move(file_info_data_list));
        file_info_list->add_widget(create_estimated_record_file_size());

        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("File info"), std::move(file_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto general_list = std::make_unique<List>(List::Orientation::VERTICAL);
        general_list->add_widget(create_save_recording_in_game_folder());
        general_list->add_widget(create_low_power_mode());

        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("General"), std::move(general_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));
        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("Recording indicator"), create_indicator(), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        view_radio_button_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            view_changed(id == "advanced");
            return true;
        };
        view_radio_button_ptr->on_selection_changed(TR("Simple"), "simple");

        video_bitrate_entry_ptr->on_changed = [this](std::string_view) {
            update_estimated_record_file_size();
        };
    }

    std::unique_ptr<ComboBox> SettingsPage::create_streaming_service_box() {
        auto streaming_service_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        streaming_service_box->add_item(TR("Twitch"), "twitch");
        streaming_service_box->add_item(TR("YouTube"), "youtube");
        streaming_service_box->add_item(TR("Rumble"), "rumble");
        streaming_service_box->add_item(TR("Kick"), "kick");
        streaming_service_box->add_item(TR("Custom"), "custom");
        streaming_service_box_ptr = streaming_service_box.get();
        return streaming_service_box;
    }

    std::unique_ptr<List> SettingsPage::create_streaming_service_section() {
        auto streaming_service_list = std::make_unique<List>(List::Orientation::VERTICAL);
        streaming_service_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream service:"), get_color_theme().text_color));
        streaming_service_list->add_widget(create_streaming_service_box());
        return streaming_service_list;
    }

    static std::unique_ptr<Button> create_mask_toggle_button(Entry *entry_to_toggle, mgl::vec2f size) {
        auto button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), "", size, mgl::Color(0, 0, 0, 0));
        Button *button_ptr = button.get();
        button->set_icon(&get_theme().masked_texture);
        button->on_click = [entry_to_toggle, button_ptr]() {
            const bool is_masked = entry_to_toggle->is_masked();
            button_ptr->set_icon(is_masked ? &get_theme().unmasked_texture : &get_theme().masked_texture);
            entry_to_toggle->set_masked(!is_masked);
        };
        return button;
    }

    static Entry* add_stream_key_entry_to_list(List *stream_key_list) {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        auto key_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        key_entry->set_masked(true);
        Entry *key_entry_ptr = key_entry.get();
        const float mask_icon_size = key_entry_ptr->get_size().y * 0.9f;
        list->add_widget(std::move(key_entry));
        list->add_widget(create_mask_toggle_button(key_entry_ptr, mgl::vec2f(mask_icon_size, mask_icon_size)));
        stream_key_list->add_widget(std::move(list));
        return key_entry_ptr;
    }

    std::unique_ptr<List> SettingsPage::create_stream_key_section() {
        auto stream_key_list = std::make_unique<List>(List::Orientation::VERTICAL);
        stream_key_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream key:"), get_color_theme().text_color));

        twitch_stream_key_entry_ptr = add_stream_key_entry_to_list(stream_key_list.get());
        youtube_stream_key_entry_ptr = add_stream_key_entry_to_list(stream_key_list.get());
        rumble_stream_key_entry_ptr = add_stream_key_entry_to_list(stream_key_list.get());

        stream_key_list_ptr = stream_key_list.get();
        return stream_key_list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_kick_url() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto stream_url_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        kick_stream_url_entry_ptr = stream_url_entry.get();
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream URL:"), get_color_theme().text_color));
        list->add_widget(std::move(stream_url_entry));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_kick_key() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        auto stream_key_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        stream_key_entry->set_masked(true);
        kick_stream_key_entry_ptr = stream_key_entry.get();
        const float mask_icon_size = kick_stream_key_entry_ptr->get_size().y * 1.0f;
        list->add_widget(std::move(stream_key_entry));
        list->add_widget(create_mask_toggle_button(kick_stream_key_entry_ptr, mgl::vec2f(mask_icon_size, mask_icon_size)));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_kick_section() {
        auto kick_stream_list = std::make_unique<List>(List::Orientation::VERTICAL);

        auto stream_url_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        stream_url_list->add_widget(create_stream_kick_url());

        kick_stream_list->add_widget(std::move(stream_url_list));
        kick_stream_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream key:"), get_color_theme().text_color));
        kick_stream_list->add_widget(create_stream_kick_key());

        kick_stream_list_ptr = kick_stream_list.get();
        return kick_stream_list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_custom_url() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        auto stream_url_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        stream_url_entry_ptr = stream_url_entry.get();
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream URL:"), get_color_theme().text_color));
        list->add_widget(std::move(stream_url_entry));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_custom_key() {
        auto list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        auto stream_key_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        stream_key_entry->set_masked(true);
        stream_key_entry_ptr = stream_key_entry.get();
        const float mask_icon_size = stream_key_entry_ptr->get_size().y * 0.9f;
        list->add_widget(std::move(stream_key_entry));
        list->add_widget(create_mask_toggle_button(stream_key_entry_ptr, mgl::vec2f(mask_icon_size, mask_icon_size)));
        return list;
    }

    std::unique_ptr<List> SettingsPage::create_stream_custom_section() {
        auto custom_stream_list = std::make_unique<List>(List::Orientation::VERTICAL);

        auto stream_url_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        stream_url_list->add_widget(create_stream_custom_url());
        stream_url_list->add_widget(create_stream_container());

        custom_stream_list->add_widget(std::move(stream_url_list));
        custom_stream_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Stream key:"), get_color_theme().text_color));
        custom_stream_list->add_widget(create_stream_custom_key());

        custom_stream_list_ptr = custom_stream_list.get();
        return custom_stream_list;
    }

    std::unique_ptr<ComboBox> SettingsPage::create_stream_container_box() {
        auto container_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        container_box->add_item("mp4", "mp4");
        container_box->add_item("flv", "flv");
        container_box->add_item("ts", "mpegts");
        container_box->add_item("m3u8", "hls");
        container_box_ptr = container_box.get();
        return container_box;
    }
    
    std::unique_ptr<List> SettingsPage::create_stream_container() {
        auto container_list = std::make_unique<List>(List::Orientation::VERTICAL);
        container_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Container:"), get_color_theme().text_color));
        container_list->add_widget(create_stream_container_box());
        return container_list;
    }

    void SettingsPage::add_stream_widgets() {
        auto streaming_info_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        streaming_info_list->add_widget(create_streaming_service_section());
        streaming_info_list->add_widget(create_stream_key_section());
        streaming_info_list->add_widget(create_stream_kick_section());
        streaming_info_list->add_widget(create_stream_custom_section());

        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("Streaming info"), std::move(streaming_info_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        auto general_list = std::make_unique<List>(List::Orientation::VERTICAL);
        general_list->add_widget(create_low_power_mode());

        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("General"), std::move(general_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));
        settings_list_ptr->add_widget(std::make_unique<Subsection>(TR("Streaming indicator"), create_indicator(), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f)));

        streaming_service_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            const bool twitch_option = id == "twitch";
            const bool youtube_option = id == "youtube";
            const bool rumble_option = id == "rumble";
            const bool kick_option = id == "kick";
            const bool custom_option = id == "custom";
            stream_key_list_ptr->set_visible(!custom_option && !kick_option);
            custom_stream_list_ptr->set_visible(custom_option);
            kick_stream_list_ptr->set_visible(kick_option);
            twitch_stream_key_entry_ptr->get_parent_widget()->set_visible(twitch_option);
            youtube_stream_key_entry_ptr->get_parent_widget()->set_visible(youtube_option);
            rumble_stream_key_entry_ptr->get_parent_widget()->set_visible(rumble_option);
            return true;
        };
        streaming_service_box_ptr->on_selection_changed("Twitch", "twitch");

        view_radio_button_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            view_changed(id == "advanced");
            return true;
        };
        view_radio_button_ptr->on_selection_changed(TR("Simple"), "simple");
    }

    void SettingsPage::on_navigate_away_from_page() {
        save();
    }

    void SettingsPage::load() {
        switch(type) {
            case Type::REPLAY:
                load_replay();
                break;
            case Type::RECORD:
                load_record();
                break;
            case Type::STREAM:
                load_stream();
                break;
        }
    }

    void SettingsPage::save() {
        Config prev_config = config;
        switch(type) {
            case Type::REPLAY:
                save_replay();
                break;
            case Type::RECORD:
                save_record();
                break;
            case Type::STREAM:
                save_stream();
                break;
        }
        save_config(config);

        if(on_config_changed && config != prev_config)
            on_config_changed();
    }

    static const std::string* get_application_audio_by_name_case_insensitive(const std::vector<std::string> &application_audio, const std::string &name) {
        for(const auto &app_audio : application_audio) {
            if(strcasecmp(app_audio.c_str(), name.c_str()) == 0)
                return &app_audio;
        }
        return nullptr;
    }

    void SettingsPage::load_audio_tracks(const RecordOptions &record_options) {
        audio_track_section_list_ptr->clear();
        for(const AudioTrack &audio_track : record_options.audio_tracks_list) {
            auto audio_track_section = create_audio_track_section(audio_section_ptr);
            List *audio_track_section_items_list_ptr = dynamic_cast<List*>(audio_track_section->get_inner_widget());
            List *audio_input_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(2));
            CheckBox *application_audio_invert_checkbox_ptr = dynamic_cast<CheckBox*>(audio_track_section_items_list_ptr->get_child_widget_by_index(3));
            List *custom_audio_track_name_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(5));
            application_audio_invert_checkbox_ptr->set_checked(audio_track.application_audio_invert);
            dynamic_cast<Entry*>(custom_audio_track_name_list_ptr->get_child_widget_by_index(1))->set_text(audio_track.name);
            audio_input_list_ptr->clear();
            for(const std::string &audio_input : audio_track.audio_inputs) {
                if(starts_with(audio_input, "app:")) {
                    if(!gsr_info->system_info.supports_app_audio)
                        continue;

                    std::string audio_track_name = audio_input.substr(4);
                    const std::string *app_audio = get_application_audio_by_name_case_insensitive(application_audio, audio_track_name);
                    if(app_audio) {
                        std::unique_ptr<List> application_audio_widget = create_application_audio(audio_input_list_ptr);
                        ComboBox *application_audio_box = dynamic_cast<ComboBox*>(application_audio_widget->get_child_widget_by_index(1));
                        application_audio_box->set_selected_item(*app_audio);
                        audio_input_list_ptr->add_widget(std::move(application_audio_widget));
                    } else {
                        std::unique_ptr<List> application_audio_widget = create_custom_application_audio(audio_input_list_ptr);
                        Entry *application_audio_entry = dynamic_cast<Entry*>(application_audio_widget->get_child_widget_by_index(1));
                        application_audio_entry->set_text(std::move(audio_track_name));
                        audio_input_list_ptr->add_widget(std::move(application_audio_widget));
                    }
                } else if(starts_with(audio_input, "device:")) {
                    const std::string device_name = audio_input.substr(7);
                    const AudioDeviceType audio_device_type = audio_device_is_output(device_name) ? AudioDeviceType::OUTPUT : AudioDeviceType::INPUT;
                    std::unique_ptr<List> audio_track_widget = create_audio_device(audio_device_type, audio_input_list_ptr);
                    ComboBox *audio_device_box = dynamic_cast<ComboBox*>(audio_track_widget->get_child_widget_by_index(1));
                    audio_device_box->set_selected_item(device_name);
                    audio_input_list_ptr->add_widget(std::move(audio_track_widget));
                } else {
                    const AudioDeviceType audio_device_type = audio_device_is_output(audio_input) ? AudioDeviceType::OUTPUT : AudioDeviceType::INPUT;
                    std::unique_ptr<List> audio_track_widget = create_audio_device(audio_device_type, audio_input_list_ptr);
                    ComboBox *audio_device_box = dynamic_cast<ComboBox*>(audio_track_widget->get_child_widget_by_index(1));
                    audio_device_box->set_selected_item(audio_input);
                    audio_input_list_ptr->add_widget(std::move(audio_track_widget));
                }
            }

            audio_track_section_list_ptr->add_widget(std::move(audio_track_section));

            if(type == Type::STREAM)
                break;
        }

        if(type == Type::STREAM && audio_track_section_list_ptr->get_num_children() == 0) {
            auto audio_track_section = create_audio_track_section(audio_section_ptr);
            audio_track_section_list_ptr->add_widget(std::move(audio_track_section));
        }

        update_application_audio_warning_visibility();
    }

    void SettingsPage::load_common(RecordOptions &record_options) {
        record_area_box_ptr->set_selected_item(record_options.record_area_option);
        change_video_resolution_checkbox_ptr->set_checked(record_options.change_video_resolution);
        load_audio_tracks(record_options);
        color_range_box_ptr->set_selected_item(record_options.color_range);
        video_quality_box_ptr->set_selected_item(record_options.video_quality);
        video_codec_box_ptr->set_selected_item(record_options.video_codec);
        audio_codec_box_ptr->set_selected_item(record_options.audio_codec);
        framerate_mode_box_ptr->set_selected_item(record_options.framerate_mode);
        view_radio_button_ptr->set_selected_item(record_options.advanced_view ? "advanced" : "simple");
        // TODO:
        //record_options.overclock = false;
        record_cursor_checkbox_ptr->set_checked(record_options.record_cursor);
        restore_portal_session_checkbox_ptr->set_checked(record_options.restore_portal_session);
        show_notification_checkbox_ptr->set_checked(record_options.show_notifications);
        led_indicator_checkbox_ptr->set_checked(record_options.use_led_indicator);
        low_power_mode_checkbox_ptr->set_checked(record_options.low_power_mode);
        enable_vulkan_checkbox_ptr->set_checked(record_options.enable_vulkan_video_encoding);

        char webcam_setup_str[256];
        snprintf(webcam_setup_str, sizeof(webcam_setup_str), "%dx%d@%dhz", record_options.webcam_camera_width, record_options.webcam_camera_height, record_options.webcam_camera_fps);

        webcam_sources_box_ptr->set_selected_item(record_options.webcam_source);
        flip_camera_horizontally_checkbox_ptr->set_checked(record_options.webcam_flip_horizontally);
        webcam_video_format_box_ptr->set_selected_item(record_options.webcam_video_format);
        webcam_video_setup_box_ptr->set_selected_item(webcam_setup_str);
        webcam_box_pos.x = ((float)record_options.webcam_x / 100.0f) * screen_inner_size.x;
        webcam_box_pos.y = ((float)record_options.webcam_y / 100.0f) * screen_inner_size.y;
        webcam_box_size.x = ((float)record_options.webcam_width / 100.0f * screen_inner_size.x);
        webcam_box_size.y = ((float)record_options.webcam_height / 100.0f * screen_inner_size.y);

        if(selected_camera_setup.has_value())
            webcam_box_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), webcam_box_size);

        if(record_options.record_area_width == 0)
            record_options.record_area_width = 1920;

        if(record_options.record_area_height == 0)
            record_options.record_area_height = 1080;

        if(record_options.video_width == 0)
            record_options.video_width = 1920;

        if(record_options.video_height == 0)
            record_options.video_height = 1080;

        if(record_options.record_area_width < 32)
            record_options.record_area_width = 32;
        area_width_entry_ptr->set_text(std::to_string(record_options.record_area_width));

        if(record_options.record_area_height < 32)
            record_options.record_area_height = 32;
        area_height_entry_ptr->set_text(std::to_string(record_options.record_area_height));

        if(record_options.video_width < 32)
            record_options.video_width = 32;
        video_width_entry_ptr->set_text(std::to_string(record_options.video_width));

        if(record_options.video_height < 32)
            record_options.video_height = 32;
        video_height_entry_ptr->set_text(std::to_string(record_options.video_height));

        if(record_options.fps < 1)
            record_options.fps = 1;
        framerate_entry_ptr->set_text(std::to_string(record_options.fps));

        if(record_options.video_bitrate < 1)
            record_options.video_bitrate = 1;
        video_bitrate_entry_ptr->set_text(std::to_string(record_options.video_bitrate));
    }

    void SettingsPage::load_replay() {
        load_common(config.replay_config.record_options);
        replay_storage_button_ptr->set_selected_item(config.replay_config.replay_storage);
        turn_on_replay_automatically_mode_ptr->set_selected_item(config.replay_config.turn_on_replay_automatically_mode);
        save_replay_in_game_folder_ptr->set_checked(config.replay_config.save_video_in_game_folder);
        replay_power_supply_checkbox_ptr->set_checked(config.replay_config.only_start_replay_if_power_supply_connected);
        if(restart_replay_on_save)
            restart_replay_on_save->set_checked(config.replay_config.restart_replay_on_save);
        
        save_directory_button_ptr->set_text(config.replay_config.save_directory);
        container_box_ptr->set_selected_item(config.replay_config.container);

        if(config.replay_config.replay_time < 2)
            config.replay_config.replay_time = 2;
        if(config.replay_config.replay_time > 86400)
            config.replay_config.replay_time = 86400;
        replay_time_entry_ptr->set_text(std::to_string(config.replay_config.replay_time));
    }

    void SettingsPage::load_record() {
        load_common(config.record_config.record_options);
        save_recording_in_game_folder_ptr->set_checked(config.record_config.save_video_in_game_folder);
        save_directory_button_ptr->set_text(config.record_config.save_directory);
        container_box_ptr->set_selected_item(config.record_config.container);
    }

    void SettingsPage::load_stream() {
        load_common(config.streaming_config.record_options);
        streaming_service_box_ptr->set_selected_item(config.streaming_config.streaming_service);
        youtube_stream_key_entry_ptr->set_text(config.streaming_config.youtube.stream_key);
        twitch_stream_key_entry_ptr->set_text(config.streaming_config.twitch.stream_key);
        rumble_stream_key_entry_ptr->set_text(config.streaming_config.rumble.stream_key);
        kick_stream_url_entry_ptr->set_text(config.streaming_config.kick.stream_url);
        kick_stream_key_entry_ptr->set_text(config.streaming_config.kick.stream_key);
        stream_url_entry_ptr->set_text(config.streaming_config.custom.url);
        stream_key_entry_ptr->set_text(config.streaming_config.custom.key);
        container_box_ptr->set_selected_item(config.streaming_config.custom.container);
    }

    static void save_audio_tracks(std::vector<AudioTrack> &audio_tracks, List *audio_track_section_list_ptr) {
        audio_tracks.clear();
        audio_track_section_list_ptr->for_each_child_widget([&audio_tracks](std::unique_ptr<Widget> &child_widget) {
            Subsection *audio_subsection = dynamic_cast<Subsection*>(child_widget.get());
            List *audio_track_section_items_list_ptr = dynamic_cast<List*>(audio_subsection->get_inner_widget());
            List *audio_input_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(2));
            CheckBox *application_audio_invert_checkbox_ptr = dynamic_cast<CheckBox*>(audio_track_section_items_list_ptr->get_child_widget_by_index(3));
            List *custom_audio_track_name_list_ptr = dynamic_cast<List*>(audio_track_section_items_list_ptr->get_child_widget_by_index(5));
            Entry *custom_audio_track_name_field = dynamic_cast<Entry*>(custom_audio_track_name_list_ptr->get_child_widget_by_index(1));
            audio_tracks.push_back({std::string(custom_audio_track_name_field->get_text()), std::vector<std::string>{}, application_audio_invert_checkbox_ptr->is_checked()});
            audio_input_list_ptr->for_each_child_widget([&audio_tracks](std::unique_ptr<Widget> &child_widget){
                List *audio_track_line = dynamic_cast<List*>(child_widget.get());
                const AudioTrackType audio_track_type = (AudioTrackType)(uintptr_t)audio_track_line->userdata;
                switch(audio_track_type) {
                    case AudioTrackType::DEVICE: {
                        ComboBox *audio_device_box = dynamic_cast<ComboBox*>(audio_track_line->get_child_widget_by_index(1));
                        audio_tracks.back().audio_inputs.push_back("device:" + std::string(audio_device_box->get_selected_id()));
                        break;
                    }
                    case AudioTrackType::APPLICATION: {
                        ComboBox *application_audio_box = dynamic_cast<ComboBox*>(audio_track_line->get_child_widget_by_index(1));
                        audio_tracks.back().audio_inputs.push_back("app:" + std::string(application_audio_box->get_selected_id()));
                        break;
                    }
                    case AudioTrackType::APPLICATION_CUSTOM: {
                        Entry *application_audio_entry = dynamic_cast<Entry*>(audio_track_line->get_child_widget_by_index(1));
                        audio_tracks.back().audio_inputs.push_back("app:" + std::string(application_audio_entry->get_text()));
                        break;
                    }
                }
                return true;
            });

            return true;
        });
    }

    void SettingsPage::save_common(RecordOptions &record_options) {
        std::stoi("23");
        record_options.record_area_option = record_area_box_ptr->get_selected_id();
        record_options.record_area_width = sv_to_int<int32_t>(area_width_entry_ptr->get_text());
        record_options.record_area_height = sv_to_int<int32_t>(area_height_entry_ptr->get_text());
        record_options.video_width = sv_to_int<int32_t>(video_width_entry_ptr->get_text());
        record_options.video_height = sv_to_int<int32_t>(video_height_entry_ptr->get_text());
        record_options.fps = sv_to_int<int32_t>(framerate_entry_ptr->get_text());
        record_options.video_bitrate = sv_to_int<int32_t>(video_bitrate_entry_ptr->get_text());
        record_options.change_video_resolution = change_video_resolution_checkbox_ptr->is_checked();
        save_audio_tracks(record_options.audio_tracks_list, audio_track_section_list_ptr);
        record_options.color_range = color_range_box_ptr->get_selected_id();
        record_options.video_quality = video_quality_box_ptr->get_selected_id();
        record_options.video_codec = video_codec_box_ptr->get_selected_id();
        record_options.audio_codec = audio_codec_box_ptr->get_selected_id();
        record_options.framerate_mode = framerate_mode_box_ptr->get_selected_id();
        record_options.advanced_view = view_radio_button_ptr->get_selected_id() == "advanced";
        // TODO:
        //record_options.overclock = false;
        record_options.record_cursor = record_cursor_checkbox_ptr->is_checked();
        record_options.restore_portal_session = restore_portal_session_checkbox_ptr->is_checked();
        record_options.show_notifications = show_notification_checkbox_ptr->is_checked();
        record_options.use_led_indicator = led_indicator_checkbox_ptr->is_checked();
        record_options.low_power_mode = low_power_mode_checkbox_ptr->is_checked();
        record_options.enable_vulkan_video_encoding = enable_vulkan_checkbox_ptr->is_checked();

        // TODO: Set selected_camera_setup properly when updating and shit

        if(selected_camera_setup.has_value())
            webcam_box_size = clamp_keep_aspect_ratio(selected_camera_setup->resolution.to_vec2f(), webcam_box_size);

        record_options.webcam_source = webcam_sources_box_ptr->get_selected_id();
        record_options.webcam_flip_horizontally = flip_camera_horizontally_checkbox_ptr->is_checked();
        record_options.webcam_x = std::round((webcam_box_pos.x / screen_inner_size.x) * 100.0f);
        record_options.webcam_y = std::round((webcam_box_pos.y / screen_inner_size.y) * 100.0f);
        record_options.webcam_width = std::round((webcam_box_size.x / screen_inner_size.x) * 100.0f);
        record_options.webcam_height = std::round((webcam_box_size.y / screen_inner_size.y) * 100.0f);

        if(record_options.record_area_width == 0)
            record_options.record_area_width = 1920;

        if(record_options.record_area_height == 0)
            record_options.record_area_height = 1080;

        if(record_options.video_width == 0)
            record_options.video_width = 1920;

        if(record_options.video_height == 0)
            record_options.video_height = 1080;

        if(record_options.record_area_width < 32) {
            record_options.record_area_width = 32;
            area_width_entry_ptr->set_text("32");
        }

        if(record_options.record_area_height < 32) {
            record_options.record_area_height = 32;
            area_height_entry_ptr->set_text("32");
        }

        if(record_options.video_width < 32) {
            record_options.video_width = 32;
            video_width_entry_ptr->set_text("32");
        }

        if(record_options.video_height < 32) {
            record_options.video_height = 32;
            video_height_entry_ptr->set_text("32");
        }

        if(record_options.fps < 1) {
            record_options.fps = 1;
            framerate_entry_ptr->set_text("1");
        }

        if(record_options.video_bitrate < 1) {
            record_options.video_bitrate = 1;
            video_bitrate_entry_ptr->set_text("1");
        }
    }

    void SettingsPage::save_replay() {
        save_common(config.replay_config.record_options);
        config.replay_config.turn_on_replay_automatically_mode = turn_on_replay_automatically_mode_ptr->get_selected_id();
        config.replay_config.save_video_in_game_folder = save_replay_in_game_folder_ptr->is_checked();
        config.replay_config.only_start_replay_if_power_supply_connected = replay_power_supply_checkbox_ptr->is_checked();
        if(restart_replay_on_save)
            config.replay_config.restart_replay_on_save = restart_replay_on_save->is_checked();
        config.replay_config.save_directory = save_directory_button_ptr->get_text();
        config.replay_config.container = container_box_ptr->get_selected_id();
        config.replay_config.replay_time = sv_to_int<int32_t>(replay_time_entry_ptr->get_text());
        config.replay_config.replay_storage = replay_storage_button_ptr->get_selected_id();

        if(config.replay_config.replay_time < 5) {
            config.replay_config.replay_time = 5;
            replay_time_entry_ptr->set_text("5");
        }
    }

    void SettingsPage::save_record() {
        save_common(config.record_config.record_options);
        config.record_config.save_video_in_game_folder = save_recording_in_game_folder_ptr->is_checked();
        config.record_config.save_directory = save_directory_button_ptr->get_text();
        config.record_config.container = container_box_ptr->get_selected_id();
    }

    void SettingsPage::save_stream() {
        save_common(config.streaming_config.record_options);
        config.streaming_config.streaming_service = streaming_service_box_ptr->get_selected_id();
        config.streaming_config.youtube.stream_key = youtube_stream_key_entry_ptr->get_text();
        config.streaming_config.twitch.stream_key = twitch_stream_key_entry_ptr->get_text();
        config.streaming_config.rumble.stream_key = rumble_stream_key_entry_ptr->get_text();
        config.streaming_config.kick.stream_url = kick_stream_url_entry_ptr->get_text();
        config.streaming_config.kick.stream_key = kick_stream_key_entry_ptr->get_text();
        config.streaming_config.custom.url = stream_url_entry_ptr->get_text();
        config.streaming_config.custom.key = stream_key_entry_ptr->get_text();
        config.streaming_config.custom.container = container_box_ptr->get_selected_id();
    }
}