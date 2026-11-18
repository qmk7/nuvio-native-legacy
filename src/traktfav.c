#include "traktfav.h"
#include "trakt.h"
#include "traktdup.h"
#include "traktid.h"
#include "js.h"
#include "rede.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FAV_MAX 1000
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static char (*favs)[24]; static int nFavs, carregado, fioVivo;
static unsigned long long credLida;

int traktfav_parse(const char *corpo, char (*ids)[24], int max) {
  const char *p = corpo ? strchr(corpo, '[') : NULL;
  int n = 0;
  if (!p) return -1;
  for (p++; *p && n < max; ) {
    const char *f, *o, *of;
    TraktId t;
    char imdb[24] = "", canon[48];
    long tmdb = 0;
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{' || !(f = js_fim(p))) break;
    // { "movie": {"ids":{...}} } or { "show": ... }
    o = strstr(p, "\"ids\"");
    if (o && o < f && (o = strchr(o, '{')) && o < f && (of = js_fim(o))) {
      js_texto(o, of, "imdb", imdb, sizeof imdb);
      tmdb = (long)js_num(o, of, "tmdb", 0);
    }
    if (imdb[0]) snprintf(canon, sizeof canon, "%s", imdb);
    else if (tmdb > 0) snprintf(canon, sizeof canon, "tmdb:%ld", tmdb);
    else canon[0] = 0;
    if (canon[0] && traktid_resolver(canon, &t) >= TID_IMDB) snprintf(ids[n++], 24, "%s", canon);
    p = f + 1;
  }
  return n;
}
static void *fioCarregar(void *u) {
  const char *cab[4];
  char aut[200], chave[140], url[200];
  char (*v)[24] = calloc(FAV_MAX, 24);
  int n = 0, ok = 1;
  (void)u;
  if (v && trakt_cabecalhos(cab, aut, sizeof aut, chave, sizeof chave)) {
    static const char *const tipos[] = { "movies", "shows" };
    for (int k = 0; k < 2; k++) {
      int st = 0, m;
      char caminho[48], *r;
      snprintf(caminho, sizeof caminho, "/sync/favorites/%s?limit=1000", tipos[k]);
      trakt_url(url, sizeof url, caminho);
      r = rede_baixar_st(url, 20, cab, &st);
      if (!r || st < 200 || st >= 300) { ok = 0; free(r); break; }
      m = traktfav_parse(r, v + n, FAV_MAX - n);
      free(r);
      if (m > 0) n += m;
    }
  } else ok = 0;
  pthread_mutex_lock(&trava);
  if (ok) { free(favs); favs = v; v = NULL; nFavs = n; carregado = 1; credLida = trakt_credencial_geracao(); }
  fioVivo = 0;
  pthread_mutex_unlock(&trava);
  free(v);
  printf("[trakt] favoritos: %s (%d)\n", ok ? "lidos" : "falhou", n);
  fflush(stdout);
  return NULL;
}
static void carregar(void) {
  pthread_t t;
  if (fioVivo || !trakt_ativo()) return;
  fioVivo = 1;
  if (pthread_create(&t, NULL, fioCarregar, NULL) != 0) { fioVivo = 0; return; }
  pthread_detach(t);
}
int traktfav_estado(const char *id) {
  TraktId t;
  char c[48];
  int r = -1;
  if (traktid_resolver(id, &t) < TID_IMDB) return 0;   // a local id is never a Trakt favourite
  t.temporada = -1; t.episodio = 0;
  traktid_canonico(&t, c, sizeof c);
  pthread_mutex_lock(&trava);
  if (carregado && credLida != trakt_credencial_geracao()) carregado = 0;   // another account
  if (carregado) { r = 0; for (int i = 0; i < nFavs && !r; i++) r = !strcmp(favs[i], c); }
  else carregar();
  pthread_mutex_unlock(&trava);
  return r;
}
typedef struct { char canon[48], ids[64]; int filme, adicionar; } Pedido;
static void *fioAlternar(void *u) {
  Pedido *p = u;
  const char *cab[4];
  char aut[200], chave[140], url[200], corpo[200];
  int st = 0;
  char *r = NULL;
  if (trakt_cabecalhos(cab, aut, sizeof aut, chave, sizeof chave)) {
    snprintf(corpo, sizeof corpo, "{\"%s\":[{\"ids\":{%s}}]}", p->filme ? "movies" : "shows", p->ids);
    trakt_url(url, sizeof url, p->adicionar ? "/sync/favorites" : "/sync/favorites/remove");
    r = trakt_postar(url, cab, corpo, &st);
  }
  free(r);
  pthread_mutex_lock(&trava);
  if (st >= 200 && st < 300 && carregado) {
    int i;
    for (i = 0; i < nFavs && strcmp(favs[i], p->canon); i++) {}
    if (p->adicionar && i == nFavs && favs && nFavs < FAV_MAX) snprintf(favs[nFavs++], 24, "%s", p->canon);
    if (!p->adicionar && i < nFavs) { memmove(favs[i], favs[i + 1], 24 * (size_t)(nFavs - i - 1)); nFavs--; }
  }
  fioVivo = 0;
  pthread_mutex_unlock(&trava);
  printf("[trakt] favorito %s %s -> HTTP %d\n", p->adicionar ? "add" : "del", p->canon, st);
  fflush(stdout);
  free(p);
  return NULL;
}
int traktfav_alternar(const char *id, const char *tipo) {
  TraktId t;
  Pedido *p;
  pthread_t th;
  int atual = traktfav_estado(id);
  if (!trakt_ativo() || traktid_resolver(id, &t) < TID_IMDB) return 0;
  if (!(p = calloc(1, sizeof *p))) return 0;
  t.temporada = -1; t.episodio = 0;
  traktid_canonico(&t, p->canon, sizeof p->canon);
  traktid_ids_json(&t, p->ids, sizeof p->ids);
  p->filme = !(tipo && (!strcmp(tipo, "series") || !strcmp(tipo, "show")));
  p->adicionar = atual != 1;
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); free(p); return 0; }
  fioVivo = 1;
  pthread_mutex_unlock(&trava);
  if (pthread_create(&th, NULL, fioAlternar, p) != 0) { free(p); pthread_mutex_lock(&trava); fioVivo = 0; pthread_mutex_unlock(&trava); return 0; }
  pthread_detach(th);
  return 1;
}
void traktfav_esquecer(void) { pthread_mutex_lock(&trava); carregado = 0; nFavs = 0; pthread_mutex_unlock(&trava); }
int traktfav_teste_esperar(int ms) {
  for (int i = 0; i < ms / 10; i++) {
    int v;
    pthread_mutex_lock(&trava); v = fioVivo; pthread_mutex_unlock(&trava);
    if (!v) return 1;
    usleep(10000);
  }
  return 0;
}
