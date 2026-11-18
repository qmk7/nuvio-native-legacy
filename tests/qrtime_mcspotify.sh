#!/bin/bash
# Music Center: Spotify import (metadata only) and the thread-safe local
# copies, against tests/qrtime_music_mock.py. The client secret must not
# reach the output.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-mcspotify.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
mkdir -p "$W/a" "$W/audio"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_mcspotify.c \
  src/mcspotify.c src/mccat.c src/mcfila.c src/mcfontes.c src/mcoffline.c src/mcfmt.c src/subsonic.c src/webdav.c src/md5.c \
  src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_music_mock.py "$PORT" "$W" "$W/audio" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
fail=0
"$W/t" "$W/a" "http://127.0.0.1:$PORT" > "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
grep -q "qrtspsecret" "$W/out" && { echo "FAIL: client secret in the output"; fail=1; }
[ "$fail" = 0 ] && echo "qrtime_mcspotify: ok"
[ -s "$W/mock.err" ] && tail -3 "$W/mock.err"
exit "$fail"
