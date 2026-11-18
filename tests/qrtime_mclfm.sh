#!/bin/bash
# Music Center: Last.fm (signed calls, sign-in, scrobble queue) and lyrics
# (LRCLIB, LRC parsing) against tests/qrtime_music_mock.py.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-mclfm.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
mkdir -p "$W/a" "$W/audio"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_mclfm.c \
  src/mclfm.c src/mclrc.c src/mcfila.c src/md5.c src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_music_mock.py "$PORT" "$W" "$W/audio" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
fail=0
"$W/t" "$W/a" "http://127.0.0.1:$PORT" "$W" > "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
grep -q "POST /lfm/ application/x-www-form-urlencoded method=track.scrobble" "$W/requests.log" \
  && echo "PASS: scrobbles are form POSTs" || { echo "FAIL: scrobble POST"; fail=1; }
grep -q "LRCLIB-UA" "$W/requests.log" && { echo "FAIL: LRCLIB got another User-Agent"; fail=1; }
for s in qrtsecret SK1; do
  grep -qF "$s" "$W/out" && { echo "FAIL: secret '$s' in the output"; fail=1; }
done
[ "$fail" = 0 ] && echo "qrtime_mclfm: ok"
exit "$fail"
