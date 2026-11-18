// QR TIME Trakt (phase 7): the real write paths of src/trakt.c (history,
// episodes, scrobble) with only the HTTP transport replaced. Proves: only
// Trakt-compatible ids are sent (resolved IPTV ids as their IMDb/TMDB id,
// raw provider ids never), no duplicate plays, 409/429 handling.
#include "trakt.h"
#include "traktid.h"
#include "traktdup.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int calls, roteiro[16], nRoteiro;
static char bodies[32][1200], urls[32][200];
static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

unsigned long long cat_historico_geracao(void) { return 1; }
void rede_avisar_401(void (*cb)(const char *)) { (void)cb; }
static char *responder(const char *url, const char *body, int *st, int *ra) {
  pthread_mutex_lock(&lock);
  int i = calls++;
  if (i < 32) { snprintf(urls[i], sizeof urls[i], "%s", url); snprintf(bodies[i], sizeof bodies[i], "%s", body); }
  *st = i < nRoteiro && roteiro[i] ? roteiro[i] : 201;
  if (ra) *ra = *st == 429 ? 1 : 0;
  pthread_mutex_unlock(&lock);
  return strdup("{}");
}
char *rede_postar_st(const char *url, int t, const char *const *cab, const char *body, int *st) { (void)t; (void)cab; return responder(url, body, st, NULL); }
char *rede_postar_st_retry(const char *url, int t, const char *const *cab, const char *body, int *st, int *ra) { (void)t; (void)cab; return responder(url, body, st, ra); }
static void dormir(int s) { (void)s; }
static int resolver(const char *id, const char *tipo, char *imdb, size_t n, long *tmdb) {
  (void)tipo;
  if (!strcmp(id, "qv:00000000000000aa.m")) { snprintf(imdb, n, "tt0111161"); *tmdb = 278; return 1; }
  if (!strcmp(id, "qv:00000000000000bb.s")) { imdb[0] = 0; *tmdb = 1396; return 1; }
  return 0;
}
static int nCalls(void) { int c; pthread_mutex_lock(&lock); c = calls; pthread_mutex_unlock(&lock); return c; }
static int esperarCalls(int n) { for (int i = 0; i < 300 && nCalls() < n; i++) usleep(10000); return nCalls() >= n; }
static void zerar(void) { pthread_mutex_lock(&lock); calls = 0; nRoteiro = 0; memset(roteiro, 0, sizeof roteiro); pthread_mutex_unlock(&lock); }

int main(void) {
  TraktDestino d;
  trakt_postar_teste(NULL, dormir);
  traktid_teste_resolvedor(resolver);
  if (!trakt_definir("fixture-A", "fixture-client") || !trakt_destino_capturar(&d)) { puts("FAIL: setup"); return 1; }

  // ---- movies: history, duplicates ----
  CONFERE(trakt_titulo_marcar_destino(&d, "tt0133093", "movie", 1) && calls == 1 && strstr(urls[0], "/sync/history")
          && !strcmp(bodies[0], "{\"movies\":[{\"ids\":{\"imdb\":\"tt0133093\"}}]}"), "mark a movie watched");
  CONFERE(trakt_titulo_marcar_destino(&d, "tt0133093", "movie", 1) && calls == 1, "marking it again sends nothing (no second play)");
  CONFERE(trakt_titulo_marcar_destino(&d, "tt0133093", "movie", 0) && calls == 2 && strstr(urls[1], "/sync/history/remove"), "unwatch");
  CONFERE(trakt_titulo_marcar_destino(&d, "tt0133093", "movie", 1) && calls == 3, "after unwatching, a new watch is recorded");
  CONFERE(trakt_titulo_marcar_destino(&d, "tmdb:603", "movie", 1) && calls == 4
          && !strcmp(bodies[3], "{\"movies\":[{\"ids\":{\"tmdb\":603}}]}"), "TMDB id: sent as {\"tmdb\":603}");
  // ---- provider-local identities ----
  CONFERE(trakt_titulo_marcar_destino(&d, "qv:00000000000000aa.m", "movie", 1) && calls == 5
          && !strcmp(bodies[4], "{\"movies\":[{\"ids\":{\"imdb\":\"tt0111161\"}}]}") && !strstr(bodies[4], "qv"),
          "resolved IPTV movie: sent as its TMDB-confirmed IMDb id");
  CONFERE(!trakt_titulo_marcar_destino(&d, "qv:00000000000000cc.m", "movie", 1) && calls == 5, "unresolved IPTV movie: nothing sent");
  CONFERE(!trakt_titulo_marcar_destino(&d, "123456", "movie", 1) && calls == 5, "raw Xtream id: nothing sent");
  CONFERE(!trakt_titulo_marcar_destino(&d, "jf:abcdef", "movie", 1) && calls == 5, "Jellyfin id: nothing sent");
  // ---- episodes ----
  {
    TraktEpisodioAlvo a[2] = { { { 1, 1 }, 0 }, { { 1, 2 }, 0 } };
    CONFERE(trakt_episodios_marcar_alvos_destino(&d, "qv:00000000000000bb.s", a, 2, 1) && calls == 6
            && !strcmp(bodies[5], "{\"shows\":[{\"ids\":{\"tmdb\":1396},\"seasons\":[{\"number\":1,\"episodes\":[{\"number\":1},{\"number\":2}]}]}]}"),
            "IPTV series episodes: the show's confirmed TMDB id + season/episode numbers");
    CONFERE(trakt_episodios_marcar_alvos_destino(&d, "qv:00000000000000bb.s", a, 2, 1) && calls == 6, "same episodes again: nothing sent");
    {
      TraktEpisodioAlvo b[3] = { { { 1, 1 }, 0 }, { { 1, 2 }, 0 }, { { 1, 3 }, 0 } };
      CONFERE(trakt_episodios_marcar_alvos_destino(&d, "tmdb:1396", b, 3, 1) && calls == 7
              && strstr(bodies[6], "{\"number\":3}") && !strstr(bodies[6], "{\"number\":1}"), "a batch sends only the episodes not yet sent");
    }
  }
  // ---- rate limit on a write ----
  zerar();
  roteiro[0] = 429; nRoteiro = 1;
  CONFERE(trakt_titulo_marcar_destino(&d, "tt0068646", "movie", 1) && calls == 2, "429 on a history write: waited and retried");
  // ---- scrobble ----
  zerar();
  {
    TraktScrobbleAlvo loc = { "qv:00000000000000cc.m", 0 };
    TraktScrobbleAlvo ep = { "tmdb:1396:3:4", 0 };
    CONFERE(trakt_scrobble_alvo(SCR_EV_TOCANDO, &loc, 30, 3000, 30, 0) == SCR_NADA, "unresolved IPTV item: scrobble refused");
    usleep(200000);
    CONFERE(nCalls() == 0, "and nothing went out");
    CONFERE(trakt_scrobble_alvo(SCR_EV_TOCANDO, &ep, 30, 3000, 30, 0) == SCR_START && esperarCalls(1)
            && strstr(urls[0], "/scrobble/start")
            && !strcmp(bodies[0], "{\"show\":{\"ids\":{\"tmdb\":1396}},\"episode\":{\"season\":3,\"number\":4},\"progress\":1.00}"),
            "TMDB episode scrobble start (correct body)");
    roteiro[1] = 409; nRoteiro = 2;
    CONFERE(trakt_scrobble_alvo(SCR_EV_SAIU, &ep, 2850, 3000, 2800, 1) == SCR_STOP && esperarCalls(2) && strstr(urls[1], "/scrobble/stop"),
            "scrobble stop at 95%");
    usleep(100000);
    {
      TraktEpisodioAlvo a[1] = { { { 3, 4 }, 0 } };
      int antes = nCalls();
      CONFERE(trakt_episodios_marcar_alvos_destino(&d, "tmdb:1396", a, 1, 1) && nCalls() == antes,
              "after the stop (even a 409 'already scrobbled'), marking the episode adds no second play");
    }
  }
  // ---- another account in the same profile is not deduplicated against the first ----
  zerar();
  {
    TraktDestino d2;
    trakt_definir("fixture-B", "fixture-client");
    trakt_destino_capturar(&d2);
    CONFERE(trakt_titulo_marcar_destino(&d2, "tt0133093", "movie", 1) && calls == 1, "account B: its own first watch is sent");
  }
  return falhas ? 1 : 0;
}
