#!/bin/sh
set -eu
test_src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_out=${1:?Usage: build.sh OUTPUT_DIRECTORY [MINECRAFT_1_0_0_CLIENT_JAR]}
mkdir -p "$test_out"
javac --release 8 -d "$test_out" "$test_src/WorldIoProbe.java" "$test_src/SchedulerProbe.java"
python3 - "$test_out" <<'PY'
import gzip
from pathlib import Path
import sys
import zlib

out = Path(sys.argv[1])
payload = bytes((i * 73 ^ i >> 8) & 255 for i in range(65537))
(out / "reference.gz").write_bytes(gzip.compress(payload, mtime=0))
(out / "reference.zlib").write_bytes(zlib.compress(payload))
PY
jar cf "$test_out/world-io-check.jar" -C "$test_out" WorldIoProbe.class \
    -C "$test_out" SchedulerProbe.class \
    -C "$test_out" reference.gz -C "$test_out" reference.zlib

if [ "$#" -gt 1 ]; then
    javac --release 8 -cp "$2" -d "$test_out" "$test_src/MinecraftRegionProbe.java"
    jar uf "$test_out/world-io-check.jar" -C "$test_out" MinecraftRegionProbe.class
fi
