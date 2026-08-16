project('gsr-ui', ['c', 'cpp'], version : '1.13.5', default_options : ['warning_level=2', 'cpp_std=c++17'], subproject_dir : 'depends')

add_project_arguments('-D_FILE_OFFSET_BITS=64', language : ['c', 'cpp'])

if get_option('buildtype') == 'debug'
    add_project_arguments('-g3', language : ['c', 'cpp'])
elif get_option('buildtype') == 'release'
    add_project_arguments('-DNDEBUG', language : ['c', 'cpp'])
endif

src = [
    'src/window_texture.c',
    'src/Theme.cpp',
    'src/gui/Widget.cpp',
    'src/gui/ScrollablePage.cpp',
    'src/gui/Button.cpp',
    'src/gui/RadioButton.cpp',
    'src/gui/Entry.cpp',
    'src/gui/CheckBox.cpp',
    'src/gui/ComboBox.cpp',
    'src/gui/Page.cpp',
    'src/gui/StaticPage.cpp',
    'src/gui/PageStack.cpp',
    'src/gui/List.cpp',
    'src/gui/Utils.cpp',
    'src/gui/DropdownButton.cpp',
    'src/gui/Label.cpp',
    'src/gui/Image.cpp',
    'src/gui/LineSeparator.cpp',
    'src/gui/CustomRendererWidget.cpp',
    'src/gui/FileChooser.cpp',
    'src/gui/SettingsPage.cpp',
    'src/gui/ScreenshotSettingsPage.cpp',
    'src/gui/GlobalSettingsPage.cpp',
    'src/gui/GsrPage.cpp',
    'src/gui/Subsection.cpp',
    'src/gui/Tooltip.cpp',
    'src/GlobalHotkeys/GlobalHotkeysX11.cpp',
    'src/GlobalHotkeys/GlobalHotkeysLinux.cpp',
    'src/GlobalHotkeys/GlobalHotkeysJoystick.cpp',
    'src/CursorTracker/CursorTrackerX11.cpp',
    'src/CursorTracker/CursorTrackerDrm.cpp',
    'src/CursorTracker/CursorTrackerHyprland.cpp',
    'src/CursorTracker/CursorTrackerNiri.cpp',
    'src/CursorTracker/CursorTrackerSway.cpp',
    'src/RegionSelector/RegionSelectorX11.cpp',
    'src/RegionSelector/RegionSelectorWayland.cpp',
    'src/DesktopEnvironment/DesktopEnvironmentX11.cpp',
    'src/DesktopEnvironment/DesktopEnvironmentWlroots.cpp',
    'src/DesktopEnvironment/DesktopEnvironmentKde.cpp',
    'src/DesktopEnvironment/DesktopEnvironmentGnome.cpp',
    'src/Utils.cpp',
    'src/WindowUtils.cpp',
    'src/WaylandHostBridge.cpp',
    'src/Clipboard/ClipboardTransfer.cpp',
    'src/Clipboard/ClipboardX11.cpp',
    'src/Clipboard/ClipboardWayland.cpp',
    'src/Config.cpp',
    'src/GsrInfo.cpp',
    'src/Process.cpp',
    'src/Overlay.cpp',
    'src/AudioPlayer.cpp',
    'src/Hotplug.cpp',
    'src/ClipboardFile.cpp',
    'src/LedIndicator.cpp',
    'src/Rpc.cpp',
    'src/Translation.cpp',
    'src/main.cpp',
]

subdir('protocol')
src += protocol_src

mglpp_proj = subproject('mglpp', default_options : ['wayland=true'])
mglpp_dep = mglpp_proj.get_variable('mglpp_dep')

prefix = get_option('prefix')
datadir = get_option('datadir')
gsr_ui_resources_path = join_paths(prefix, datadir, 'gsr-ui')
icons_path = join_paths(prefix, datadir, 'icons')

add_project_arguments('-DGSR_UI_VERSION="' + meson.project_version() + '"', language: ['c', 'cpp'])
add_project_arguments('-DGSR_FLATPAK_VERSION="6.0.0"', language: ['c', 'cpp'])

add_project_arguments('-DKWIN_HELPER_SCRIPT_PATH="' + gsr_ui_resources_path + '/gsrkwinhelper.js"', language: ['c', 'cpp'])
add_project_arguments('-DGNOME_EXTENSION_SOURCE_DIR="' + gsr_ui_resources_path + '/gnome-extension"', language: ['c', 'cpp'])

executable(
    meson.project_name(),
    src,
    install : true,
    dependencies : [
        mglpp_dep,
        dependency('threads'),
        dependency('xcomposite'),
        dependency('xfixes'),
        dependency('xext'),
        dependency('xi'),
        dependency('xcursor'),
        dependency('xrandr'),
        dependency('libpulse-simple'),
        dependency('libdrm'),
        dependency('wayland-client'),
    ],
    cpp_args : '-DGSR_UI_RESOURCES_PATH="' + gsr_ui_resources_path + '"',
)

executable(
    'gsr-global-hotkeys',
    [
        'tools/gsr-global-hotkeys/hotplug.c',
        'tools/gsr-global-hotkeys/keyboard_event.c',
        'tools/gsr-global-hotkeys/keys.c',
        'tools/gsr-global-hotkeys/leds.c',
        'tools/gsr-global-hotkeys/main.c'
    ],
    c_args : '-fstack-protector-all',
    install : true
)

executable(
    'gsr-ui-cli',
    [
        'tools/gsr-ui-cli/main.c'
    ],
    install : true
)

executable(
    'gsr-wayland-bridge',
    [
        'tools/gsr-wayland-bridge/main.c'
    ],
    install : true
)

executable(
    'gsr-kwin-helper',
    [
        'tools/gsr-kwin-helper/main.cpp'
    ],
    install : true,
    dependencies: [
        dependency('dbus-1'),
    ]
)

executable(
    'gsr-gnome-helper',
    [
        'tools/gsr-gnome-helper/main.cpp'
    ],
    install : true,
    dependencies: [
        dependency('dbus-1'),
    ]
)

executable(
    'gsr-game-tracker',
    [
        'tools/gsr-game-tracker/native_games.c',
        'tools/gsr-game-tracker/main.c'
    ],
    install : true
)

install_data(
    'tools/gsr-kwin-helper/gsrkwinhelper.js',
    install_dir: gsr_ui_resources_path,
    install_mode: 'rwxr-xr-x'
)

install_data(
    'tools/gsr-gnome-helper/extension/metadata.json',
    'tools/gsr-gnome-helper/extension/extension.js',
    install_dir: join_paths(gsr_ui_resources_path, 'gnome-extension'),
    install_mode: 'rw-r--r--'
)

install_subdir('images', install_dir : gsr_ui_resources_path)
install_subdir('translations', install_dir : gsr_ui_resources_path)
install_man('gsr-ui.1', 'gsr-ui-cli.1')

if get_option('desktop-files') == true
    install_data(files('gpu-screen-recorder.desktop'), install_dir : join_paths(prefix, datadir, 'applications'))
    install_subdir('icons/hicolor', install_dir : icons_path)

    gnome = import('gnome')
    gnome.post_install(update_desktop_database : true)
endif

if get_option('capabilities') == true
    meson.add_install_script('meson_post_install.sh')
endif
