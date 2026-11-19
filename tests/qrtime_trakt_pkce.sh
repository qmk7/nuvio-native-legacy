#!/bin/bash
# QR TIME: Trakt linking without a client secret (PKCE).
set -u
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-pkce.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wno-format-truncation -Isrc -fsanitize=address,undefined \
  tests/qrtime_trakt_pkce.c src/traktauth.c src/credfio.c src/js.c src/jsw.c src/admin.c -lpthread -o "$tmp/t" 2> "$tmp/cc.log" \
  || { tail -20 "$tmp/cc.log"; echo "FAIL: build"; exit 1; }
"$tmp/t" > "$tmp/out" 2>&1; r=$?
grep -E "^(PASS|FAIL)" "$tmp/out"
read -r _ v ch < <(grep "^VECTOR" "$tmp/out")
esperado=$(python3 -c "import hashlib,base64,sys; print(base64.urlsafe_b64encode(hashlib.sha256(sys.argv[1].encode()).digest()).rstrip(b'=').decode())" "$v")
if [ "$esperado" = "$ch" ]; then echo "PASS: challenge = base64url(SHA-256(verifier)) (checked independently)"; else echo "FAIL: challenge mismatch"; r=1; fi
exit $r
