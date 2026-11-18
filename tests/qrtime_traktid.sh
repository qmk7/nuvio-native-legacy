#!/bin/bash
# QR TIME Trakt: identity gate, scrobble bodies, duplicate ledger, retries.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-traktid.XXXXXX")
trap 'rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP -DNV_DUBLE_SESSAO tests/qrtime_traktid.c \
  src/traktid.c src/traktdup.c src/traktscrobble.c src/metaep.c src/js.c src/dados.c -lpthread -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
mkdir -p "$W/a"
"$W/t" "$W/a"; r=$?
[ "$r" = 0 ] && echo "qrtime_traktid: ok"
exit "$r"
