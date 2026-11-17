#!/bin/bash
# Xtream VOD/series adapter: tests/qrtime_xtvod.c against the local fixtures.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-xtvod.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
SRC="src/admin.c src/iptvfontes.c src/xtream.c src/m3u.c src/m3u_parse.c src/stalker.c src/js.c src/dados.c"
cc -std=gnu11 -O1 -g -Isrc -Wno-format-truncation tests/qrtime_iptv_seed.c $SRC -lpthread -o "$W/seed" \
  || { echo "FAIL: seed tool"; exit 1; }
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP tests/qrtime_xtvod.c src/xtvod.c \
  $SRC src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
mkdir -p "$W/a"
QRT_SEED_XT2=1 "$W/seed" "$W/a" "http://127.0.0.1:$PORT" >/dev/null || { echo "FAIL: seeding"; exit 1; }
fail=0
"$W/t" "$W/a" > "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
if grep -qE "qrtpass" "$W/out"; then echo "FAIL: a password reached the output"; fail=1; fi
if grep -q "qrtpass" "$W/requests.log"; then echo "FAIL: mock log not masked"; fail=1; fi
[ "$fail" = 0 ] && echo "qrtime_xtvod: ok"
exit "$fail"
