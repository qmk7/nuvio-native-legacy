// Hooks the IPTV library (iptvvod.c) into addons.c as one more source of
// streams for catalogue titles (a separate file so iptvvod.c compiles and is
// tested without the addons module, like plexaddons.c).
#include "addons.h"
#include "iptvvod.h"
#include "catalogo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// The catalogue's title and year for an addon id ("2022 · 3 temporadas" ->
// 2022). 1 when the title is known.
static int tituloDoCatalogo(const char *id, const char *tipo, char *titulo, size_t n, int *ano) {
  CatItem *c = calloc(1, sizeof *c);
  int ok = 0;
  *ano = 0;
  if (!c) return 0;
  if (cat_copiar_por_id(id, tipo, c) && c->titulo[0]) {
    snprintf(titulo, n, "%s", c->titulo);
    for (const char *p = c->meta; *p; p++)
      if ((p[0] == '1' || p[0] == '2') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])
          && (p == c->meta || !isdigit((unsigned char)p[-1])) && !isdigit((unsigned char)p[4])) {
        int y = atoi(p);
        if (y >= 1900 && y <= 2100) { *ano = y; break; }
      }
    ok = 1;
  }
  free(c);
  return ok;
}

static int origem(const char *id, const char *tipo, int (*cancelado)(void *), void *ctx,
                  OrigemAviso aviso, void *avisoU, void *saida) {
  int n;
  if (aviso) aviso(avisoU, ADDONS_VAGA_IPTV, "IPTV", "qrtime-iptv", 1, NULL, 0);
  n = iptvvod_origem(id, tipo, cancelado, ctx, NULL, NULL, saida);
  if (aviso) aviso(avisoU, ADDONS_VAGA_IPTV, "IPTV", "qrtime-iptv", n >= 0 ? 2 : 3,
                   n > 0 ? *(Stream **)saida : NULL, n > 0 ? n : 0);
  return n;
}
// A home catalogue item with the same normalised title (and year within one
// when both are known) that has a real IMDb id.
typedef struct { const char *norm; int ano; char kind; char imdb[16]; int n; } AchaImdb;
static int anoDoMeta(const char *meta) {
  for (const char *p = meta; *p; p++)
    if ((p[0] == '1' || p[0] == '2') && isdigit((unsigned char)p[1]) && isdigit((unsigned char)p[2]) && isdigit((unsigned char)p[3])
        && (p == meta || !isdigit((unsigned char)p[-1])) && !isdigit((unsigned char)p[4])) {
      int y = atoi(p);
      if (y >= 1900 && y <= 2100) return y;
    }
  return 0;
}
static int conferirItem(const CatItem *c, void *u) {
  AchaImdb *a = u;
  char nn[128];
  int y;
  if (strncmp(c->imdb, "tt", 2) || !isdigit((unsigned char)c->imdb[2])) return 0;
  if ((a->kind == 's') != (!strcmp(c->tipo, "series") || !strcmp(c->tipo, "tv"))) return 0;
  iptvvod_normalizar(c->titulo, nn, sizeof nn);
  if (strcmp(nn, a->norm)) return 0;
  y = anoDoMeta(c->meta);
  if (a->ano > 0 && y > 0 && (y - a->ano > 1 || a->ano - y > 1)) return 0;
  if (a->n && strcmp(a->imdb, c->imdb)) { a->n = -1; return 1; }   // two different films: no answer
  snprintf(a->imdb, sizeof a->imdb, "%.*s", (int)strcspn(c->imdb, ":"), c->imdb);
  a->n = 1;
  return 0;
}
static int imdbDoCatalogo(const char *norm, int ano, char kind, char *imdb, size_t n) {
  AchaImdb a;
  memset(&a, 0, sizeof a);
  a.norm = norm; a.ano = ano; a.kind = kind;
  cat_percorrer(conferirItem, &a);
  if (a.n != 1) return 0;
  snprintf(imdb, n, "%s", a.imdb);
  return 1;
}
void iptvvod_ligar_aos_addons(void) {
  iptvvod_definir_titulo(tituloDoCatalogo);
  iptvvod_definir_imdb_catalogo(imdbDoCatalogo);
  addons_definir_traducao_local(iptvvod_e_id, iptvvod_id_addons);
  addons_definir_origem_extra(origem, iptvvod_origem_ativa);
}
