#!/bin/bash
# Music Assistant client (tests/qrtime_massist.c).
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-massist.XXXXXX"); trap 'rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Isrc -I/usr/include/SDL2 -DNV_LINUX_DESKTOP tests/qrtime_massist.c src/massist.c src/js.c src/dados.c src/rede.c src/redeurl.c \
  -lpthread -ldl -lz -o "$W/t" 2>"$W/err" || { cat "$W/err"; echo "FAIL: test build"; exit 1; }
mkdir -p "$W/d"
MA_DIR="$W/d" "$W/t" || { echo "qrtime_massist: FAIL"; exit 1; }
echo "qrtime_massist: ok"
