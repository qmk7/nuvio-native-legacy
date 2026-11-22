#!/bin/bash
# QR TIME sleep timer logic (src/sono.c alone, the app parts stubbed).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-sono.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/sono.c tests/qrtime_sono.c -Isrc -o "$d/t" -O1 -g -DNV_LINUX_DESKTOP $(pkg-config --cflags --libs sdl2) || { echo "FAIL: test build"; exit 1; }
"$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_sono: ok"
exit "$r"
