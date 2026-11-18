#!/bin/bash
# QR TIME intro (brand video at start-up) in the real app (Xvfb): it plays
# frame by frame for ~3 s over the home that builds underneath, a key skips
# it, the old brand's what's-new cards are marked seen on a fresh install, and
# a desktop build without QRT_INTRO=1 shows none (other tests stay as they were).
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi
W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-intro.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5; for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done
        : > /tmp/nuvio-key; [ "${QRT_LT_KEEP:-0}" = 1 ] && echo "kept: $W" || rm -rf "$W"; }
trap fim EXIT
fail=0
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }
D=$((80 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
abrir() { local d="$W/$2"; mkdir -p "$d"; printf '1\n' > "$d/convidado.txt"
          env $3 NUVIO_DADOS="$d" DISPLAY=":$D" "$ROOT/$BIN" "$ROOT/deploy/app/art" >"$1" 2>&1 & APP=$!; LOG=$1; PIDS+=($APP); }
fechar() { kill "$APP" 2>/dev/null; sleep 1; kill -9 "$APP" 2>/dev/null; }
espera() { local i; for i in $(seq 1 "$((${2:-30} * 5))"); do grep -qE "$1" "$LOG" && return 0; sleep 0.2; done; return 1; }
pixel() {   # mean RGB of the screen centre band (BGRA, alpha ignored)
  rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -s /tmp/nuvio-shot.bmp ] && break; sleep 0.1; done
  [ -n "${QRT_SHOTS:-}" ] && { mkdir -p "$QRT_SHOTS"; convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_SHOTS/intro-$1.png" 2>/dev/null; }
  convert /tmp/nuvio-shot.bmp -alpha off -crop 1920x200+0+0 -resize 1x1! -format "%[fx:int(255*r)] %[fx:int(255*g)] %[fx:int(255*b)]" info: 2>/dev/null
}

# 1. Plays, then hands over to the home.
abrir "$W/1.log" a "QRT_INTRO=1"
espera "\[intro\] playing" 20 && ok "the intro starts" || erro "intro did not start"
grep -q "\[intro\] sound: none on this build" "$LOG" && ok "desktop: picture only (sound goes through the TV's pipeline on webOS)" || erro "sound line"
sleep 1.0
read -r r g b <<< "$(pixel playing)"
[ -n "${b:-}" ] && [ "$b" -gt "$r" ] && [ "$b" -gt 20 ] && [ "$r" -lt 40 ] && ok "mid-intro the screen top is the intro's navy ($r,$g,$b)" || erro "intro not on screen ($r,$g,$b)"
espera "\[intro\] end after [0-9]+ ms" 10 && ok "the intro ends by itself" || erro "no end"
ms=$(grep -oE "\[intro\] end after [0-9]+" "$LOG" | grep -oE "[0-9]+$")
fr=$(grep -oE "last frame [0-9]+" "$LOG" | grep -oE "[0-9]+$")
[ -n "$ms" ] && [ "$ms" -ge 2900 ] && [ "$ms" -le 5200 ] && ok "it lasts ${ms} ms (3 s video + hand-over)" || erro "duration ${ms:-?}"
[ -n "$fr" ] && [ "$fr" -ge 85 ] && ok "it reached the last frames (frame $fr of 89)" || erro "frames ${fr:-?}"
grep -q "\[abertura\] fim" "$LOG" && erro "the old opening also ran" || ok "the old opening does not run"
[ -f "$W/a/novidades-201.txt" ] && [ -f "$W/a/novidades-180-ui.txt" ] && ok "fresh install: the old brand's what's-new cards are marked seen" || erro "what's-new marks"
espera "tela=home" 20 && ok "the home is there after it" || erro "home"
fechar

# 2. A key skips it.
abrir "$W/2.log" b "QRT_INTRO=1"
espera "\[intro\] playing" 20
sleep 0.5; echo ok > /tmp/nuvio-key
espera "\[intro\] skipped by a key" 5 && ok "a key skips it" || erro "skip"
espera "\[intro\] end after" 3 && ok "and it ends right away (fade only)" || erro "skip end"
fechar

# 3. Desktop default: none (the other tests see no intro).
abrir "$W/3.log" c ""
espera "tela=home" 40
sleep 2
grep -q "\[intro\]" "$LOG" && erro "intro ran without QRT_INTRO=1" || ok "desktop without QRT_INTRO=1: no intro"
fechar
exit $fail
