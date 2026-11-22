#!/bin/sh
set -eu
mkdir -p src/build/musl-check
python3 src/user/thirdparty/musl/opensillicon/check.py
