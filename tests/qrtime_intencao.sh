#!/bin/bash
# QR TIME voice intents (intencao.c alone).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-intencao.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/intencao.c tests/qrtime_intencao.c -Isrc -o "$d/t" || { echo "FAIL: test build"; exit 1; }
"$d/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_intencao: ok"
exit "$r"
