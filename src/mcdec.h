// MUSIC CENTER: software decoding (dr_libs: FLAC, WAV, MP3) for the DESKTOP
// build and the tests. The LG webOS build plays through the TV's own media
// pipeline (mcsaida_ums.c) and compiles this as a stub (mcdec_disponivel 0).
//
// Samples come out as signed 32-bit, full scale: a 16-bit source is shifted,
// never dithered or resampled; `bits` keeps what the source really is.
#ifndef NV_MCDEC_H
#define NV_MCDEC_H
#include <stddef.h>
#include <stdint.h>

typedef struct McDec McDec;
typedef struct { char codec[12]; int taxa, bits, canais; long long frames; } McDecInfo;

int    mcdec_disponivel(void);
// From a whole file in memory (copied: the caller may free its buffer).
McDec *mcdec_abrir(const void *dados, size_t n, McDecInfo *info);
// Up to `frames` frames into `out` (frames * canais int32). Returns frames read, 0 at the end.
long   mcdec_ler(McDec *d, int32_t *out, long frames);
int    mcdec_buscar(McDec *d, long long frame);
void   mcdec_fechar(McDec *d);
// FLAC only: 1 when the decoded PCM matches the MD5 in STREAMINFO (bit-exact
// decode), 0 mismatch, -1 the file carries no MD5 / not FLAC.
int    mcdec_flac_md5_confere(const void *dados, size_t n);
#endif
