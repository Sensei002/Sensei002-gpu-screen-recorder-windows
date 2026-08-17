/* ui-golden-test (Phase 10 remaining): renders the real settings page UI
 * headless on Windows and compares it against a committed golden image.
 *
 * This exercises the full UI render path that unit tests can't: theme
 * texture loading, fontconfig text layout, widget draw calls, and the WGL
 * swap — an end-to-end "the overlay really draws" check on CI's virtual
 * display (GDI Generic software GL).
 *
 * Golden strategy (self-bootstrapping + tolerant):
 *   - The render is always written to ui-golden-render.ppm in the CWD so
 *     CI can upload it as an artifact.
 *   - If the committed golden (tests/golden/ui-settings-golden.ppm) does not
 *     exist, the test passes with a warning ("bootstrapped") so the first CI
 *     run is green; the artifact render is then committed as the golden.
 *   - If GSR_GOLDEN_UPDATE=1, the render is written over the golden path and
 *     the test passes (explicit re-baseline).
 *   - Otherwise the render is compared per-pixel with a small per-channel
 *     tolerance (antialiasing/fontconfig differences) and requires >= 99.5%
 *     of pixels to match.
 *
 * Usage: ui-golden-test <repo-root>   (absolute path, e.g. ${CMAKE_SOURCE_DIR})
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

/* The mgl C headers have no extern "C" guards; the C++ wrapper (mglpp)
   wraps them itself, so C++ TUs must do the same. */
extern "C" {
#include <mgl/mgl.h>
}
#include <mglpp/window/Window.hpp>

#include "Config.hpp"
#include "GsrInfo.hpp"
#include "Theme.hpp"
#include "Translation.hpp"
#include "gui/PageStack.hpp"
#include "gui/SettingsPage.hpp"

static int num_checks = 0;
static int num_failures = 0;

#define CHECK(cond) do { \
    ++num_checks; \
    if(!(cond)) { \
        ++num_failures; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while(0)

struct Image {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels; /* RGB, row-major, top-left origin */
};

static void write_ppm(const char *path, const Image &image) {
    FILE *f = fopen(path, "wb");
    if(!f) {
        fprintf(stderr, "error: failed to write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", image.width, image.height);
    fwrite(image.pixels.data(), 1, image.pixels.size(), f);
    fclose(f);
}

static bool read_ppm(const char *path, Image *image) {
    FILE *f = fopen(path, "rb");
    if(!f)
        return false;
    char magic[3] = { 0 };
    if(fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) {
        fclose(f);
        return false;
    }
    int width = 0;
    int height = 0;
    int maxval = 0;
    if(fscanf(f, "%d %d %d", &width, &height, &maxval) != 3) {
        fclose(f);
        return false;
    }
    /* Skip exactly one whitespace byte after the header. */
    fgetc(f);
    if(maxval != 255 || width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        fclose(f);
        return false;
    }
    image->width = width;
    image->height = height;
    image->pixels.resize((size_t)width * (size_t)height * 3);
    if(fread(image->pixels.data(), 1, image->pixels.size(), f) != image->pixels.size()) {
        fclose(f);
        image->pixels.clear();
        return false;
    }
    fclose(f);
    return true;
}

/* Renders |page| into the window and captures the framebuffer. */
static Image render_page(mgl::Window &window, gsr::Page &page) {
    window.clear();
    page.draw(window, mgl::vec2f(0.0f, 0.0f));

    /* Read BEFORE mgl_window_display (SwapBuffers): the back buffer is only
       defined until the swap. */
    const int width = window.get_size().x;
    const int height = window.get_size().y;
    std::vector<unsigned char> flipped((size_t)width * (size_t)height * 3);
    std::vector<unsigned char> raw((size_t)width * (size_t)height * 4);

    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());

    /* GL is bottom-up; PPM is top-down. */
    for(int y = 0; y < height; ++y) {
        memcpy(&flipped[(size_t)(height - 1 - y) * width * 3],
               &raw[(size_t)y * width * 4], (size_t)width * 3);
    }

    Image image;
    image.width = width;
    image.height = height;
    image.pixels = std::move(flipped);
    return image;
}

static int compare_images(const Image &render, const Image &golden) {
    if(render.width != golden.width || render.height != golden.height) {
        fprintf(stderr, "size mismatch: render %dx%d vs golden %dx%d\n",
            render.width, render.height, golden.width, golden.height);
        return 1;
    }
    size_t total = render.pixels.size() / 3;
    size_t mismatched = 0;
    for(size_t i = 0; i < render.pixels.size(); i += 3) {
        const int dr = abs((int)render.pixels[i] - (int)golden.pixels[i]);
        const int dg = abs((int)render.pixels[i + 1] - (int)golden.pixels[i + 1]);
        const int db = abs((int)render.pixels[i + 2] - (int)golden.pixels[i + 2]);
        if(dr > 4 || dg > 4 || db > 4)
            ++mismatched;
    }
    const double match_ratio = 1.0 - (double)mismatched / (double)total;
    fprintf(stderr, "golden compare: %zu/%zu pixels within tolerance (%.4f%%)\n",
        total - mismatched, total, match_ratio * 100.0);
    /* Tolerant by design: fontconfig/antialiasing differences between
       runner images are a few pixels, not pages. */
    return match_ratio >= 0.995 ? 0 : 1;
}

int main(int argc, char **argv) {
    printf("ui-golden-test: settings page golden render test\n");

    if(argc < 2) {
        fprintf(stderr, "usage: ui-golden-test <repo-root>\n");
        return 1;
    }
    const std::string repo_root = argv[1];
    const std::string resources_path = repo_root + "/ui/";
    const std::string golden_path = repo_root + "/tests/golden/ui-settings-golden.ppm";

    if(mgl_init(MGL_WINDOW_SYSTEM_WIN32) != 0) {
        fprintf(stderr, "FAIL: mgl_init failed\n");
        return 1;
    }

    /* Everything that touches the GL context / theme lives in this scope so
       the mgl window (and the widgets referencing theme textures) are
       destroyed before the theme and mgl are torn down. */
    Image render;
    {
        mgl::Window window;
        mgl::Window::CreateParams params;
        params.size = mgl::vec2i(1280, 720);
        params.hidden = true;
        params.graphics_api = MGL_GRAPHICS_API_WGL;
        CHECK(window.create("ui-golden-test", params));
        if(!window.internal_window())
            return 1;

        /* Theme + translations + color theme: what Overlay::show() does
           before building pages. */
        if(!gsr::init_theme(resources_path)) {
            fprintf(stderr, "FAIL: init_theme failed (resources: %s)\n", resources_path.c_str());
            return 1;
        }
        gsr::get_theme().set_window_size(mgl::vec2i(1280, 720));
        gsr::Translation::instance().init((resources_path + "translations/").c_str(), "en");

        gsr::GsrInfo gsr_info; /* all defaults */
        gsr::Config config(gsr::SupportedCaptureOptions{});
        gsr::init_color_theme(config, gsr_info);

        /* Build the real settings page (RECORD tab) and render one frame. */
        gsr::PageStack page_stack;
        gsr::SettingsPage settings_page(gsr::SettingsPage::Type::RECORD, &gsr_info, config, &page_stack, true);
        settings_page.on_navigate_to_page();

        render = render_page(window, settings_page);
    }

    /* Always write the render next to the test so CI can upload it. */
    write_ppm("ui-golden-render.ppm", render);
    CHECK(!render.pixels.empty());
    if(render.pixels.empty())
        return 1;

    const char *update_env = getenv("GSR_GOLDEN_UPDATE");
    if(update_env && strcmp(update_env, "1") == 0) {
        write_ppm(golden_path.c_str(), render);
        printf("golden updated: %s\n", golden_path.c_str());
    } else {
        Image golden;
        if(!read_ppm(golden_path.c_str(), &golden)) {
            fprintf(stderr, "warning: golden %s missing — bootstrapped (commit ui-golden-render.ppm as the golden)\n", golden_path.c_str());
        } else {
            CHECK(compare_images(render, golden) == 0);
        }
    }

    gsr::deinit_color_theme();
    gsr::deinit_theme();
    mgl_deinit();

    printf("\n%d checks, %d failures\n", num_checks, num_failures);
    return num_failures == 0 ? 0 : 1;
}
