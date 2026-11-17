#!/bin/bash
# IPTV library: tests/qrtime_iptvvod.c against the local fixtures.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-iptvvod.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; rm -rf "$W"' EXIT
SRC="src/admin.c src/iptvfontes.c src/xtream.c src/m3u.c src/m3u_parse.c src/stalker.c src/js.c src/dados.c"
cc -std=gnu11 -O1 -g -Isrc -Wno-format-truncation tests/qrtime_iptv_seed.c $SRC -lpthread -o "$W/seed" \
  || { echo "FAIL: seed tool"; exit 1; }
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -I/usr/include/SDL2 -DNV_LINUX_DESKTOP \
  tests/qrtime_iptvvod.c src/iptvvod.c src/xtvod.c src/stvod.c src/m3uvod.c $SRC src/rede.c src/redeurl.c -lpthread -ldl -lz -o "$W/t" \
  || { echo "FAIL: test build"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
mkdir -p "$W/a"
QRT_SEED_XT2=1 QRT_SEED_M3UVOD=1 "$W/seed" "$W/a" "http://127.0.0.1:$PORT" >/dev/null || { echo "FAIL: seeding"; exit 1; }
fail=0
"$W/t" "$W/a" online "http://127.0.0.1:$PORT" > "$W/out" 2>&1 || fail=1; [ -n "${QRT_DUMP:-}" ] && cp "$W/out" "$QRT_DUMP"
# Providers down and the cache stale: the library must survive.
kill "$MOCK"; MOCK=""
sed -i '1s/.*/v1\t1/' "$W/a/iptvvod-p1.tsv"
"$W/t" "$W/a" offline >> "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL)" "$W/out"
if grep -q "/3/find/.*api_key=QRTKEY\|qv:" "$W/requests.log"; then echo "FAIL: key or local id reached the fixture log"; fail=1; fi
if grep -q "/3/find/" "$W/requests.log" && ! grep -qE "/3/find/(tt0133093|tt1375666|tt0903747|tt9999999|tt0000404)" "$W/requests.log"; then echo "FAIL: unexpected TMDB lookup"; fail=1; fi
if grep -qE "/3/(find|movie|tv)/[^t0-9]" "$W/requests.log"; then echo "FAIL: a non-TMDB id went to TMDB"; fail=1; fi
if grep -qE "qrtpass" "$W/out"; then echo "FAIL: a password reached the output"; fail=1; fi
[ "$fail" = 0 ] && echo "qrtime_iptvvod: ok"
exit "$fail"
