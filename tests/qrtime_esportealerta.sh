#!/bin/bash
# QR TIME sports alerts and Arabic names.
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-festa.XXXXXX"); trap 'rm -rf "$d"' EXIT
mkdir -p "$d/dados"
sources=()
for source in src/*.c src/dts/*.c; do case "$source" in src/main.c) continue;; esac; sources+=("$source"); done
cc "${sources[@]}" tests/qrtime_esportealerta.c -Isrc -o "$d/t" -O1 -g -DNV_LINUX_DESKTOP -Wno-format-truncation \
  $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf glesv2 egl zlib) -ldl -pthread -lm || { echo "FAIL: test build"; exit 1; }
NUVIO_DADOS="$d/dados" "$d/t" 2>&1 | grep -E "^(PASS|FAIL)"; r=${PIPESTATUS[0]}
[ "$r" = 0 ] && echo "qrtime_esportealerta: ok"
exit "$r"
