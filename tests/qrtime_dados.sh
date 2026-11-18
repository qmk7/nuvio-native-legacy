#!/bin/bash
# QR TIME phase 8: data schema, cache, resets, profile isolation.
set -u
cd "$(dirname "$0")/.."
NUVIO_DADOS=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-dados.XXXXXX")
export NUVIO_DADOS
trap 'rm -rf "$NUVIO_DADOS"' EXIT
sources=()
for source in src/*.c src/dts/*.c; do
  case "$source" in src/main.c) continue;; esac
  sources+=("$source")
done
cc "${sources[@]}" tests/qrtime_dados.c -Isrc -o "$NUVIO_DADOS/t" -O1 -g -DNV_LINUX_DESKTOP -Wno-format-truncation \
  $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf glesv2 egl zlib) -ldl -pthread -lm || { echo "FAIL: test build"; exit 1; }
"$NUVIO_DADOS/t" 2>&1 | grep -E "^(PASS|FAIL)"; r=${PIPESTATUS[0]}
[ "$r" = 0 ] && echo "qrtime_dados: ok"
exit "$r"
