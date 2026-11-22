#!/bin/bash
# QR TIME DeepL Arabic subtitles (legtrad.c pure parts).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-legtrad.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/legtrad.c src/js.c tests/qrtime_legtrad.c -Isrc -o "$d/t" -pthread || { echo "FAIL: test build"; exit 1; }
"$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_legtrad: ok"
exit "$r"
