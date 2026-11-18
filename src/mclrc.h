// MUSIC CENTER: lyrics. Adapters in order: the track's own server (Subsonic /
// OpenSubsonic, in mccat.c), then LRCLIB (lrclib.net: public, free, no key;
// only artist, title, album and duration are sent). LRC text is parsed into
// timed lines for the Now Playing view.
#ifndef NV_MCLRC_H
#define NV_MCLRC_H
#include "mcmodelo.h"

#define MCLRC_MAX 400
typedef struct { int ms; char texto[200]; } McLinha;

void mclrc_definir_base(const char *url);   // tests
// LRCLIB: synced when there is, else plain. MC_OK / MC_ERRO_NAO_SUPORTA / MC_ERRO_REDE.
int  mclrc_buscar(const McFaixa *f, char *lrc, size_t n);
// LRC (or plain text) -> lines. *sincronizada = 1 when timed. Count.
int  mclrc_parse(const char *lrc, McLinha *out, int max, int *sincronizada);
// The line playing at `ms` (-1 before the first).
int  mclrc_linha_em(const McLinha *v, int n, int ms);
#endif
