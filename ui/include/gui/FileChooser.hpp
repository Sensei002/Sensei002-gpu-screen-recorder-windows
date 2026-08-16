#pragma once

#include "Widget.hpp"
#include "ScrollablePage.hpp"
#include <functional>
#include <vector>

#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/system/Clock.hpp>

// This currently only supports displaying folders
// TODO: Support files as well

namespace gsr {
    class FileChooser;

    class FileChooserBody : public Widget {
        friend class FileChooser;
    public:
        FileChooserBody(FileChooser *file_chooser, mgl::vec2f size);
        FileChooserBody(const FileChooserBody&) = delete;
        FileChooserBody& operator=(const FileChooserBody&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;

        mgl::vec2f get_size() override;
        mgl::vec2f get_inner_size() override;
        void set_size(mgl::vec2f size);
    private:
        void set_current_directory(std::string_view directory);
    private:
        struct Folder {
            mgl::Text text;
            time_t last_modified_seconds = 0;
        };

        FileChooser *file_chooser = nullptr;
        mgl::vec2f size;
        mgl::vec2f inner_size;
        std::vector<Folder> folders;
        int mouse_over_item = -1;
        int selected_item = -1;
        mgl::Clock double_click_timer;
        int times_clicked_within_timer = 0;
    };

    class FileChooser : public Widget {
    public:
        FileChooser(std::string_view start_directory, mgl::vec2f size);
        FileChooser(const FileChooser&) = delete;
        FileChooser& operator=(const FileChooser&) = delete;

        bool on_event(mgl::Event &event, mgl::Window &window, mgl::vec2f offset) override;
        void draw(mgl::Window &window, mgl::vec2f offset) override;
        void draw_navigation(mgl::Window &window, mgl::vec2f draw_pos);

        mgl::vec2f get_size() override;

        void set_current_directory(std::string_view directory);
        void open_subdirectory(std::string_view name);
        void open_parent_directory();
        std::string_view get_current_directory() const;
    private:
        struct Folder {
            mgl::Text text;
            time_t last_modified_seconds = 0;
        };

        mgl::vec2f size;
        mgl::Text current_directory_text;
        mgl::Sprite up_arrow_sprite;
        ScrollablePage scrollable_page;
        FileChooserBody *file_chooser_body_ptr = nullptr;
    };
}