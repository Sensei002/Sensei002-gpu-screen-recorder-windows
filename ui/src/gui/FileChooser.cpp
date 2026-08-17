#include "../../include/gui/FileChooser.hpp"
#include "../../include/gui/Utils.hpp"
#include "../../include/Utils.hpp"
#include "../../include/Theme.hpp"

#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/system/FloatRect.hpp>

#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <algorithm>

namespace gsr {
    static const float current_directory_padding_top_scale = 0.004629f;
    static const float current_directory_padding_bottom_scale = 0.004629f;
    static const float current_directory_padding_left_scale = 0.004629f;
    static const float current_directory_padding_right_scale = 0.004629f;
    static const float spacing_between_current_directory_and_content = 0.015f;
    static const int num_columns = 5;
    static const float content_padding_top_scale = 0.03f;
    static const float content_padding_bottom_scale = 0.03f;
    static const float content_padding_left_scale = 0.03f;
    static const float content_padding_right_scale = 0.03f;
    static const float content_margin_left_scale = 0.005f;
    static const float content_margin_right_scale = 0.005f;
    static const float folder_text_spacing_scale = 0.005f;
    static const float up_button_spacing_scale = 0.01f;

    FileChooserBody::FileChooserBody(FileChooser *file_chooser, mgl::vec2f size) :
        file_chooser(file_chooser), size(size), inner_size(size) {}

    bool FileChooserBody::on_event(mgl::Event &event, mgl::Window&, mgl::vec2f) {
        if(!visible)
            return true;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            if(double_click_timer.get_elapsed_time_seconds() <= get_theme().double_click_timeout_seconds) {
                ++times_clicked_within_timer;
            } else {
                times_clicked_within_timer = 1;
            }
            double_click_timer.restart();

            const int prev_selected_item = selected_item;
            selected_item = mouse_over_item;
            const bool item_changed = selected_item != prev_selected_item;
            if(item_changed)
                times_clicked_within_timer = 1;

            if(selected_item != -1 && times_clicked_within_timer > 0 && times_clicked_within_timer % 2 == 0) {
                file_chooser->open_subdirectory(folders[selected_item].text.get_string());
            }
        }
        return true;
    }

    void FileChooserBody::draw(mgl::Window &window, mgl::vec2f offset) {
        mouse_over_item = -1;

        if(!visible)
            return;

        const mgl::Scissor scissor = window.get_scissor();

        const mgl::vec2f draw_pos = position + offset;
        const mgl::vec2f mouse_pos = window.get_mouse_position().to_vec2f();

        const int content_padding_top = content_padding_top_scale * get_theme().window_height;
        const int content_padding_bottom = content_padding_bottom_scale * get_theme().window_height;
        const int content_padding_left = content_padding_left_scale * get_theme().window_height;
        const int content_padding_right = content_padding_right_scale * get_theme().window_height;

        const float folder_width = (int)((size.x - (content_padding_left + content_padding_right) * num_columns) / num_columns);
        const float width_per_item_after = content_padding_right + folder_width + content_padding_left;
        mgl::vec2f folder_pos = draw_pos + mgl::vec2f(content_padding_left, content_padding_top);
        bool end_is_newline = false;

        for(int i = 0; i < (int)folders.size(); ++i) {
            auto &folder = folders[i];

            mgl::Sprite folder_sprite(&get_theme().folder_texture);
            folder_sprite.set_width((int)folder_width);
            folder_sprite.set_position((folder_pos - mgl::vec2f(0.0f, folder_sprite.get_size().y * 0.3f)).floor());

            const mgl::vec2f item_pos = folder_pos - mgl::vec2f(content_padding_left, content_padding_top);
            const mgl::vec2f item_size = folder_sprite.get_size() + mgl::vec2f(content_padding_left + content_padding_right, content_padding_top + content_padding_bottom);
            if(i == selected_item) {
                mgl::Rectangle selected_item_background(item_size.floor());
                selected_item_background.set_position(item_pos.floor());
                selected_item_background.set_color(get_color_theme().tint_color);
                window.draw(selected_item_background);
            }

            if(!has_parent_with_selected_child_widget() && mouse_over_item == -1 &&
                mouse_pos.x >= scissor.position.x && mouse_pos.x <= scissor.position.x + scissor.size.x &&
                mouse_pos.y >= scissor.position.y && mouse_pos.y <= scissor.position.y + scissor.size.y &&
                mgl::FloatRect(item_pos, item_size).contains(mouse_pos))
            {
                // mgl::Rectangle selected_item_background(item_size.floor());
                // selected_item_background.set_position(item_pos.floor());
                // selected_item_background.set_color(mgl::Color(20, 20, 20, 150));
                // window.draw(selected_item_background);
                const float border_scale = 0.0015f;
                draw_rectangle_outline(window, item_pos.floor(), item_size.floor(), get_color_theme().tint_color, border_scale * get_theme().window_height);
                mouse_over_item = i;
            }

            if(item_pos.y + item_size.y >= scissor.position.y && item_pos.y < scissor.position.y + scissor.size.y) {
                window.draw(folder_sprite);

                // TODO: Dont allow text to go further left/right than item_pos (on the left side) and item_pos + item_size (on the right side).
                folder.text.set_wrap_width(item_size.x);
                folder.text.set_max_rows(2);
                folder.text.set_position((folder_sprite.get_position() + mgl::vec2f(folder_sprite.get_size().x * 0.5f - folder.text.get_bounds().size.x * 0.5f, folder_sprite.get_size().y + folder_text_spacing_scale * get_theme().window_height)).floor());
                window.draw(folder.text);
            }

            folder_pos.x += width_per_item_after;
            if(folder_pos.x + folder_width > draw_pos.x + size.x) {
                folder_pos.x = draw_pos.x + content_padding_left;
                folder_pos.y += content_padding_bottom + folder_sprite.get_size().y + content_padding_top;
                if(i == (int)folders.size() - 1)
                    end_is_newline = true;
            }
        }

        if(!end_is_newline)
            folder_pos.y += content_padding_bottom + folder_width;

        inner_size = mgl::vec2f(size.x, folder_pos.y - draw_pos.y);
    }

    void FileChooserBody::set_current_directory(std::string_view directory) {
        folders.clear();
        selected_item = -1;
        mouse_over_item = -1;

        char path[4096];
        snprintf(path, sizeof(path), "%.*s", (int)directory.size(), directory.data());

        DIR *d = opendir(path);
        if(!d) {
            fprintf(stderr, "gsr-ui error: failed to open directory: %s, error: %s\n", path, strerror(errno));
            return;
        }

        struct dirent *dir = NULL;
        char filepath[PATH_MAX];
        while((dir = readdir(d)) != NULL) {
            /* Ignore hidden files */
            if(dir->d_name[0] == '.')
                continue;

            snprintf(filepath, sizeof(filepath), "%s/%s", path, dir->d_name);

            struct stat st;
            if(stat(filepath, &st) == -1)
                continue;

            if(!S_ISDIR(st.st_mode))
                continue;

#ifdef _WIN32
            folders.push_back({mgl::Text(dir->d_name, get_theme().body_font_desc.c_str()), st.st_mtime});
#else
            folders.push_back({mgl::Text(dir->d_name, get_theme().body_font_desc.c_str()), st.st_mtim.tv_sec});
#endif
        }

        closedir(d);

        std::sort(folders.begin(), folders.end(), [](const Folder &folder_a, const Folder &folder_b) {
            return folder_a.last_modified_seconds > folder_b.last_modified_seconds;
        });
    }

    mgl::vec2f FileChooserBody::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    mgl::vec2f FileChooserBody::get_inner_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return inner_size;
    }

    void FileChooserBody::set_size(mgl::vec2f size) {
        this->size = size;
    }

    FileChooser::FileChooser(std::string_view start_directory, mgl::vec2f size) :
        size(size),
        current_directory_text(start_directory, get_theme().body_font_desc.c_str()),
        up_arrow_sprite(&get_theme().up_arrow_texture),
        scrollable_page(size)
    {
        auto file_chooser_body = std::make_unique<FileChooserBody>(this, size);
        file_chooser_body_ptr = file_chooser_body.get();
        scrollable_page.add_widget(std::move(file_chooser_body));
        set_current_directory(start_directory);
    }

    bool FileChooser::on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return true;

        if(event.type == mgl::Event::MouseButtonPressed && event.mouse_button.button == mgl::Mouse::Left) {
            if(mgl::FloatRect(up_arrow_sprite.get_position(), up_arrow_sprite.get_size()).contains(mgl::vec2f(event.mouse_button.x, event.mouse_button.y))) {
                open_parent_directory();
                return false;
            }
        }

        return scrollable_page.on_event(event, window, offset);
    }

    void FileChooser::draw(mgl::Window &window, mgl::vec2f offset) {
        if(!visible)
            return;

        const mgl::vec2f draw_pos_start = position + offset;
        mgl::vec2f draw_pos = draw_pos_start;
        const mgl::vec2f current_directory_padding(
            current_directory_padding_left_scale * get_theme().window_height + current_directory_padding_right_scale * get_theme().window_height,
            current_directory_padding_top_scale * get_theme().window_height + current_directory_padding_bottom_scale * get_theme().window_height
        );

        const float current_directory_background_height = (int)(current_directory_text.get_bounds().size.y + current_directory_padding.y);

        draw_pos += mgl::vec2f(0.0f, current_directory_background_height + spacing_between_current_directory_and_content * get_theme().window_height);
        const mgl::vec2f body_size = mgl::vec2f(size.x, size.y - (draw_pos.y - draw_pos_start.y)).floor();
        scrollable_page.set_size(body_size);
        file_chooser_body_ptr->set_size(scrollable_page.get_inner_size());

        mgl::Rectangle content_background(scrollable_page.get_inner_size().floor());
        content_background.set_position(draw_pos.floor());
        content_background.set_color(mgl::Color(0, 0, 0, 120));
        window.draw(content_background);

        draw_navigation(window, draw_pos_start);

        scrollable_page.draw(window, draw_pos.floor());
    }

    void FileChooser::draw_navigation(mgl::Window &window, mgl::vec2f draw_pos) {
        const mgl::vec2f current_directory_padding(
            current_directory_padding_left_scale * get_theme().window_height + current_directory_padding_right_scale * get_theme().window_height,
            current_directory_padding_top_scale * get_theme().window_height + current_directory_padding_bottom_scale * get_theme().window_height
        );
        mgl::vec2f current_directory_background_size = mgl::vec2f(size.x, current_directory_text.get_bounds().size.y + current_directory_padding.y).floor();
        up_arrow_sprite.set_height((int)(current_directory_background_size.y * 0.8f));
        up_arrow_sprite.set_position((draw_pos + mgl::vec2f(file_chooser_body_ptr->get_size().x - up_arrow_sprite.get_size().x, current_directory_background_size.y * 0.5f - up_arrow_sprite.get_size().y * 0.5f)).floor());
        const bool mouse_inside_up_arrow = mgl::FloatRect(up_arrow_sprite.get_position(), up_arrow_sprite.get_size()).contains(window.get_mouse_position().to_vec2f()) && !has_parent_with_selected_child_widget();
        up_arrow_sprite.set_color(mouse_inside_up_arrow ? get_color_theme().tint_color : mgl::Color(255, 255, 255));
        window.draw(up_arrow_sprite);

        current_directory_background_size.x = file_chooser_body_ptr->get_size().x - up_arrow_sprite.get_size().x - up_button_spacing_scale * get_theme().window_height;
        mgl::Rectangle current_directory_background(current_directory_background_size.floor());
        current_directory_background.set_color(mgl::Color(0, 0, 0, 120));
        current_directory_background.set_position(draw_pos.floor());
        window.draw(current_directory_background);

        current_directory_text.set_color(get_color_theme().text_color);
        current_directory_text.set_position((draw_pos + mgl::vec2f(current_directory_padding.x, current_directory_background_size.y * 0.5f - current_directory_text.get_bounds().size.y * 0.5f)).floor());
        window.draw(current_directory_text);
    }

    mgl::vec2f FileChooser::get_size() {
        if(!visible)
            return {0.0f, 0.0f};

        return size;
    }

    void FileChooser::set_current_directory(std::string_view directory) {
        current_directory_text.set_string(directory);
        file_chooser_body_ptr->set_current_directory(directory);
        scrollable_page.reset_scroll();
    }

    void FileChooser::open_subdirectory(std::string_view name) {
        char filepath[PATH_MAX];
        const std::string_view current_dir = current_directory_text.get_string();
        if(current_dir == "/") {
            snprintf(filepath, sizeof(filepath), "/%.*s", (int)name.size(), name.data());
        } else {
            snprintf(filepath, sizeof(filepath), "%.*s/%.*s", (int)current_dir.size(), current_dir.data(), (int)name.size(), name.data());
        }
        set_current_directory(filepath);
    }

    void FileChooser::open_parent_directory() {
        set_current_directory(get_parent_directory(current_directory_text.get_string()).c_str());
    }

    std::string_view FileChooser::get_current_directory() const {
        return current_directory_text.get_string();
    }
}
