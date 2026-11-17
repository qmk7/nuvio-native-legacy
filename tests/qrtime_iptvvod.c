// IPTV library (src/iptvvod.c) over real HTTP against tests/qrtime_iptv_mock.py
// with two Xtream accounts whose catalogs overlap.
//
//   qrtime_iptvvod <data dir> online    full checks (fixture running)
//   qrtime_iptvvod <data dir> offline   fixture down, stale cache: still there
#include "iptvvod.h"
#include "m3uvod.h"
#include "stvod.h"
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
static const char *chaveTeste(void) { return "QRTKEY"; }
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static CatItem itens[64];
static char serieId[64];   // Breaking Bad's local id, kept across profile switches
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
  if (argc < 3) { fprintf(stderr, "usage: %s <data dir> online|offline [base url]\n", argv[0]); return 2; }
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

  // ---- M3U VOD and Stalker episode rules, no network ----
  {
    char serie[160];
    int t = 0, e = 0, nums[8];
    CONFERE(m3uvod_url_vod("http://h/x/movie.MKV?token=1") && m3uvod_url_vod("http://h/movie/u/p/1.ts") &&
            !m3uvod_url_vod("http://h/live/1.m3u8") && !m3uvod_url_vod("http://h/ch/1.ts") && !m3uvod_url_vod("rtmp://h/a.mp4"),
            "M3U: video files and /movie/ paths are VOD, live streams are not");
    CONFERE(m3uvod_episodio("Breaking Bad S02E05", serie, sizeof serie, &t, &e) && !strcmp(serie, "Breaking Bad") && t == 2 && e == 5,
            "M3U: S02E05 marker");
    CONFERE(m3uvod_episodio("The Office - 3x12", serie, sizeof serie, &t, &e) && !strcmp(serie, "The Office") && t == 3 && e == 12,
            "M3U: 3x12 marker");
    CONFERE(!m3uvod_episodio("Room 1408", serie, sizeof serie, &t, &e) && !m3uvod_episodio("Seven", serie, sizeof serie, &t, &e),
            "M3U: no false episodes");
    CONFERE(stvod_episodios("3,1,2,2", nums, 8) == 3 && nums[0] == 1 && nums[2] == 3, "Stalker: episode numbers sorted, unique");
  }

  // ---- library ----
  carregar();
  CONFERE(iptvvod_ativo(), "library loaded");
  CONFERE(iptvvod_n_itens('m') == 5, "9 movie copies (Xtream x2, Stalker, M3U) -> 5 movies");
  CONFERE(iptvvod_n_itens('s') == 2, "5 series copies -> 2 series");
  CONFERE(nf == 2 && fils[0].n == 5 && fils[1].n == 2, "two Home rows: recent movies, recent series");
  {
    const CatItem *m = porTitulo("The Matrix"), *in = porTitulo("Inception"), *ind = porTitulo("Local Indie Film");
    const CatItem *bb = porTitulo("Breaking Bad");
    CONFERE(m && in && ind && bb, "clean titles in the rows");
    CONFERE(nf && ni >= 3 && !strcmp(itens[0].titulo, "Inception") && !strcmp(itens[2].titulo, "Local Indie Film"),
            "movies ordered by most recently added copy");
    CONFERE(m && iptvvod_n_copias(m->imdb) == 4 && in && iptvvod_n_copias(in->imdb) == 2 &&
            ind && iptvvod_n_copias(ind->imdb) == 1, "The Matrix: 4 copies (2 Xtream by tmdb, Stalker and M3U by title+year)");
    CONFERE(porTitulo("Stalker Only Movie") && porTitulo("M3U Only Movie"), "provider-only movies stay their own items");
    CONFERE(m && !strncmp(m->imdb, "qv:", 3) && !strcmp(m->tipo, "movie") && strstr(m->poster, "matrix.jpg"),
            "item id is a local qv: id, never a provider or TMDB id");
    CONFERE(bb && !strcmp(bb->tipo, "series") && iptvvod_n_copias(bb->imdb) == 4,
            "Breaking Bad: Xtream x2, Stalker (title+year), M3U (unique title, no year)");

    if (m) {
      CatItem f = *m;
      Stream *l = NULL;
      int n;
      CONFERE(iptvvod_ficha(&f, NULL, 0) == 0 && strstr(f.sinopse, "Plot of") && f.nElenco == 2 &&
              strstr(f.meta, "136 min") && f.backdrop[0], "movie details from the provider");
      n = fontes(m->imdb, &l);
      CONFERE(n == 4 && temUrl(l, n, "/movie/qrtuser/") && temUrl(l, n, "/movie/qrtuser2/") &&
              temUrl(l, n, "/stvod/7001.mp4?series=0") && temUrl(l, n, "/m3uvod/matrix.mp4"),
              "The Matrix: one source per provider (Stalker link asked at play time)");
      CONFERE(n >= 2 && strstr(l[0].provedor, "IPTV · QRT") && l[0].fileIdx == -1, "source attribution");
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
      CONFERE(n == 4 && temUrl(l, n, "/series/qrtuser/") && temUrl(l, n, "/40001.mp4") &&
              temUrl(l, n, "/series/qrtuser2/") && temUrl(l, n, "/50001.mp4") &&
              temUrl(l, n, "/stvod/7003.mp4?series=1") && temUrl(l, n, "/m3uvod/bb101.mp4"),
              "S1E1: four providers, each with its own episode reference");
      free(l);
      n = fontes(eps[2].vid, &l);
      CONFERE(n == 2 && temUrl(l, n, "/40011.mp4") && temUrl(l, n, "/50011.mp4"), "S2E1 maps to each provider's own id");
      free(l);
    }
    CONFERE(fontes("qv:ffffffffffffffff.m", &(Stream *){0}) < 0, "unknown id: no sources (answered as failed)");
  }
  {
    char *c = dados_ler("iptvvod-p1.tsv");
    CONFERE(c && strstr(c, "Breaking Bad") && !strstr(c, "qrtpass") && !strstr(c, "qrtuser") &&
            !strstr(c, "/media/") && !strstr(c, "m3uvod") && !strstr(c, "Stalker Only"),
            "cache: Xtream copies only, no credentials, no Stalker cmd or M3U URL");
    free(c);
  }

  // ---- addon titles: real ids resolved through TMDB (fixture) ----
  if (argc > 3) {
    char base[200];
    Stream *l = NULL;
    int n;
    snprintf(base, sizeof base, "%s/3", argv[3]);
    iptvvod_teste_tmdb_base(base);
    CONFERE(iptvvod_origem("tt0133093", "movie", NULL, NULL, NULL, NULL, &l) < 0 ||
            (free(l), l = NULL, 1), "no TMDB key: nothing is asked");
    iptvvod_definir_chave_tmdb(chaveTeste);
    n = iptvvod_origem("tt0133093", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 4 && temUrl(l, n, "/movie/qrtuser/") && temUrl(l, n, "/movie/qrtuser2/") &&
            temUrl(l, n, "/stvod/7001") && temUrl(l, n, "/m3uvod/matrix.mp4"),
            "addon title tt0133093 (The Matrix): all four providers' copies");
    free(l); l = NULL;
    n = iptvvod_origem("tt1375666", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 2 && temUrl(l, n, "/1003.mp4") && temUrl(l, n, "/2002.mkv"),
            "Inception: hinted copy and title+year copy");
    free(l); l = NULL;
    n = iptvvod_origem("tmdb:27205", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 2, "tmdb:27205 resolves the same way");
    free(l); l = NULL;
    n = iptvvod_origem("tt0903747:2:1", "series", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 2 && temUrl(l, n, "/40011.mp4") && temUrl(l, n, "/50011.mp4"),
            "addon episode tt0903747:2:1 -> each provider's own S2E1");
    free(l); l = NULL;
    n = iptvvod_origem("tt9999999", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 0, "a provider TMDB hint whose title disagrees with TMDB is refused");
    free(l); l = NULL;
    n = iptvvod_origem("tt0000404", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 0, "an addon title no provider has: no IPTV source");
    n = iptvvod_origem("qv:0123456789abcdef.m", "movie", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 0, "a local id is never resolved through TMDB");
    n = iptvvod_origem("kitsu:1", "series", NULL, NULL, NULL, NULL, &l);
    CONFERE(n == 0, "other addon namespaces are not guessed");
  }

  // ---- browsing by category, page by page ----
  {
    char cats[16][96];
    CatItem r[IPTVVOD_PAGINA];
    int total = 0, n, nc = iptvvod_categorias('m', cats, 16), tem = 0;
    for (int i = 0; i < nc; i++) tem += !strcmp(cats[i], "QRT Action") + !strcmp(cats[i], "ST Films") + !strcmp(cats[i], "VOD Movies");
    CONFERE(nc >= 4 && tem == 3, "movie categories from every provider");
    n = iptvvod_pagina('m', "", 1, r, IPTVVOD_PAGINA, &total);
    CONFERE(n == 5 && total == 5 && !strcmp(r[0].titulo, "Inception"), "All movies: one page, newest first");
    n = iptvvod_pagina('m', "QRT Action", 1, r, IPTVVOD_PAGINA, &total);
    CONFERE(n == 2 && total == 2, "a category: its items only (The Matrix, Inception)");
    n = iptvvod_pagina('m', "ST Films", 1, r, IPTVVOD_PAGINA, &total);
    CONFERE(n == 2 && total == 2, "a title is in every provider's category (The Matrix is also in ST Films)");
    CONFERE(iptvvod_pagina('m', "", 2, r, IPTVVOD_PAGINA, &total) == 0 && total == 5, "past the end: empty page, same total");
    CONFERE(iptvvod_pagina('s', "", 1, r, IPTVVOD_PAGINA, &total) == 2, "series browse");
  }

  // ---- search (local, no network) ----
  {
    CatItem r[8];
    int n = iptvvod_buscar("MATRIX", 'm', r, 8);
    CONFERE(n == 1 && !strcmp(r[0].titulo, "The Matrix") && !strncmp(r[0].imdb, "qv:", 3), "search: case-insensitive, one item for 4 copies");
    n = iptvvod_buscar("in", 'm', r, 8);
    CONFERE(n >= 2 && !strcmp(r[0].titulo, "Inception"), "search: prefix matches before inner matches");
    CONFERE(iptvvod_buscar("breaking", 's', r, 8) == 1 && iptvvod_buscar("breaking", 'm', r, 8) == 0, "search: by kind");
    CONFERE(iptvvod_buscar("stalker only", 'm', r, 8) == 1 && iptvvod_buscar("m3u only", 'm', r, 8) == 1,
            "search: provider-only titles are found");
    CONFERE(iptvvod_buscar("zzz", 'm', r, 8) == 0 && iptvvod_buscar("", 'm', r, 8) == 0, "search: nothing for no match");
  }

  // ---- progress, continue watching, watched/unwatched ----
  {
    char mid[64] = "", sid[64] = "";
    const CatItem *m = porTitulo("The Matrix"), *bb = porTitulo("Breaking Bad"), *cw;
    int pct = 0, t = 0, e = 0;
    if (m) snprintf(mid, sizeof mid, "%s", m->imdb);
    if (bb) snprintf(sid, sizeof sid, "%s", bb->imdb);
    snprintf(serieId, sizeof serieId, "%s", sid);
    CONFERE(!iptvvod_progresso_gravar("tt0133093", 0, 0, 10, 100), "progress: only local qv ids are kept");
    CONFERE(iptvvod_progresso_gravar(mid, 0, 0, 600, 8160), "movie position recorded");
    sleep(1);
    CONFERE(iptvvod_progresso_gravar(sid, 1, 2, 1000, 2820), "episode position recorded");
    nf = iptvvod_fileiras_copiar(itens, 64, fils, 4, &ni);
    CONFERE(nf == 3 && !strcmp(fils[0].chave, "iptvvod_continuar") && fils[0].n == 2, "Continue Watching row first, 2 items");
    cw = &itens[0];
    CONFERE(!strcmp(cw->imdb, sid) && cw->temporada == 1 && cw->episodio == 2 && cw->progresso == 35,
            "most recent first: Breaking Bad at S1E2, 35%");
    CONFERE(!strcmp(itens[1].imdb, mid) && itens[1].progresso == 7, "The Matrix at 7%");
    CONFERE(iptvvod_progresso_gravar(mid, 0, 0, 8000, 8160) && iptvvod_progresso_de(mid, &pct, NULL, NULL) && pct == 100,
            "98% counts as watched");
    nf = iptvvod_fileiras_copiar(itens, 64, fils, 4, &ni);
    CONFERE(fils[0].n == 1 && !strcmp(itens[0].imdb, sid), "a watched movie leaves Continue Watching");
    { const CatItem *mm = porTitulo("The Matrix");
      int achou = 0;
      for (int i = fils[0].n; i < ni; i++) if (!strcmp(itens[i].imdb, mid)) achou = itens[i].progresso == 100;
      CONFERE(mm && achou, "its card elsewhere shows it watched"); }
    CONFERE(iptvvod_progresso_gravar(mid, 0, 0, 0, 8160) && !iptvvod_progresso_de(mid, &pct, NULL, NULL),
            "marked unwatched: its records are gone");
    CONFERE(iptvvod_progresso_de(sid, &pct, &t, &e) && t == 1 && e == 2, "the series keeps its episode");
    { char *f = dados_ler("iptvvod-prog-p1.tsv");
      CONFERE(f && strstr(f, sid) && !strstr(f, "tt0") && !strstr(f, "qrt"), "stored per profile, local ids only");
      free(f); }
  }

  // ---- profile isolation ----
  perfil = 2;
  carregar();
  CONFERE(!iptvvod_ativo() && nf == 0 && iptvvod_n_itens('m') == 0, "profile 2 (no providers): nothing from profile 1");
  { int pct = 0;
    CONFERE(serieId[0] && !iptvvod_progresso_de(serieId, &pct, NULL, NULL),
            "profile 2 sees none of profile 1's progress"); }
  perfil = 1;
  carregar();
  CONFERE(iptvvod_n_itens('m') == 5, "back to profile 1: its library again");
  { int pct = 0, t = 0, e = 0;
    CONFERE(iptvvod_progresso_de(serieId, &pct, &t, &e) && t == 1 && e == 2, "and its progress again"); }
  return falhas ? 1 : 0;
}
