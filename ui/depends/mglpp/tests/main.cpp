
#include <stdio.h>
#include <mglpp/mglpp.hpp>
#include <mglpp/window/Window.hpp>
#include <mglpp/window/Event.hpp>
#include <mglpp/graphics/Texture.hpp>
#include <mglpp/graphics/Sprite.hpp>
#include <mglpp/graphics/Text.hpp>
#include <mglpp/graphics/Rectangle.hpp>
#include <mglpp/graphics/VertexBuffer.hpp>
#include <mglpp/graphics/Vertex.hpp>
#include <mglpp/graphics/Shader.hpp>
#include <mglpp/system/Clock.hpp>
#include <mglpp/system/MemoryMappedFile.hpp>
#include <mglpp/system/Utf8.hpp>

struct Delegate {
    Delegate() {}

    void draw() {
        mgl::Rectangle rect(window->get_mouse_position().to_vec2f(), { 100.0f, 500.0f });
        rect.set_color({255, 0, 0, 255});
        window->draw(rect);

        shader_program->set_uniform("resolution", mgl::vec2f(texture->get_size().x, texture->get_size().y));

        mgl::Sprite sprite(texture, { 100.0f - 10.0f, 0.0f });
        sprite.set_color({255, 255, 255, 128});
        window->draw(sprite, shader_program);

        std::string str = "hello world!\nelapsed time: " + std::to_string(clock.get_elapsed_time_seconds());
        mgl::Text text(str, { 0.0f, 0.0f }, "Sans 30");
        window->draw(text);
    }

    mgl::Window *window;
    mgl::Texture *texture;
    mgl::Shader *shader_program;
    mgl::Clock clock;
};

int main(int argc, char **argv) {
    mgl::Init init(mgl::WindowSystem::NATIVE);

    mgl::Window::CreateParams window_create_params;
    window_create_params.size = { 1280, 720 };
    mgl::Window window;
    if(!window.create("mglpp", window_create_params))
        return 1;

    mgl::Texture texture;
    if(!texture.load_from_file("depends/mgl/tests/X11.jpg"))
        return 1;

    mgl::Shader shader;
    if(!shader.load_from_file("depends/mgl/tests/circle_mask.glsl", mgl::Shader::Fragment))
        return 1;

    Delegate delegate;
    delegate.window = &window;
    delegate.texture = &texture;
    delegate.shader_program = &shader;

    mgl::Event event;
    while(window.is_open()) {
        while(window.poll_event(event)) {
            
        }

        window.clear(mgl::Color(0, 0, 0, 255));
        delegate.draw();
        window.display();
    }

    return 0;
}
