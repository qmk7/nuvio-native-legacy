# dr_libs (vendored)

- Source: https://github.com/mackron/dr_libs
- Commit: `0901cdc54e719e2175827cb60b78fe725e581fe2`
- Files: `dr_flac.h` (v0.13.5 dev), `dr_wav.h` (v0.14.7 dev), `dr_mp3.h` (v0.7.5 dev)
- License: public domain (Unlicense) or MIT No Attribution, at the user's choice
  (see the end of each header). No attribution or notice requirement.

Used by `src/musdec.c` ONLY in the desktop (Linux) build and the tests: FLAC,
WAV and MP3 decoding to verify sources and to play music on the desktop. The
LG webOS build decodes through the TV's own media pipeline (uMS) and does not
compile these.
