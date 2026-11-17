#!/bin/bash
# XMLTV time zones: see tests/qrtime_xmltv_tz.c.
set -euo pipefail
cd "$(dirname "$0")/.."
out=$(mktemp "${TMPDIR:-/tmp}/qrtime-xmltv-tz.XXXXXX")
trap 'rm -f "$out"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc tests/qrtime_xmltv_tz.c src/xmltv.c -lz -o "$out"
"$out"
