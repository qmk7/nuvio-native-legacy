#!/bin/bash
# Music Center queue, store and player: the logic with a fake output, then the
# real chain (mock Navidrome + WebDAV, SDL "disk" audio driver) where the
# bytes the device received are compared with the decoded files.
set -u
cd "$(dirname "$0")/.."
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-mcplayer.XXXXXX")
MOCK=""
trap '[ -n "$MOCK" ] && kill $MOCK 2>/dev/null; [ "${QRT_KEEP:-0}" = 1 ] && echo "kept: $W" || rm -rf "$W"' EXIT
mkdir -p "$W/audio" "$W/a" "$W/b"
bash tests/qrtime_music_fixtures.sh "$W/audio"; r=$?
[ "$r" = 3 ] && { echo "SKIP: ffmpeg not installed (fixture generator)"; exit 0; }
[ "$r" = 0 ] || { echo "FAIL: fixtures"; exit 1; }
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -DNV_LINUX_DESKTOP $(pkg-config --cflags sdl2) tests/qrtime_mcplayer.c \
  src/mccat.c src/mcoffline.c src/mcfila.c src/mcfontes.c src/mcplayer.c src/mcsaida_sdl.c src/mcsaida_ums.c src/mcdec.c src/mcfmt.c \
  src/subsonic.c src/webdav.c src/md5.c src/js.c src/dados.c src/rede.c src/redeurl.c \
  $(pkg-config --libs sdl2) -lpthread -ldl -lz -lm -o "$W/t" || { echo "FAIL: test build"; exit 1; }
fail=0
"$W/t" "$W/a" logic > "$W/out" 2>&1 || fail=1
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_music_mock.py "$PORT" "$W" "$W/audio" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$W/out.raw" SDL_DISKAUDIODELAY=2 \
  "$W/t" "$W/b" "http://127.0.0.1:$PORT" "$W/audio" "$W/out.raw" >> "$W/out" 2>&1 || fail=1
grep -E "^(PASS|FAIL|info:)" "$W/out"
grep -q "DAVGET .*01%20-%20Blue.flac range=- auth=yes" "$W/requests.log" && echo "PASS: WebDAV stream sent with Basic auth" \
  || { echo "FAIL: WebDAV stream auth"; fail=1; }
grep -q "TRANSCODE-REQUESTED" "$W/requests.log" && { echo "FAIL: a transcode was requested"; fail=1; }
grep -q "^s4 \|^s.* true" "$W/scrobbles.log" 2>/dev/null; grep -c "false" "$W/scrobbles.log" >/dev/null 2>&1 \
  && echo "PASS: now-playing sent to the server" || { echo "FAIL: now-playing scrobble"; fail=1; }
for s in "qrt pass" "davpass" "ZGF2dXNlcjpkYXZwYXNz"; do
  grep -qF "$s" "$W/out" "$W/requests.log" && { echo "FAIL: secret '$s' leaked"; fail=1; }
done
[ "$fail" = 0 ] && echo "qrtime_mcplayer: ok"
exit "$fail"
