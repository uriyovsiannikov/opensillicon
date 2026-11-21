#!/bin/sh
set -eu
cd "$(dirname "$0")/../../.."
check_dir=$(mktemp -d)
trap 'rm -rf "$check_dir"' EXIT HUP INT TERM
"${CC:-gcc}" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    src/tools/checks/libos_dns.c src/user/libos/src/net/dns.c -o "$check_dir/check"
"$check_dir/check"
