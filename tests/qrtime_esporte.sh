#!/bin/bash
# Sport Center data and store: tests/qrtime_esporte.c against the fixture.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-esporte.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_esporte.c \
  src/espn.c src/esporte.c src/esportenomes.c src/tsdb.c src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
mkdir -p "$W/a"
fail=0
"$W/t" "$W/a" "http://127.0.0.1:$PORT" > "$W/out" 2>&1 || fail=1
kill "$MOCK"; MOCK=""
"$W/t" "$W/a" "http://127.0.0.1:$PORT" offline >> "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
[ "$fail" = 0 ] && echo "qrtime_esporte: ok"
exit "$fail"
