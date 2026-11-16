#!/bin/bash
# Builds the webOS cross-compilation image (tools/Dockerfile) reproducibly on
# x86_64 or arm64 hosts.
#
#   bash tools/qrtime/sdk-image.sh                 # image: qrtime-webos-sdk
#   QRT_SDK_IMAGE=name bash tools/qrtime/sdk-image.sh
#
# Host architecture picks the matching openlgtv SDK tarball (same release as
# tools/Dockerfile), so no qemu emulation is needed.
#
# Behind an HTTPS-intercepting proxy, set QRT_EXTRA_CA=/path/to/ca.pem (and
# HTTPS_PROXY). The script then copies the CA into a temporary build context,
# switches Debian sources to HTTPS (an HTTPS-only proxy cannot serve apt's
# default http:// mirrors) and builds with --network host. tools/Dockerfile
# itself is used unchanged after that prelude.
set -euo pipefail
cd "$(dirname "$0")/../.."

IMAGE="${QRT_SDK_IMAGE:-qrtime-webos-sdk}"
SDK_TAG="webos-a38c582"
case "$(uname -m)" in
  x86_64|amd64)  PLATFORM=linux/amd64; SDK_HOST=x86_64 ;;
  aarch64|arm64) PLATFORM=linux/arm64; SDK_HOST=aarch64 ;;
  *) echo "sdk-image.sh: unsupported host $(uname -m)" >&2; exit 2 ;;
esac
SDK_URL="https://github.com/openlgtv/buildroot-nc4/releases/download/$SDK_TAG/arm-webos-linux-gnueabi_sdk-buildroot-$SDK_HOST.tar.gz"

CTX=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-sdk-ctx.XXXXXX")
trap 'rm -rf "$CTX"' EXIT
cp tools/Dockerfile tools/build-ass-arm.sh tools/build-dts-ffmpeg.sh "$CTX/"

ARGS=(--platform "$PLATFORM" --build-arg "SDK_URL=$SDK_URL"
      --build-arg "NUVIO_DV7=${NUVIO_DV7:-0}"
      --build-arg "NUVIO_DTS_FFMPEG=${NUVIO_DTS_FFMPEG:-1}")

if [ -n "${QRT_EXTRA_CA:-}" ]; then
  cp "$QRT_EXTRA_CA" "$CTX/extra-ca.crt"
  # Prelude: trust the proxy CA before the first apt/curl, then the original
  # Dockerfile from its first RUN on.
  {
    sed -n '1,/^FROM /p' tools/Dockerfile
    cat <<'EOF'
COPY extra-ca.crt /usr/local/share/ca-certificates/extra-ca.crt
RUN sed -i 's|http://deb.debian.org|https://deb.debian.org|g' /etc/apt/sources.list.d/debian.sources \
  && echo 'Acquire::https::CAInfo "/usr/local/share/ca-certificates/extra-ca.crt";' > /etc/apt/apt.conf.d/99extra-ca
ENV SSL_CERT_FILE=/usr/local/share/ca-certificates/extra-ca.crt \
    CURL_CA_BUNDLE=/usr/local/share/ca-certificates/extra-ca.crt \
    GIT_SSL_CAINFO=/usr/local/share/ca-certificates/extra-ca.crt
EOF
    sed -n '/^FROM /,$p' tools/Dockerfile | sed '1d'
  } > "$CTX/Dockerfile"
  ARGS+=(--network host)
  [ -n "${HTTPS_PROXY:-}" ] && ARGS+=(--build-arg "HTTPS_PROXY=$HTTPS_PROXY" --build-arg "https_proxy=$HTTPS_PROXY")
fi

echo "==> building $IMAGE for $PLATFORM (SDK $SDK_TAG, $SDK_HOST host)"
docker build "${ARGS[@]}" -t "$IMAGE" "$CTX"
