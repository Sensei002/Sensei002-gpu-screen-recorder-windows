#!/bin/sh

/usr/sbin/setcap cap_setuid+ep ${MESON_INSTALL_DESTDIR_PREFIX}/bin/gsr-global-hotkeys \
    || echo "\n!!! Please re-run install as root\n"