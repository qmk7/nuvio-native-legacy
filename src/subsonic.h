// MUSIC CENTER: a Subsonic-compatible server (Navidrome, Airsonic, gonic,
// Ampache...) over the Subsonic REST API 1.16.1, with the OpenSubsonic
// extensions where the server has them (bit depth, sample rate, channels,
// structured lyrics).
//
// AUTH. Token authentication: t = md5(password + salt), s = a fresh random
// salt on every request; the password itself never travels. A server that
// refuses tokens (error 41, e.g. LDAP users) gets the legacy hex-encoded
// password ONLY over https. Credentials and the t/s pair are never logged:
// stream URLs carry them, so they are redacted in every log line.
//
// QUALITY. Streams ask for format=raw: the original file, no transcoding.
//
// Ids returned here are the server's own (unqualified); mccat.c qualifies
// them with the source key. Every call BLOCKS: run it on a worker thread.
#ifndef NV_SUBSONIC_H
#define NV_SUBSONIC_H
#include "mcmodelo.h"
#include <stddef.h>

typedef struct {
  char url[256];      // "https://music.example.com" (no /rest)
  char usuario[96], senha[128];
} SsServidor;

typedef struct {
  char tipo[32], versao[32], apiVersao[16];
  int  openSubsonic;
} SsInfo;

// ---- parsers (no network; tests feed fixtures) ----
// MC_OK, or MC_ERRO_LOGIN (40/41/44), MC_ERRO_NAO_SUPORTA (30/70 missing),
// MC_ERRO_SERVIDOR (other "failed"), MC_ERRO_OUTRO (not a Subsonic answer).
int ss_parse_estado(const char *json, int *codigo, char *msg, size_t nmsg);
int ss_parse_faixa(const char *obj, const char *fim, McFaixa *f);
int ss_parse_faixas(const char *json, const char *chave, McFaixa *out, int max);

// ---- calls ----
int ss_ping(const SsServidor *s, SsInfo *info);
int ss_artistas(const SsServidor *s, McArtista *out, int max, int *n);
int ss_artista_albuns(const SsServidor *s, const char *id, McAlbum *out, int max, int *n);
// tipo: newest, recent, frequent, random, alphabeticalByName, starred, byGenre
int ss_albuns(const SsServidor *s, const char *tipo, const char *genero, int offset,
              McAlbum *out, int max, int *n);
int ss_album(const SsServidor *s, const char *id, McAlbum *info, McFaixa *out, int max, int *n);
int ss_generos(const SsServidor *s, McGenero *out, int max, int *n);
int ss_genero_faixas(const SsServidor *s, const char *genero, int offset, McFaixa *out, int max, int *n);
int ss_playlists(const SsServidor *s, McPlaylist *out, int max, int *n);
int ss_playlist(const SsServidor *s, const char *id, McPlaylist *info, McFaixa *out, int max, int *n);
int ss_playlist_criar(const SsServidor *s, const char *nome, const char *const *ids, int n, char *novoId, size_t nn);
// Appends `add`, removes the entries at `remover` (indexes before the change).
int ss_playlist_editar(const SsServidor *s, const char *id, const char *nome,
                       const char *const *add, int nAdd, const int *remover, int nRem);
int ss_playlist_apagar(const SsServidor *s, const char *id);
int ss_buscar(const SsServidor *s, const char *q, int offsetFaixas,
              McArtista *ar, int maxAr, int *nAr, McAlbum *al, int maxAl, int *nAl,
              McFaixa *fx, int maxFx, int *nFx);
// getRandomSongs (Party mode); genero may be NULL.
int ss_aleatorias(const SsServidor *s, const char *genero, McFaixa *out, int max, int *n);
int ss_favoritar(const SsServidor *s, const char *faixaId, int liga);
int ss_favoritos(const SsServidor *s, McFaixa *out, int max, int *n);
int ss_scrobble(const SsServidor *s, const char *faixaId, long long quandoMs, int submissao);
// Lyrics as LRC text ("[mm:ss.xx] line" when synced, plain lines otherwise).
// MC_ERRO_NAO_SUPORTA when the server has none.
int ss_letra(const SsServidor *s, const McFaixa *f, char *lrc, size_t n);

// URLs carrying fresh auth (keep them out of logs: ss_redigir).
void ss_url_stream(const SsServidor *s, const char *id, char *url, size_t n);
// "download": the original file, and only when the server's download role
// allows this user (the server answers error 50 otherwise).
void ss_url_download(const SsServidor *s, const char *id, char *url, size_t n);
// Cover URLs use a salt derived from the id (not random), so the same cover
// keeps one URL and the image cache works; the token still never reveals the
// password.
void ss_url_capa(const SsServidor *s, const char *capaId, int tamanho, char *url, size_t n);
// Copy of `url` with u/t/s/p values replaced by "***".
void ss_redigir(const char *url, char *d, size_t n);
// Tests: fixed salt (NULL = random again).
void ss_teste_sal(const char *sal);
#endif
