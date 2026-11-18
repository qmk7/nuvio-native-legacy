#include "traktdup.h"
#include "dados.h"
#include "perfis.h"
#include "rede.h"
#include "traktid.h"

// Weak: test binaries that link the Trakt modules without the data folder,
// profiles or the full network layer still build (memory-only ledger).
#pragma weak dados_ler
#pragma weak dados_gravar
#pragma weak dados_apagar
#pragma weak perfis_ativo
#pragma weak rede_postar_st_retry
#pragma weak rede_postar_st
static int perfilAtual(void) { return perfis_ativo ? perfis_ativo() : 1; }
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------- base URL
static char base[160] = "https://api.trakt.tv";
void trakt_definir_base(const char *b) { if (b && b[0]) snprintf(base, sizeof base, "%s", b); }
const char *trakt_base(void) { return base; }
void trakt_url(char *dst, size_t n, const char *caminho) { snprintf(dst, n, "%s%s", base, caminho); }

// ---------------------------------------------------------------- ledger
#define DUP_MAX 4000
typedef struct { char conta[17], id[48]; long long quando; } Envio;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static Envio *v; static int n, cap, perfilLido = -1;

static void contaDe(const char *aut, char d[17]) {
  unsigned long long h = 1469598103934665603ULL;
  for (const char *p = aut ? aut : ""; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
  snprintf(d, 17, "%016llx", h);
}
static void arquivo(char *d, size_t tam) { int p = perfilAtual(); snprintf(d, tam, "trakt-sent-p%d.txt", p > 0 ? p : 1); }
static void carregarLocked(void) {
  char nome[40], *t, *l, *sp;
  arquivo(nome, sizeof nome);
  n = 0;
  perfilLido = perfilAtual();
  t = dados_ler ? dados_ler(nome) : NULL;
  for (l = t ? strtok_r(t, "\n", &sp) : NULL; l; l = strtok_r(NULL, "\n", &sp)) {
    char *t1 = strchr(l, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    if (!t1 || !t2 || t1 - l != 16 || t2 - t1 - 1 >= 48) continue;
    if (n == cap) { int nc = cap ? cap * 2 : 64; Envio *x = realloc(v, sizeof *x * (size_t)nc); if (!x) break; v = x; cap = nc; }
    snprintf(v[n].conta, sizeof v[n].conta, "%.16s", l);
    snprintf(v[n].id, sizeof v[n].id, "%.*s", (int)(t2 - t1 - 1), t1 + 1);
    v[n].quando = atoll(t2 + 1);
    n++;
  }
  free(t);
}
static void garantir(void) { if (perfilLido != perfilAtual()) carregarLocked(); }
static void gravarLocked(void) {
  char nome[40];
  size_t tam = 16 + (size_t)n * 96, k = 0;
  char *t = malloc(tam);
  arquivo(nome, sizeof nome);
  if (!t) return;
  t[0] = 0;
  for (int i = 0; i < n; i++) k += (size_t)snprintf(t + k, tam - k, "%s\t%s\t%lld\n", v[i].conta, v[i].id, v[i].quando);
  if (n) { if (dados_gravar) dados_gravar(nome, t); } else if (dados_apagar) dados_apagar(nome);
  free(t);
}
int traktdup_tem(const char *aut, const char *c) {
  int r = 0;
  char k[17];
  if (!c || !c[0]) return 0;
  contaDe(aut, k);
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < n && !r; i++) r = !strcmp(v[i].id, c) && !strcmp(v[i].conta, k);
  pthread_mutex_unlock(&trava);
  return r;
}
void traktdup_por(const char *aut, const char *c) {
  char k[17];
  if (!c || !c[0] || strlen(c) >= 48) return;
  contaDe(aut, k);
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < n; i++) if (!strcmp(v[i].id, c) && !strcmp(v[i].conta, k)) { pthread_mutex_unlock(&trava); return; }
  if (n == DUP_MAX) { memmove(v, v + 1, sizeof *v * (size_t)(n - 1)); n--; }   // oldest out
  if (n == cap) { int nc = cap ? cap * 2 : 64; Envio *x = realloc(v, sizeof *x * (size_t)nc); if (!x) { pthread_mutex_unlock(&trava); return; } v = x; cap = nc; }
  snprintf(v[n].id, sizeof v[n].id, "%s", c);
  snprintf(v[n].conta, sizeof v[n].conta, "%s", k);
  v[n].quando = (long long)time(NULL);
  n++;
  gravarLocked();
  pthread_mutex_unlock(&trava);
}
void traktdup_tirar(const char *aut, const char *c) {
  size_t l = c ? strlen(c) : 0;
  int k = 0, mudou = 0, titulo;
  char ct[17];
  TraktId t;
  if (!l) return;
  contaDe(aut, ct);
  titulo = traktid_resolver(c, &t) >= TID_IMDB && t.temporada < 0;   // a whole title, not one episode
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < n; i++) {
    // "tt1" also takes "tt1:S:E" (unwatching a whole show).
    int casa = !strcmp(v[i].conta, ct) && (!strcmp(v[i].id, c) || (titulo && !strncmp(v[i].id, c, l) && v[i].id[l] == ':'));
    if (casa) { mudou = 1; continue; }
    v[k++] = v[i];
  }
  n = k;
  if (mudou) gravarLocked();
  pthread_mutex_unlock(&trava);
}
void traktdup_renovar(const char *velho, const char *novo) {
  char a[17], b[17];
  int mudou = 0;
  contaDe(velho, a); contaDe(novo, b);
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < n; i++) if (!strcmp(v[i].conta, a)) { snprintf(v[i].conta, sizeof v[i].conta, "%s", b); mudou = 1; }
  if (mudou) gravarLocked();
  pthread_mutex_unlock(&trava);
}
void traktdup_esquecer(void) {
  pthread_mutex_lock(&trava); garantir();
  n = 0;
  gravarLocked();
  pthread_mutex_unlock(&trava);
}
void traktdup_teste_recarregar(void) { pthread_mutex_lock(&trava); perfilLido = -1; pthread_mutex_unlock(&trava); }

// ---------------------------------------------------------------- writes with retries
static char *(*postarFn)(const char *, int, const char *const *, const char *, int *, int *);
static char *postarSimples(const char *u, int s, const char *const *c, const char *b, int *st, int *ra) {
  if (ra) *ra = 0;
  return rede_postar_st ? rede_postar_st(u, s, c, b, st) : NULL;
}
static void dormirReal(int s) { sleep((unsigned)s); }
static void (*dormirFn)(int) = dormirReal;
void trakt_postar_teste(char *(*p)(const char *, int, const char *const *, const char *, int *, int *), void (*d)(int)) {
  postarFn = p;
  dormirFn = d ? d : dormirReal;
}
char *trakt_postar(const char *url, const char *const *cab, const char *corpo, int *status) {
  int esperas = 0, falhas = 0, st = 0, ra = 0;
  char *r;
  for (;;) {
    st = 0; ra = 0;
    r = postarFn ? postarFn(url, 20, cab, corpo, &st, &ra)
      : rede_postar_st_retry ? rede_postar_st_retry(url, 20, cab, corpo, &st, &ra) : postarSimples(url, 20, cab, corpo, &st, &ra);
    if (st == 429 && esperas < 2) {
      int s = ra > 0 ? ra : 2;
      if (s > 30) s = 30;
      printf("[trakt] limite de pedidos (429): nova tentativa em %ds\n", s);
      fflush(stdout);
      free(r);
      dormirFn(s);
      esperas++;
      continue;
    }
    if ((st == 0 || st >= 500) && falhas < 1) {
      printf("[trakt] %s: nova tentativa em 2s\n", st ? "erro do servidor" : "sem rede");
      fflush(stdout);
      free(r);
      dormirFn(2);
      falhas++;
      continue;
    }
    break;
  }
  if (status) *status = st;
  return r;
}
