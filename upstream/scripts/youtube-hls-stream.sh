#!/bin/sh

[ "$#" -ne 3 ] && echo "usage: youtube-hls-stream.sh <window_id> <fps> <livestream_key>" && exit 1
active_sink=default_output
gpu-screen-recorder -w "$1" -c hls -f "$2" -bm cbr -q 8000 -a "$active_sink" -ac aac -o "https://a.upload.youtube.com/http_upload_hls?cid=$3&copy=0&file=stream.m3u8"