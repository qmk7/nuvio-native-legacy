#!/bin/bash
# QR TIME main navigation, end to end, on the real app (docs/qrtime/UI-SPEC.md).
#
# Runs build/linux/nuvio-native-legacy on a virtual display (Xvfb) as a guest,
# with a LOCAL TEST ADD-ON (one movie and one series catalog served from
# 127.0.0.1; a test fixture, not a real provider). Remote keys go through the
# app's own injection file (/tmp/nuvio-key). For every visible destination of
# the main menu: Left, Down x N, OK -> the expected screen or action (from the
# app log) -> Back -> Home again.
#
#   bash tests/qrtime_nav.sh   (QRT_NAV_SHOTS=dir: a PNG per step; QRT_NAV_KEEP=1: keep log/data)
set -u
cd "$(dirname "$0")/.."
ROOT=$PWD
command -v Xvfb >/dev/null || { echo "SKIP: Xvfb not installed"; exit 0; }
command -v python3 >/dev/null || { echo "SKIP: python3 not installed"; exit 0; }
BIN=build/linux/nuvio-native-legacy
if [ ! -x "$BIN" ] || [ -n "$(find src -newer "$BIN" -name '*.[ch]' -print -quit)" ]; then
  bash tools/linux.sh --build >/dev/null || { echo "FAIL: host build"; exit 1; }
fi

W=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-nav.XXXXXX")
PIDS=()
fim() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; sleep 0.5
        for p in "${PIDS[@]}"; do kill -9 "$p" 2>/dev/null; done; rm -f /tmp/nuvio-key
        if [ "${QRT_NAV_KEEP:-0}" = 1 ]; then echo "kept: $W"; else rm -rf "$W"; fi; }
trap fim EXIT

# --- test add-on (Stremio protocol: manifest + catalogs) ---------------------
mkdir -p "$W/addon/catalog/movie" "$W/addon/catalog/series" "$W/data"
cat > "$W/addon/manifest.json" <<'EOF'
{"id":"org.qrtime.navtest","version":"1.0.0","name":"QRT Nav Test","resources":["catalog"],
 "types":["movie","series"],"idPrefixes":["tt"],
 "catalogs":[{"type":"movie","id":"qrtnav-movies","name":"Nav Movies"},
             {"type":"series","id":"qrtnav-series","name":"Nav Series"}]}
EOF
cat > "$W/addon/catalog/movie/qrtnav-movies.json" <<'EOF'
{"metas":[{"id":"tt0111161","type":"movie","name":"The Shawshank Redemption","releaseInfo":"1994","poster":"POSTER"},
          {"id":"tt0068646","type":"movie","name":"The Godfather","releaseInfo":"1972","poster":"POSTER"}]}
EOF
cat > "$W/addon/catalog/series/qrtnav-series.json" <<'EOF'
{"metas":[{"id":"tt0903747","type":"series","name":"Breaking Bad","releaseInfo":"2008-2013","poster":"POSTER"}]}
EOF
PORT=$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
# The app drops catalog items without a poster (descoberta.c, deMeta).
sed -i "s|POSTER|http://127.0.0.1:$PORT/p.jpg|g" "$W"/addon/catalog/*/*.json
command -v convert >/dev/null && convert -size 200x300 xc:'#3a4a6a' "$W/addon/p.jpg" 2>/dev/null
python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$W/addon" >/dev/null 2>&1 &
PIDS+=($!)

# --- package folder = deploy/app, with art/ = package art + test add-on list -
# (fonts and the rest are found next to art/, so the whole folder is mirrored)
mkdir -p "$W/app/art"
for e in deploy/app/*; do [ "$(basename "$e")" = art ] || ln -s "$ROOT/$e" "$W/app/$(basename "$e")"; done
for e in deploy/app/art/*; do ln -s "$ROOT/$e" "$W/app/art/$(basename "$e")"; done
rm -f "$W/app/art/addons.txt"
printf 'QRT Nav Test\thttp://127.0.0.1:%s\n' "$PORT" > "$W/app/art/addons.txt"
printf '1\n' > "$W/data/convidado.txt"            # guest, whatever the build config
# First-run "What's new" cards already seen (the files the app writes on dismiss).
for n in 11 12 13 131 132 133 134 139 14-celebracao 142 150 152-ui 160-ui 170-ui \
         172-ui 174-ui 180-ui 20-guia 201 202 208 guia; do
  printf '1\n' > "$W/data/novidades-$n.txt"
done
printf '2.0.8\n' > "$W/data/dicas-controle-205.txt"   # remote-tips card seen
printf '1\n' > "$W/data/salvos-intro.txt"            # "Saving with +" seen
printf '1\n' > "$W/data/guia-dica.txt"               # guide tip seen
printf '1\n' > "$W/data/aviso-log.txt"               # "log opens with the red button" seen
printf 'versao=1\n' > "$W/data/diagnostico-otimizacao-intro.cfg"  # optimisation intro seen

D=$((90 + RANDOM % 9))
Xvfb ":$D" -screen 0 1920x1080x24 >/dev/null 2>&1 &
PIDS+=($!)
sleep 1
LOG=$W/app.log
NUVIO_DADOS=$W/data DISPLAY=":$D" "$ROOT/$BIN" "$W/app/art" >"$LOG" 2>&1 &
APP=$!
PIDS+=($APP)

fail=0
espera() {  # espera <regex> [segundos]
  local i
  for i in $(seq 1 "${2:-60}"); do grep -qE "$1" "$LOG" && return 0; sleep 1; done
  return 1
}
tecla() { echo "$1" > /tmp/nuvio-key; sleep "${2:-1.2}"; }
conta() { grep -cE "$1" "$LOG"; }
foto() {
  [ -n "${QRT_NAV_SHOTS:-}" ] || return 0
  mkdir -p "$QRT_NAV_SHOTS"; rm -f /tmp/nuvio-shot.bmp; echo 1 > /tmp/nuvio-shot-req
  local i; for i in $(seq 1 30); do [ -f /tmp/nuvio-shot.bmp ] && break; sleep 0.5; done
  convert /tmp/nuvio-shot.bmp -resize 50% "$QRT_NAV_SHOTS/nav-$1.png" 2>/dev/null
}
ok()   { echo "PASS: $1"; }
erro() { echo "FAIL: $1"; fail=1; }

espera "guest mode" 60 || { erro "app did not start as guest"; tail -20 "$LOG"; exit 1; }
espera "tela=home" 60 || { erro "home never drawn"; exit 1; }
# Catalogs of the test add-on reach the home (needed by Movies/Series).
espera "Nav Movies|qrtnav-movies" 60 || echo "note: catalog name not seen in log yet"
sleep 6

# Visible order for a guest with default settings (Music hidden until phase 7):
# 0 Home 1 Live TV 2 Movies 3 Series 4 Sport 5 YouTube 6 Agenda 7 Search
# 8 Add-ons 9 Library 10 Explore 11 Profile 12 Settings
vai() {   # vai <index>: open the menu, move down <index> rows, OK
  local k
  # On the hero, Left steps the carousel back until its first slide and only
  # then opens the menu (home.c); inside the menu Left does nothing. Four
  # presses reach the menu from any slide, as a person would.
  for k in 1 2 3 4; do tecla left 0.8; done
  sleep 0.8
  for k in $(seq 1 "$1"); do tecla down 0.6; done
  tecla ok 2.5
}
volta_home() {  # volta_home <screen>: Back (at most 3 presses) until "<screen> -> home"
  local k n=$(conta "\[transicao\] tela $1 -> 2 ")
  for k in 1 2 3; do
    tecla back 1.5
    [ "$(conta "\[transicao\] tela $1 -> 2 ")" -gt "$n" ] && return 0
  done
  return 1
}
fecha_grade() {  # fecha_grade <id>: Back (at most 3) until the grid closes
  local k n=$(conta "\[nav\] $1 grid closed")
  for k in 1 2 3; do
    tecla back 1.5
    [ "$(conta "\[nav\] $1 grid closed")" -gt "$n" ] && return 0
  done
  return 1
}
transicao() { conta "\[transicao\] tela 2 -> $1 "; }

# 1 Live TV -> TELA_GUIA (4)
a=$(transicao 4); vai 1; foto livetv
[ "$(transicao 4)" -gt "$a" ] && ok "Live TV opens the TV guide" || erro "Live TV"
volta_home 4 && ok "Back from Live TV returns Home" || erro "Back from Live TV"

# 2 Movies -> grid with the add-on's movie catalog
vai 2; foto movies
grep -qE "\[nav\] qrtime_movies: [1-9]" "$LOG" && ok "Movies opens a grid of movie catalogs" || erro "Movies grid ($(grep -m1 '\[nav\] qrtime_movies' "$LOG"))"
fecha_grade qrtime_movies && ok "Back from Movies returns Home" || erro "Back from Movies"

# 3 Series
vai 3; foto series
grep -qE "\[nav\] qrtime_series: [1-9]" "$LOG" && ok "Series opens a grid of series catalogs" || erro "Series grid ($(grep -m1 '\[nav\] qrtime_series' "$LOG"))"
fecha_grade qrtime_series && ok "Back from Series returns Home" || erro "Back from Series"

# 4 Sport Center -> its own screen (TELA_ESPORTES = 16, esporteui.c)
a=$(transicao 16); vai 4; foto sport
[ "$(transicao 16)" -gt "$a" ] && ok "Sport Center opens its screen" || erro "Sport Center"
volta_home 16 && ok "Back from Sport Center returns Home" || erro "Back from Sport Center"

# 5 YouTube -> launcher (desktop: information only), stays on Home
a=$(conta "\[transicao\] tela 2 -> ")
vai 5; foto youtube
grep -q "\[ondever\] youtube:" "$LOG" && ok "YouTube asks the TV app launcher" || erro "YouTube launcher"
[ "$(conta "\[transicao\] tela 2 -> ")" = "$a" ] && ok "YouTube is an action, the screen stays Home" || erro "YouTube changed screen"

# 6 Agenda (13), 7 Search (5), 9 Library (6), 10 Explore (3), 11 Profile (7), 12 Settings (8)
for par in "6 13 Agenda" "7 5 Search" "9 6 Library" "10 3 Explore" "11 7 Profile" "12 8 Settings"; do
  set -- $par
  a=$(transicao "$2"); vai "$1"; foto "$(echo "$3" | tr 'A-Z' 'a-z')"
  [ "$(transicao "$2")" -gt "$a" ] && ok "$3 opens its screen" || erro "$3 (expected screen $2)"
  volta_home "$2" && ok "Back from $3 returns Home" || erro "Back from $3"
done

# 8 Add-ons (12): Back goes Home (not to Settings) when opened from the menu
a=$(transicao 12); vai 8; foto addons
[ "$(transicao 12)" -gt "$a" ] && ok "Add-ons opens its screen" || erro "Add-ons"
b=$(conta "\[transicao\] tela 12 -> 2 ")
tecla back 2
[ "$(conta "\[transicao\] tela 12 -> 2 ")" -gt "$b" ] && ok "Back from Add-ons (menu) returns Home" || erro "Back from Add-ons went elsewhere"

kill -0 "$APP" 2>/dev/null && ok "app alive after the whole tour" || erro "app died"
[ "$fail" = 0 ] && echo "qrtime_nav: ok"
exit "$fail"
