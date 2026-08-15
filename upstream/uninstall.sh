#!/bin/sh -e

script_dir=$(dirname "$0")
cd "$script_dir"

[ $(id -u) -ne 0 ] && echo "You need root privileges to run the uninstall script" && exit 1

ninja -C build uninstall

echo "Successfully uninstalled gpu-screen-recorder"
