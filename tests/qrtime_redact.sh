#!/bin/bash
# QR TIME phase 8: log redaction of credentials in URLs and payloads.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-redact.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
cc -O1 -g -std=gnu11 -Wall -Wextra -Werror -Isrc -fsanitize=address,undefined tests/qrtime_redact.c src/redeurl.c -o "$tmp/t"
"$tmp/t"
