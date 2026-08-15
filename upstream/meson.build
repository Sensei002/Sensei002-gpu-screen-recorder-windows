project('gpu-screen-recorder', 'c', version : '6.0.0', default_options : ['c_std=gnu11', 'warning_level=2'])

add_project_arguments('-Wshadow', language : 'c')
if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : 'c')
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : 'c')
endif

src = [
    'kms/client/kms_client.c',
    'src/capture/capture.c',
    'src/capture/nvfbc.c',
    'src/capture/xcomposite.c',
    'src/capture/ximage.c',
    'src/capture/kms.c',
    'src/capture/v4l2.c',
    'src/encoder/encoder.c',
    'src/encoder/video/video.c',
    'src/encoder/video/nvenc.c',
    'src/encoder/video/vaapi.c',
    'src/encoder/video/vulkan.c',
    'src/encoder/video/software.c',
    'src/codec_query/nvenc.c',
    'src/codec_query/vaapi.c',
    'src/codec_query/vulkan.c',
    'src/window/window.c',
    'src/window/x11.c',
    'src/window/wayland.c',
    'src/replay_buffer/replay_buffer.c',
    'src/replay_buffer/replay_buffer_ram.c',
    'src/replay_buffer/replay_buffer_disk.c',
    'src/log.c',
    'src/json.c',
    'src/ffmpeg_utils.c',
    'src/recorder/audio_codec.c',
    'src/recorder/video_codec.c',
    'src/recorder/codec_select.c',
    'src/recorder/windowing.c',
    'src/recorder/capture_source.c',
    'src/recorder/capture_setup.c',
    'src/recorder/audio_input.c',
    'src/recorder/audio_capture.c',
    'src/recorder/recording_clock.c',
    'src/recorder/muxer.c',
    'src/recorder/replay_save.c',
    'src/recorder/screenshot.c',
    'src/recorder/recorder.c',
    'src/cli/commands.c',
    'src/cli/ipc.c',
    'src/egl.c',
    'src/cuda.c',
    'src/window_texture.c',
    'src/shader.c',
    'src/color_conversion.c',
    'src/utils.c',
    'src/library_loader.c',
    'src/cursor.c',
    'src/damage.c',
    'src/image_writer.c',
    'src/args_parser.c',
    'src/kde_night_light.c',
    'src/defs.c',
    'src/plugins.c',
    'src/wayland_host_bridge.c',
    'src/sound.c',
    'src/cli/main.c',
]

subdir('protocol')
src += protocol_src

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required : true)

# Overrides the libav* dependencies below with a statically linked ffmpeg built from source.
if get_option('ffmpeg_static') == true
    subproject('ffmpeg')
endif

dep = [
    dependency('threads'),
    m_dep,
    dependency('libavcodec'),
    dependency('libavformat'),
    dependency('libavutil'),
    dependency('x11'),
    dependency('xcomposite'),
    dependency('xrandr'),
    dependency('xfixes'),
    dependency('xdamage'),
    dependency('libpulse'),
    dependency('libswresample'),
    dependency('libavfilter'),
    dependency('libva'),
    dependency('libva-drm'),
    dependency('libdrm'),
    dependency('wayland-egl'),
    dependency('wayland-client'),
]

if build_machine.system() == 'linux'
    dep += [
        dependency('libcap'),
    ]
endif

uses_pipewire = false

if get_option('portal') == true
    src += [
        'src/capture/portal.c',
        'src/dbus.c',
        'src/pipewire_video.c',
    ]
    add_project_arguments('-DGSR_PORTAL', language : 'c')
    uses_pipewire = true
endif

if get_option('app_audio') == true
    src += [
        'src/pipewire_audio.c',
    ]
    add_project_arguments('-DGSR_APP_AUDIO', language : 'c')
    uses_pipewire = true
endif

if uses_pipewire == true
    dep += [
        dependency('libpipewire-0.3'),
        dependency('libspa-0.2'),
        dependency('dbus-1'),
    ]
    add_project_arguments('-DGSR_DBUS', language : 'c')
endif

add_project_arguments('-DGSR_VERSION="' + meson.project_version() + '"', language: 'c')

executable('gsr-kms-server', 'kms/server/kms_server.c', dependencies : dependency('libdrm'), c_args : '-fstack-protector-all', install : true)
executable('gsr-cli', ['tools/gsr-cli/main.c', 'src/json.c', 'src/log.c'], install : true)
executable('gpu-screen-recorder', src, dependencies : dep, install : true)

install_headers('plugin/plugin.h', install_dir : 'include/gsr')
install_man('gpu-screen-recorder.1', 'gsr-cli.1', 'gsr-kms-server.1')
install_subdir('scripts', install_dir: 'share/gpu-screen-recorder')

if get_option('systemd') == true
    install_data(files('extra/gpu-screen-recorder.service'), install_dir : 'lib/systemd/user')
endif

if get_option('capabilities') == true
    meson.add_install_script('extra/meson_post_install.sh')
endif

if get_option('nvidia_suspend_fix') == true
    install_data(files('extra/gsr-nvidia.conf'), install_dir : 'lib/modprobe.d')
endif

if get_option('plugin_examples') == true
    shared_library('triangle', 'plugin/examples/hello_triangle/triangle.c',
        dependencies: [dependency('gl'), m_dep],
        name_prefix : '',
        name_suffix: 'so')
endif
