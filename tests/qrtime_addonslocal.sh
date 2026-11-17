#!/bin/bash
# Guest add-on list: tests/qrtime_addonslocal.c against the local fixture.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-addonslocal.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_addonslocal.c \
  src/addonslocal.c src/js.c src/dados.c src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
mkdir -p "$W/a"
fail=0
"$W/t" "$W/a" "http://127.0.0.1:$PORT" > "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
grep -q "127.0.0.1:$PORT/addon" "$W/out" && { echo "FAIL: an add-on URL reached the log"; fail=1; }
[ "$fail" = 0 ] && echo "qrtime_addonslocal: ok"
exit "$fail"
