// Ver pularserie.h.
#include "pularserie.h"
#include "dados.h"
#include "intro.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXS 400
typedef struct { char imdb[24]; int mask, nAb, nRes; } Serie;
static Serie s[MAXS];
static int n, carregado = -1;   // profile loaded (-1 = none)

static int bit(int tipo) { return tipo == INTRO_ABERTURA ? 1 : tipo == INTRO_RESUMO ? 2 : 0; }
static void arquivo(char *b, size_t t) { int p = perfis_ativo(); snprintf(b, t, "skip-series-p%d.txt", p > 0 ? p : 1); }

static void carregar(void) {
  char nome[48], *txt, *l, *prox;
  int p = perfis_ativo();
  if (carregado == p) return;
  carregado = p; n = 0;
  arquivo(nome, sizeof nome);
  txt = dados_ler(nome);
  for (l = txt; l && *l && n < MAXS; l = prox) {
    Serie e;
    prox = strchr(l, '\n');
    if (prox) *prox++ = 0;
    memset(&e, 0, sizeof e);
    if (sscanf(l, "%23[^\t]\t%d\t%d\t%d", e.imdb, &e.mask, &e.nAb, &e.nRes) >= 2 && e.imdb[0]) s[n++] = e;
  }
  free(txt);
}
static void gravar(void) {
  char nome[48], *b = malloc((size_t)MAXS * 48 + 1);
  size_t u = 0;
  if (!b) return;
  b[0] = 0;
  for (int i = 0; i < n; i++) u += (size_t)snprintf(b + u, 48, "%s\t%d\t%d\t%d\n", s[i].imdb, s[i].mask, s[i].nAb, s[i].nRes);
  arquivo(nome, sizeof nome);
  if (n) dados_gravar(nome, b); else dados_apagar(nome);
  free(b);
}
static Serie *achar(const char *imdb, int criar) {
  if (!imdb || !imdb[0] || strlen(imdb) >= sizeof s[0].imdb) return NULL;
  carregar();
  for (int i = 0; i < n; i++) if (!strcmp(s[i].imdb, imdb)) return &s[i];
  if (!criar) return NULL;
  if (n == MAXS) { memmove(s, s + 1, sizeof *s * (MAXS - 1)); n--; }   // oldest out
  memset(&s[n], 0, sizeof s[n]);
  snprintf(s[n].imdb, sizeof s[n].imdb, "%s", imdb);
  return &s[n++];
}

int pularserie_manual(const char *imdb, int tipo) {
  int b = bit(tipo), *c;
  Serie *e;
  if (!b || !(e = achar(imdb, 1))) return 0;
  if (e->mask & b) return 0;
  c = b == 1 ? &e->nAb : &e->nRes;
  if (++*c >= PULARSERIE_VEZES) e->mask |= b;
  gravar();
  if (e->mask & b) {
    printf("[pularserie] %s: %s automatico a partir de agora\n", imdb, b == 1 ? "abertura" : "resumo");
    fflush(stdout);
    return 1;
  }
  return 0;
}
int pularserie_sempre(const char *imdb, int tipo) {
  Serie *e = achar(imdb, 0);
  return e && (e->mask & bit(tipo)) != 0;
}
void pularserie_esquecer(const char *imdb) {
  Serie *e = achar(imdb, 0);
  if (!e) return;
  *e = s[--n];
  gravar();
}
void pularserie_recarregar(void) { carregado = -1; }
