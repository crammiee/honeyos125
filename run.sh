#!/bin/sh
set -e

DISK="disk.img"

if [ ! -f "$DISK" ]; then
    echo "disk.img not found — run 'make' first."
    exit 1
fi

if [ "$1" = "--debug" ]; then
    exec qemu-system-i386 \
        -fda "$DISK"       \
        -display curses    \
        -no-reboot         \
        -serial stdio      \
        -d int,cpu_reset   \
        2>&1 | tee qemu.log
else
    exec qemu-system-i386 \
        -fda "$DISK"       \
        -display curses    \
        -no-reboot
fi
