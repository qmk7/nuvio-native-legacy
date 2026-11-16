#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/nuvio-epg-accounts.XXXXXX")
trap 'rm -rf "$work"' EXIT
flags=(-std=c11 -D_POSIX_C_SOURCE=200809L -O1 -g -Wall -Wextra -Werror -Isrc)
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
# Exercise the real player resolver without linking its unrelated UI paths.
python3 - "$work/player_epg.inc" <<'PY'
from pathlib import Path
import sys
source = Path('src/player.c').read_text()
start = source.index('static void pEpgAtualizar(void) {')
end = source.index('\n}', start) + 2
Path(sys.argv[1]).write_text(source[start:end])
PY
flags+=(-ffunction-sections -fdata-sections)
# GCC has warnings clang (the macOS compiler this test was written for) lacks;
# -Waddress/-Wformat-truncation flag intentional snprintf truncation and the
# array guard in guia.c's ADD macro. Keep -Werror for everything else.
if [ "$(uname -s)" = Darwin ]; then flags+=(-Wl,-dead_strip); else flags+=(-Wl,--gc-sections -Wno-format-truncation -Wno-address); fi
cc "${flags[@]}" -Wno-unused-function -Wno-sign-compare -Wno-macro-redefined -I"$work" \
  -I/opt/homebrew/include -I/opt/homebrew/include/SDL2 \
  tests/epg_shared_consumers.c tests/epg_accounts.c src/xmltv.c -lz -lpthread -lm -o "$work/test"
"$work/test" "$work/data"
