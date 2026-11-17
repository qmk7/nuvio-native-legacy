#include "stvod.h"
#include "stalker.h"
#include "js.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STVOD_PAGINAS_MAX 4000   // 14 per page on most portals: 56k items

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
static int idSeguro(const char *s) {
  if (!s || !*s || strlen(s) >= XTVOD_SID) return 0;
  for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_') return 0;
  return 1;
}
// The "series" array as "1,2,3". js_array only walks arrays of objects or
// strings; portals send numbers ([1,2]) or strings (["1","2"]).
static void listaSeries(const char *ini, const char *fim, char *dst, size_t tam) {
  const char *p = ini;
  size_t n = 0;
  dst[0] = 0;
  while ((p = strstr(p, "\"series\"")) != NULL && p < fim) {
    const char *q = p + 8;
    while (q < fim && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
    if (q < fim && *q == ':') {
      q++;
      while (q < fim && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) q++;
      if (q < fim && *q == '[') { p = q + 1; break; }
    }
    p += 8;
  }
  if (!p || p >= fim) return;
  while (p < fim && *p != ']' && n + 8 < tam) {
    while (p < fim && (*p == ' ' || *p == ',' || *p == '"' || *p == '\n')) p++;
    if (p >= fim || !isdigit((unsigned char)*p)) break;
    n += (size_t)snprintf(dst + n, tam - n, "%s%d", n ? "," : "", atoi(p));
    while (p < fim && isdigit((unsigned char)*p)) p++;
  }
}
int stvod_episodios(const char *eps, int *out, int max) {
  int n = 0;
  for (const char *p = eps; p && *p && n < max;) {
    int v = atoi(p), k;
    if (v > 0) {
      for (k = n; k > 0 && out[k - 1] > v; k--) out[k] = out[k - 1];
      if (k == 0 || out[k - 1] != v) { out[k] = v; n++; }
      else for (; k < n; k++) out[k] = out[k + 1];
    }
    p = strchr(p, ',');
    if (p) p++;
  }
  return n;
}

int stvod_parse_pagina(const char *json, const char *(*nomeCat)(const char *, void *), void *uCat,
                       StVodCb cb, void *u, int *total, int *porPagina) {
  const char *p;
  int n = 0;
  if (total) *total = json ? (int)js_num(json, NULL, "total_items", 0.0) : 0;
  if (porPagina) *porPagina = json ? (int)js_num(json, NULL, "max_page_items", 0.0) : 0;
  p = json ? strstr(json, "\"data\"") : NULL;
  p = p ? strchr(p, '[') : NULL;
  p = p ? p + 1 : NULL;
  while (p && *p) {
    const char *fim;
    XtVodItem it;
    char cmd[STVOD_CMD], eps[STVOD_EPS], t[40], cid[XTVOD_SID];
    int serie;
    while (*p == ' ' || *p == ',' || *p == '\n') p++;
    if (*p != '{') break;
    fim = js_fim(p);
    if (!fim) break;
    memset(&it, 0, sizeof it);
    cmd[0] = eps[0] = 0;
    if (campo(p, fim, "id", it.sid, sizeof it.sid) && idSeguro(it.sid) &&
        js_texto(p, fim, "name", it.nome, sizeof it.nome) && it.nome[0] &&
        js_texto(p, fim, "cmd", cmd, sizeof cmd) && cmd[0]) {
      serie = campo(p, fim, "is_series", t, sizeof t) && atoi(t) == 1;
      if (serie) listaSeries(p, fim, eps, sizeof eps);
      js_texto(p, fim, "screenshot_uri", it.poster, sizeof it.poster);
      if (strncmp(it.poster, "http://", 7) && strncmp(it.poster, "https://", 8)) it.poster[0] = 0;
      if (campo(p, fim, "year", t, sizeof t)) { int a = atoi(t); it.ano = a >= 1888 && a <= 2100 ? a : 0; }
      if (!it.ano) it.ano = xtvod_ano_do_nome(it.nome);
      if (campo(p, fim, "tmdb_id", t, sizeof t)) {
        int ok = t[0] != 0;
        for (const char *c = t; *c; c++) if (!isdigit((unsigned char)*c)) ok = 0;
        if (ok) it.tmdbProv = atol(t);
      }
      if (campo(p, fim, "category_id", cid, sizeof cid) && nomeCat) {
        const char *nm = nomeCat(cid, uCat);
        if (nm) snprintf(it.categoria, sizeof it.categoria, "%s", nm);
      }
      if (js_texto(p, fim, "added", t, sizeof t)) {
        long long ms = js_ms_iso(t);
        it.adicionado = ms > 0 ? ms / 1000 : 0;
      }
      n++;
      if (cb && !cb(&it, serie, cmd, eps, u)) break;
    }
    p = fim + 1;
  }
  return n;
}

typedef struct { char id[XTVOD_SID]; char nome[96]; } Cat;
typedef struct { Cat *v; int n, cap; } Cats;
static const char *achaCat(const char *id, void *u) {
  Cats *c = u;
  for (int i = 0; i < c->n; i++) if (!strcmp(c->v[i].id, id)) return c->v[i].nome;
  return NULL;
}
static void lerCategorias(unsigned conta, Cats *c, uint64_t sessao) {
  char *corpo = stalker_fonte_api(conta, "type=vod&action=get_categories", sessao);
  const char *p = corpo ? strstr(corpo, "\"js\"") : NULL;
  p = p ? strchr(p, '[') : NULL;
  p = p ? p + 1 : NULL;
  while (p && *p) {
    const char *fim;
    while (*p == ' ' || *p == ',') p++;
    if (*p != '{' || !(fim = js_fim(p))) break;
    if (c->n == c->cap) {
      int nova = c->cap ? c->cap * 2 : 32;
      Cat *t = realloc(c->v, (size_t)nova * sizeof *t);
      if (!t) break;
      c->v = t; c->cap = nova;
    }
    if (campo(p, fim, "id", c->v[c->n].id, sizeof c->v[c->n].id) &&
        js_texto(p, fim, "title", c->v[c->n].nome, sizeof c->v[c->n].nome) && strcmp(c->v[c->n].id, "*"))
      c->n++;
    p = fim + 1;
  }
  free(corpo);
}
int stvod_listar(unsigned conta, StVodCb cb, void *u, uint64_t sessao) {
  Cats cats = {0};
  int n = 0, pagina, algum = 0;
  unsigned long anterior = 0;
  lerCategorias(conta, &cats, sessao);
  for (pagina = 1; pagina <= STVOD_PAGINAS_MAX; pagina++) {
    char consulta[160];
    char *corpo;
    int k, total = 0, por = 0;
    unsigned long h = 5381;
    snprintf(consulta, sizeof consulta,
             "type=vod&action=get_ordered_list&category=*&sortby=added&fav=0&hd=0&p=%d", pagina);
    corpo = stalker_fonte_api(conta, consulta, sessao);
    if (!corpo) break;
    algum = 1;
    // A portal ignoring `p` repeats page 1: stop instead of looping.
    for (const unsigned char *s = (const unsigned char *)corpo; *s; s++) h = h * 33 + *s;
    if (pagina > 1 && h == anterior) { free(corpo); break; }
    anterior = h;
    k = stvod_parse_pagina(corpo, achaCat, &cats, cb, u, &total, &por);
    free(corpo);
    n += k;
    if (!k || (total > 0 && n >= total)) break;
  }
  free(cats.v);
  printf("[stvod] conta %u: %d titulo(s) do portal\n", conta, n);
  return algum ? n : -1;
}
