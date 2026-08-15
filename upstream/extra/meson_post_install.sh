#!/bin/sh

# Needed to remove password prompt when recording a monitor (without desktop portal option) on amd/intel or nvidia wayland
/usr/sbin/setcap cap_sys_admin+ep ${MESON_INSTALL_DESTDIR_PREFIX}/bin/gsr-kms-server \
    || echo "\n!!! Please re-run install as root\n"
