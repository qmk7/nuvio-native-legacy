#!/bin/bash
# QR TIME phase 8: TLS verification everywhere + owner-only credential files.
set -euo pipefail
cd "$(dirname "$0")/.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-tls.XXXXXX")
srv=""
trap '[ -z "$srv" ] || kill "$srv" 2>/dev/null || true; rm -rf "$tmp"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost -keyout "$tmp/key.pem" -out "$tmp/cert.pem" > "$tmp/gen.log" 2>&1
python3 tests/rede_pedido_server.py "$tmp/ports" "$tmp/cert.pem" "$tmp/key.pem" &
srv=$!
for i in $(seq 50); do [ -s "$tmp/ports" ] && break; sleep 0.1; done
tlsbase=$(sed -n '2p' "$tmp/ports")
mkdir -p "$tmp/dados"
cc -O1 -g -std=gnu11 -Wall -Wextra -Isrc -pthread -fsanitize=address,undefined \
  tests/qrtime_tls.c src/rede.c src/redeurl.c src/dados.c -ldl -o "$tmp/t" 2> "$tmp/cc.log" || { cat "$tmp/cc.log"; exit 1; }
NUVIO_DADOS="$tmp/dados" "$tmp/t" "$tlsbase" "$tmp/cert.pem" "$tmp/dados"
