// Xtream VOD and series adapter (src/xtvod.c): parsers on fixed bodies, then
// the real fetchers over HTTP against tests/qrtime_iptv_mock.py with two
// Xtream accounts whose catalogs overlap.
//
//   qrtime_xtvod <data dir seeded with QRT_SEED_XT2=1>
#include "xtvod.h"
#include "xtream.h"
#include "iptvfontes.h"
#include "dados.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t perfis_sessao_geracao(void) { return 1; }
int perfis_sessao_atual(uint64_t s) { return s == 1; }
int perfis_sessao_travar(uint64_t s) { return s == 1; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return 1; }
int ajustes_livetv_formato(void) { return 0; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

typedef struct { XtVodItem v[16]; int n; } Lista;
static int juntar(const XtVodItem *it, void *u) { Lista *l = u; if (l->n < 16) l->v[l->n++] = *it; return 1; }
static const XtVodItem *achar(const Lista *l, const char *sid) {
  for (int i = 0; i < l->n; i++) if (!strcmp(l->v[i].sid, sid)) return &l->v[i];
  return NULL;
}

int main(int argc, char **argv) {
  IptvConta contas[8];
  unsigned x1 = 0, x2 = 0;
  int n;
  if (argc != 2) { fprintf(stderr, "usage: %s <data dir>\n", argv[0]); return 2; }

  // ---- parsers, no network ----
  CONFERE(xtvod_ano_do_nome("Blade Runner 2049 (2017)") == 2017, "year: last year-like token");
  CONFERE(xtvod_ano_do_nome("EN - The Matrix (1999) 4K") == 1999, "year in parentheses");
  CONFERE(xtvod_ano_do_nome("Room 1408") == 0, "a number glued to a word is not a year");
  {
    Lista l = {0};
    const char *j = "[{\"stream_id\":\"7\",\"name\":\"A (2001)\",\"stream_icon\":\" javascript:x \","
                    "\"tmdb\":\"abc\",\"rating\":\"7.5\",\"container_extension\":\"MKV\"},"
                    "{\"stream_id\":\"../etc\",\"name\":\"Evil id\"},{\"name\":\"no id\"}]";
    n = xtvod_parse_lista(j, 0, NULL, NULL, juntar, &l);
    CONFERE(n == 1 && l.n == 1, "only items with a safe provider id are kept");
    CONFERE(l.n && l.v[0].ano == 2001 && l.v[0].nota10 == 75 && !strcmp(l.v[0].ext, "mkv"), "year, rating, lower-case ext");
    CONFERE(l.n && l.v[0].tmdbProv == 0, "a non-numeric tmdb field is ignored");
    CONFERE(l.n && l.v[0].poster[0] == 0, "non-http artwork is dropped");
  }
  {
    XtVodFicha f; XtVodEp e[8];
    CONFERE(xtvod_parse_serie("{\"info\":[],\"episodes\":[]}", &f, e, 8) == 0, "empty series record");
    CONFERE(xtvod_parse_filme("[]", &f) == 0, "not a movie record");
  }

  // ---- network, two accounts ----
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);
  n = iptv_contas(contas, 8);
  for (int i = 0; i < n; i++) if (contas[i].tipo == IPTV_XTREAM) { if (!x1) x1 = contas[i].id; else x2 = contas[i].id; }
  if (!x1 || !x2) { fprintf(stderr, "need two Xtream accounts\n"); return 2; }

  {
    Lista m1 = {0}, m2 = {0}, s1 = {0}, s2 = {0};
    const XtVodItem *a;
    CONFERE(xtvod_listar(x1, 0, juntar, &m1, 1) == 3, "account 1: 3 movies");
    CONFERE(xtvod_listar(x2, 0, juntar, &m2, 1) == 2, "account 2: 2 movies");
    CONFERE(xtvod_listar(x1, 1, juntar, &s1, 1) == 2, "account 1: 2 series");
    CONFERE(xtvod_listar(x2, 1, juntar, &s2, 1) == 1, "account 2: 1 series");
    a = achar(&m1, "1001");
    CONFERE(a && a->tmdbProv == 603 && a->ano == 1999 && !strcmp(a->categoria, "QRT Action") && !strcmp(a->ext, "mkv"),
            "movie 1001: tmdb hint, year from name, category name, ext");
    a = achar(&m1, "1003");
    CONFERE(a && a->tmdbProv == 27205 && a->ano == 2010 && a->nota10 == 88, "numeric tmdb_id and rating");
    a = achar(&m1, "1002");
    CONFERE(a && a->tmdbProv == 0 && a->ano == 2021, "provider-local movie: no tmdb");
    a = achar(&m2, "2002");
    CONFERE(a && a->tmdbProv == 0 && a->ano == 2010, "empty tmdb field stays 0");
    a = achar(&s1, "3001");
    CONFERE(a && a->tmdbProv == 1396 && a->ano == 2008 && a->adicionado == 1700002000LL, "series: tmdb, year, last_modified");
  }
  {
    XtVodFicha f; XtVodEp e[16];
    CONFERE(xtvod_ficha_filme(x1, "1001", &f, 1) && !strcmp(f.sid, "1001") && !strcmp(f.ext, "mkv") &&
            f.duracaoMin == 136 && f.fundo[0] && strstr(f.elenco, "Keanu"), "get_vod_info: details");
    n = xtvod_ficha_serie(x1, "3001", &f, e, 16, 1);
    CONFERE(n == 3 && e[0].temporada == 1 && e[0].episodio == 1 && !strcmp(e[0].sid, "40001") &&
            e[2].temporada == 2 && !strcmp(e[2].sid, "40011"), "get_series_info {season:[..]}: 3 episodes in order");
    CONFERE(n == 3 && !strcmp(e[0].nome, "Pilot") && e[0].duracaoMin == 47 && strstr(e[0].sinopse, "Pilot"),
            "episode title, runtime, plot");
    n = xtvod_ficha_serie(x2, "4001", &f, e, 16, 1);
    CONFERE(n == 3 && e[2].temporada == 2 && !strcmp(e[2].sid, "50011") && f.tmdbProv == 1396,
            "get_series_info [[..],[..]] shape");
    CONFERE(xtvod_ficha_serie(x1, "9999", &f, e, 16, 1) == 0, "unknown series: no episodes");
    CONFERE(xtvod_ficha_filme(x1, "../1", &f, 1) == 0, "unsafe id never reaches the server");
  }
  {
    char url[512];
    CONFERE(xtream_fonte_vod_url(x1, 0, "1001", "mkv", url, sizeof url) && strstr(url, "/movie/qrtuser/") &&
            strstr(url, "/1001.mkv"), "movie URL");
    CONFERE(xtream_fonte_vod_url(x2, 1, "50011", "mp4", url, sizeof url) && strstr(url, "/series/qrtuser2/") &&
            strstr(url, "/50011.mp4"), "episode URL uses its own account");
    CONFERE(!xtream_fonte_vod_url(x1, 0, "1/../2", "mp4", url, sizeof url), "unsafe id: no URL");
    CONFERE(!xtream_fonte_vod_url(x1, 0, "1", "m p4", url, sizeof url), "unsafe ext: no URL");
  }
  return falhas ? 1 : 0;
}
