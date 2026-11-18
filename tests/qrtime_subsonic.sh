#!/bin/bash
# Music Center providers: Subsonic (Navidrome-shaped) and WebDAV clients
# against tests/qrtime_music_mock.py with real audio fixtures; then with the
# server down. No secret may reach the output or the server's request log.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-subsonic.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
mkdir -p "$W/audio"
bash tests/qrtime_music_fixtures.sh "$W/audio"; r=$?
[ "$r" = 3 ] && { echo "SKIP: ffmpeg not installed (fixture generator)"; exit 0; }
[ "$r" = 0 ] || { echo "FAIL: fixtures"; exit 1; }
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_subsonic.c \
  src/subsonic.c src/webdav.c src/mcfmt.c src/md5.c src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_music_mock.py "$PORT" "$W" "$W/audio" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
fail=0
"$W/t" "http://127.0.0.1:$PORT" "$W/audio" > "$W/out" 2>&1 || fail=1
kill "$MOCK"; MOCK=""
sleep 0.5
"$W/t" "http://127.0.0.1:$PORT" "$W/audio" offline >> "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
for s in "qrt pass" "qrt%20pass" "davpass" "ZGF2dXNlcjpkYXZwYXNz" "ldappw"; do
  if grep -qF "$s" "$W/out" "$W/requests.log" 2>/dev/null; then echo "FAIL: secret '$s' in the output or request log"; fail=1; fi
done
grep -q "TRANSCODE-REQUESTED" "$W/requests.log" && { echo "FAIL: a transcode was requested"; fail=1; }
grep -q "PROPFIND .* depth=1 auth=yes ct=application/xml" "$W/requests.log" && echo "PASS: PROPFIND Depth 1, authenticated, XML body" \
  || { echo "FAIL: PROPFIND shape"; fail=1; }
[ "$fail" = 0 ] && echo "qrtime_subsonic: ok"
[ -s "$W/mock.err" ] && { echo "mock stderr:"; tail -5 "$W/mock.err"; }
exit "$fail"
