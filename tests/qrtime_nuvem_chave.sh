#!/bin/bash
# QR TIME: Supabase publishable vs legacy anon key headers (src/nuvem.c).
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-nuvem.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-format-truncation -Isrc -fsanitize=address,undefined tests/qrtime_nuvem_chave.c src/nuvem.c -o "$tmp/t"
"$tmp/t" "$tmp"
