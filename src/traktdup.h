// QR TIME Trakt: no duplicate history, and writes that survive rate limits.
//
// DUPLICATES. POST /sync/history adds a PLAY every time: marking a title
// watched twice (a double press, a retry, the scrobble's own stop followed by
// a manual mark) would count it twice on Trakt. This ledger remembers, per
// profile and per Trakt credential, what this app already put in the history
// ("trakt-sent-p<N>.txt": canonical id -> when). A second "watched" for the
// same title/episode is not sent again; "unwatched" clears the entry so a
// real rewatch can be recorded after it.
//
// RATE LIMITS. trakt_postar() repeats a write after HTTP 429 (waiting the
// Retry-After Trakt gives, at most 30 s; at most 2 waits) and once after a
// network failure or a 5xx. It runs on worker threads only (it sleeps).
#ifndef NV_TRAKTDUP_H
#define NV_TRAKTDUP_H
#include <stddef.h>

// `aut` is the request's "Authorization: Bearer ..." line: entries belong to
// the ACCOUNT that made them (a 64-bit FNV tag of it is stored, never the token).
int  traktdup_tem(const char *aut, const char *canon);   // 1: already sent to the history
void traktdup_por(const char *aut, const char *canon);
void traktdup_tirar(const char *aut, const char *canon); // a title without ":S:E" takes its episodes too
// Token refresh (same account, new token): the entries follow it.
void traktdup_renovar(const char *autVelho, const char *autNovo);
void traktdup_esquecer(void);
void traktdup_teste_recarregar(void);

// API base ("https://api.trakt.tv"); tests point it at a fixture.
void trakt_definir_base(const char *base);
const char *trakt_base(void);
// "<base><caminho>" into dst.
void trakt_url(char *dst, size_t n, const char *caminho);

// POST with the retries above. Body/status like rede_postar_st.
char *trakt_postar(const char *url, const char *const *cab, const char *corpo, int *status);
// Tests: replace the transport and the sleep.
void trakt_postar_teste(char *(*postar)(const char *, int, const char *const *, const char *, int *, int *),
                        void (*dormir)(int segundos));
#endif
