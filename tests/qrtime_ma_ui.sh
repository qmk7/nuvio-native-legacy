#!/bin/bash
# Music Assistant in the Music screen, end to end on the real app (Xvfb)
# against tests/qrtime_ma_mock.py: the section opens from Home, a speaker is
# chosen and kept, a track plays on it (player_queues/play_media with that
# speaker), and the token never reaches the app log.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
[ -x "$BIN" ] || { echo "FAIL: build first (tools/linux.sh --build)"; exit 1; }
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-maui.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
TOKEN=tok-secret-4242
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_ma_mock.py "$PORT" "$W" "$TOKEN" 2>"$W/mock.err" &
PIDS+=($!)
d="$W/a"; mkdir -p "$d"
printf 'http://127.0.0.1:%s\n%s\n' "$PORT" "$TOKEN" > "$d/music-assistant.txt"
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
printf '2.0.8\n' > "$d/dicas-controle-205.txt"
for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"
D=$((80 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
LOG="$W/app.log"
NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$LOG" 2>&1 & PIDS+=($!)
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$3" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
shot() { rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req; sleep 3; [ -f /tmp/nuvio-shot.bmp ] && cp /tmp/nuvio-shot.bmp "$W/$1.bmp"; }
espera "tela=home" 90 "$LOG" || { erro "home did not open"; exit 1; }
sleep 4
tecla "tela:musica" 3
espera "players/all|recently_played" 5 "$W/ma-commands.log" >/dev/null 2>&1
# Music Assistant and no server of its own: Music opens on its home.
sleep 3   # Music opens straight on the Music Assistant home
espera "music/recently_played_items" 20 "$W/ma-commands.log" && ok "Music Assistant home asks the recent items" || erro "no recently_played_items"
espera "music/playlists/library_items" 20 "$W/ma-commands.log" && ok "...and the playlists row" || erro "no playlists row"
shot ma-home
tecla ok 3                                      # first cover, no speaker yet: the soundbar is picked by itself
grep -q "media_player.q990f" "$d/music-assistant-player.txt" 2>/dev/null && ok "no speaker yet: the soundbar picked and kept" || erro "speaker not picked"
espera "player_queues/play_media" 20 "$W/ma-commands.log" && ok "the cover plays at once" || erro "no play_media"
grep "player_queues/play_media" "$W/ma-commands.log" | grep -q '"queue_id": "media_player.q990f"' && ok "on the chosen speaker" || erro "wrong speaker: $(grep play_media "$W/ma-commands.log")"
sleep 6
shot ma-tocando
# The sections on the left are Music Assistant's too: Artists lists its artists.
tecla back 1
for i in 1 2; do tecla down 1; done
espera 'music/artists/library_items \{"limit": 80' 10 "$W/ma-commands.log" && ok "Artists section asks Music Assistant" || erro "Artists section not from Music Assistant"
shot ma-artistas
# Magic Remote: a click on the second cover of the home plays it.
tecla up 1; tecla up 1; tecla right 2
tecla "mover:800,760" 1; tecla "clicar:800,760" 3
espera '"media": \["ytmusic://track/t2"\]' 10 "$W/ma-commands.log" && ok "pointer click plays the cover under it" || erro "pointer click did not play"
grep -q "$TOKEN" "$LOG" && erro "the token is in the app log" || ok "no token in the app log"
[ "$fail" = 0 ] && echo "qrtime_ma_ui: ok"
exit "$fail"
