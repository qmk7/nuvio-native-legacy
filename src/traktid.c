#include "traktid.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// IPTV VOD resolution, weak so trackers/tests link without the IPTV library.
__attribute__((weak)) int iptvvod_poster_ids(const char *id, const char *tipo, char *imdb, size_t n, long *tmdb);
static int (*resolvedor)(const char *, const char *, char *, size_t, long *);
void traktid_teste_resolvedor(int (*f)(const char *, const char *, char *, size_t, long *)) { resolvedor = f; }

static int digitos(const char *s, size_t n) {
  if (!n) return 0;
  for (size_t i = 0; i < n; i++) if (!isdigit((unsigned char)s[i])) return 0;
  return 1;
}
// ":S:E" (both numbers, S >= 0, E >= 1) or "". 1 ok.
static int sufixo(const char *s, int *t, int *e) {
  int a, b, k = 0;
  *t = -1; *e = 0;
  if (!*s) return 1;
  if (sscanf(s, ":%d:%d%n", &a, &b, &k) != 2 || s[k] || a < 0 || b < 1 || a > 999 || b > 9999) return 0;
  // Only digits between the colons (no "+1", no spaces).
  for (const char *p = s + 1; *p; p++) if (!isdigit((unsigned char)*p) && *p != ':') return 0;
  *t = a; *e = b;
  return 1;
}
int traktid_resolver(const char *id, TraktId *o) {
  size_t k;
  memset(o, 0, sizeof *o);
  o->temporada = -1;
  if (!id || !*id) return o->tipo = TID_INVALIDO;
  // IMDb.
  if (id[0] == 't' && id[1] == 't') {
    k = strcspn(id, ":");
    if (k < 9 || k > 12 || !digitos(id + 2, k - 2) || !sufixo(id + k, &o->temporada, &o->episodio)) return o->tipo = TID_INVALIDO;
    snprintf(o->imdb, sizeof o->imdb, "%.*s", (int)k, id);
    return o->tipo = TID_IMDB;
  }
  // TMDB / TVDB.
  if (!strncmp(id, "tmdb:", 5) || !strncmp(id, "tvdb:", 5)) {
    const char *n = id + 5;
    int tmdb = id[1] == 'm';
    if (tmdb && (*n == 'm' || *n == 't')) n++;   // "tmdb:m603" / "tmdb:t1396"
    k = strcspn(n, ":");
    if (!digitos(n, k) || k > 10 || !sufixo(n + k, &o->temporada, &o->episodio)) return o->tipo = TID_INVALIDO;
    o->num = atol(n);
    if (o->num <= 0) return o->tipo = TID_INVALIDO;
    return o->tipo = tmdb ? TID_TMDB : TID_TVDB;
  }
  // IPTV VOD: only through a resolution TMDB confirmed.
  if (!strncmp(id, "qv:", 3)) {
    char base[40], imdb[24] = "";
    long tmdb = 0;
    int t = -1, e = 0, ok = 0;
    const char *ponto = strchr(id, '.');
    int (*f)(const char *, const char *, char *, size_t, long *) = resolvedor ? resolvedor : iptvvod_poster_ids;
    if (!ponto || ponto - id != 19 || !f) return o->tipo = TID_LOCAL;
    if (ponto[1] == 'e') {   // an episode: ask about the series, keep S/E
      if (sscanf(ponto + 2, "%dx%d", &t, &e) != 2 || t < 0 || e < 1) return o->tipo = TID_LOCAL;
      snprintf(base, sizeof base, "%.*s.s", (int)(ponto - id), id);
      ok = f(base, "series", imdb, sizeof imdb, &tmdb);
    } else if (ponto[1] == 'm' || ponto[1] == 's') {
      snprintf(base, sizeof base, "%s", id);
      ok = f(base, ponto[1] == 'm' ? "movie" : "series", imdb, sizeof imdb, &tmdb);
    }
    if (!ok) return o->tipo = TID_LOCAL;
    {
      char canon[48];
      TraktId r;
      if (imdb[0]) snprintf(canon, sizeof canon, "%s", imdb);
      else if (tmdb > 0) snprintf(canon, sizeof canon, "tmdb:%ld", tmdb);
      else return o->tipo = TID_LOCAL;
      if (traktid_resolver(canon, &r) < TID_IMDB) return o->tipo = TID_LOCAL;
      *o = r;
      o->temporada = t; o->episodio = e;
      o->resolvido = 1;
      return o->tipo;
    }
  }
  // Personal servers, raw provider ids, Kitsu, anything else: local only.
  if (!strncmp(id, "jf:", 3) || !strncmp(id, "em:", 3) || !strncmp(id, "px:", 3) || !strncmp(id, "kitsu:", 6) ||
      !strncmp(id, "xtream:", 7) || !strncmp(id, "stalker:", 8) || !strncmp(id, "m3u:", 4) || isdigit((unsigned char)id[0]))
    return o->tipo = TID_LOCAL;
  return o->tipo = TID_INVALIDO;
}
int traktid_enviavel(const char *id) { TraktId t; return traktid_resolver(id, &t) >= TID_IMDB; }
int traktid_ids_json(const TraktId *t, char *dst, size_t n) {
  int r;
  if (!t || t->tipo < TID_IMDB) { if (n) dst[0] = 0; return 0; }
  if (t->tipo == TID_IMDB) r = snprintf(dst, n, "\"imdb\":\"%s\"", t->imdb);
  else r = snprintf(dst, n, "\"%s\":%ld", t->tipo == TID_TMDB ? "tmdb" : "tvdb", t->num);
  return r > 0 && (size_t)r < n;
}
int traktid_canonico(const TraktId *t, char *dst, size_t n) {
  int r;
  if (!t || t->tipo < TID_IMDB) { if (n) dst[0] = 0; return 0; }
  if (t->tipo == TID_IMDB) r = snprintf(dst, n, "%s", t->imdb);
  else r = snprintf(dst, n, "%s:%ld", t->tipo == TID_TMDB ? "tmdb" : "tvdb", t->num);
  if (r > 0 && (size_t)r < n && t->temporada >= 0 && t->episodio > 0)
    r += snprintf(dst + r, n - (size_t)r, ":%d:%d", t->temporada, t->episodio);
  return r > 0 && (size_t)r < n;
}
