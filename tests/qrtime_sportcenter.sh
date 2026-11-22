#!/bin/bash
# Sport Center end to end on the real app (Xvfb): ESPN-shaped fixtures and the
# local IPTV providers (tests/qrtime_iptv_mock.py). The live Saudi match's
# programme on "QRT Sports Arena" names both teams, so the match page must
# suggest that channel; linking it and pressing Watch must hand the channel to
# the native player. Also: a reminder for an upcoming match with no channel,
# hiding scores, and both surviving a restart.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-sport.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
cc -std=gnu11 -O1 -g -Isrc -Wno-format-truncation tests/qrtime_iptv_seed.c src/iptvfontes.c src/xtream.c src/admin.c \
  src/m3u.c src/m3u_parse.c src/stalker.c src/js.c src/dados.c -lpthread -o "$W/seed" || { echo "FAIL: seed tool"; exit 1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
PIDS+=($!)
sleep 1
d="$W/a"; mkdir -p "$d"
"$W/seed" "$d" "http://127.0.0.1:$PORT" >/dev/null || { echo "FAIL: seeding"; exit 1; }
printf '1\n' > "$d/convidado.txt"
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
printf '2.0.8\n' > "$d/dicas-controle-205.txt"
for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"
D=$((60 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
APP=""
abrir() { QRT_ESPN_BASE="http://127.0.0.1:$PORT/espn" QRT_TSDB_BASE="http://127.0.0.1:$PORT/tsdb" NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$1" 2>&1 &
          APP=$!; LOG=$1; PIDS+=($APP); }
fechar() { kill "$APP" 2>/dev/null; sleep 1; kill -9 "$APP" 2>/dev/null; }
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
sport() {   # Home -> main menu -> Sport Center
  espera "tela=home" 60 || return 1
  sleep 4; local k; for k in 1 2 3 4; do tecla left 0.8; done
  for k in 1 2 3 4; do tecla down 0.6; done
  tecla ok 2
  espera "\[esporte\] [0-9]+ evento" 30
}

abrir "$W/1.log"
sport || erro "Sport Center did not load"
grep -qE "\[esporte\] 5 evento\(s\) de 6 liga\(s\)" "$LOG" && ok "followed leagues loaded (5 events, 6 leagues)" || erro "events: $(grep '\[esporte\] [0-9]' "$LOG" | tail -1)"
grep -qE "soccer/esp.1( [0-9]{6})?: o provedor nao respondeu" "$LOG" && ok "a failing league is reported, the others still load" || erro "provider failure not reported"
# Tabs -> filters -> first row (the live match) -> match page; the guide's EPG
# must load before the suggestion can appear.
tecla down 0.6; tecla down 0.6; tecla ok 3
espera "grade publicada" 40 || erro "guide EPG did not load"
sleep 6
espera "TheSportsDB: [0-9]+ canal\(is\) para o evento 700001" 20 && grep -q "TheSportsDB: 3 canal(is) para o evento 700001" "$LOG" \
  && ok "TheSportsDB channels fetched for the match (beIN / SSC)" || erro "TheSportsDB lookup"
tecla ok 2      # first action: the suggestion "QRT Sports Arena · showing it"
grep -q "canal	700001	xtream:@00000001:201	QRT Sports Arena" "$d/esporte-p1.txt" && ok "suggested channel (programme names both teams) linked to the match" \
  || erro "suggestion not linked: $(cat "$d/esporte-p1.txt" 2>/dev/null)"
tecla up 0.5; tecla up 0.5; tecla up 0.5; tecla ok 5   # "Watch in QRT Sports Arena"
grep -q "\[esporte\] assistir: evento 700001 no canal xtream:@00000001:201" "$LOG" && ok "Watch asks for the linked channel" || erro "watch request"
grep -q "\[canal\] tocar id=xtream:@00000001:201 titulo=QRT Sports Arena" "$LOG" && ok "the native player gets the channel" || erro "player handoff"
tecla back 3; tecla back 2
# Second row: the upcoming match without a channel -> reminder.
tecla down 0.6; tecla ok 3
tecla down 0.6; tecla ok 2     # actions: choose channel, reminder, follow, follow
grep -q "esporte:700002" "$d"/guia-lembretes-p1.txt 2>/dev/null && ok "reminder for an upcoming match without a channel" || erro "reminder"
tecla back 2
tecla up 0.6; tecla up 0.6     # list -> filters
for k in 1 2 3; do tecla right 0.5; done
tecla ok 1
grep -q "^spoiler	1" "$d/esporte-p1.txt" && ok "hide scores saved" || erro "hide scores"
fechar

abrir "$W/2.log"
sport || erro "Sport Center did not load after restart"
grep -q "canal	700001	xtream:@00000001:201" "$d/esporte-p1.txt" && grep -q "^spoiler	1" "$d/esporte-p1.txt" \
  && ok "links and settings survive a restart" || erro "after restart"
fechar
[ "$fail" = 0 ] && echo "qrtime_sportcenter: ok"
exit "$fail"
