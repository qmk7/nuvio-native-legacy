#!/bin/bash
# Music Center fixtures: real audio files made by ffmpeg (a TEST tool here,
# not part of the app). Writes into $1. Exit 0 when every file exists.
set -u
W="$1"
command -v ffmpeg >/dev/null || exit 3
F="ffmpeg -nostdin -loglevel error -y"
TONE="-f lavfi -i sine=frequency=440:duration=5"
TONE2="-f lavfi -i sine=frequency=660:duration=5"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -sample_fmt s16 -metadata title="Test Tone" \
   -metadata artist="QRT Artist" -metadata album="QRT Album" -metadata track=3 -metadata date=2024 -metadata genre=Jazz "$W/a16.flac"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 96000 -sample_fmt s32 -bits_per_raw_sample 24 "$W/a24.flac"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 96000 -sample_fmt s32 -bits_per_raw_sample 24 -metadata title="With Cover" "$W/a24art.flac"
# A 200 KiB PICTURE block right after STREAMINFO, BEFORE the Vorbis comments:
# the tags are then beyond the first 64 KiB read.
python3 -I - "$W/a24art.flac" <<'PY' || { echo "FAIL: picture block"; exit 1; }
import os, struct, sys
p = sys.argv[1]; b = open(p, "rb").read()
assert b[:4] == b"fLaC" and b[4] & 0x7f == 0
si_len = int.from_bytes(b[5:8], "big"); si_end = 8 + si_len
img = os.urandom(200000)
mime = b"image/jpeg"
pic = struct.pack(">II", 3, len(mime)) + mime + struct.pack(">I", 0) + struct.pack(">IIIII", 1500, 1500, 24, 0, len(img)) + img
blk = bytes([6]) + len(pic).to_bytes(3, "big") + pic
si = bytes([b[4] & 0x7f]) + b[5:si_end]   # STREAMINFO no longer last
open(p, "wb").write(b[:4] + si + blk + b[si_end:])
PY
$F -f lavfi -i "sine=frequency=300:duration=3" -af "pan=5.1|c0=c0|c1=c0|c2=c0|c3=c0|c4=c0|c5=c0" -ar 48000 -sample_fmt s16 "$W/a51.flac"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 48000 -c:a pcm_s24le "$W/a24.wav"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -c:a pcm_s16be "$W/a16.aiff"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -c:a libmp3lame -b:a 320k -write_xing 0 -id3v2_version 3 \
   -metadata title="MP3 Title" -metadata artist="MP3 Artist" -metadata track=7 "$W/a320.mp3" 2>/dev/null || \
   $F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -c:a mp3 -b:a 320k -write_xing 0 -id3v2_version 3 \
   -metadata title="MP3 Title" -metadata artist="MP3 Artist" -metadata track=7 "$W/a320.mp3"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -c:a aac -b:a 256k -movflags +faststart -metadata title="AAC Title" "$W/aac.m4a"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 44100 -c:a aac -b:a 256k "$W/aac_tail.m4a"
$F $TONE $TONE2 -filter_complex amerge=inputs=2 -ar 96000 -sample_fmt s32p -c:a alac -movflags +faststart "$W/alac.m4a"
for f in a16.flac a24.flac a24art.flac a51.flac a24.wav a16.aiff a320.mp3 aac.m4a aac_tail.m4a alac.m4a; do
  [ -s "$W/$f" ] || { echo "fixture missing: $f" >&2; exit 1; }
done
