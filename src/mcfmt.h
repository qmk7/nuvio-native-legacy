// MUSIC CENTER: what a music FILE is, from its first bytes. Container and
// codec, sample rate, bit depth, channels and duration as the file itself
// declares them (FLAC STREAMINFO, WAV fmt, MPEG frame header, MP4 sample
// entry / ALAC cookie), plus its tags (Vorbis comments, ID3v2, iTunes ilst).
// Nothing is guessed: a field the header does not carry stays 0 / "".
#ifndef NV_MCFMT_H
#define NV_MCFMT_H
#include "mcmodelo.h"
#include <stddef.h>

enum { MCF_DESCONHECIDO = 0, MCF_FLAC, MCF_WAV, MCF_MP3, MCF_M4A, MCF_OGG, MCF_AIFF };

typedef struct {
  int  tipo;              // MCF_*
  char codec[16];         // "FLAC", "PCM", "MP3", "AAC", "ALAC", "Vorbis", "Opus"
  int  taxa, bits, canais;
  long long amostras;     // total samples per channel (0 unknown)
  int  kbps;              // MP3/AAC nominal bitrate when the header says
  int  completo;          // 1 = the stream info was found (not only the magic)
} McFmt;

// Parse `b[0..n)` (the file's start; `total` = file size or 0). Fills `fmt`
// and the tag fields of `f` (titulo, artista, album, faixa, disco, ano,
// genero; dur / taxa / bits / canais / formato from the stream info).
// Returns the McFmt tipo. *precisa > n when the header continues beyond
// what was given (a big ID3 tag, a cover-art block): read that much and call
// again.
int mcfmt_ler(const unsigned char *b, size_t n, long long total, McFmt *fmt, McFaixa *f, size_t *precisa);
// "FLAC 24-bit / 96 kHz · 2.0", "MP3 320 kbps · 44.1 kHz", "" when unknown.
void mcfmt_descrever(const McFaixa *f, char *d, size_t n);
#endif
