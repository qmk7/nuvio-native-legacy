#!/bin/bash
# YouTube section end to end on the real app (Xvfb) against the local Data API
# and OAuth fixtures (tests/qrtime_iptv_mock.py). With the remote only:
# open the section from the menu, no key -> the setup message (no fake list);
# enter the key and the OAuth client on the Account tab (pasted as from the
# phone); sign in with the device code; Trending -> video page -> "watch in the
# official app"; next page; Search; Subscriptions -> channel; Playlists ->
# playlist; network loss -> error; Back all the way Home. No secret in the log.
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-yt.XXXXXX")
PIDS=()
MOCK=""
fim() { for p in "${PIDS[@]}" $MOCK; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; : > /tmp/nuvio-key
        if [ "${QRT_LT_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
python3 tests/qrtime_iptv_mock.py "$PORT" "$W" >/dev/null 2>"$W/mock.err" &
MOCK=$!
sleep 1
d="$W/a"; mkdir -p "$d"
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
LOG="$W/app.log"
QRT_YT_BASE="http://127.0.0.1:$PORT/yt/v3" QRT_OAUTH_BASE="http://127.0.0.1:$PORT/goauth" NUVIO_DADOS="$d" DISPLAY=":$D" \
  "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$LOG" 2>&1 &
APP=$!; PIDS+=($APP)
espera() { local i; for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done; return 1; }
conta() { grep -cE "$1" "$LOG"; }
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.0}"; }
foto() {
  [ -n "${QRT_SHOTS:-}" ] || return 0
  mkdir -p "$QRT_SHOTS"; rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -f /tmp/nuvio-shot.bmp ] && break; sleep 0.5; done
  convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_SHOTS/youtube-$1.png" 2>/dev/null
}
lista() { espera "\[youtube\] lista tipo=$1 itens=$2 erro=$3" "${4:-20}"; }

espera "tela=home" 60 || { erro "home never drawn"; exit 1; }
sleep 4
for k in 1 2 3 4; do tecla left 0.8; done
for k in 1 2 3 4 5; do tecla down 0.6; done
tecla ok 2.5
espera "\[transicao\] tela 2 -> 17 " 10 && ok "menu YouTube opens its own section (screen 17)" || erro "YouTube section did not open"
lista 0 0 6 && ok "no API key: Trending says how to set it up, shows no list" || erro "no-key state"
foto sem-chave

# Account tab: key, client id, secret (pasted as from the phone), sign in.
for k in 1 2 3 4; do tecla right 0.6; done
tecla down 0.8
tecla ok 1.2; tecla "colar:QRTYTKEY" 1.2
grep -q "^chave=QRTYTKEY" "$d/youtube.txt" 2>/dev/null && ok "API key saved on this TV" || erro "API key not saved"
tecla down 0.6; tecla ok 1.2; tecla "colar:qrt-client.apps.googleusercontent.com" 1.2
tecla down 0.6; tecla ok 1.2; tecla "colar:qrt-secret" 1.2
grep -q "^cliente=qrt-client.apps.googleusercontent.com" "$d/youtube.txt" && grep -q "^segredo=qrt-secret" "$d/youtube.txt" \
  && ok "OAuth client id and secret saved" || erro "OAuth client not saved"
foto conta
tecla down 0.6; tecla ok 1.5
foto entrar
espera "\[youtube\] conta conectada neste perfil" 20 && ok "device-code sign-in completes (code shown, phone approves)" || erro "sign-in"
[ -f "$d/youtube-account-p1.txt" ] && ok "account tokens saved for this profile" || erro "account file"

# Trending -> video page -> official app.
for k in 1 2 3 4; do tecla up 0.5; done
for k in 1 2 3 4; do tecla left 0.6; done
lista 0 3 0 && ok "Trending: 3 videos from the API" || erro "Trending list"
tecla down 0.8; tecla ok 2
foto video
tecla ok 1.5
grep -q "\[youtube\] abrir video qrtVideo001 no app oficial" "$LOG" && ok "Watch hands the video id to the official YouTube app" || erro "watch handoff"
tecla back 1
tecla down 0.6; tecla down 1.5
lista 0 1 0 && ok "next page loads when the focus nears the end" || erro "pagination"
tecla back 1

# Search.
tecla right 0.6; tecla ok 1.2; tecla "colar:pasta" 1
lista 1 2 0 && ok "Search: 2 results" || erro "Search"
foto busca
tecla back 1

# Subscriptions -> channel; Playlists -> playlist.
tecla right 0.6
lista 2 1 0 && ok "Subscriptions (signed in): 1 channel" || erro "Subscriptions"
tecla down 0.8; tecla ok 2
lista 5 1 0 && ok "a subscription opens the channel's videos" || erro "channel page"
tecla back 1; tecla up 0.6
tecla right 0.6
lista 3 1 0 && ok "Playlists: 1 playlist" || erro "Playlists"
tecla down 0.8; tecla ok 2
lista 4 2 0 && ok "a playlist opens its videos (deleted entries skipped)" || erro "playlist page"
tecla back 1

# Network loss: the error is shown, nothing crashes.
kill "$MOCK"; MOCK=""
sleep 1
tecla ok 3
lista 4 0 1 30 && ok "network loss -> error state on the page" || erro "network error state"
foto erro
tecla back 1; tecla up 0.6; tecla back 2
espera "\[transicao\] tela 17 -> 2 " 10 && ok "Back from the section returns Home" || erro "Back to Home"
kill -0 "$APP" 2>/dev/null && ok "app alive" || erro "app died"
for s in QRTYTKEY QRTREFRESH QRTACCESS qrt-secret QRTDEVICE; do
  grep -q "$s" "$LOG" && erro "secret $s in the app log"
done
grep -q "QRTYTKEY\|QRTACCESS" "$W/requests.log" && erro "secret in the fixture's request log" || true
[ "$fail" = 0 ] && echo "qrtime_youtube_ui: ok"
exit "$fail"
