project('mgl', ['c'], version : '1.1.0', default_options : ['warning_level=2'])

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c'])
endif

src = [
    'src/graphics/texture.c',
    'src/graphics/sprite.c',
    'src/graphics/image.c',
    'src/graphics/font_atlas.c',
    'src/graphics/glyph_map.c',
    'src/graphics/quad_batch.c',
    'src/graphics/text.c',
    'src/graphics/text_edit.c',
    'src/graphics/vertex.c',
    'src/graphics/primitive_type.c',
    'src/graphics/vertex_buffer.c',
    'src/graphics/shader.c',
    'src/graphics/rectangle.c',
    'src/graphics/backend/graphics.c',
    'src/graphics/backend/glx.c',
    'src/graphics/backend/egl.c',
    'src/system/fileutils.c',
    'src/system/utf8.c',
    'src/system/clock.c',
    'src/mgl.c',
    'src/window/x11.c',
    'src/window/window.c',
    'src/window/key.c',
    'src/gl.c',
]

cc = meson.get_compiler('c')
dep = [
    dependency('pangoft2'),
    dependency('gio-2.0'),
    dependency('x11'),
    dependency('xrender'),
    dependency('xrandr'),
    dependency('threads'),
    cc.find_library('m'),
]

if get_option('wayland') == true
    src += [
        'src/window/wayland.c',
    ]

    subdir('protocol')
    src += protocol_src

    dep += [
        dependency('wayland-client'),
        dependency('wayland-egl'),
        dependency('wayland-cursor'),
        dependency('xkbcommon'),
    ]

    libdecor_dep = dependency('libdecor-0', required: false)
    if libdecor_dep.found()
        dep += libdecor_dep
        add_project_arguments('-DMGL_LIBDECOR', language: ['c'])
    endif

    add_project_arguments('-DMGL_WAYLAND', language : ['c'])
endif

public_headers = include_directories('include')

project_target = static_library(
    meson.project_name(),
    src,
    install : false,
    include_directories : public_headers,
    dependencies : dep,
)

project_dep = declare_dependency(include_directories : public_headers, link_with : project_target)
set_variable(meson.project_name() + '_dep', project_dep)

#install_headers(project_headers, subdir : meson.project_name(), preserve_path : true)

# pkg_mod = import('pkgconfig')
# pkg_mod.generate(
#     name : meson.project_name(),
#     filebase : meson.project_name(),
#     description : 'Minimal Graphics Library',
#     subdirs : meson.project_name(),
#     libraries : project_target,
# )

if not meson.is_subproject() and get_option('tests') == true
    subdir('tests')
    executable('run_tests',
        files(['tests/main.c']),
        dependencies : [project_dep, test_dep],
        install : false
    )
endif