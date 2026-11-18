#!/bin/bash
# QR TIME phase 8: Accounts, providers and data, in the real app (Xvfb).
# Two Navidrome sources against the local Subsonic mock (one right, one wrong
# password): the hub shows them, tests them for real, switches one off (kept
# on disk, password intact); a reset asks first with Cancel focused and
# deletes only after an explicit confirm, only this profile's file; Back
# returns to Settings; no password reaches the log.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-contas.XXXXXX")
PIDS=(); MOCK=""
fim() { for p in "${PIDS[@]}" $MOCK; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
mkdir -p "$W/audio"
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
# Two sources: the right password and a wrong one.
printf '1\tss0000000a\tHome NAS\thttp://127.0.0.1:%s\tqrtuser\tqrt pass+1\n1\tss0000000b\tBad login\thttp://127.0.0.1:%s\tqrtuser\twrongpw77\n' \
  "$PORT" "$PORT" > "$d/music-sources-p1.txt"
printf 'x\n' > "$d/music-history-p1.txt"; printf 'x\n' > "$d/music-history-p2.txt"
D=$((80 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
APP=""
abrir() { NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$1" 2>&1 & APP=$!; LOG=$1; PIDS+=($APP); }
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
foto() {
  [ -n "${QRT_SHOTS:-}" ] || return 0
  mkdir -p "$QRT_SHOTS"; rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -f /tmp/nuvio-shot.bmp ] && break; sleep 0.5; done
  convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_SHOTS/contas-$1.png" 2>/dev/null
}

abrir "$W/1.log"
espera "tela=home" 60 || { erro "home"; exit 1; }
sleep 3
tecla "tela:contas" 2
espera "\[contas\] aberto: [0-9]+ provedor" 10 && ok "the screen opens" || erro "screen did not open"
# Rows: type 3 = music source (state 1 configured), type 10 = TheSportsDB.
nm=$(grep -cE "\[contas\]   [0-9]+ tipo=3 estado=1 pode=11" "$LOG")
[ "$nm" = 2 ] && ok "both music sources listed, configured, with test / on-off / open" || erro "music rows ($nm)"
grep -qE "\[contas\]   [0-9]+ tipo=10 estado=1 pode=16" "$LOG" && ok "TheSportsDB row: free key, personal key button" || erro "TheSportsDB row"
grep -qE "\[contas\]   [0-9]+ tipo=4 estado=0" "$LOG" && ok "Trakt row: not connected (real state)" || erro "Trakt row"
i=$(grep -oE "\[contas\]   [0-9]+ tipo=3 " "$LOG" | head -1 | grep -oE "[0-9]+ tipo" | grep -oE "^[0-9]+")
for k in $(seq 1 "$i"); do tecla down 0.4; done
foto provedores
# Button 0 = Test connection.
tecla ok 1
espera "\[contas\] test music source ss0000000a -> ok" 15 && ok "test with the right password: OK (real request to the server)" || erro "test 1"
tecla down 0.5; tecla ok 1
espera "\[contas\] test music source ss0000000b -> failed" 15 && ok "test with a wrong password: failed" || erro "test 2"
foto teste
# Button 1 = switch off (second source).
tecla right 0.5; tecla ok 1.2
grep -qP "^1\tss0000000b\tBad login\t.*\twrongpw77\toff$" "$d/music-sources-p1.txt" && ok "switched off: kept on disk, marked off, password intact" || erro "switch off"
grep -qP "^1\tss0000000a\t.*\tqrt pass\+1$" "$d/music-sources-p1.txt" && ok "the other source untouched" || erro "other source changed"
# Data tab: up to the tabs, right, down into the list, to "music history" (2).
for k in $(seq 1 $((i + 2))); do tecla up 0.3; done
tecla right 0.6; tecla down 0.6; tecla down 0.5; tecla down 0.5
foto dados
tecla ok 1
espera "\[contas\] confirmar reset 2 \(foco em Cancelar\)" 5 && ok "a reset asks first, Cancel focused" || erro "confirmation"
foto confirmar
tecla ok 1
espera "\[contas\] reset 2: cancelado" 5 && [ -f "$d/music-history-p1.txt" ] && ok "OK on the default button cancels; nothing deleted" || erro "cancel"
tecla ok 1; tecla right 0.5; tecla ok 1.5
espera "\[contas\] reset 2: confirmado" 5 && [ ! -f "$d/music-history-p1.txt" ] && ok "explicit confirm: this profile's history deleted" || erro "confirm"
[ -f "$d/music-history-p2.txt" ] && ok "profile 2's history untouched" || erro "profile 2 touched"
grep -qE "\[dados\] reset 2 \(this profile\) -> ok" "$LOG" && ok "reset logged (scope, result)" || erro "reset log"
tecla back 0.8; tecla back 1.5
espera "\[transicao\] tela 19 -> 8 " 5 && ok "Back returns to Settings" || erro "back to settings"
if grep -qE "qrt pass|wrongpw77" "$LOG"; then erro "a password appears in the log"; else ok "no password in the log"; fi
[ -f "$d/music-sources-p1.txt" ] && [ "$(stat -c %a "$d/music-sources-p1.txt")" = 600 ] && ok "sources file rewritten owner-only (0600)" || erro "file mode"
exit $fail
