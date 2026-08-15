#!/bin/sh

window=$(xdotool selectwindow)
window_name=$(xdotool getwindowname "$window" || xdotool getwindowclassname "$window" || echo "Game")
window_name="$(echo "$window_name" | tr '/\\' '_')"
gpu-screen-recorder -w "$window" -f 60 -a default_output -o "$HOME/Videos/recording/$window_name/$(date +"Video_%Y-%m-%d_%H-%M-%S.mp4")"
