#include "xtvod.h"
#include "xtream.h"
#include "js.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// get_vod_streams of a big panel is tens of MB; the info calls are small.
#define XTVOD_PRAZO_LISTA 120
#define XTVOD_PRAZO_INFO 30

// A field some panels send as text ("5") and others as a number (5).
static int campo(const char *ini, const char *fim, const char *chave, char *dst, unsigned tam) {
  double v;
  dst[0] = 0;
  if (js_texto(ini, fim, chave, dst, tam) && dst[0]) return 1;
  dst[0] = 0;
  v = js_num(ini, fim, chave, -1.0);
  if (v < 0.0) return 0;
  snprintf(dst, tam, "%.0f", v);
  return 1;
}
static double numero(const char *ini, const char *fim, const char *chave) {
  char t[32];
  double v;
  t[0] = 0;
  if (js_texto(ini, fim, chave, t, sizeof t) && t[0]) return atof(t);
  v = js_num(ini, fim, chave, 0.0);
  return v;
}
// Only http(s) artwork, trimmed (panels pad URLs and send "null" or paths).
static void arte(const char *ini, const char *fim, const char *chave, char *dst, unsigned tam) {
  char t[512], *s, *e;
  dst[0] = 0;
  t[0] = 0;
  if (!js_texto(ini, fim, chave, t, sizeof t)) return;
  for (s = t; *s && (unsigned char)*s <= ' '; s++) {}
  e = s + strlen(s);
  while (e > s && (unsigned char)e[-1] <= ' ') *--e = 0;
  if (strncmp(s, "http://", 7) && strncmp(s, "https://", 8)) return;
  snprintf(dst, tam, "%s", s);
}
static int idSeguro(const char *s) {
  if (!s || !*s || strlen(s) >= XTVOD_SID) return 0;
  for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return 0;
  return 1;
}
static int anoDeData(const char *s) {
  int a;
  while (s && *s && !isdigit((unsigned char)*s)) s++;
  if (!s || !*s) return 0;
  a = atoi(s);
  return a >= 1888 && a <= 2100 ? a : 0;
}
int xtvod_ano_do_nome(const char *nome) {
  // The LAST 4-digit year-like token: "Blade Runner 2049 (2017)" -> 2017.
  int melhor = 0;
  if (!nome) return 0;
  for (const char *p = nome; *p; p++) {
    if (isdigit((unsigned char)p[0]) && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) &&
        isdigit((unsigned char)p[3]) && !isdigit((unsigned char)p[4]) &&
        (p == nome || !isdigit((unsigned char)p[-1]))) {
      int a = atoi(p);
      int entre = p > nome && (p[-1] == '(' || p[-1] == ' ' || p[-1] == '-' || p[-1] == '[');
      if (a >= 1900 && a <= 2100 && entre) melhor = a;
    }
  }
  return melhor;
}
static int anoDe(const char *ini, const char *fim, const char *nome) {
  char t[40];
  int a;
  if (campo(ini, fim, "year", t, sizeof t) && (a = anoDeData(t)) > 0) return a;
  t[0] = 0;
  if ((js_texto(ini, fim, "releaseDate", t, sizeof t) || js_texto(ini, fim, "releasedate", t, sizeof t) ||
       js_texto(ini, fim, "release_date", t, sizeof t)) && (a = anoDeData(t)) > 0) return a;
  return xtvod_ano_do_nome(nome);
}
static long tmdbDe(const char *ini, const char *fim) {
  char t[24];
  long v;
  if (!campo(ini, fim, "tmdb", t, sizeof t) && !campo(ini, fim, "tmdb_id", t, sizeof t)) return 0;
  for (const char *c = t; *c; c++) if (!isdigit((unsigned char)*c)) return 0;
  v = atol(t);
  return v > 0 && v < 100000000L ? v : 0;
}
static int nota10De(const char *ini, const char *fim) {
  double r = numero(ini, fim, "rating");
  if (r <= 0.0) r = numero(ini, fim, "rating_5based") * 2.0;
  if (r <= 0.0 || r > 10.0) return 0;
  return (int)(r * 10.0 + 0.5);
}
static void extDe(const char *ini, const char *fim, char *dst, unsigned tam) {
  char t[16];
  int ok = 1;
  dst[0] = 0;
  t[0] = 0;
  if (!js_texto(ini, fim, "container_extension", t, sizeof t) || !t[0]) return;
  for (char *c = t; *c; c++) { *c = (char)tolower((unsigned char)*c); if (!isalnum((unsigned char)*c)) ok = 0; }
  if (ok && strlen(t) < tam) snprintf(dst, tam, "%s", t);
}

// Value of `chave` among the depth-1 keys of the object at `obj` ('{').
// Points at the value's first character, or NULL.
static const char *valorRaiz(const char *obj, const char *fim, const char *chave) {
  size_t n = strlen(chave);
  int prof = 0;
  if (!obj) return NULL;
  for (const char *p = obj; *p && (!fim || p < fim); p++) {
    if (*p == '"') {
      const char *ini = p + 1, *q = ini;
      while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
      if (!*q) return NULL;
      if (prof == 1 && (size_t)(q - ini) == n && !strncmp(ini, chave, n)) {
        const char *v = q + 1;
        while (*v && (unsigned char)*v <= ' ') v++;
        if (*v == ':') {
          v++;
          while (*v && (unsigned char)*v <= ' ') v++;
          return v;
        }
      }
      p = q;
    } else if (*p == '{' || *p == '[') prof++;
    else if (*p == '}' || *p == ']') { if (--prof <= 0) return NULL; }
  }
  return NULL;
}
static const char *pularBranco(const char *p) {
  while (p && *p && (unsigned char)*p <= ' ') p++;
  return p;
}
static const char *pulaBom(const char *c) {
  if (c && (unsigned char)c[0] == 0xEF && (unsigned char)c[1] == 0xBB && (unsigned char)c[2] == 0xBF) c += 3;
  return pularBranco(c);
}

int xtvod_parse_categorias(const char *json, void (*cat)(const char *, const char *, void *), void *u) {
  const char *p = json ? js_raiz_array(json) : NULL;
  int n = 0;
  while (p && *p) {
    const char *fim = js_fim(p);
    char id[XTVOD_SID], nome[96];
    if (!fim) break;
    nome[0] = 0;
    if (campo(p, fim, "category_id", id, sizeof id) && js_texto(p, fim, "category_name", nome, sizeof nome)) {
      if (cat) cat(id, nome, u);
      n++;
    }
    p = js_prox(fim);
  }
  return n;
}

int xtvod_parse_lista(const char *json, int serie,
                      const char *(*nomeCat)(const char *, void *), void *uCat,
                      XtVodCb cb, void *u) {
  const char *p = json ? js_raiz_array(json) : NULL;
  int n = 0;
  while (p && *p) {
    const char *fim = js_fim(p);
    XtVodItem it;
    char cid[XTVOD_SID], t[32];
    if (!fim) break;
    memset(&it, 0, sizeof it);
    if (campo(p, fim, serie ? "series_id" : "stream_id", it.sid, sizeof it.sid) && idSeguro(it.sid) &&
        js_texto(p, fim, "name", it.nome, sizeof it.nome) && it.nome[0]) {
      arte(p, fim, serie ? "cover" : "stream_icon", it.poster, sizeof it.poster);
      if (!it.poster[0]) arte(p, fim, "stream_icon", it.poster, sizeof it.poster);
      if (campo(p, fim, "category_id", cid, sizeof cid) && nomeCat) {
        const char *nm = nomeCat(cid, uCat);
        if (nm) snprintf(it.categoria, sizeof it.categoria, "%s", nm);
      }
      if (!serie) extDe(p, fim, it.ext, sizeof it.ext);
      it.tmdbProv = tmdbDe(p, fim);
      it.ano = anoDe(p, fim, it.nome);
      it.nota10 = nota10De(p, fim);
      if (campo(p, fim, serie ? "last_modified" : "added", t, sizeof t)) it.adicionado = atoll(t);
      if (!it.adicionado && campo(p, fim, "added", t, sizeof t)) it.adicionado = atoll(t);
      n++;
      if (cb && !cb(&it, u)) break;
    }
    p = js_prox(fim);
  }
  return n;
}

// The "info" object's common fields (movie and series).
static void fichaInfo(const char *ini, const char *fim, XtVodFicha *f) {
  char t[32];
  const char *bd;
  if (!f->nome[0]) js_texto(ini, fim, "name", f->nome, sizeof f->nome);
  if (!f->nome[0]) js_texto(ini, fim, "title", f->nome, sizeof f->nome);
  js_texto(ini, fim, "plot", f->sinopse, sizeof f->sinopse);
  if (!f->sinopse[0]) js_texto(ini, fim, "description", f->sinopse, sizeof f->sinopse);
  arte(ini, fim, "movie_image", f->poster, sizeof f->poster);
  if (!f->poster[0]) arte(ini, fim, "cover_big", f->poster, sizeof f->poster);
  if (!f->poster[0]) arte(ini, fim, "cover", f->poster, sizeof f->poster);
  // backdrop_path is an ARRAY of URLs on most panels, a string on a few.
  bd = js_array(ini, fim, "backdrop_path");
  if (bd && *bd == '"') {
    char u[512];
    u[0] = 0;
    js_cadeia(bd, u, sizeof u);
    if (!strncmp(u, "http://", 7) || !strncmp(u, "https://", 8)) snprintf(f->fundo, sizeof f->fundo, "%s", u);
  } else arte(ini, fim, "backdrop_path", f->fundo, sizeof f->fundo);
  js_texto(ini, fim, "genre", f->genero, sizeof f->genero);
  js_texto(ini, fim, "cast", f->elenco, sizeof f->elenco);
  if (!f->elenco[0]) js_texto(ini, fim, "actors", f->elenco, sizeof f->elenco);
  js_texto(ini, fim, "director", f->direcao, sizeof f->direcao);
  if (!f->tmdbProv) f->tmdbProv = tmdbDe(ini, fim);
  if (!f->ano) f->ano = anoDe(ini, fim, f->nome);
  if (!f->nota10) f->nota10 = nota10De(ini, fim);
  if (campo(ini, fim, "duration_secs", t, sizeof t) && atoi(t) > 0) f->duracaoMin = (atoi(t) + 59) / 60;
  else if (campo(ini, fim, "episode_run_time", t, sizeof t) && atoi(t) > 0) f->duracaoMin = atoi(t);
}
static const char *objetoRaiz(const char *corpo, const char *chave, const char **fim) {
  const char *raiz = pulaBom(corpo), *v;
  if (!raiz || *raiz != '{') return NULL;
  v = valorRaiz(raiz, NULL, chave);
  if (!v || (*v != '{' && *v != '[')) return NULL;
  *fim = js_fim(v);
  return *fim ? v : NULL;
}

int xtvod_parse_filme(const char *json, XtVodFicha *f) {
  const char *info, *fimInfo = NULL, *md, *fimMd = NULL;
  if (!json || !f) return 0;
  memset(f, 0, sizeof *f);
  md = objetoRaiz(json, "movie_data", &fimMd);
  info = objetoRaiz(json, "info", &fimInfo);
  if (!md && !info) return 0;
  if (md && *md == '{') {
    campo(md, fimMd, "stream_id", f->sid, sizeof f->sid);
    if (!idSeguro(f->sid)) f->sid[0] = 0;
    js_texto(md, fimMd, "name", f->nome, sizeof f->nome);
    extDe(md, fimMd, f->ext, sizeof f->ext);
  }
  if (info && *info == '{') fichaInfo(info, fimInfo, f);
  return f->nome[0] || f->sid[0];
}

static int compararEp(const void *a, const void *b) {
  const XtVodEp *x = a, *y = b;
  if (x->temporada != y->temporada) return x->temporada < y->temporada ? -1 : 1;
  return (x->episodio > y->episodio) - (x->episodio < y->episodio);
}
static int lerEp(const char *p, const char *fim, int temporadaChave, XtVodEp *e) {
  char t[24];
  const char *info, *fimInfo;
  memset(e, 0, sizeof *e);
  if (!campo(p, fim, "id", e->sid, sizeof e->sid) || !idSeguro(e->sid)) return 0;
  e->episodio = campo(p, fim, "episode_num", t, sizeof t) ? atoi(t) : 0;
  e->temporada = campo(p, fim, "season", t, sizeof t) ? atoi(t) : temporadaChave;
  if (e->temporada <= 0) e->temporada = temporadaChave;
  js_texto(p, fim, "title", e->nome, sizeof e->nome);
  extDe(p, fim, e->ext, sizeof e->ext);
  info = valorRaiz(p, fim, "info");
  if (info && *info == '{' && (fimInfo = js_fim(info)) != NULL) {
    js_texto(info, fimInfo, "plot", e->sinopse, sizeof e->sinopse);
    arte(info, fimInfo, "movie_image", e->thumb, sizeof e->thumb);
    if (campo(info, fimInfo, "duration_secs", t, sizeof t) && atoi(t) > 0) e->duracaoMin = (atoi(t) + 59) / 60;
  }
  return e->episodio > 0 || e->nome[0];
}
static int lerArrayEps(const char *arr, int temporadaChave, XtVodEp *eps, int n, int max) {
  const char *p = pularBranco(arr + 1);
  while (p && *p == '{' && n < max) {
    const char *fim = js_fim(p);
    if (!fim) break;
    if (lerEp(p, fim, temporadaChave, &eps[n])) n++;
    p = pularBranco(fim + 1);
    if (p && *p == ',') p = pularBranco(p + 1);
  }
  return n;
}
int xtvod_parse_serie(const char *json, XtVodFicha *f, XtVodEp *eps, int max) {
  const char *info, *fimInfo = NULL, *ep, *fimEp = NULL;
  int n = 0;
  if (!json || !f) return -1;
  memset(f, 0, sizeof *f);
  info = objetoRaiz(json, "info", &fimInfo);
  ep = objetoRaiz(json, "episodes", &fimEp);
  if (!info && !ep) return -1;
  if (info && *info == '{') fichaInfo(info, fimInfo, f);
  if (ep && eps && max > 0) {
    if (*ep == '{') {
      // {"1":[...], "2":[...]}: the key is the season.
      const char *p = pularBranco(ep + 1);
      while (p && *p == '"' && n < max) {
        char chave[16];
        const char *v;
        chave[0] = 0;
        js_cadeia(p, chave, sizeof chave);
        v = p + 1;
        while (*v && *v != '"') { if (*v == '\\' && v[1]) v++; v++; }
        if (!*v) break;
        v = pularBranco(v + 1);
        if (*v != ':') break;
        v = pularBranco(v + 1);
        if (*v != '[') break;
        n = lerArrayEps(v, atoi(chave), eps, n, max);
        p = js_fim(v);
        if (!p) break;
        p = pularBranco(p + 1);
        if (*p == ',') p = pularBranco(p + 1);
      }
    } else if (*ep == '[') {
      // [[...],[...]]: season = position + 1 unless each episode says.
      const char *p = pularBranco(ep + 1);
      int k = 1;
      while (p && *p == '[' && n < max) {
        n = lerArrayEps(p, k++, eps, n, max);
        p = js_fim(p);
        if (!p) break;
        p = pularBranco(p + 1);
        if (*p == ',') p = pularBranco(p + 1);
      }
    }
    if (n > 1) qsort(eps, (size_t)n, sizeof *eps, compararEp);
  }
  return n;
}

// ---- fetchers ----
typedef struct { char id[XTVOD_SID]; char nome[96]; } Cat;
typedef struct { Cat *v; int n, cap; } Cats;
static void juntarCat(const char *id, const char *nome, void *u) {
  Cats *c = u;
  if (c->n == c->cap) {
    int nova = c->cap ? c->cap * 2 : 64;
    Cat *t = realloc(c->v, (size_t)nova * sizeof *t);
    if (!t) return;
    c->v = t; c->cap = nova;
  }
  snprintf(c->v[c->n].id, sizeof c->v[c->n].id, "%s", id);
  snprintf(c->v[c->n].nome, sizeof c->v[c->n].nome, "%s", nome);
  c->n++;
}
static const char *achaCat(const char *id, void *u) {
  Cats *c = u;
  for (int i = 0; i < c->n; i++) if (!strcmp(c->v[i].id, id)) return c->v[i].nome;
  return NULL;
}
// A list body must be a JSON array; a refusal ({"user_info":{"auth":0}}) or an
// HTML page is "no answer" for the caller, which keeps its previous copy.
static int ehLista(const char *corpo, int st) {
  const char *p = pulaBom(corpo);
  return corpo && st > 0 && st < 400 && p && *p == '[';
}
int xtvod_listar(unsigned conta, int serie, XtVodCb cb, void *u, uint64_t sessao) {
  Cats cats = {0};
  int st = 0, n;
  char *corpo = xtream_fonte_api(conta, serie ? "get_series_categories" : "get_vod_categories",
                                 XTVOD_PRAZO_INFO, &st, sessao);
  if (ehLista(corpo, st)) xtvod_parse_categorias(corpo, juntarCat, &cats);
  free(corpo);
  st = 0;
  corpo = xtream_fonte_api(conta, serie ? "get_series" : "get_vod_streams", XTVOD_PRAZO_LISTA, &st, sessao);
  if (!ehLista(corpo, st)) {
    printf("[xtvod] conta %u: lista de %s sem resposta (HTTP %d)\n", conta, serie ? "series" : "filmes", st);
    free(corpo); free(cats.v);
    return -1;
  }
  n = xtvod_parse_lista(corpo, serie, achaCat, &cats, cb, u);
  printf("[xtvod] conta %u: %d %s em %d categoria(s)\n", conta, n, serie ? "series" : "filmes", cats.n);
  free(corpo); free(cats.v);
  return n;
}
int xtvod_ficha_filme(unsigned conta, const char *sid, XtVodFicha *f, uint64_t sessao) {
  char acao[80];
  int st = 0, ok;
  char *corpo;
  if (!idSeguro(sid)) return 0;
  snprintf(acao, sizeof acao, "get_vod_info&vod_id=%s", sid);
  corpo = xtream_fonte_api(conta, acao, XTVOD_PRAZO_INFO, &st, sessao);
  ok = corpo && st > 0 && st < 400 && xtvod_parse_filme(corpo, f);
  if (ok && !f->sid[0]) snprintf(f->sid, sizeof f->sid, "%s", sid);
  free(corpo);
  return ok;
}
int xtvod_ficha_serie(unsigned conta, const char *sid, XtVodFicha *f, XtVodEp *eps, int max,
                      uint64_t sessao) {
  char acao[80];
  int st = 0, n = -1;
  char *corpo;
  if (!idSeguro(sid)) return -1;
  snprintf(acao, sizeof acao, "get_series_info&series_id=%s", sid);
  corpo = xtream_fonte_api(conta, acao, XTVOD_PRAZO_INFO, &st, sessao);
  if (corpo && st > 0 && st < 400) n = xtvod_parse_serie(corpo, f, eps, max);
  if (n >= 0) snprintf(f->sid, sizeof f->sid, "%s", sid);
  free(corpo);
  return n;
}
