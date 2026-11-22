#!/bin/bash
# QR TIME phone pages server (src/lanweb.c alone).
set -u
cd "$(dirname "$0")/.."
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-lanweb.XXXXXX"); trap 'rm -rf "$d"' EXIT
cc src/lanweb.c tests/qrtime_lanweb.c -Isrc -o "$d/t" -O1 -g -DNV_LINUX_DESKTOP -pthread || { echo "FAIL: test build"; exit 1; }
"$d/t" | grep -E "^(PASS|FAIL)"; r=${PIPESTATUS[0]}
[ "$r" = 0 ] && echo "qrtime_lanweb: ok"
exit "$r"
