// Ver velmem.h.
#include "velmem.h"
#include "dados.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void arq(char *d, size_t n) { snprintf(d, n, "velocidade-p%d.txt", perfis_ativo()); }
int velmem_ler(const char *serie) {
  char n[40], *t, *p;
  int v = 0;
  size_t l = serie ? strlen(serie) : 0;
  if (!l) return 0;
  arq(n, sizeof n);
  if (!(t = dados_ler(n))) return 0;
  for (p = t; p && *p; ) {
    if (!strncmp(p, serie, l) && p[l] == '\t') { v = atoi(p + l + 1); break; }
    p = strchr(p, '\n'); if (p) p++;
  }
  free(t);
  return v >= 50 && v <= 300 ? v : 0;
}
void velmem_gravar(const char *serie, int cent) {
  char n[40], *t, *novo, *p;
  size_t l = serie ? strlen(serie) : 0, cap;
  if (!l || l > 30 || strchr(serie, '\t') || strchr(serie, '\n')) return;
  arq(n, sizeof n);
  t = dados_ler(n);
  cap = (t ? strlen(t) : 0) + l + 16;
  if (!(novo = calloc(1, cap))) { free(t); return; }
  for (p = t; p && *p; ) {   // every other series' line stays
    char *nl = strchr(p, '\n');
    size_t ll = nl ? (size_t)(nl - p) : strlen(p);
    if (!(ll > l && !strncmp(p, serie, l) && p[l] == '\t')) { strncat(novo, p, ll); strcat(novo, "\n"); }
    p = nl ? nl + 1 : NULL;
  }
  if (cent != 100) snprintf(novo + strlen(novo), cap - strlen(novo), "%s\t%d\n", serie, cent);
  if (novo[0]) dados_gravar(n, novo); else dados_apagar(n);
  free(novo); free(t);
}
