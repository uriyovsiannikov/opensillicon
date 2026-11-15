#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p src/build
test_bin=$(mktemp src/build/usb-mouse-queue.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include -Isrc/limine -Isrc/kernel/drivers/input/usb/opensillicon \
    -Isrc/kernel/drivers/input/usb/usbx/common/core/inc \
    -Isrc/kernel/drivers/input/usb/usbx/common/usbx_host_classes/inc \
    -Wl,--gc-sections src/tests/usb_mouse_queue.c -o "$test_bin"
"$test_bin"
