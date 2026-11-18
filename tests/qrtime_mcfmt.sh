#!/bin/bash
# Music Center formats and decoding: real files made by ffmpeg (a TEST tool
# here, not part of the app) -> src/mcfmt.c probe and src/mcdec.c decode.
set -u
cd "$(dirname "$0")/.."
command -v ffmpeg >/dev/null || { echo "SKIP: ffmpeg not installed (fixture generator)"; exit 0; }
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-mcfmt.XXXXXX")
trap 'rm -rf "$W"' EXIT
bash tests/qrtime_music_fixtures.sh "$W" || { echo "FAIL: fixtures"; exit 1; }
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DMC_DEC_TESTE tests/qrtime_mcfmt.c src/mcfmt.c src/mcdec.c src/md5.c \
  -lm -o "$W/t" || { echo "FAIL: test build"; exit 1; }
"$W/t" "$W"; r=$?
[ "$r" = 0 ] && echo "qrtime_mcfmt: ok"
exit "$r"
