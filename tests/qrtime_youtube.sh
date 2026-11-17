#!/bin/bash
# YouTube section data layer: tests/qrtime_youtube.c against the fixture, then
# with the fixture down. No secret may reach the program's output or the
# fixture's request log.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-youtube.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_youtube.c \
  src/ytapi.c src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
mkdir -p "$W/a"
fail=0
"$W/t" "$W/a" "http://127.0.0.1:$PORT" > "$W/out" 2>&1 || fail=1
kill "$MOCK"; MOCK=""
mkdir -p "$W/b"
"$W/t" "$W/b" "http://127.0.0.1:$PORT" offline >> "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
for s in QRTYTKEY QRTREFRESH QRTACCESS qrt-secret QRTDEVICE; do
  if grep -q "$s" "$W/out" "$W/requests.log" 2>/dev/null; then echo "FAIL: secret $s leaked to output or request log"; fail=1; fi
done
grep -q "POST /goauth/token application/x-www-form-urlencoded" "$W/requests.log" \
  && echo "PASS: OAuth posts are form-encoded" || { echo "FAIL: OAuth content type"; fail=1; }
[ "$fail" = 0 ] && echo "qrtime_youtube: ok"
exit "$fail"
