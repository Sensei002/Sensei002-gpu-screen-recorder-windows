project('mglpp', ['cpp'], version : '1.1.0', default_options : ['warning_level=2', 'cpp_std=c++17'], subproject_dir : 'depends')

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['cpp'])
endif

src = [
    'src/graphics/Text.cpp',
    'src/graphics/TextEdit.cpp',
    'src/graphics/Image.cpp',
    'src/graphics/VertexBuffer.cpp',
    'src/graphics/Rectangle.cpp',
    'src/graphics/Texture.cpp',
    'src/graphics/Sprite.cpp',
    'src/graphics/Shader.cpp',
    'src/system/MemoryMappedFile.cpp',
    'src/system/Clock.cpp',
    'src/system/Utf8.cpp',
    'src/window/Window.cpp',
    'src/window/Keyboard.cpp',
    'src/mglpp.cpp',
]

mgl_proj = subproject('mgl', default_options : ['wayland=' + get_option('wayland').to_string()])
mgl_dep = mgl_proj.get_variable('mgl_dep')

dep = [
    mgl_dep,
]

public_headers = include_directories('include')

project_target = static_library(
    meson.project_name(),
    src,
    install : false,
    include_directories : public_headers,
    dependencies : dep,
)

project_dep = declare_dependency(include_directories : public_headers, link_with : project_target, dependencies : mgl_dep)
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
        files(['tests/main.cpp']),
        dependencies : [project_dep, test_dep],
        install : false
    )
endif