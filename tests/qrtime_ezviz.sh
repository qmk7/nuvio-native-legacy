#!/bin/bash
# QR TIME EZVIZ parsers (ezviz.c with its neighbours stubbed).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-ezviz.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/ezviz.c src/js.c tests/qrtime_ezviz.c -Isrc $(pkg-config --cflags sdl2) -o "$d/t" -pthread -Wno-builtin-declaration-mismatch 2>&1 | grep -v "conflicting types\|note:\|^ *[0-9]* |\|^ *|" ; [ -x "$d/t" ] || { echo "FAIL: test build"; exit 1; }
"$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_ezviz: ok"
exit "$r"
