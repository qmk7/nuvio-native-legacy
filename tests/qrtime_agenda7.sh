#!/bin/bash
# QR TIME Agenda: Trakt calendar + timezones, date confidence, follows.
set -u
cd "$(dirname "$0")/.."
NUVIO_DADOS=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-agenda7.XXXXXX")
export NUVIO_DADOS
trap 'rm -rf "$NUVIO_DADOS"' EXIT
sources=()
for source in src/*.c src/dts/*.c; do
  case "$source" in src/main.c|src/agenda.c) continue;; esac
  sources+=("$source")
done
cc "${sources[@]}" tests/qrtime_agenda7.c -Isrc -o "$NUVIO_DADOS/t" -O1 -g -DNV_LINUX_DESKTOP -Wno-format-truncation \
  $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf glesv2 egl zlib) -ldl -pthread -lm || { echo "FAIL: test build"; exit 1; }
"$NUVIO_DADOS/t" 2>&1 | grep -E "^(PASS|FAIL)"; r=${PIPESTATUS[0]}
[ "$r" = 0 ] && echo "qrtime_agenda7: ok"
exit "$r"
