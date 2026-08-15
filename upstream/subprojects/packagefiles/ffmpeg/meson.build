project('ffmpeg', 'c', version : '9.0')

# Neither ffmpeg nor the libraries it depends on use meson, so they are built with their
# own build systems into a shared prefix and then exposed to gpu-screen-recorder as
# regular pkg-config dependencies.

install_prefix = meson.current_build_dir() / 'install'
c_compiler = ' '.join(meson.get_compiler('c').cmd_array())
build_library = find_program(meson.global_source_root() / 'extra' / 'build_ffmpeg.sh')

# srt links against mbedtls and ffmpeg links against all of them, so the order matters.
libraries = ['nv-codec-headers', 'x264', 'opus', 'mbedtls', 'srt', 'ffmpeg']

foreach library : libraries
    source_dir = library == 'ffmpeg' ? meson.current_source_dir() : subproject(library).get_variable('source_dir')
    build_result = run_command(build_library, library, source_dir, meson.current_build_dir() / library, install_prefix, c_compiler, check : true)
    if build_result.stderr().strip() != ''
        message(build_result.stderr().strip())
    endif
endforeach

ffmpeg_libs = ['libavformat', 'libavfilter', 'libavcodec', 'libswresample', 'libavutil']

pkgconfig = find_program('pkg-config')
pkgconfig_env = environment()
pkgconfig_env.prepend('PKG_CONFIG_PATH', install_prefix / 'lib' / 'pkgconfig')

ffmpeg_cflags = run_command(pkgconfig, '--cflags', ffmpeg_libs, env : pkgconfig_env, check : true).stdout().split()
ffmpeg_ldflags = run_command(pkgconfig, '--static', '--libs', ffmpeg_libs, env : pkgconfig_env, check : true).stdout().split()

ffmpeg_dep = declare_dependency(compile_args : ffmpeg_cflags, link_args : ffmpeg_ldflags)

foreach ffmpeg_lib : ffmpeg_libs
    meson.override_dependency(ffmpeg_lib, ffmpeg_dep)
endforeach
