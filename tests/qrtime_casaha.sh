#!/bin/bash
# Home Assistant "play this film": title -> IMDb id (tests/qrtime_casaha.c).
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-casaha.XXXXXX"); trap 'rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -I/usr/include/SDL2 -DNV_LINUX_DESKTOP tests/qrtime_casaha.c src/casaha.c \
  src/iptvvod.c src/xtvod.c src/stvod.c src/m3uvod.c src/admin.c src/iptvfontes.c src/xtream.c src/m3u.c src/m3u_parse.c src/stalker.c \
  src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" || { echo "FAIL: test build"; exit 1; }
"$W/t" || { echo "qrtime_casaha: FAIL"; exit 1; }
echo "qrtime_casaha: ok"
