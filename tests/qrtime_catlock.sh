#!/bin/bash
# Parental lock for Live TV categories, end to end on the real app (Xvfb)
# against the local IPTV providers (tests/qrtime_iptv_mock.py).
#
# The seed tool stores an administrator PIN (1234) and locks the Xtream
# category "QRT Sports" (1 of the 7 channels). Checks: the locked channel is
# gone from the guide; a wrong PIN changes nothing; the right PIN shows it until
# the app closes; "Lock again" hides it; a restart starts locked; no PIN in logs.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-catlock.XXXXXX")
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
QRT_SEED_PIN=1234 QRT_SEED_LOCK="1:1:QRT Sports" "$W/seed" "$d" "http://127.0.0.1:$PORT" >/dev/null \
  || { echo "FAIL: seeding"; exit 1; }
printf '1\n' > "$d/convidado.txt"
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
printf '2.0.8\n' > "$d/dicas-controle-205.txt"
for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"

D=$((70 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
APP=""
abrir() { NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$1" 2>&1 & APP=$!; LOG=$1; PIDS+=($APP); }
fechar() { kill "$APP" 2>/dev/null; sleep 1; kill -9 "$APP" 2>/dev/null; }
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
guia() {
  espera "tela=home" 60 || return 1
  sleep 4; local k; for k in 1 2 3 4; do tecla left 0.8; done
  tecla down 0.8; tecla ok 1.0
  espera "grade publicada" 60
}
total() { grep -oE "\[guia\] [0-9]+ canais em [0-9]+ grupos" "$LOG" | tail -1 | grep -oE "[0-9]+ canais em [0-9]+"; }
# Guide top bar -> Options -> the last row (lock / unlock).
opcaoBloq() {
  tecla up 0.8; tecla up 0.8; tecla up 0.8
  tecla right 0.6; tecla right 0.6; tecla ok 1.2
  local i; for i in 1 2 3 4 5; do tecla down 0.5; done
  tecla ok 1.5
}
# Digit grid: focus starts on 0; type each digit, then the Done button.
pin() {
  local atual=0 c alvo
  for c in $(echo "$1" | grep -o .); do
    alvo=$c
    while [ "$atual" -lt "$alvo" ]; do tecla right 0.4; atual=$((atual + 1)); done
    while [ "$atual" -gt "$alvo" ]; do tecla left 0.4; atual=$((atual - 1)); done
    tecla ok 0.5
  done
  tecla down 0.5; tecla down 0.6
  tecla right 0.4; tecla right 0.4; tecla right 0.4; tecla right 0.4   # Done is the last button
  tecla ok 5
}

abrir "$W/1.log"
guia || erro "guide did not load"
sleep 3
[ "$(total)" = "6 canais em 5" ] && ok "locked category hidden (6 channels, 5 groups)" || erro "locked: $(total)"

opcaoBloq; pin 1111
grep -q "desbloqueio: PIN recusado" "$LOG" && ok "wrong PIN refused" || erro "wrong PIN not refused"
[ "$(total)" = "6 canais em 5" ] && ok "wrong PIN keeps the category locked" || erro "after wrong PIN: $(total)"

opcaoBloq; pin 1234
grep -q "categorias desbloqueadas ate fechar o app" "$LOG" && ok "right PIN unlocks" || erro "right PIN did not unlock"
sleep 2
[ "$(total)" = "7 canais em 6" ] && ok "unlocked: 7 channels, 6 groups" || erro "unlocked: $(total)"

opcaoBloq
grep -q "categorias bloqueadas de novo" "$LOG" && ok "Lock again from Options" || erro "lock again"
sleep 2
[ "$(total)" = "6 canais em 5" ] && ok "locked again: 6 channels" || erro "locked again: $(total)"
fechar

abrir "$W/2.log"
guia || erro "guide (restart) did not load"
sleep 3
[ "$(total)" = "6 canais em 5" ] && ok "a restart starts locked" || erro "after restart: $(total)"
fechar

if grep -qE "1234|1111" "$W/1.log" "$W/2.log"; then erro "a PIN appears in the app log"
else ok "no PIN in the app logs"; fi
[ "$fail" = 0 ] && echo "qrtime_catlock: ok"
exit "$fail"
