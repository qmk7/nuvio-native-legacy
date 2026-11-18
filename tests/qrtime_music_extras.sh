#!/bin/bash
# Music Center extras end to end on the real app (Xvfb, remote only):
#  - YouTube Music: Services -> YouTube Music -> music search (official API,
#    Music category) -> a result opens in the official YouTube app;
#  - screen off: Now Playing -> "Turn off screen" -> pure black frame while
#    the music keeps playing; any key turns it back on and does nothing else.
# Mocks: tests/qrtime_iptv_mock.py (YouTube Data API) and
# tests/qrtime_music_mock.py (Navidrome). SDL "disk" audio driver.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null 2>&1 || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-mcx.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
mkdir -p "$W/audio" "$W/yt" "$W/mu"
bash tests/qrtime_music_fixtures.sh "$W/audio"; r=$?
[ "$r" = 3 ] && { echo "SKIP: ffmpeg not installed (fixture generator)"; exit 0; }
[ "$r" = 0 ] || { echo "FAIL: fixtures"; exit 1; }
porta() { python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])'; }
PY=$(porta); PM=$(porta)
python3 tests/qrtime_iptv_mock.py "$PY" "$W/yt" >/dev/null 2>"$W/yt.err" &
PIDS+=($!)
python3 tests/qrtime_music_mock.py "$PM" "$W/mu" "$W/audio" >/dev/null 2>"$W/mu.err" &
PIDS+=($!)
sleep 1
d="$W/a"; mkdir -p "$d"
printf '1\n' > "$d/convidado.txt"
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
printf '2.0.8\n' > "$d/dicas-controle-205.txt"
for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"
printf 'chave=QRTYTKEY\ncliente=\nsegredo=\n' > "$d/youtube.txt"
printf '1\tss00000001\tQRT Navidrome\thttp://127.0.0.1:%s\tqrtuser\tqrt pass+1\n' "$PM" > "$d/music-sources-p1.txt"
D=$((90 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
LOG="$W/app.log"
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$W/out.raw" QRT_YT_BASE="http://127.0.0.1:$PY/yt/v3" QRT_OAUTH_BASE="http://127.0.0.1:$PY/goauth" \
  NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$LOG" 2>&1 &
APP=$!; PIDS+=($APP)
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
foto() {   # foto <name> [file]: a 50% PNG for the docs (QRT_SHOTS) and the raw BMP for checks
  rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -f /tmp/nuvio-shot.bmp ] && break; sleep 0.5; done
  cp /tmp/nuvio-shot.bmp "$W/$1.bmp" 2>/dev/null
  [ -n "${QRT_SHOTS:-}" ] && { mkdir -p "$QRT_SHOTS"; convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_SHOTS/music-$1.png" 2>/dev/null; }
  return 0
}

espera "tela=home" 60 || { erro "home never drawn"; exit 1; }
sleep 4
for k in 1 2 3 4; do tecla left 0.8; done
for k in 1 2 3 4 5; do tecla down 0.6; done
tecla ok 2.5
espera "\[transicao\] tela 2 -> 18 " 10 && ok "Music Center open" || erro "Music Center"
# Menu: Home (0) -> Music services (11).
for k in $(seq 1 11); do tecla down 0.5; done
espera "\[musica\] pagina 11: [0-9]+ linha\(s\) \(local\)" 10 || erro "Music services page"
tecla right 0.8
for k in 1 2 3 4 5 6; do tecla down 0.5; done
tecla ok 2
espera "\[musica\] pagina 105: 3 linha" 15 && ok "YouTube Music page (search; sign-in hint; quality note)" || erro "YouTube Music page"
foto ytmusic
tecla ok 1.2; tecla "colar:pasta" 1
espera "\[musica\] pagina 106: 2 linha" 15 && ok "music search: 2 results from the official API" || erro "music search"
grep -q "videoCategoryId=10" "$W/yt/requests.log" && ok "the search asks for YouTube's Music category" || erro "music category"
foto ytbusca
tecla ok 2
espera "\[youtube\] abrir video qrtSearch01 no app oficial" 5 && ok "a result opens in the official YouTube app" || erro "handoff"

# Screen off: Albums -> Day Album -> a track -> Now Playing -> Turn off screen.
tecla back 1; tecla back 1; tecla left 0.8
for k in $(seq 1 8); do tecla up 0.5; done
espera "\[musica\] pagina 3: 3 linha" 15 || erro "Albums"
tecla right 0.8; tecla down 0.6; tecla ok 2      # Day Album
espera "\[musica\] pagina 100: 4 linha" 15 || erro "album page"
tecla down 0.6; tecla down 0.6; tecla ok 3       # first track (Morning Tone, MP3)
espera "\[musica\] tocar \"Morning Tone\"" 15 && ok "a track plays" || erro "play"
sleep 1
foto tocando
for k in 1 2 3 4 5 6; do tecla right 0.4; done  # Pause(2) -> Turn off screen(8)
tecla ok 1.5
espera "\[musica\] tela desligada; a musica continua" 5 && ok "Turn off screen" || erro "turn off screen"
foto apagada
python3 -I - "$W/apagada.bmp" <<'PY' && ok "the frame is pure black (OLED pixels off)" || erro "the frame is not black"
import sys
b = open(sys.argv[1], "rb").read()
off = int.from_bytes(b[10:14], "little")
bpp = int.from_bytes(b[28:30], "little")
px = b[off:]
if bpp == 32:   # B G R A: the alpha byte is 255 on every pixel; only colour counts
    px = bytes(x for i, x in enumerate(px) if i % 4 != 3)
sys.exit(0 if px and max(px) <= 2 else 1)
PY
n=$(grep -c "\[musica\] tocar \"" "$LOG")
sleep 3
! grep -q "\[musica\] pausa em" "$LOG" && ok "the music keeps playing with the screen off" || erro "music paused"
tecla right 1.5
espera "\[musica\] tela ligada" 5 && ok "any key turns the screen back on" || erro "wake"
[ "$(grep -c "\[musica\] tocar \"" "$LOG")" = "$n" ] && ok "the waking key does nothing else (no track change)" || erro "the waking key acted"
foto acesa
kill -0 "$APP" 2>/dev/null && ok "app alive" || erro "app died"
grep -qF "qrt pass" "$LOG" && erro "password in the log"
[ "$fail" = 0 ] && echo "qrtime_music_extras: ok"
exit "$fail"
