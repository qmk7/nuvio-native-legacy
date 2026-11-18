// QR TIME Trakt (phase 7): the identity gate (src/traktid.c), the scrobble
// body built through it (src/traktscrobble.c), the duplicate-history ledger
// and the write retries (src/traktdup.c). No network: the transport and the
// sleep are replaced.
//
//   qrtime_traktid <data dir>
#include "traktid.h"
#include "traktdup.h"
#include "traktscrobble.h"
#include "dados.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

// IPTV resolution fixture: "qv:00000000000000aa.m" is a confirmed movie,
// "qv:00000000000000bb.s" a confirmed series with TMDB only; anything else unknown.
static int resolver(const char *id, const char *tipo, char *imdb, size_t n, long *tmdb) {
  (void)tipo;
  if (!strcmp(id, "qv:00000000000000aa.m")) { snprintf(imdb, n, "tt0133093"); *tmdb = 603; return 1; }
  if (!strcmp(id, "qv:00000000000000bb.s")) { imdb[0] = 0; *tmdb = 1396; return 1; }
  return 0;
}

// Transport fixture for trakt_postar.
static int roteiro[8], nRoteiro, chamadas, retryAfter = 7;
static int dormiu[8], nDormiu;
static char *postar(const char *u, int s, const char *const *c, const char *b, int *st, int *ra) {
  (void)u; (void)s; (void)c; (void)b;
  *st = chamadas < nRoteiro ? roteiro[chamadas] : 201;
  *ra = *st == 429 ? retryAfter : 0;
  chamadas++;
  return *st ? strdup("{}") : NULL;
}
static void dormir(int sgs) { if (nDormiu < 8) dormiu[nDormiu++] = sgs; }
static void cenario(const int *r, int n) { memcpy(roteiro, r, sizeof(int) * (size_t)n); nRoteiro = n; chamadas = 0; nDormiu = 0; }

int main(int argc, char **argv) {
  TraktId t;
  char b[400];
  if (argc < 2) return 2;
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(argv[1]);
  traktid_teste_resolvedor(resolver);

  // ---- identity gate ----
  CONFERE(traktid_resolver("tt0133093", &t) == TID_IMDB && !strcmp(t.imdb, "tt0133093") && t.temporada < 0, "IMDb movie id");
  CONFERE(traktid_resolver("tt0903747:2:5", &t) == TID_IMDB && t.temporada == 2 && t.episodio == 5, "IMDb episode id");
  CONFERE(traktid_resolver("tmdb:603", &t) == TID_TMDB && t.num == 603, "TMDB id");
  CONFERE(traktid_resolver("tmdb:t1396:1:2", &t) == TID_TMDB && t.num == 1396 && t.temporada == 1 && t.episodio == 2, "TMDB series episode (tmdb:t...)");
  CONFERE(traktid_resolver("tvdb:81189", &t) == TID_TVDB && t.num == 81189, "TVDB id");
  CONFERE(traktid_resolver("tt12", &t) == TID_INVALIDO && traktid_resolver("ttabc1234", &t) == TID_INVALIDO
          && traktid_resolver("tt0133093:x:1", &t) == TID_INVALIDO && traktid_resolver("tmdb:", &t) == TID_INVALIDO
          && traktid_resolver("tmdb:0", &t) == TID_INVALIDO && traktid_resolver("tt0133093:1:0", &t) == TID_INVALIDO,
          "malformed ids refused");
  CONFERE(traktid_resolver("123456", &t) == TID_LOCAL, "a raw Xtream stream id is LOCAL, never an IMDb/TMDB id");
  CONFERE(traktid_resolver("xtream:@00000001:301", &t) == TID_LOCAL && traktid_resolver("stalker:abc", &t) == TID_LOCAL,
          "Xtream / Stalker channel ids are local");
  CONFERE(traktid_resolver("jf:abcd", &t) == TID_LOCAL && traktid_resolver("em:1", &t) == TID_LOCAL && traktid_resolver("px:9", &t) == TID_LOCAL
          && traktid_resolver("kitsu:41370", &t) == TID_LOCAL, "personal servers and Kitsu are local");
  CONFERE(traktid_resolver("qv:00000000000000aa.m", &t) == TID_IMDB && !strcmp(t.imdb, "tt0133093") && t.resolvido,
          "IPTV movie resolved by TMDB -> its canonical IMDb id");
  CONFERE(traktid_resolver("qv:00000000000000bb.e3x4", &t) == TID_TMDB && t.num == 1396 && t.temporada == 3 && t.episodio == 4,
          "IPTV episode: the series' confirmed TMDB id + S/E");
  CONFERE(traktid_resolver("qv:00000000000000cc.m", &t) == TID_LOCAL, "IPTV movie not resolved: stays LOCAL (nothing sent)");
  traktid_resolver("tmdb:1396:3:4", &t);
  CONFERE(traktid_ids_json(&t, b, sizeof b) && !strcmp(b, "\"tmdb\":1396"), "ids JSON for TMDB is a number");
  CONFERE(traktid_canonico(&t, b, sizeof b) && !strcmp(b, "tmdb:1396:3:4"), "canonical string keeps S/E");
  traktid_resolver("jf:abcd", &t);
  CONFERE(!traktid_ids_json(&t, b, sizeof b) && !b[0], "a local id produces no ids JSON");

  // ---- scrobble bodies through the gate ----
  scrobble_corpo_ep(b, sizeof b, "tt0133093", 50, NULL);
  CONFERE(!strcmp(b, "{\"movie\":{\"ids\":{\"imdb\":\"tt0133093\"}},\"progress\":50.00}"), "movie scrobble body");
  scrobble_corpo_ep(b, sizeof b, "tmdb:1396:3:4", 12.5, NULL);
  CONFERE(!strcmp(b, "{\"show\":{\"ids\":{\"tmdb\":1396}},\"episode\":{\"season\":3,\"number\":4},\"progress\":12.50}"),
          "TMDB episode scrobble body (was {\"imdb\":\"tmdb\"} before)");
  scrobble_corpo_ep(b, sizeof b, "qv:00000000000000aa.m", 90, NULL);
  CONFERE(strstr(b, "\"imdb\":\"tt0133093\"") && !strstr(b, "qv:"), "resolved IPTV movie scrobbles as its IMDb id");
  scrobble_corpo_ep(b, sizeof b, "qv:00000000000000cc.m", 90, NULL);
  CONFERE(!b[0], "unresolved IPTV movie: no body at all");
  scrobble_corpo_ep(b, sizeof b, "jf:abcd", 90, NULL);
  CONFERE(!b[0], "Jellyfin id: no body");

  // ---- duplicate ledger (per account, per profile, persisted) ----
  {
    const char *A = "Authorization: Bearer token-A", *B = "Authorization: Bearer token-B";
    CONFERE(!traktdup_tem(A, "tt0133093"), "nothing sent yet");
    traktdup_por(A, "tt0133093");
    traktdup_por(A, "tt0903747:1:1");
    traktdup_por(A, "tt0903747:1:2");
    CONFERE(traktdup_tem(A, "tt0133093") && !traktdup_tem(B, "tt0133093"), "ledger is per account");
    traktdup_teste_recarregar();
    CONFERE(traktdup_tem(A, "tt0903747:1:2"), "ledger survives a restart (disk)");
    {
      char *f = dados_ler("trakt-sent-p1.txt");
      CONFERE(f && !strstr(f, "token-A"), "the token itself is never stored");
      free(f);
    }
    traktdup_tirar(A, "tt0903747");
    CONFERE(!traktdup_tem(A, "tt0903747:1:1") && !traktdup_tem(A, "tt0903747:1:2") && traktdup_tem(A, "tt0133093"),
            "unwatching a show clears its episodes (a real rewatch can be recorded)");
    traktdup_renovar(A, "Authorization: Bearer token-A2");
    CONFERE(traktdup_tem("Authorization: Bearer token-A2", "tt0133093") && !traktdup_tem(A, "tt0133093"),
            "token refresh: the account's entries follow the new token");
    perfil = 2;
    CONFERE(!traktdup_tem("Authorization: Bearer token-A2", "tt0133093"), "profile 2 has its own ledger");
    perfil = 1;
    CONFERE(traktdup_tem("Authorization: Bearer token-A2", "tt0133093"), "profile 1 keeps its ledger");
  }

  // ---- writes: rate limits and network failures ----
  trakt_postar_teste(postar, dormir);
  {
    int st;
    char *r;
    int a[] = { 429, 201 };
    cenario(a, 2);
    r = trakt_postar("u", NULL, "{}", &st); free(r);
    CONFERE(st == 201 && chamadas == 2 && nDormiu == 1 && dormiu[0] == 7, "429: waits Retry-After (7 s), then succeeds");
    retryAfter = 120;
    cenario(a, 2);
    r = trakt_postar("u", NULL, "{}", &st); free(r);
    CONFERE(st == 201 && dormiu[0] == 30, "a long Retry-After is capped at 30 s");
    retryAfter = 7;
    {
      int c[] = { 429, 429, 429, 201 };
      cenario(c, 4);
      r = trakt_postar("u", NULL, "{}", &st); free(r);
      CONFERE(st == 429 && chamadas == 3, "at most 2 waits; then the failure is reported (journal retries later)");
    }
    {
      int c[] = { 0, 201 };
      cenario(c, 2);
      r = trakt_postar("u", NULL, "{}", &st); free(r);
      CONFERE(st == 201 && chamadas == 2 && dormiu[0] == 2, "network failure: one retry after 2 s");
    }
    {
      int c[] = { 0, 0, 201 };
      cenario(c, 3);
      r = trakt_postar("u", NULL, "{}", &st); free(r);
      CONFERE(st == 0 && chamadas == 2, "offline: gives up after one retry (no tight loop)");
    }
    {
      int c[] = { 503, 201 };
      cenario(c, 2);
      r = trakt_postar("u", NULL, "{}", &st); free(r);
      CONFERE(st == 201 && chamadas == 2, "5xx: one retry");
    }
    {
      int c[] = { 422 };
      cenario(c, 1);
      r = trakt_postar("u", NULL, "{}", &st); free(r);
      CONFERE(st == 422 && chamadas == 1, "4xx (not 429): no retry");
    }
  }
  return falhas ? 1 : 0;
}
