#!/bin/bash
# Reproducible QR TIME webOS .ipk build, with or without Docker.
#
#   bash tools/qrtime/build-ipk.sh            # local SDK (no container)
#   QRT_BUILD_MODE=docker bash tools/qrtime/build-ipk.sh
#
# Output: build/ipk/com.qrtime.tv_<version>_arm.ipk
#
# Local mode downloads the openlgtv webOS SDK for this host into
# $QRT_SDK_DIR (default ~/qrtime-sdk), checks its SHA-256, installs the pinned
# @webos-tools/cli into build/webos-cli and runs tools/arm.sh --ipk --build with
# NUVIO_CTR=local. It never contacts a TV.
#
# Optional features need sources or prebuilt parts the local mode does not
# fetch; they default to OFF here and can be turned on when available:
#   NUVIO_ASS_LIBASS=1  (needs NUVIO_ASS_ROOT with a static ARM libass)
#   NUVIO_DTS_FFMPEG=1  (needs NUVIO_DTS_ROOT with a static ARM FFmpeg)
#   NUVIO_RUST_MKVCORE=1, NUVIO_DV7=1  (need cargo + armv7 target)
#   NUVIO_P2P_MOTOR=<dir>  (prebuilt P2P engine)
# Build-time keys come from local.properties (see local.properties.example).
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD

SDK_TAG="webos-a38c582"
ARES_VERSION="3.2.6"
case "$(uname -m)" in
  x86_64|amd64)  SDK_HOST=x86_64
                 SDK_SHA256=04ad3311b48b4557a7002aef56ae2e167478e8e129f37daac04649bddf813616 ;;
  aarch64|arm64) SDK_HOST=aarch64; SDK_SHA256="${QRT_SDK_SHA256:-}" ;;
  *) echo "build-ipk.sh: unsupported host $(uname -m)" >&2; exit 2 ;;
esac
SDK_URL="https://github.com/openlgtv/buildroot-nc4/releases/download/$SDK_TAG/arm-webos-linux-gnueabi_sdk-buildroot-$SDK_HOST.tar.gz"
SDK_DIR="${QRT_SDK_DIR:-$HOME/qrtime-sdk}"
SDK_ROOT="$SDK_DIR/arm-webos-linux-gnueabi_sdk-buildroot"

export NUVIO_ASS_LIBASS="${NUVIO_ASS_LIBASS:-0}"
export NUVIO_DTS_FFMPEG="${NUVIO_DTS_FFMPEG:-0}"
export NUVIO_RUST_MKVCORE="${NUVIO_RUST_MKVCORE:-0}"
export NUVIO_DV7="${NUVIO_DV7:-0}"
export NUVIO_P2P_MOTOR="${NUVIO_P2P_MOTOR:-none}"
export NUVIO_BUILD_JOBS="${NUVIO_BUILD_JOBS:-$(nproc 2>/dev/null || echo 2)}"
export NUVIO_RELEASE_OUT="${NUVIO_RELEASE_OUT:-$ROOT/build/ipk}"

# ares-package, pinned.
ARES="$ROOT/build/webos-cli/node_modules/.bin/ares-package"
if [ ! -x "$ARES" ] || ! grep -q "\"version\": \"$ARES_VERSION\"" \
     "$ROOT/build/webos-cli/node_modules/@webos-tools/cli/package.json" 2>/dev/null; then
  echo "==> installing @webos-tools/cli@$ARES_VERSION into build/webos-cli"
  npm install --prefix "$ROOT/build/webos-cli" "@webos-tools/cli@$ARES_VERSION" --no-audit --no-fund >/dev/null
fi
export NUVIO_ARES_PACKAGE="$ARES"

if [ "${QRT_BUILD_MODE:-local}" = docker ]; then
  export NUVIO_CTR=docker NUVIO_SDK_IMAGE="${QRT_SDK_IMAGE:-qrtime-webos-sdk}"
  case "$(uname -m)" in x86_64|amd64) export NUVIO_BUILD_PLATFORM=linux/amd64;; esac
  docker image inspect "$NUVIO_SDK_IMAGE" >/dev/null 2>&1 || bash tools/qrtime/sdk-image.sh
else
  if [ ! -x "$SDK_ROOT/bin/arm-webos-linux-gnueabi-gcc" ]; then
    mkdir -p "$SDK_DIR"
    echo "==> downloading webOS SDK ($SDK_TAG, $SDK_HOST host)"
    curl -fsSL -o "$SDK_DIR/sdk.tar.gz" "$SDK_URL"
    if [ -n "$SDK_SHA256" ]; then
      echo "$SDK_SHA256  $SDK_DIR/sdk.tar.gz" | sha256sum -c - >/dev/null \
        || { echo "build-ipk.sh: SDK checksum mismatch" >&2; exit 3; }
    else
      echo "    warning: no pinned SHA-256 for the $SDK_HOST SDK; got $(sha256sum "$SDK_DIR/sdk.tar.gz" | cut -d' ' -f1)"
    fi
    tar -xzf "$SDK_DIR/sdk.tar.gz" -C "$SDK_DIR"
    "$SDK_ROOT/relocate-sdk.sh" >/dev/null
    rm -f "$SDK_DIR/sdk.tar.gz"
  fi
  export NUVIO_CTR=local QRT_WEBOS_SDK="$SDK_ROOT"
fi

bash tools/arm.sh --ipk --build
ls -la "$NUVIO_RELEASE_OUT"/*.ipk
