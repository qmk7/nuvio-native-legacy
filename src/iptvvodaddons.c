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
  (void)aviso; (void)avisoU;   // one quiet lookup, no per-source progress rows
  return iptvvod_origem(id, tipo, cancelado, ctx, NULL, NULL, saida);
}
void iptvvod_ligar_aos_addons(void) {
  iptvvod_definir_titulo(tituloDoCatalogo);
  addons_definir_origem_extra(origem, iptvvod_origem_ativa);
}
