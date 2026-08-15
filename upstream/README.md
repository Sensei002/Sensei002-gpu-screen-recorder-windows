![](https://dec05eba.com/images/gpu_screen_recorder_logo_small.png)

# GPU Screen Recorder
This is a screen recorder that has minimal impact on system performance by recording your monitor using the GPU only,
similar to shadowplay on windows. This is the fastest screen recording tool for Linux.

This screen recorder can be used for recording your desktop offline, for live streaming and for nvidia shadowplay-like instant replay,
where only the last few minutes are saved.

This software can also take screenshots.

This is a cli-only tool, if you want an UI for this check out [GPU Screen Recorder GTK](https://git.dec05eba.com/gpu-screen-recorder-gtk/) or if you prefer a ShadowPlay-like UI then check out [GPU Screen Recorder UI](https://git.dec05eba.com/gpu-screen-recorder-ui/).

Supported video codecs:
* H264 (default)
* HEVC (Optionally with HDR)
* AV1 (Optionally with HDR)
* VP8
* VP9

Supported audio codecs:
* Opus (default)
* AAC

Supported image formats:
* JPEG
* PNG

This software works on X11 and Wayland on AMD, Intel and NVIDIA.

# Installation
If you are running an Arch Linux based distro then you can find gpu screen recorder in the official repositories under the name gpu-screen-recorder (`sudo pacman -S gpu-screen-recorder`).\
If you are running another distro then you can run `sudo ./install.sh`, but you need to manually install the dependencies, as described below.\
You can also install gpu screen recorder ([the ui version](https://git.dec05eba.com/gpu-screen-recorder-gtk/)) from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder), which is the easiest method
to install GPU Screen Recorder on non-arch based distros.\
You can alternatively install GPU Screen Recorder from one of the unofficial sources mentioned below.\
If you install GPU Screen Recorder flatpak, which is the ui version then you can still run GPU Screen Recorder command line by using the flatpak command option, for example `flatpak run --command=gpu-screen-recorder com.dec05eba.gpu_screen_recorder -w screen -f 60 -o video.mp4`. Note that if you want to record your monitor on AMD/Intel then you need to install the flatpak system-wide (like so: `flatpak install --system com.dec05eba.gpu_screen_recorder`).

## Unofficial install methods
The only official ways to install GPU Screen Recorder is either from source, arch linux extra repository or flathub. Other sources may be out of date and missing features or may not work correctly.\
If you install GPU Screen Recorder from somewhere else and have an issue then try installing it from one of the official sources before reporting it as an issue.\
If you still prefer to install GPU Screen Recorder with a package manager instead of from source or as a flatpak then you may be able to find a package for your distro.\
Here are some known unofficial packages:
* Alpine Linux: [gpu-screen-recorder](https://pkgs.alpinelinux.org/package/edge/testing/x86/gpu-screen-recorder)
* AppImage [AppImage GitHub releases](https://github.com/pkgforge-dev/gpu-screen-recorder-AppImage/releases)
* Debian: [gpu-screen-recorder-cli](https://tracker.debian.org/pkg/gpu-screen-recorder) or [gpu-screen-recorder alternative repository](https://sekoohaka.ddns.net/?p=about#debian-repository)
* Nix: [NixOS wiki](https://wiki.nixos.org/wiki/Gpu-screen-recorder)
* Nobara: [Nobara wiki](https://wiki.nobaraproject.org/en/general-usage/additional-software/GPU-Screen-Recorder)
* OpenMandriva: [gpu-screen-recorder](https://github.com/OpenMandrivaAssociation/gpu-screen-recorder)
* openSUSE: [openSUSE software repository](https://software.opensuse.org/package/gpu-screen-recorder)
* Ubuntu: [gpu-screen-recorder-cli](https://launchpad.net/ubuntu/+source/gpu-screen-recorder)
* Void Linux: [gpu-screen-recorder](https://github.com/cherrybtw/gsr4xbps)

# Dependencies
GPU Screen Recorder uses meson build system so you need to install `meson` to build GPU Screen Recorder.

## Build dependencies
These are the dependencies needed to build GPU Screen Recorder:

* x11 (libx11, libxcomposite, libxrandr, libxfixes, libxdamage)
* wayland (wayland-client, wayland-egl, wayland-scanner)
* ffmpeg (libavcodec, libavformat, libavutil, libswresample, libavfilter)
* libva (and libva-drm)
* libpulse
* libdrm
* libcap
* vulkan-headers
* linux-api-headers

## Optional dependencies
When building GPU Screen Recorder with portal support (`-Dportal=true` meson option, which is enabled by default) these dependencies are also needed:
* libdbus
* libpipewire (and libspa which is usually part of libpipewire)

## Building with a statically linked ffmpeg
The `-Dffmpeg_static=true` meson option (disabled by default) downloads ffmpeg and the libraries that ffmpeg depends on (x264, opus, mbedtls, srt and nv-codec-headers), builds them from source with lto and only the components that GPU Screen Recorder uses and then links them statically into GPU Screen Recorder.
This removes the runtime dependency on the system ffmpeg and on all of those libraries. `make`, `nasm`, `cmake` and `python3` are needed to build them and the first `meson setup` takes a few minutes longer because of it.

mbedtls is used as the tls backend instead of openssl because it's much smaller. mbedtls has no built-in default certificate location, so ffmpeg is patched to look for the certificate store of the system (and to honor the `SSL_CERT_FILE` and `SSL_CERT_DIR` environment variables) the same way that openssl does.

The only libraries that ffmpeg is still dynamically linked to are libva, libdrm (which GPU Screen Recorder also uses directly) and the c/c++ runtime.

This uses the same ffmpeg version as the GPU Screen Recorder flatpak, including the patch that makes nvenc negotiate the NVENC API version at runtime instead of at build time, which makes nvidia video encoding work on drivers down to 470.57.02 (Kepler) while still supporting AV1 and the RTX 5000 series.

## Runtime dependencies
* libglvnd (which provides libgl, libglx and libegl) is needed. Your system needs to support at least OpenGL ES 3.0 (released in 2012)
* vulkan-icd-loader (which provides the runtime vulkan library. This is only needed if vulkan video encoding option is used)
* libturbojpeg (aka libjpeg-turbo) is needed when capturing camera with mjpeg pixel format option, or for faster jpeg screenshots

There are also additional dependencies needed at runtime depending on your GPU vendor:

### AMD
* mesa
* vaapi (libva-mesa-driver)

### Intel
* mesa
* vaapi (intel-media-driver/libva-intel-driver/linux-firmware-intel, depending on which intel GPU you have)

### NVIDIA
* cuda runtime (libcuda.so.1) (libnvidia-compute)
* nvenc (libnvidia-encode)
* nvfbc (libnvidia-fbc1, when recording the screen on x11)

# How to use
Run `gpu-screen-recorder --help` to see all options and run `man gpu-screen-recorder` to see more detailed explanations for the options and also examples.\
There is also a gui for the gpu screen recorder called [GPU Screen Recorder GTK](https://git.dec05eba.com/gpu-screen-recorder-gtk/).\
There is also a new alternative UI for GPU Screen Recorder in the style of ShadowPlay called [GPU Screen Recorder UI](https://git.dec05eba.com/gpu-screen-recorder-ui/).
## Recording
Here is an example of how to record your monitor and the default audio output: `gpu-screen-recorder -w screen -f 60 -a default_output -o ~/Videos/test_video.mp4`.
Yyou can stop and save the recording with `Ctrl+C` or by running `pkill -SIGINT -f "^gpu-screen-recorder"`.
You can see a list of capture options to record if you run `gpu-screen-recorder --list-capture-options`. This will list possible capture options and monitor names, for example:\
```
  window
  DP-1|1920x1080
```
in this case you could record a window or a monitor with the name `DP-1`.\
To list available audio devices that you can use you can run `gpu-screen-recorder --list-audio-devices` and the name to use is on the left size of the `|`.\
To list available audio application names that you can use you can run `gpu-screen-recorder --list-application-audio`.\
You can run `gpu-screen-recorder --info` to list more information about the system, such as the device that is used for capture and video encoding and supported codecs. These commands can be parsed by scripts/programs.
## Replay mode
Run `gpu-screen-recorder` with the `-c mp4` and `-r` option, for example: `gpu-screen-recorder -w screen -f 60 -r 30 -c mp4 -o ~/Videos`. Note that in this case, `-o` should point to a directory.\
If `-df yes` is set, replays are save in folders based on the date.
The file path to the saved replay is output to stdout. All other output from GPU Screen Recorder are output to stderr.
You can also use the `-sc` option to specify a script that should be run (asynchronously) when the video has been saved and the script will have access to the location of the saved file as its first argument.
This can be used for example to show a notification when a replay has been saved, to rename the video with a title that matches the game played (see `scripts/record-save-application-name.sh` as an example on how to do this on X11) or to re-encode the video.

The replay buffer is stored in ram (as encoded video) by default, so don't use a too large replay time and/or video quality unless you have enough ram to store it.\
You can use the `-replay-storage disk` option to store the replay buffer on disk instead of ram (in the same location as the output video).\
By default videos are recorded with constant quality, but with replay mode you might want to record in constant bitrate mode instead for consistent ram/disk usage in high motion scenes. You can do that by using the `-bm cbr` option (along with `-q` option, for example `-bm cbr -q 20000`).
## Streaming
Streaming works the same way as recording, but the `-o` argument should be path to the live streaming service you want to use (including your live streaming key). Take a look at `scripts/twitch-stream.sh` to see an example of how to stream to twitch.\
GPU Screen Recorder uses Ffmpeg so GPU Screen Recorder supports all protocols that Ffmpeg supports.\
If you want to reduce latency one thing you can do is to use the `-keyint` option, for example `-keyint 0.5`. Lower value means lower latency at the cost of increased bitrate/decreased quality.
## Very low latency streaming (WHIP)
FFmpeg since version 7.1 supports the WHIP protocol for very low latency streaming (milliseconds latency). Here is a simple way to set it up:
1. Download the latest [mediamtx](https://github.com/bluenviron/mediamtx/releases/) binary and run it (just run `./mediamtx`)
2. Run `gpu-screen-recorder` with WHIP, for example: `gpu-screen-recorder -w screen -f 60 -k h264 -bm cbr -q 20000 -a default_output -ac opus -c whip -o "http://localhost:8889/mystream/whip"`.
   If you have a high refresh rate monitor you can set `-f 60` framerate option to your monitors framerate (for example `-f 144`) to have even lower latency.
## Recording while using replay/streaming
You can record a regular video while using replay/streaming by launching GPU Screen Recorder with the `-ro` option to specify a directory where to save the recording (for example: `gpu-screen-recorder -w screen -c mp4 -r 60 -o "$HOME/Videos/replays" -ro "$HOME/Videos/recordings"`).\
To start/stop (and save) recording use the SIGRTMIN signal, for example `pkill -SIGRTMIN -f "^gpu-screen-recorder"`. The path to the video will be displayed in stdout when saving the video.\
This way of recording while using replay/streaming is more efficient than running GPU Screen Recorder multiple times since this way it only records the screen and encodes the video once.
## Controlling GPU Screen Recorder remotely
GPU Screen Recorder can be controlled with signals, or with commands over a unix domain socket when it's started with the `-ipc` option.
### Signals
To save a video in replay mode, you need to send signal SIGUSR1 to gpu screen recorder. You can do this by running `pkill -SIGUSR1 -f "^gpu-screen-recorder"`.\
To stop recording send SIGINT to gpu screen recorder. You can do this by running `pkill -SIGINT -f "^gpu-screen-recorder"` or pressing `Ctrl-C` in the terminal that runs gpu screen recorder. When recording a regular non-replay video this will also save the video.\
To pause/unpause recording send SIGUSR2 to gpu screen recorder. You can do this by running `pkill -SIGUSR2 -f "^gpu-screen-recorder"`. This is only applicable and useful when recording (not streaming nor replay).\
There are more signals to control GPU Screen Recorder. Run `gpu-screen-recorder --help` to list them all (under `NOTES` section).
### IPC
Start GPU Screen Recorder with the `-ipc` option to also make it listen for commands on a unix domain socket, for example:\
```
gpu-screen-recorder -w screen -f 60 -c mp4 -r 60 -o ~/Videos -ipc "$XDG_RUNTIME_DIR/gsr.sock"
```
and then use the `gsr-cli` program to send commands to it:\
```
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" save-replay 30
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" save-replay
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" toggle-replay-recording
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" start-replay-recording
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" stop-replay-recording
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" toggle-pause
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" set-paused true
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" stop
gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" status
```
This gives the same control as the signals, with these differences:
* `save-replay` takes the number of seconds to save, instead of the fixed times that the signals provide. The whole replay buffer is saved when no number of seconds is given.
* `gsr-cli` exits with 0 only when the command succeeded and prints the reason to stderr when it didn't, so commands don't have to be sent blindly.
* `stop`, `save-replay` and `stop-replay-recording` are replied to when the file they save has been saved (and after the `-sc` script has been started), and `gsr-cli` prints the path of the saved file.
* `set-paused`, `start-replay-recording` and `stop-replay-recording` set an absolute state instead of toggling, so the result doesn't depend on the current state.
* `status` prints `running` or `not running` and exits with 0 when GPU Screen Recorder is running, which a script can use to only start replay when it isn't already running: `gsr-cli -ipc "$XDG_RUNTIME_DIR/gsr.sock" status >/dev/null || start-replay.sh`.
* The commands are sent to one specific GPU Screen Recorder instance instead of every instance that `pkill` matches.

Run `man gsr-cli` to see all commands. The commands are newline terminated json messages, which are described in the `IPC` section of `man gpu-screen-recorder` if you want to talk to the socket directly instead of using `gsr-cli`.
## Simple way to run replay without gui
Run the script `scripts/start-replay.sh` to start replay and then `scripts/save-replay.sh` to save a replay and `scripts/stop-replay.sh` to stop the replay. The videos are saved to `$HOME/Videos`.
You can use these scripts to start replay at system startup if you add `scripts/start-replay.sh` to startup (this can be done differently depending on your desktop environment / window manager) and then go into
hotkey settings on your system and choose a hotkey to run the script `scripts/save-replay.sh`. Modify `scripts/start-replay.sh` if you want to use other replay options.
## Run replay on system startup
If you installed GPU Screen Recorder from AUR or from source and you are running a distro that uses systemd then you will have a systemd service installed that can be started with `systemctl enable --now --user gpu-screen-recorder`. This systemd service runs GPU Screen Recorder on system startup.\
It's configured with `$HOME/.config/gpu-screen-recorder/gpu-screen-recorder.env` (create it if it doesn't exist). You can look at [extra/gpu-screen-recorder.env](https://git.dec05eba.com/gpu-screen-recorder/plain/extra/gpu-screen-recorder.env) to see an example.
You can see which variables that you can use in the `gpu-screen-recorder.env` file by looking at the `extra/gpu-screen-recorder.service` file. Note that all of the variables are optional, you only have to set the ones that are you interested in.
You can use the `scripts/save-replay.sh` script to save a replay and by default the systemd service saves videos in `$HOME/Videos`.
## Run a script when a video is saved
Run `gpu-screen-recorder` with the `-sc` option to specify a script that should be run when a recording/replay a saved, for example `gpu-screen-recorder -w screen -sc ./script.sh -o video.mp4`.\
The first argument to the script is the file path to the saved video. The second argument is either "regular" for regular recordings, "replay" for replays or "screenshot" for screenshots.\
This can be used to for example showing a notification with the name of video or moving a video to a folder based on the name of the game that was recorded.
## Plugins
GPU Screen Recorder supports plugins for rendering additional graphics on top of the monitor/window capture. The plugin interface is defined in `plugin/plugin.h` and it gets installed to `gsr/plugin.h` in the systems include directory (usually `/usr/include`).
An example plugin can be found at `plugin/examples/hello_triangle`.\
Run `gpu-screen-recorder` with the `-p` option to specify a plugin to load, for example `gpu-screen-recorder -w screen -p ./triangle.so -o video.mp4`.
`-p` can be specified multiple times to load multiple plugins.\
Build GPU Screen Recorder with the `-Dplugin_examples=true` meson option to build plugin examples.
## Smoother recording
If you record at your monitors refresh rate and enabled vsync in a game then there might be a desync between the game updating a frame and GPU Screen Recorder capturing a frame.
This is an issue in some games.
If you experience this issue then you might want to either disable vsync in the game or use the `-fm content` option to sync capture to the content on the screen. For example: `gpu-screen-recorder -w screen -fm content -o video.mp4`.\
Note that this option is currently only available on X11, or with desktop portal capture on Wayland (`-w portal`).

# Performance
On a system with an i5 4690k CPU and a GTX 1080 GPU:\
When recording Legend of Zelda Breath of the Wild at 4k, fps drops from 30 to 7 when using OBS Studio + nvenc, however when using this screen recorder the fps remains at 30.\
When recording GTA V at 4k on highest settings, fps drops from 60 to 23 when using obs-nvfbc + nvenc, however when using this screen recorder the fps only drops to 58.\
On a system with an AMD Ryzen 9 5900X CPU and an RX 7800XT GPU I don't see any fps drop at all, even when recording at 4k 60fps with AV1 codec with 10-bit colors.\
GPU Screen Recorder also produces much smoother videos than OBS when GPU utilization is close to 100%, see comparison here: [https://www.youtube.com/watch?v=zfj4sNVLLLg](https://www.youtube.com/watch?v=zfj4sNVLLLg) and [https://www.youtube.com/watch?v=aK67RSZw2ZQ](https://www.youtube.com/watch?v=aK67RSZw2ZQ).\
GPU Screen Recorder has much better performance than OBS Studio even with version 30.2 that does "zero-copy" recording and encoding, see: [https://www.youtube.com/watch?v=jdroRjibsDw](https://www.youtube.com/watch?v=jdroRjibsDw).\
It is recommended to save the video to a SSD because of the large file size, which a slow HDD might not be fast enough to handle. Using variable framerate mode (-fm vfr) which is the default is also recommended as this reduces encoding load. Ultra quality is also overkill most of the time, very high (the default) or lower quality is usually enough.\
Note that for best performance you should close other screen recorders such as OBS Studio when using GPU Screen Recorder even if they are not recording, since they can affect performance even when idle. This is the case with OBS Studio.
## Note about optimal performance on NVIDIA
NVIDIA driver has a "feature" (read: bug) where it will downclock memory transfer rate when a program uses cuda (or nvenc, which uses cuda), such as GPU Screen Recorder. This can affect game performance.
GPU Screen Recorder installs an NVIDIA profile on the system (`~/.nv/nvidia-application-profiles-rc.d/10-gsr-cuda-no-stable-perf-limit`) to get around this issue but this requires a relatively
new NVIDIA driver version (580+). If you have an older NVIDIA driver then you can choose to record with vulkan video encoding instead as it doesn't have this issue.

You can use vulkan video encoding by adding `_vulkan` at the end of the video codec option, for example: `-k h264_vulkan` or a full command example: `gpu-screen-recorder -w screen -k h264_vulkan -o video.mp4`.
Vulkan video encoding in GPU Screen Recorder supports `h264`, `hevc` and `av1` (along with `hdr` and `10bit` options), assuming your gpu drivers, ffmpeg and vulkan is up to date.

Note: vulkan video encoding support is experimental and you may experience GPU Screen Recorder or gpu driver bugs with it.

# Issues
## NVIDIA
NVIDIA drivers have an issue where CUDA breaks if CUDA is running when suspend/hibernation happens, and it remains broken until you reload the nvidia driver. `extra/gsr-nvidia.conf` will be installed by default when you install GPU Screen Recorder and that should fix this issue. If this doesn't fix the issue for you then your distro may use a different path for modprobe files. In that case you have to install that `extra/gsr-nvidia.conf` yourself into that location.
You have to reboot your computer after installing GPU Screen Recorder for the first time for the fix to have any effect.

Note: this issue doesn't happen when you using the vulkan video encoding option (for example when using `-k h264_vulkan`).

## TEMPORARY ISSUES
1) Videos are in variable framerate format. Use MPV to play such videos, otherwise you might experience stuttering in the video if you are using a buggy video player. You can try saving the video into a .mkv file instead as some software may have better support for .mkv files (such as kdenlive). You can use the "-fm cfr" option to to use constant framerate mode.
2) FLAC audio codec is disabled at the moment because of temporary issues.

# Examples
Look at the [scripts](https://git.dec05eba.com/gpu-screen-recorder/tree/scripts) directory for script examples. For example if you want to automatically save a recording/replay into a folder with the same name as the game you are recording.

# AMD/Intel/Wayland root permission
When recording a window or when using the `-w portal` option no special user permission is required,
however when recording a monitor the program needs root permission (to access KMS).\
This is safe in GPU Screen Recorder as the part that needs root access has been moved to its own small program that only does one thing.\
For you as a user this only means that if you installed GPU Screen Recorder as a flatpak then a prompt asking for root password will show up once when you start recording.

# VRR/G-SYNC
This should work fine on AMD/Intel X11 or Wayland. On Nvidia X11 G-SYNC only works with the -w screen-direct option, but because of bugs in the Nvidia driver this option is not always recommended.
For example it can cause your computer to freeze when recording certain games.

# License
This software is licensed under GPL-3.0-only, see the LICENSE file for more information.

# Reporting bugs, contributing patches, questions or donation
See [https://git.dec05eba.com/?p=about](https://git.dec05eba.com/?p=about).

# Demo
[![Click here to watch a demo video on youtube](https://img.youtube.com/vi/n5tm0g01n6A/0.jpg)](https://www.youtube.com/watch?v=n5tm0g01n6A)

# FAQ
## It tells me that my AMD/Intel GPU is not supported or that my GPU doesn't support h264/hevc, but that's not true!
Some linux distros (such as manjaro and fedora) disable hardware accelerated h264/hevc on AMD/Intel because of "patent license issues". If you are using an arch-based distro then you can install mesa-git instead of mesa and if you are using another distro then you may have to switch to a better distro. On fedora based distros you can follow this: [Hardware Accelerated Codec](https://rpmfusion.org/Howto/Multimedia).\
You can alternatively install the flatpak version of GPU Screen Recorder from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder) which doesn't have this issue on any distro.
## I have an old nvidia GPU that supports nvenc but I get a cuda error when trying to record
Newer ffmpeg versions don't support older nvidia cards. Try installing GPU Screen Recorder flatpak from [flathub](https://flathub.org/apps/details/com.dec05eba.gpu_screen_recorder) instead. It comes with a custom version of ffmpeg to support older nvidia GPUs.
## I get a black screen/glitches while live streaming
It seems like ffmpeg earlier than version 6.1 has some type of bug. Install ffmpeg version 6.1 or later and then reinstall GPU Screen Recorder to fix this issue. The flatpak version of GPU Screen Recorder comes with a newer version of ffmpeg so no extra steps are needed.
## I can't play the video in my browser directly or in discord
Browsers and discord don't support hevc video codec at the moment. You can instead choose h264 video codec with the -k h264 option or av1 video codec with the -k av1 option.
Note that websites such as youtube support hevc so there is no need to choose h264 video codec if you intend to upload the video to youtube or if you want to play the video locally or if you intend to
edit the video with a video editor. Hevc allows for better video quality (especially at lower file sizes) so hevc (or av1) is recommended for source videos.
## I get a black bar/distorted colors on the sides in the video
This is mostly an issue on AMD. For av1 it's a hardware issue, see: https://gitlab.freedesktop.org/mesa/mesa/-/issues/9185. For hevc it's a software issue in ffmpeg that was fixed in ffmpeg version 8.\
If your ffmpeg version is older than 8 then you can use the flatpak version of GPU Screen Recorder which comes with ffmpeg version >= 8.\
Alternatively you can record with h264 codec (-k h264, which is also the default codec) to workaround this issue.
## The video doesn't display or has a green/yellow overlay
This can happen if your video player is missing the H264/HEVC video codecs. Either install the codecs or use mpv.
## I get stutter in the video
Try recording to an SSD and make sure it's not using NTFS file system. Also try recording with "content" framerate mode (`-fm content`).
## GPU Screen Recorder records night light
On KDE Plasma the night light tint is automatically removed from the recording (both in SDR and HDR mode).
On other compositors you can record with desktop portal option (`-w portal`) instead which ignores night light, if you are ok with recording without HDR.
## Kdenlive says that the video is not usable for editing because it has variable frame rate
To fix this you can either just press cancel, which will allow you to continue or record the video in .mkv format or constant frame rate (-fm cfr). I recommend recording the video in .mkv format and variable frame rate (-fm vfr).
## GPU Screen Recorder starts lagging after 30-40 minutes when launching GPU Screen Recorder from steam command launcher
This is a [steam issue](https://github.com/ValveSoftware/steam-for-linux/issues/11446). Prepend the gpu-screen-recorder command with `LD_PREFIX=""`, for example `LD_PREFIX="" gpu-screen-recorder -w screen -o video.mp4`.
## How do I apply audio effects, such as noise suppression?
You have to use external software for that, such as Easy Effects or NoiseTorch.
## How do I choose which GPU to record/encode with?
It's not really possible except in some cases. You can only record with the GPU that is displaying the graphics on your monitor.\
Some laptops have display adapters that connect external monitors directly to the external GPU (if you have one)
and on Wayland the external GPU will display the graphics for that monitor.
In that case you can record the monitor with the external GPU by launching GPU Screen Recorder with [prime-run or by setting the DRI_PRIME environment variable](https://wiki.archlinux.org/title/PRIME) depending on your GPU brand.
Alternatively you can capture with the desktop portal option (`-w portal`), which should allow you to capture any monitor.\
However if you really want to change which GPU you want to record and encode with with then you can instead configure your display server (Xorg or Wayland compositor) to run with that GPU,
then GPU Screen Recorder will automatically use that same GPU for recording and encoding.
## The rotation of the video is incorrect when the monitor is rotated when using desktop portal capture
This is a bug in kde plasma wayland. When using desktop portal capture and the monitor is rotated and a window is made fullscreen kde plasma wayland will give incorrect rotation to GPU Screen Recorder.
This also affects other screen recording software such as obs studio.\
Capture a monitor directly instead to workaround this issue until kde plasma devs fix it, or use another wayland compositor that doesn't have this issue.
## System notifications get disabled when recording with desktop portal option
Some desktop environments such as KDE Plasma turn off notifications when you record the screen with the desktop portal option. You can disable this by going into KDE Plasma settings -> search for notifications and then under "Do Not Disturb mode" untick "During screen sharing".
## The recorded video lags or I get dropped frames in the video
This is likely not an issue in the recorded video itself, but the video player you use. GPU Screen Recorder doesn't record by dropping frames. Some video players don't play videos with hardware acceleration by default,
especially if you record with HEVC/AV1 video codec. In such cases it's recommended to play the video with mpv instead with hardware acceleration enabled (for example: `mpv --vo=gpu --hwdec=auto video.mp4`).
Some corporate distros such as Fedora (or some Fedora based distros) also disable hardware accelerated video codecs on AMD/Intel GPUs, so you might need to install mpv (or another video player) with flathub instead, which bypasses this restriction.
## My cursor is flickering in the recorded video
This is likely an AMD gpu driver issue. It only happens to certain generations of AMD GPUs. On Wayland you can record with the desktop portal option (`-w portal`) to workaround this issue.
This issue hasn't been observed on X11 yet, but if you do observe it you can either record a window (`-w $(xdotool selectwindow)`) or change your xorg config to use software cursor instead (Add `Option "SWcursor" "true"` under modesetting "Device" section in your xorg config file).
## Password prompt shows up when I try to record my screen
If GPU Screen Recorder is installed with -Dcapabilities=true (which is the default option) then `gsr-kms-server` is installed with admin capabilities.
This removes a password prompt when recording a monitor with the `-w monitor` option (for example `-w screen`). However if the root user is disabled on the system then the password prompt will show up anyways.
If the root user is disabled on your system then you can instead record with `-w focused` or `-w window_id` on X11 or `-w portal` on Wayland.
## CPU/GPU usage is high
CPU/GPU usage can be misleading. CPUs/GPUs have multiple power/performance levels and the CPU/GPU usage reported on the system may be the CPU/GPU usage at the current performance level.
The performance level changes depending on the CPU/GPU load, so it may say that CPU/GPU usage is 80%, but the actual total CPU/GPU usage may be 5%.
The only way to properly test the performance of GPU Screen Recorder is to use it while recording a game and seeing the effect on the games framerate.
## The video is too dark when capturing full-range video or 10-bit video
This is an issue in some broken video players such as vlc. Play the video with a video player such as mpv (or a mpv frontend such as celluloid) or a browser instead.
## The video has glitches
This may be caused by buggy GPU drivers. Try recording with HEVC video codec instead (`-k hevc`).
## The quality is low when recording my desktop
Color gradients encode badly, especially with h264 video codec. You can increase the bitrate, for example by using `-bm cbr -q 30000` or by using hevc (`-k hevc`) and increasing the quality (`-q ultra`).
## My system stutters sometimes while recording on nvidia
This is an nvidia power management driver bug which can happen when the system is idle. You can change the gpu power performance level to "performance" in nvidia settings to fix this.
## Recording fails when using webcam on nvidia X11
This is a known issue. The issue is that nvfbc which is used for screen capture only supports glx on older systems and glx is not compatible with the webcam capture method used in GPU Screen Recorder.
This will be fixed in the future.
## Desktop portal capture doesn't work on my AMD GPU on Hyprland
This is a hyprland bug. Capture the monitor directly with `-w monitor` or use another Wayland compositor.
## The captured video is laggy when capturing with desktop portal on COSMIC/Wlroots (sway)
This is a bug in COSMIC and Wlroots. Their desktop portals send frame updates at low framerates (such as 25 fps) instead of the selected framerate (such as 60 fps). Capture a monitor directly instead (`-w DP-1` for example) to workaround that problem.
## How to adjust the volume of the recorded audio?
GPU Screen Recorder doesn't have the option to do that yet, but you can change the volume by starting recording and then open `pavucontrol`, go to the `Recording` tab and set the volume there. The system will remember the settings even after reboot, as long as the audio capture options remain the same.
