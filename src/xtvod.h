// XTREAM VOD AND SERIES (QR TIME): the provider side of the unified catalog.
//
// Reads an Xtream account's movies (get_vod_categories, get_vod_streams,
// get_vod_info) and series (get_series_categories, get_series,
// get_series_info). Everything here is PROVIDER DATA with provider ids:
// `sid` is the panel's stream_id / series_id / episode id, meaningful only on
// that account. `tmdbProv` is what the PANEL claims the TMDB id is; it is a
// hint for matching (iptvvod.c), never an identity by itself, and nothing in
// this file sends any id anywhere but the account's own server.
//
// The parsers take a JSON body and need no network (tests feed fixtures); the
// xtvod_* fetchers BLOCK and run on a worker thread.
#ifndef NV_XTVOD_H
#define NV_XTVOD_H
#include <stdint.h>

#define XTVOD_SID 24
typedef struct {
  char sid[XTVOD_SID];   // stream_id (movie) or series_id (series)
  char nome[160];        // as the panel writes it ("EN - Title (2019) 4K")
  char poster[512];      // stream_icon / cover, http(s) only
  char categoria[96];    // category name ("" when unknown)
  char ext[8];           // container_extension (movies)
  long tmdbProv;         // panel's tmdb / tmdb_id; 0 = none (a hint, not an identity)
  int  ano;              // year field, release date, or "(2019)" in the name; 0 = unknown
  int  nota10;           // rating x10 (0..100); 0 = unknown
  long long adicionado;  // "added" / "last_modified", epoch seconds; 0 = unknown
} XtVodItem;

typedef struct {
  char sid[XTVOD_SID];
  char nome[160];
  char sinopse[900];
  char poster[512];
  char fundo[512];       // first backdrop
  char genero[160];
  char elenco[400];      // "A, B, C" as the panel writes it
  char direcao[128];
  char ext[8];
  long tmdbProv;
  int  ano, duracaoMin, nota10;
} XtVodFicha;

typedef struct {
  char sid[XTVOD_SID];   // episode id: the stream id of THIS episode
  int  temporada, episodio;
  char nome[120];
  char sinopse[420];
  char thumb[512];
  char ext[8];
  int  duracaoMin;
} XtVodEp;

typedef int (*XtVodCb)(const XtVodItem *it, void *u);   // 0 stops

// ---- parsers (no network) ----
// Category list: calls `cat` for each (id, name). Returns the count.
int xtvod_parse_categorias(const char *json, void (*cat)(const char *id, const char *nome, void *u), void *u);
// get_vod_streams / get_series list. `nomeCat` maps a category id to its name
// (may be NULL). Returns items delivered.
int xtvod_parse_lista(const char *json, int serie,
                      const char *(*nomeCat)(const char *id, void *u), void *uCat,
                      XtVodCb cb, void *u);
// get_vod_info: 1 when the movie record was read.
int xtvod_parse_filme(const char *json, XtVodFicha *f);
// get_series_info: info + episodes (both the {"1":[..]} and [[..]] shapes).
// Returns episodes written (<= max), sorted by season then episode; -1 when
// the body is not a series record.
int xtvod_parse_serie(const char *json, XtVodFicha *f, XtVodEp *eps, int max);
// Year from a title: "Title (2019)", "Title 2019", "Title - 2019". 0 = none.
int xtvod_ano_do_nome(const char *nome);

// ---- fetchers (BLOCK; worker thread) ----
// Whole movie (serie 0) or series (serie 1) catalog of account `conta`.
// Returns items delivered; -1 when the server did not answer or refused.
int xtvod_listar(unsigned conta, int serie, XtVodCb cb, void *u, uint64_t sessao);
int xtvod_ficha_filme(unsigned conta, const char *sid, XtVodFicha *f, uint64_t sessao);
// Returns episodes (>= 0) or -1.
int xtvod_ficha_serie(unsigned conta, const char *sid, XtVodFicha *f, XtVodEp *eps, int max,
                      uint64_t sessao);
#endif
