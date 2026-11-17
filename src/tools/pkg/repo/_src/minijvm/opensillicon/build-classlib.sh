#!/bin/sh
set -eu
src=$1
output=$2
javac=$3
jar=$4

mkdir -p "$(dirname "$output")"
work=$(mktemp -d "$(dirname "$output")/classlib.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
find "$src/java" -type f -name '*.java' | LC_ALL=C sort > "$work/sources"
mkdir "$work/classes"
"$javac" --release 8 -encoding UTF-8 -d "$work/classes" @"$work/sources"
if [ -d "$src/resource" ]; then
    cp -R "$src/resource/." "$work/classes/"
fi
"$jar" cf "$work/minijvm_rt.jar" -C "$work/classes" .
mv "$work/minijvm_rt.jar" "$output"
