#!/bin/bash
# QR TIME Trakt: real write paths (history, episodes, scrobble) with the HTTP
# transport replaced: identity safety, duplicates, 409/429.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-traktsync.XXXXXX")
trap 'rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wno-format-truncation -ffunction-sections -fdata-sections -Wl,--gc-sections -Isrc -DNV_DUBLE_SESSAO \
  src/trakt.c src/js.c src/jsw.c src/metaep.c src/traktscrobble.c src/traktid.c src/traktdup.c tests/qrtime_trakt_sync.c \
  -lpthread -o "$W/t" || { echo "FAIL: test build"; exit 1; }
"$W/t"; r=$?
[ "$r" = 0 ] && echo "qrtime_trakt_sync: ok"
exit "$r"
