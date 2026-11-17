// IPTV library (src/iptvvod.c) over real HTTP against tests/qrtime_iptv_mock.py
// with two Xtream accounts whose catalogs overlap.
//
//   qrtime_iptvvod <data dir> online    full checks (fixture running)
//   qrtime_iptvvod <data dir> offline   fixture down, stale cache: still there
#include "iptvvod.h"
#include "streams.h"
#include "dados.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }
int ajustes_livetv_formato(void) { return 0; }
const char *i18n(const char *s) { return s; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static CatItem itens[64];
static CatFileira fils[4];
static int nf, ni;
static const CatItem *porTitulo(const char *t) {
  for (int i = 0; i < ni; i++) if (!strcmp(itens[i].titulo, t)) return &itens[i];
  return NULL;
}
static int fontes(const char *alvo, Stream **l) {
  int n = 0, r = 0;
  if (!iptvvod_fontes_pedir(alvo)) return -1;
  for (int i = 0; i < 500; i++) {
    r = iptvvod_fontes_colher(alvo, l, &n);
    if (r != 1) break;
    usleep(20000);
  }
  return r == 2 ? n : -1;
}
static int temUrl(const Stream *l, int n, const char *s) {
  for (int i = 0; i < n; i++) if (strstr(l[i].url, s)) return 1;
  return 0;
}
static void carregar(void) {
  iptvvod_passo();
  usleep(50000);
  iptvvod_teste_esperar(30000);
  iptvvod_passo();
  nf = iptvvod_fileiras_copiar(itens, 64, fils, 4, &ni);
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: %s <data dir> online|offline\n", argv[0]); return 2; }
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);

  if (!strcmp(argv[2], "offline")) {
    carregar();
    CONFERE(iptvvod_n_itens('m') == 3 && iptvvod_n_itens('s') == 2,
            "providers down, stale cache: the library is still there");
    return falhas ? 1 : 0;
  }

  // ---- identity, no network ----
  {
    char n[128], t[160], id[64];
    uint64_t g; char k; int s, e;
    iptvvod_normalizar("EN - The Matrix (1999) 4K", n, sizeof n);
    CONFERE(!strcmp(n, "the matrix"), "normalize: language prefix, year, quality tag");
    iptvvod_normalizar("|FR| Inception HEVC 1080p", n, sizeof n);
    CONFERE(!strcmp(n, "inception"), "normalize: |FR| prefix and codecs");
    iptvvod_titulo_limpo("EN - The Matrix (1999) 4K", t, sizeof t);
    CONFERE(!strcmp(t, "The Matrix"), "display title");
    iptvvod_titulo_limpo("Room 1408", t, sizeof t);
    CONFERE(!strcmp(t, "Room 1408"), "a number in a title is kept");
    iptvvod_id_montar(id, sizeof id, 0x0123456789abcdefULL, 'e', 2, 13);
    CONFERE(!strcmp(id, "qv:0123456789abcdef.e2x13") && iptvvod_id_ler(id, &g, &k, &s, &e) &&
            g == 0x0123456789abcdefULL && k == 'e' && s == 2 && e == 13, "episode id round trip");
    CONFERE(!iptvvod_id_ler("qv:0123.m", NULL, NULL, NULL, NULL) && !iptvvod_id_ler("tt0133093", NULL, NULL, NULL, NULL) &&
            !iptvvod_id_ler("qv:0123456789ABCDEF.m", NULL, NULL, NULL, NULL) &&
            !iptvvod_id_ler("qv:0123456789abcdef.e1x0", NULL, NULL, NULL, NULL), "malformed ids refused");
  }

  // ---- library ----
  carregar();
  CONFERE(iptvvod_ativo(), "library loaded");
  CONFERE(iptvvod_n_itens('m') == 3, "5 movie copies -> 3 movies (Matrix x2 by tmdb, Inception x2 by title+year)");
  CONFERE(iptvvod_n_itens('s') == 2, "3 series copies -> 2 series");
  CONFERE(nf == 2 && fils[0].n == 3 && fils[1].n == 2, "two Home rows: recent movies, recent series");
  {
    const CatItem *m = porTitulo("The Matrix"), *in = porTitulo("Inception"), *ind = porTitulo("Local Indie Film");
    const CatItem *bb = porTitulo("Breaking Bad");
    CONFERE(m && in && ind && bb, "clean titles in the rows");
    CONFERE(nf && ni >= 3 && !strcmp(itens[0].titulo, "Inception") && !strcmp(itens[2].titulo, "Local Indie Film"),
            "movies ordered by most recently added copy");
    CONFERE(m && iptvvod_n_copias(m->imdb) == 2 && in && iptvvod_n_copias(in->imdb) == 2 &&
            ind && iptvvod_n_copias(ind->imdb) == 1, "copies per item: 2, 2, 1");
    CONFERE(m && !strncmp(m->imdb, "qv:", 3) && !strcmp(m->tipo, "movie") && strstr(m->poster, "matrix.jpg"),
            "item id is a local qv: id, never a provider or TMDB id");
    CONFERE(bb && !strcmp(bb->tipo, "series") && iptvvod_n_copias(bb->imdb) == 2, "series grouped across providers");

    if (m) {
      CatItem f = *m;
      Stream *l = NULL;
      int n;
      CONFERE(iptvvod_ficha(&f, NULL, 0) == 0 && strstr(f.sinopse, "Plot of") && f.nElenco == 2 &&
              strstr(f.meta, "136 min") && f.backdrop[0], "movie details from the provider");
      n = fontes(m->imdb, &l);
      CONFERE(n == 2 && temUrl(l, n, "/movie/qrtuser/") && temUrl(l, n, "/movie/qrtuser2/"),
              "The Matrix: one source per provider");
      CONFERE(n == 2 && strstr(l[0].provedor, "IPTV · QRT Xtream") && l[0].fileIdx == -1, "source attribution");
      free(l);
    }
    if (ind) {
      Stream *l = NULL;
      int n = fontes(ind->imdb, &l);
      CONFERE(n == 1 && temUrl(l, n, "/movie/qrtuser/") && temUrl(l, n, "/1002.mp4"), "provider-local movie: its one source");
      free(l);
    }
    if (bb) {
      CatItem f = *bb;
      CatEp eps[16];
      Stream *l = NULL;
      int n = iptvvod_ficha(&f, eps, 16);
      CONFERE(n == 3 && eps[0].temporada == 1 && eps[0].episodio == 1 && eps[2].temporada == 2 &&
              f.nTemporadas == 2 && !strcmp(f.meta, "%d temporadas") == 0, "series: 3 merged episodes, 2 seasons");
      CONFERE(n == 3 && !strncmp(eps[0].vid, "qv:", 3) && strstr(eps[0].vid, ".e1x1") && !strcmp(eps[0].nome, "Pilot"),
              "episode ids are local and carry season/episode");
      n = fontes(eps[0].vid, &l);
      CONFERE(n == 2 && temUrl(l, n, "/series/qrtuser/") && temUrl(l, n, "/40001.mp4") &&
              temUrl(l, n, "/series/qrtuser2/") && temUrl(l, n, "/50001.mp4"), "S1E1: one source per provider, right episode ids");
      free(l);
      n = fontes(eps[2].vid, &l);
      CONFERE(n == 2 && temUrl(l, n, "/40011.mp4") && temUrl(l, n, "/50011.mp4"), "S2E1 maps to each provider's own id");
      free(l);
    }
    CONFERE(fontes("qv:ffffffffffffffff.m", &(Stream *){0}) < 0, "unknown id: no sources (answered as failed)");
  }
  {
    char *c = dados_ler("iptvvod-p1.tsv");
    CONFERE(c && strstr(c, "Breaking Bad") && !strstr(c, "qrtpass") && !strstr(c, "qrtuser"),
            "cache written, without credentials");
    free(c);
  }

  // ---- profile isolation ----
  perfil = 2;
  carregar();
  CONFERE(!iptvvod_ativo() && nf == 0 && iptvvod_n_itens('m') == 0, "profile 2 (no providers): nothing from profile 1");
  perfil = 1;
  carregar();
  CONFERE(iptvvod_n_itens('m') == 3, "back to profile 1: its library again");
  return falhas ? 1 : 0;
}
