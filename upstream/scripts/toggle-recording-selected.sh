#!/bin/sh -e

killall -SIGINT gpu-screen-recorder && sleep 0.5 && notify-send -t 1500 -u low 'GPU Screen Recorder' 'Stopped recording' && exit 0;
window=$(xdotool selectwindow)
active_sink=default_output
mkdir -p "$HOME/Videos"
video="$HOME/Videos/$(date +"Video_%Y-%m-%d_%H-%M-%S.mp4")"
notify-send -t 1500 -u low 'GPU Screen Recorder' "Started recording video to $video"
gpu-screen-recorder -w "$window" -c mp4 -f 60 -a "$active_sink" -o "$video"
