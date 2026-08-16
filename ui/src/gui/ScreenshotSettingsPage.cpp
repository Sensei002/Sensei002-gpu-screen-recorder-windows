#include "../../include/gui/ScreenshotSettingsPage.hpp"
#include "../../include/gui/GsrPage.hpp"
#include "../../include/gui/PageStack.hpp"
#include "../../include/Theme.hpp"
#include "../../include/GsrInfo.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Translation.hpp"
#include "../../include/gui/List.hpp"
#include "../../include/gui/ScrollablePage.hpp"
#include "../../include/gui/Label.hpp"
#include "../../include/gui/Subsection.hpp"
#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/Image.hpp"

#include <charconv>

namespace gsr {
    template <typename T>
    static T sv_to_int(std::string_view str) {
        T result = 0;
        std::from_chars(str.data(), str.data() + str.size(), result);
        return result;
    }

    ScreenshotSettingsPage::ScreenshotSettingsPage(const GsrInfo *gsr_info, Config &config, PageStack *page_stack, bool supports_window_title, bool propery_supports_clipboard_image) :
        StaticPage(mgl::vec2f(get_theme().window_width, get_theme().window_height).floor()),
        config(config),
        gsr_info(gsr_info),
        page_stack(page_stack),
        supports_window_title(supports_window_title),
        propery_supports_clipboard_image(propery_supports_clipboard_image)
    {
        capture_options = get_supported_capture_options(*gsr_info);

        auto content_page = std::make_unique<GsrPage>(TR("Screenshot"), TR("Settings"));
        content_page->add_button(TR("Back"), "back", get_color_theme().page_bg_color);
        content_page->on_click = [page_stack](const std::string &id) {
            if(id == "back")
                page_stack->pop();
        };
        content_page_ptr = content_page.get();
        add_widget(std::move(content_page));

        add_widgets();
        load();
    }

    std::unique_ptr<ComboBox> ScreenshotSettingsPage::create_record_area_box() {
        auto record_area_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        // TODO: Show options not supported but disable them
        if(capture_options.window)
            record_area_box->add_item(TR("Window"), "window");
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

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_record_area() {
        auto record_area_list = std::make_unique<List>(List::Orientation::VERTICAL);
        record_area_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Capture source:"), get_color_theme().text_color));
        record_area_list->add_widget(create_record_area_box());
        return record_area_list;
    }

    std::unique_ptr<Entry> ScreenshotSettingsPage::create_image_width_entry() {
        auto image_width_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1920", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        image_width_entry->set_number_mode(true, 1, 1 << 15);
        image_width_entry_ptr = image_width_entry.get();
        return image_width_entry;
    }

    std::unique_ptr<Entry> ScreenshotSettingsPage::create_image_height_entry() {
        auto image_height_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "1080", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 4);
        image_height_entry->set_number_mode(true, 1, 1 << 15);
        image_height_entry_ptr = image_height_entry.get();
        return image_height_entry;
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_image_resolution() {
        auto area_size_params_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        area_size_params_list->add_widget(create_image_width_entry());
        area_size_params_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), "x", get_color_theme().text_color));
        area_size_params_list->add_widget(create_image_height_entry());
        return area_size_params_list;
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_image_resolution_section() {
        auto image_resolution_list = std::make_unique<List>(List::Orientation::VERTICAL);
        image_resolution_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Image resolution limit:"), get_color_theme().text_color));
        image_resolution_list->add_widget(create_image_resolution());
        image_resolution_list_ptr = image_resolution_list.get();
        return image_resolution_list;
    }

    std::unique_ptr<CheckBox> ScreenshotSettingsPage::create_restore_portal_session_checkbox() {
        auto restore_portal_session_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Restore portal session"));
        restore_portal_session_checkbox->set_checked(true);
        restore_portal_session_checkbox_ptr = restore_portal_session_checkbox.get();
        return restore_portal_session_checkbox;
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_restore_portal_session_section() {
        auto restore_portal_session_list = std::make_unique<List>(List::Orientation::VERTICAL);
        restore_portal_session_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), " ", get_color_theme().text_color));
        restore_portal_session_list->add_widget(create_restore_portal_session_checkbox());
        restore_portal_session_list_ptr = restore_portal_session_list.get();
        return restore_portal_session_list;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_change_image_resolution_section() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Change image resolution"));
        change_image_resolution_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_capture_target_section() {
        auto ll = std::make_unique<List>(List::Orientation::VERTICAL);

        auto capture_target_list = std::make_unique<List>(List::Orientation::HORIZONTAL, List::Alignment::CENTER);
        capture_target_list->add_widget(create_record_area());
        capture_target_list->add_widget(create_image_resolution_section());
        capture_target_list->add_widget(create_restore_portal_session_section());

        ll->add_widget(std::move(capture_target_list));
        ll->add_widget(create_change_image_resolution_section());

        return std::make_unique<Subsection>(TR("Capture"), std::move(ll), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_image_quality_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Image quality:"), get_color_theme().text_color));

        auto image_quality_box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        image_quality_box->add_item(TR("Medium"), "medium");
        image_quality_box->add_item(TR("High"), "high");
        image_quality_box->add_item(TR("Very high (Recommended)"), "very_high");
        image_quality_box->add_item(TR("Ultra"), "ultra");
        image_quality_box->set_selected_item("very_high");

        image_quality_box_ptr = image_quality_box.get();
        list->add_widget(std::move(image_quality_box));

        return list;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_record_cursor_section() {
        auto record_cursor_checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Record cursor"));
        record_cursor_checkbox->set_checked(true);
        record_cursor_checkbox_ptr = record_cursor_checkbox.get();
        return record_cursor_checkbox;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_image_section() {
        auto image_section_list = std::make_unique<List>(List::Orientation::VERTICAL);
        image_section_list->add_widget(create_image_quality_section());
        image_section_list->add_widget(create_record_cursor_section());
        return std::make_unique<Subsection>(TR("Image"), std::move(image_section_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_save_directory(const char *label) {
        auto save_directory_list = std::make_unique<List>(List::Orientation::VERTICAL);
        save_directory_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), label, get_color_theme().text_color));
        auto save_directory_button = std::make_unique<Button>(get_theme().body_font_desc.c_str(), get_pictures_dir().c_str(), mgl::vec2f(0.0f, 0.0f), mgl::Color(0, 0, 0, 120));
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

    std::unique_ptr<ComboBox> ScreenshotSettingsPage::create_image_format_box() {
        auto box = std::make_unique<ComboBox>(get_theme().body_font_desc.c_str());
        if(gsr_info->supported_image_formats.jpeg)
            box->add_item("jpg", "jpg");
        if(gsr_info->supported_image_formats.png)
            box->add_item("png", "png");
        image_format_box_ptr = box.get();
        return box;
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_image_format_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Image format:"), get_color_theme().text_color));
        list->add_widget(create_image_format_box());
        return list;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_file_info_section() {
        auto file_info_data_list = std::make_unique<List>(List::Orientation::HORIZONTAL);
        file_info_data_list->add_widget(create_save_directory(TR("Directory to save screenshots:")));
        file_info_data_list->add_widget(create_image_format_section());
        return std::make_unique<Subsection>(TR("File info"), std::move(file_info_data_list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<CheckBox> ScreenshotSettingsPage::create_save_screenshot_in_game_folder() {
        char text[256];
        snprintf(text, sizeof(text), "%s%s", TR("Save screenshot in a folder based on the games name"), supports_window_title ? "" : " (X11 applications only)");
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), text);
        save_screenshot_in_game_folder_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<CheckBox> ScreenshotSettingsPage::create_save_screenshot_to_clipboard() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), propery_supports_clipboard_image ? TR("Save screenshot to clipboard") : TR("Save screenshot to clipboard (Not supported properly by Wayland)"));
        save_screenshot_to_clipboard_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<CheckBox> ScreenshotSettingsPage::create_save_screenshot_to_disk() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Save screenshot to disk"));
        save_screenshot_to_disk_checkbox_ptr = checkbox.get();
        checkbox->set_checked(true);
        return checkbox;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_notifications() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Show screenshot notifications"));
        checkbox->set_checked(true);
        show_notification_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_led_indicator() {
        auto checkbox = std::make_unique<CheckBox>(get_theme().body_font_desc.c_str(), TR("Blink scroll lock led when taking a screenshot"));
        checkbox->set_checked(true);
        led_indicator_checkbox_ptr = checkbox.get();
        return checkbox;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_general_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(create_save_screenshot_in_game_folder());
        list->add_widget(create_save_screenshot_to_clipboard());
        list->add_widget(create_save_screenshot_to_disk());
        return std::make_unique<Subsection>(TR("General"), std::move(list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_screenshot_indicator_section() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL);
        list->add_widget(create_notifications());
        list->add_widget(create_led_indicator());
        return std::make_unique<Subsection>(TR("Screenshot indicator"), std::move(list), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_custom_script_screenshot_entry() {
        auto list = std::make_unique<List>(List::Orientation::VERTICAL, List::Alignment::CENTER);

        auto create_custom_script_screenshot_entry = std::make_unique<Entry>(get_theme().body_font_desc.c_str(), "", 2.0f*mgl::Text::get_font_size_from_font_description(get_theme().body_font_desc.c_str()) * 20);
        create_custom_script_screenshot_entry_ptr = create_custom_script_screenshot_entry.get();
        list->add_widget(std::move(create_custom_script_screenshot_entry));

        return list;
    }

    std::unique_ptr<List> ScreenshotSettingsPage::create_custom_script_screenshot() {
        auto custom_script_screenshot_list = std::make_unique<List>(List::Orientation::VERTICAL);
        custom_script_screenshot_list->add_widget(std::make_unique<Label>(get_theme().body_font_desc.c_str(), TR("Command to open the screenshot with:"), get_color_theme().text_color));
        custom_script_screenshot_list->add_widget(create_custom_script_screenshot_entry());
        return custom_script_screenshot_list;
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_custom_script_screenshot_section() {
        return std::make_unique<Subsection>(TR("Script"), create_custom_script_screenshot(), mgl::vec2f(settings_scrollable_page_ptr->get_inner_size().x, 0.0f));
    }

    std::unique_ptr<Widget> ScreenshotSettingsPage::create_settings() {
        auto page_list = std::make_unique<List>(List::Orientation::VERTICAL);
        page_list->set_spacing(0.018f);
        auto scrollable_page = std::make_unique<ScrollablePage>(content_page_ptr->get_inner_size() - mgl::vec2f(0.0f, page_list->get_size().y));
        settings_scrollable_page_ptr = scrollable_page.get();
        page_list->add_widget(std::move(scrollable_page));

        auto settings_list = std::make_unique<List>(List::Orientation::VERTICAL);
        settings_list->set_spacing(0.018f);
        settings_list->add_widget(create_capture_target_section());
        settings_list->add_widget(create_image_section());
        settings_list->add_widget(create_file_info_section());
        settings_list->add_widget(create_general_section());
        settings_list->add_widget(create_screenshot_indicator_section());
        settings_list->add_widget(create_custom_script_screenshot_section());
        settings_scrollable_page_ptr->add_widget(std::move(settings_list));
        return page_list;
    }

    void ScreenshotSettingsPage::add_widgets() {
        content_page_ptr->add_widget(create_settings());

        record_area_box_ptr->on_selection_changed = [this](std::string_view, std::string_view id) {
            const bool portal_selected = id == "portal";
            image_resolution_list_ptr->set_visible(change_image_resolution_checkbox_ptr->is_checked());
            restore_portal_session_list_ptr->set_visible(portal_selected);
            return true;
        };

        change_image_resolution_checkbox_ptr->on_changed = [this](bool checked) {
            image_resolution_list_ptr->set_visible(checked);
        };

        if(!capture_options.monitors.empty())
            record_area_box_ptr->set_selected_item("focused_monitor");
        else if(capture_options.portal)
            record_area_box_ptr->set_selected_item("portal");
        else if(capture_options.window)
            record_area_box_ptr->set_selected_item("window");
        else
            record_area_box_ptr->on_selection_changed("", "");
    }

    void ScreenshotSettingsPage::on_navigate_away_from_page() {
        save();
    }

    void ScreenshotSettingsPage::load() {
        record_area_box_ptr->set_selected_item(config.screenshot_config.record_area_option);
        change_image_resolution_checkbox_ptr->set_checked(config.screenshot_config.change_image_resolution);
        image_quality_box_ptr->set_selected_item(config.screenshot_config.image_quality);
        image_format_box_ptr->set_selected_item(config.screenshot_config.image_format);
        record_cursor_checkbox_ptr->set_checked(config.screenshot_config.record_cursor);
        restore_portal_session_checkbox_ptr->set_checked(config.screenshot_config.restore_portal_session);
        save_directory_button_ptr->set_text(config.screenshot_config.save_directory);
        save_screenshot_in_game_folder_checkbox_ptr->set_checked(config.screenshot_config.save_screenshot_in_game_folder);
        save_screenshot_to_clipboard_checkbox_ptr->set_checked(config.screenshot_config.save_screenshot_to_clipboard);
        save_screenshot_to_disk_checkbox_ptr->set_checked(config.screenshot_config.save_screenshot_to_disk);
        show_notification_checkbox_ptr->set_checked(config.screenshot_config.show_notifications);
        led_indicator_checkbox_ptr->set_checked(config.screenshot_config.use_led_indicator);

        if(config.screenshot_config.image_width == 0)
            config.screenshot_config.image_width = 1920;

        if(config.screenshot_config.image_height == 0)
            config.screenshot_config.image_height = 1080;

        if(config.screenshot_config.image_width < 32)
            config.screenshot_config.image_width = 32;
        image_width_entry_ptr->set_text(std::to_string(config.screenshot_config.image_width));

        if(config.screenshot_config.image_height < 32)
            config.screenshot_config.image_height = 32;
        image_height_entry_ptr->set_text(std::to_string(config.screenshot_config.image_height));

        create_custom_script_screenshot_entry_ptr->set_text(config.screenshot_config.custom_script);
    }

    void ScreenshotSettingsPage::save() {
        Config prev_config = config;

        config.screenshot_config.record_area_option = record_area_box_ptr->get_selected_id();
        config.screenshot_config.image_width = sv_to_int<int32_t>(image_width_entry_ptr->get_text());
        config.screenshot_config.image_height = sv_to_int<int32_t>(image_height_entry_ptr->get_text());
        config.screenshot_config.change_image_resolution = change_image_resolution_checkbox_ptr->is_checked();
        config.screenshot_config.image_quality = image_quality_box_ptr->get_selected_id();
        config.screenshot_config.image_format = image_format_box_ptr->get_selected_id();
        config.screenshot_config.record_cursor = record_cursor_checkbox_ptr->is_checked();
        config.screenshot_config.restore_portal_session = restore_portal_session_checkbox_ptr->is_checked();
        config.screenshot_config.save_directory = save_directory_button_ptr->get_text();
        config.screenshot_config.save_screenshot_in_game_folder = save_screenshot_in_game_folder_checkbox_ptr->is_checked();
        config.screenshot_config.save_screenshot_to_clipboard = save_screenshot_to_clipboard_checkbox_ptr->is_checked();
        config.screenshot_config.save_screenshot_to_disk = save_screenshot_to_disk_checkbox_ptr->is_checked();
        config.screenshot_config.show_notifications = show_notification_checkbox_ptr->is_checked();
        config.screenshot_config.use_led_indicator = led_indicator_checkbox_ptr->is_checked();
        config.screenshot_config.custom_script = create_custom_script_screenshot_entry_ptr->get_text();

        if(config.screenshot_config.image_width == 0)
            config.screenshot_config.image_width = 1920;

        if(config.screenshot_config.image_height == 0)
            config.screenshot_config.image_height = 1080;

        if(config.screenshot_config.image_width < 32) {
            config.screenshot_config.image_width = 32;
            image_width_entry_ptr->set_text("32");
        }

        if(config.screenshot_config.image_height < 32) {
            config.screenshot_config.image_height = 32;
            image_height_entry_ptr->set_text("32");
        }

        save_config(config);

        if(on_config_changed && config != prev_config)
            on_config_changed();
    }
}
