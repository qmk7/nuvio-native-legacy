#!/bin/bash
# QR TIME QR generator up to version 10 (qr.c alone).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-qr.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc -Isrc src/qr.c tests/qrtime_qr.c -o "$d/t" || { echo "FAIL: test build"; exit 1; }
"$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_qr: ok"
exit "$r"
