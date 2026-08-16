#!/bin/sh -e

script_dir=$(dirname "$0")
cd "$script_dir"

[ $(id -u) -ne 0 ] && echo "You need root privileges to run the install script" && exit 1

rm -rf build
meson setup build
meson configure --prefix=/usr --buildtype=release -Dstrip=true build
ninja -C build install

echo "Successfully installed gsr-ui"
