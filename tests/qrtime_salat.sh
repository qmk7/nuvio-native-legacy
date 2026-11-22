#!/bin/bash
# QR TIME prayer times (salat.c alone).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-salat.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/salat.c tests/qrtime_salat.c -Isrc $(pkg-config --cflags sdl2) -o "$d/t" -lm || { echo "FAIL: test build"; exit 1; }
TZ=UTC "$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_salat: ok"
exit "$r"
