#!/bin/bash
# QR TIME Live TV end to end, on the real app (Xvfb), against LOCAL IPTV test
# providers (tests/qrtime_iptv_mock.py: Xtream Codes, M3U + XMLTV, Stalker).
# The fixtures prove protocol handling and the app's own flows; playback of a
# real stream is only provable on the TV (the desktop build has no decoder).
#
# Accounts are created by tests/qrtime_iptv_seed.c through the app's own
# account code. Remote keys go through the app's injection file.
#
#   bash tests/qrtime_livetv.sh      (QRT_LT_KEEP=1 keeps logs and data)
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi

W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-livetv.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; rm -f /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }

cc -std=gnu11 -O1 -g -Isrc -Wno-format-truncation tests/qrtime_iptv_seed.c src/iptvfontes.c src/xtream.c \
  src/m3u.c src/m3u_parse.c src/stalker.c src/js.c src/dados.c -lpthread -o "$W/seed" \
  || { echo "FAIL: seed tool"; exit 1; }

PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
PIDS+=($!)
sleep 1
BASE="http://127.0.0.1:$PORT"

preparar() {  # preparar <data dir>: guest + first-run cards already seen
  local d=$1 n f
  printf '1\n' > "$d/convidado.txt"
  for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
           172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do printf '1\n' > "$d/novidades-$n.txt"; done
  printf '2.0.8\n' > "$d/dicas-controle-205.txt"
  for f in salvos-intro guia-dica aviso-log; do printf '1\n' > "$d/$f.txt"; done
  printf 'versao=1\n' > "$d/diagnostico-otimizacao-intro.cfg"
}
mkdir -p "$W/a" "$W/b"
"$W/seed" "$W/a" "$BASE" >/dev/null || { echo "FAIL: seeding"; exit 1; }
"$W/seed" "$W/b" "$BASE" badpass >/dev/null || { echo "FAIL: seeding (bad)"; exit 1; }
preparar "$W/a"; preparar "$W/b"

D=$((80 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
APP=""
abrir() {  # abrir <data dir> <log>
  NUVIO_DADOS="$1" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$2" 2>&1 &
  APP=$!; LOG=$2
}
fechar() { kill "$APP" 2>/dev/null; sleep 1; kill -9 "$APP" 2>/dev/null; }
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
guia() {  # open Live TV from Home via the main menu
  espera "tela=home" 60 || return 1
  sleep 4; local k; for k in 1 2 3 4; do tecla left 0.8; done
  tecla down 0.8; tecla ok 1.0
  espera "grade publicada" 60
}

# ---- 1. three providers load, EPG matches, time zone is right ----------------
abrir "$W/a" "$W/a.log"
guia || erro "guide did not finish loading"
grep -q "\[xtream\] conta: auth=1 status=Active" "$LOG" && ok "Xtream login (auth=1, Active)" || erro "Xtream login"
grep -q "\[xtream\] 3 canal(is) em 2 categoria(s)" "$LOG" && ok "Xtream: 3 channels in 2 categories" || erro "Xtream channels"
grep -qE "\[ltv\] m3u .*2 canais na lista.*com url-tvg" "$LOG" && ok "M3U: 2 channels + url-tvg" || erro "M3U channels"
grep -q "\[stalker\] portal ok, 2 canal(is) em 2 categoria(s)" "$LOG" && ok "Stalker: handshake + 2 channels" || erro "Stalker channels"
grep -qE "\[guia\] 7 canais em 6 grupos, 3 fonte\(s\)" "$LOG" && ok "guide: 7 channels, 6 groups, 3 providers" || erro "guide totals"
grep -qE "tipo=xtream canais=3 grupos=2 grade=100%" "$LOG" && ok "EPG matched 100% on Xtream (by id)" || erro "Xtream EPG match"
grep -qE "tipo=m3u canais=2 grupos=2 grade=100%" "$LOG" && ok "EPG matched 100% on M3U (by tvg-id)" || erro "M3U EPG match"
grep -q "handshake" "$W/requests.log" && grep -q "get_ordered_list" "$W/requests.log" \
  && ok "Stalker protocol calls seen by the provider" || erro "Stalker calls"

# ---- 2. playback start, channel switching (CH+/CH-) across providers ---------
sleep 3
tecla ok 5
grep -q "\[canal\] tocar id=nvm3u:.*titulo=M3U Music Hits" "$LOG" && ok "OK plays the focused channel" || erro "channel start"
tecla chup 4; tecla chup 4; tecla chup 4; tecla chdown 4
seq=$(grep -o "\[canal\] tocar id=[^ ]*" "$LOG" | sed 's/.*id=//' | tr '\n' ' ')
case "$seq" in
  *"nvm3u:"*"nvm3u:"*"xtream:@00000001:101 xtream:@00000001:102 xtream:@00000001:101"*)
    ok "CH+ x3 then CH- zaps across M3U -> Xtream and back" ;;
  *) erro "zap order: $seq" ;;
esac
grep -qE "proxy failed, retrying direct|renovando|tentando de novo|reconect" "$LOG" \
  && ok "failed stream triggers recovery (retry path in the log)" || erro "no recovery attempt for a failed stream"

# ---- 3. provider switching (source bar) and Stalker playback ----------------
tecla back 3
tecla left 1; tecla right 0.6; tecla right 0.6; tecla right 0.6; tecla ok 3
tecla ok 8
grep -q "\[canal\] tocar id=stalker:@00000003:501" "$LOG" && ok "source bar -> Stalker, OK plays a Stalker channel" || erro "provider switch to Stalker"
grep -q "action=create_link" "$W/requests.log" && ok "Stalker create_link requested" || erro "Stalker create_link"
n=$(grep -c "action=create_link" "$W/requests.log")
[ "$n" -le 4 ] && ok "Stalker link renewal is capped ($n requests)" || erro "Stalker renewal not capped ($n)"

# ---- 4. favorite via the channel options (hold OK), persisted ----------------
tecla back 3
tecla ok:hold 2; tecla ok 3
[ -f "$W/a/guia-ltv-fav-p1.txt" ] && ok "favorite stored (guia-ltv-fav-p1.txt)" || erro "favorite file"
fechar
abrir "$W/a" "$W/a2.log"
guia || erro "guide (restart) did not load"
grep -qE "\[guia\] 7 canais .*\(1 favoritos\)" "$LOG" && ok "favorite survives a restart" || erro "favorite after restart"
fechar

# ---- 5. wrong Xtream password is a refusal, not "no answer" ------------------
abrir "$W/b" "$W/b.log"
guia; sleep 6
grep -q "\[xtream\] servidor recusou a credencial (auth 0)" "$LOG" && ok "wrong password detected as refused" || erro "wrong password"
grep -q "em vez da lista" "$LOG" && erro "wrong password reported as 'no answer'" || ok "no misleading 'no answer' for a wrong password"
fechar

# ---- 6. no credential in any app log ----------------------------------------
if grep -qE "qrtpass|wrongpass|QRTTOKEN" "$W"/a.log "$W"/a2.log "$W"/b.log; then
  erro "a credential appears in the app log"
else ok "no password or token in the app logs"; fi

[ "$fail" = 0 ] && echo "qrtime_livetv: ok"
exit "$fail"
