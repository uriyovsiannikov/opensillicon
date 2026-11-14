#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p src/build
test_bin=$(mktemp src/build/xhci-ownership.XXXXXX)
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -Isrc/include -Isrc/limine -Isrc/kernel/usb/opensillicon \
    -Isrc/kernel/usb/usbx/common/core/inc \
    -Wl,--gc-sections src/tests/xhci_ownership.c -o "$test_bin"
"$test_bin"
