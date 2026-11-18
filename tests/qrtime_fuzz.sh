#!/bin/bash
# QR TIME phase 8: parser robustness under ASan/UBSan (malformed playlists,
# API responses, audio headers, corrupted saved files).
set -u
cd "$(dirname "$0")/.."
NUVIO_DADOS=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-fuzz.XXXXXX")
export NUVIO_DADOS
trap 'rm -rf "$NUVIO_DADOS"' EXIT
sources=()
for source in src/*.c src/dts/*.c; do
  case "$source" in src/main.c) continue;; esac
  sources+=("$source")
done
cc "${sources[@]}" tests/qrtime_fuzz.c -Isrc -o "$NUVIO_DADOS/t" -O1 -g -DNV_LINUX_DESKTOP -Wno-format-truncation \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
  $(pkg-config --cflags --libs sdl2 SDL2_image SDL2_ttf glesv2 egl zlib) -ldl -pthread -lm 2> "$NUVIO_DADOS/cc.log" \
  || { tail -20 "$NUVIO_DADOS/cc.log"; echo "FAIL: test build"; exit 1; }
ASAN_OPTIONS=detect_leaks=0 "$NUVIO_DADOS/t" > "$NUVIO_DADOS/out.log" 2>&1; r=$?
grep -E "^(PASS|FAIL)|ERROR: AddressSanitizer|runtime error|SUMMARY" "$NUVIO_DADOS/out.log"
[ "$r" = 0 ] && echo "qrtime_fuzz: ok"
exit "$r"
