#!/bin/sh

# Simple script to start recording if it's not recording and stop recording
# if it's already recording. This script can be bound to a single hotkey
# to start/stop recording with a single hotkey.

killall -SIGINT -q gpu-screen-recorder && exit 0
video="$HOME/Videos/$(date +"Video_%Y-%m-%d_%H-%M-%S.mp4")"
gpu-screen-recorder -w screen -f 60 -a default_output -o "$video"
notify-send -t 2000 -u low "GPU Screen Recorder" "Video saved to $video"
