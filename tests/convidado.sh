#!/bin/bash
# Guest mode: start-up policy and persistence (src/convidado.c).
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-convidado.XXXXXX")
trap 'rm -rf "$work"' EXIT
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Wno-format-truncation)
if [ "${SANITIZE:-0}" = 1 ]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
cc "${flags[@]}" tests/convidado.c src/convidado.c src/dados.c -pthread -o "$work/test"
mkdir "$work/data"
"$work/test" "$work/data"
