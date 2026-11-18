#!/bin/bash
# Music Center end to end on the real app (Xvfb), with the remote only,
# against tests/qrtime_music_mock.py (Navidrome-shaped server + WebDAV) and
# SDL's "disk" audio driver: add a server from the form (pasted as from the
# phone), browse Albums -> album, play a track, pause/continue, next, lyrics,
# the queue, search, the Now Playing format lines, resume after a restart,
# Back to Home. No password in the log.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-music.XXXXXX")
PIDS=()
MOCK=""
fim() { for p in "${PIDS[@]}" $MOCK; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
mkdir -p "$W/audio"
bash tests/qrtime_music_fixtures.sh "$W/audio"; r=$?
[ "$r" = 3 ] && { echo "SKIP: ffmpeg not installed (fixture generator)"; exit 0; }
[ "$r" = 0 ] || { echo "FAIL: fixtures"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_music_mock.py "$PORT" "$W" "$W/audio" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
d="$W/a"; mkdir -p "$d"
printf '1\n' > "$d/convidado.txt"
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
printf '2.0.8\n' > "$d/dicas-controle-205.txt"
for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"
D=$((80 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
APP=""
abrir() { SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$W/audio-out.raw" \
          QRT_LRCLIB_BASE="http://127.0.0.1:$PORT/lrclib/api" QRT_LFM_BASE="http://127.0.0.1:$PORT/lfm/" \
          NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$1" 2>&1 &
          APP=$!; LOG=$1; PIDS+=($APP); }
fechar() { kill "$APP" 2>/dev/null; sleep 1; kill -9 "$APP" 2>/dev/null; }
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
foto() {
  [ -n "${QRT_SHOTS:-}" ] || return 0
  mkdir -p "$QRT_SHOTS"; rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -f /tmp/nuvio-shot.bmp ] && break; sleep 0.5; done
  convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_SHOTS/music-$1.png" 2>/dev/null
}
musica() {   # Home -> main menu -> Music (5)
  espera "tela=home" 60 || return 1
  sleep 4; local k; for k in 1 2 3 4; do tecla left 0.8; done
  for k in 1 2 3 4 5; do tecla down 0.6; done
  tecla ok 2.5
  espera "\[transicao\] tela 2 -> 18 " 10
}

abrir "$W/1.log"
musica && ok "menu Music opens the Music Center (screen 18)" || erro "Music Center did not open"
espera "\[musica\] pagina 11: [0-9]+ linha\(s\) \(local\)" 10 && ok "no server yet: it opens on Music services" || erro "first section"
foto servicos
# Add Navidrome: list -> "Add Navidrome / Subsonic" -> form.
tecla right 1
tecla ok 1.2
tecla ok 1.2; tecla "colar:http://127.0.0.1:$PORT" 1.2
tecla ok 1.2; tecla "colar:qrtuser" 1.2
tecla ok 1.2; tecla "colar:qrt pass+1" 1.2
foto form
tecla down 0.6; tecla down 0.6; tecla ok 3
grep -q "^1	ss[0-9a-f]\{8\}	127.0.0.1:$PORT	http://127.0.0.1:$PORT	qrtuser	qrt pass+1" "$d/music-sources-p1.txt" 2>/dev/null \
  && ok "server tested and saved from the form" || erro "server not saved: $(cut -f1-5 "$d/music-sources-p1.txt" 2>/dev/null)"
# Albums: menu up to section 3.
tecla left 0.8
for k in 1 2 3 4 5 6 7 8; do tecla up 0.6; done
espera "\[musica\] pagina 3: 3 linha" 15 && ok "Albums: 3 albums from the server" || erro "Albums"
foto albums
tecla right 0.8; tecla ok 2
espera "\[musica\] pagina 100: 63 linha" 15 && ok "album page: play all, shuffle, 61 tracks" || erro "album page"
tecla down 0.6; tecla down 0.6; tecla ok 3
espera "\[musica\] tocar \"Track 05\" \(flac\) em desktop" 15 && ok "OK on a track plays it" || erro "play"
espera "\[musica\] saida SDL \(disk\): pedido 44100 Hz s32 2ch, obtido 44100 Hz s32 2ch \(sem conversao\)" 15 \
  && ok "output opened at the source format, no conversion" || erro "output format"
sleep 2
foto tocando
tecla ok 1.5
grep -q "\[musica\] pausa em" "$LOG" && ok "Play/Pause pauses" || erro "pause"
tecla ok 1.5
grep -q "\[musica\] continua" "$LOG" && ok "and continues" || erro "continue"
n=$(grep -c "\[musica\] tocar \"" "$LOG")
tecla right 0.6; tecla ok 3
[ "$(grep -c "\[musica\] tocar \"" "$LOG")" -gt "$n" ] && ok "Next opens the next track" || erro "next"
grep -qE "^s5 false" "$W/scrobbles.log" && ok "now playing reported to the server (Subsonic scrobble)" || erro "now playing"
for k in 1 2 3; do tecla right 0.5; done
tecla ok 2
foto letra
grep -q "GET /lrclib/api/get?artist_name=Many+Songs&track_name=Track+[0-9][0-9]&album_name=Big+Box&duration=5" "$W/requests.log" \
  && ok "lyrics: server first, then LRCLIB with artist/title only" || erro "lyrics lookup"
tecla right 0.6; tecla ok 2
espera "\[musica\] pagina 9: 6[0-9] linha\(s\) \(local\)" 10 && ok "Queue button opens the queue (actions + 61 tracks)" || erro "queue page"
foto fila
# Search: menu -> down to Search (10) -> OK -> keyboard.
tecla left 0.8; tecla down 0.8; tecla ok 1.5
tecla "colar:night" 1
espera "\[musica\] pagina 10: [1-9]" 15 && ok "Search finds the album and its tracks" || erro "search"
foto busca
sleep 11   # position saved every 10 s
fechar
grep -q "^Q	" "$d/music-queue-p1.txt" 2>/dev/null && ok "queue and position saved for this profile" || erro "queue not saved"
cp "$d/music-queue-p1.txt" "$W/fila-antes.txt" 2>/dev/null

# Restart: Home -> "Continue" -> paused at the saved point.
abrir "$W/2.log"
musica || erro "Music Center after restart"
espera "\[musica\] pagina 0:" 15
tecla right 0.8; tecla ok 3
sal=$(sed -n 's/^Q\t\([0-9]*\)\t.*/\1/p' "$W/fila-antes.txt")
espera "\[musica\] retomar a fila na faixa $((sal + 1)) em [0-9]+s" 10 && ok "resume after a restart: same queue, same track" || erro "resume (saved index $sal)"
espera "\[musica\] tocar \"" 10 && ! grep -q "\[musica\] continua" "$LOG" && ok "the track reopens, paused until Play" || erro "resume track"
foto retomar
grep -q "^Q	" "$d/music-queue-p1.txt" && ok "the saved queue survives the app's start-up profile session" || erro "queue file lost at start-up"
tecla back 1; tecla back 1; tecla left 0.8; tecla back 2
espera "\[transicao\] tela 18 -> 2 " 10 && ok "Back returns Home" || erro "back to home"
kill -0 "$APP" 2>/dev/null && ok "app alive" || erro "app died"
fechar
for s in "qrt pass" "qrt%20pass"; do
  grep -qF "$s" "$W/1.log" "$W/2.log" "$W/requests.log" && erro "password '$s' in a log"
done
grep -q "TRANSCODE-REQUESTED" "$W/requests.log" && erro "a transcode was requested"
[ "$fail" = 0 ] && echo "qrtime_music_ui: ok"
exit "$fail"
