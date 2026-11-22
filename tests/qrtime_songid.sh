#!/bin/bash
# QR TIME song sample extraction (songid.c) on MKV files made here by ffmpeg.
set -u
cd "$(dirname "$0")/.."
command -v ffmpeg >/dev/null && command -v ffprobe >/dev/null || { echo "SKIP: needs ffmpeg/ffprobe"; exit 0; }
d=$(mktemp -d "${TMPDIR:-/tmp}/qrtime-songid.XXXXXX"); trap 'rm -rf "$d"' EXIT
v="-f lavfi -i testsrc=size=320x240:rate=24:duration=30"
ffmpeg -loglevel error -y $v -f lavfi -i sine=frequency=440:duration=30 -f lavfi -i sine=frequency=880:duration=30 \
  -map 0 -map 1 -map 2 -c:v libx264 -preset ultrafast -c:a:0 ac3 -c:a:1 aac -shortest "$d/a.mkv" &&
ffmpeg -loglevel error -y $v -f lavfi -i sine=frequency=440:duration=30 -c:v libx264 -preset ultrafast -c:a aac -shortest "$d/b.mkv" &&
ffmpeg -loglevel error -y $v -f lavfi -i sine=frequency=440:duration=30 -c:v libx264 -preset ultrafast -c:a eac3 -shortest "$d/c.mkv" &&
ffmpeg -loglevel error -y $v -f lavfi -i sine=frequency=440:duration=30 -c:v libx264 -preset ultrafast -c:a aac -shortest "$d/d.mp4" || { echo "FAIL: fixtures"; exit 1; }
cc src/songid.c tests/qrtime_songid.c -Isrc src/js.c -o "$d/t" -pthread || { echo "FAIL: test build"; exit 1; }
"$d/t" "$d"; r=$?
[ "$r" = 0 ] && echo "qrtime_songid: ok"
exit "$r"
