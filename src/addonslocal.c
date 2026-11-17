#include "addonslocal.h"
#include "addons.h"
#include "dados.h"
#include "js.h"
#include "perfis.h"
#include "perfisessao.h"
#include "rede.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ADL_MAX 64
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int estado, fioVivo;
static char nomeNovo[64];

static void arquivo(char *dst, size_t tam) {
  int p = perfis_ativo();
  snprintf(dst, tam, "addons-local-p%d.txt", p > 0 ? p : 1);
}
// The host only, for the log.
static void host(const char *url, char *dst, size_t tam) {
  const char *p = strstr(url, "://");
  p = p ? p + 3 : url;
  snprintf(dst, tam, "%.*s", (int)strcspn(p, "/?#"), p);
}

int addonslocal_base(const char *entrada, char *base, size_t tam) {
  char t[NV_ADDON_URL_MAX];
  size_t n;
  const char *s = entrada;
  if (!s || !base || tam < 16) return 0;
  while (*s == ' ' || *s == '\t') s++;
  if (!strncasecmp(s, "stremio://", 10)) snprintf(t, sizeof t, "https://%s", s + 10);
  else snprintf(t, sizeof t, "%s", s);
  n = strlen(t);
  while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n')) t[--n] = 0;
  if (n >= 14 && !strcasecmp(t + n - 14, "/manifest.json")) t[n -= 14] = 0;
  while (n && t[n - 1] == '/') t[--n] = 0;
  if (strncmp(t, "http://", 7) && strncmp(t, "https://", 8)) return 0;
  if (!strchr(t, '.') && !strstr(t, "localhost") && !strstr(t, "127.0.0.1")) return 0;
  for (const char *c = t; *c; c++) if ((unsigned char)*c <= ' ') return 0;
  if (n + 1 > tam || n < 10) return 0;
  snprintf(base, tam, "%s", t);
  return 1;
}

// Current list from addons.c, as the file stores it.
static int lerAtual(AddonRemoto *v, int max) {
  int n = addons_n(), k = 0;
  for (int i = 0; i < n && k < max; i++) {
    const char *b = addons_base(i), *nm = addons_nome(i);
    if (!b || !b[0]) continue;
    snprintf(v[k].nome, sizeof v[k].nome, "%s", nm && nm[0] ? nm : "Add-on");
    snprintf(v[k].url, sizeof v[k].url, "%s", b);
    v[k].ativo = addons_ativo(i);
    k++;
  }
  return k;
}
static int gravar(const AddonRemoto *v, int n) {
  char nome[48], *t;
  size_t cap = (size_t)n * (NV_ADDON_URL_MAX + 80) + 1, k = 0;
  int ok;
  arquivo(nome, sizeof nome);
  if (!n) return dados_apagar(nome) || 1;
  t = malloc(cap);
  if (!t) return 0;
  t[0] = 0;
  for (int i = 0; i < n && k < cap; i++) {
    char nm[64];
    snprintf(nm, sizeof nm, "%s", v[i].nome);
    for (char *c = nm; *c; c++) if (*c == '\t' || *c == '\n') *c = ' ';
    k += (size_t)snprintf(t + k, cap - k, "%s\t%s\t%d\n", nm, v[i].url, v[i].ativo ? 1 : 0);
  }
  ok = dados_gravar(nome, t);
  free(t);
  return ok;
}
static void aplicar(const AddonRemoto *v, int n) {
  if (n > 0) addons_definir_lista(v, n);
  else addons_esquecer();   // an empty list is "no add-ons", not "keep the old ones"
}

void addonslocal_carregar(void) {
  AddonRemoto *v = calloc(ADL_MAX, sizeof *v);
  char nome[48], *t, *p;
  int n = 0;
  if (!v) return;
  arquivo(nome, sizeof nome);
  t = dados_ler(nome);
  for (p = t; p && *p && n < ADL_MAX;) {
    char *fim = strchr(p, '\n'), *a, *b;
    if (fim) *fim = 0;
    a = strchr(p, '\t');
    b = a ? strchr(a + 1, '\t') : NULL;
    if (a && b) {
      *a = 0; *b = 0;
      snprintf(v[n].nome, sizeof v[n].nome, "%s", p);
      snprintf(v[n].url, sizeof v[n].url, "%s", a + 1);
      v[n].ativo = atoi(b + 1) != 0;
      if (!strncmp(v[n].url, "http", 4)) n++;
    }
    p = fim ? fim + 1 : NULL;
  }
  free(t);
  aplicar(v, n);
  printf("[addons] convidado: %d addon(s) desta TV no perfil %d\n", n, perfis_ativo());
  fflush(stdout);
  free(v);
}

typedef struct { char base[NV_ADDON_URL_MAX]; uint64_t sessao; } Pedido;
static void *fio(void *u) {
  Pedido p = *(Pedido *)u;
  char url[NV_ADDON_URL_MAX + 32], id[128] = "", nm[64] = "", h[128];
  char *corpo;
  int r = ADL_FALHOU;
  free(u);
  host(p.base, h, sizeof h);
  snprintf(url, sizeof url, "%s/manifest.json", p.base);
  corpo = rede_baixar(url, 15);
  if (corpo && js_texto_raiz(corpo, "id", id, sizeof id) && id[0] &&
      js_texto_raiz(corpo, "name", nm, sizeof nm) && nm[0] &&
      (strstr(corpo, "\"resources\"") || strstr(corpo, "\"catalogs\""))) {
    AddonRemoto *v = calloc(ADL_MAX, sizeof *v);
    if (v && nv_perfil_sessao_atual(p.sessao)) {
      int n = lerAtual(v, ADL_MAX), dup = 0;
      for (int i = 0; i < n; i++) if (!strcmp(v[i].url, p.base)) dup = 1;
      if (dup) r = ADL_REPETIDO;
      else if (n < ADL_MAX) {
        snprintf(v[n].nome, sizeof v[n].nome, "%s", nm);
        snprintf(v[n].url, sizeof v[n].url, "%s", p.base);
        v[n].ativo = 1;
        n++;
        if (nv_perfil_sessao_travar(p.sessao)) {
          if (gravar(v, n)) { aplicar(v, n); r = ADL_OK; }
          nv_perfil_sessao_destravar();
        }
      }
    }
    free(v);
  }
  printf("[addons] convidado: addon de %s %s\n", h,
         r == ADL_OK ? "adicionado" : r == ADL_REPETIDO ? "ja estava na lista" : "recusado (manifesto invalido ou sem resposta)");
  fflush(stdout);
  free(corpo);
  pthread_mutex_lock(&trava);
  estado = r;
  snprintf(nomeNovo, sizeof nomeNovo, "%s", r == ADL_OK ? nm : "");
  fioVivo = 0;
  pthread_mutex_unlock(&trava);
  return NULL;
}
int addonslocal_adicionar(const char *entrada) {
  Pedido *p;
  pthread_t t;
  char base[NV_ADDON_URL_MAX];
  if (!addonslocal_base(entrada, base, sizeof base)) {
    pthread_mutex_lock(&trava); estado = ADL_FALHOU; nomeNovo[0] = 0; pthread_mutex_unlock(&trava);
    return 0;
  }
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); return 0; }
  fioVivo = 1;
  estado = ADL_TRABALHANDO;
  pthread_mutex_unlock(&trava);
  p = malloc(sizeof *p);
  if (!p) { pthread_mutex_lock(&trava); fioVivo = 0; estado = ADL_FALHOU; pthread_mutex_unlock(&trava); return 0; }
  snprintf(p->base, sizeof p->base, "%s", base);
  p->sessao = nv_perfil_sessao_ler();
  if (pthread_create(&t, NULL, fio, p) != 0) {
    free(p);
    pthread_mutex_lock(&trava); fioVivo = 0; estado = ADL_FALHOU; pthread_mutex_unlock(&trava);
    return 0;
  }
  pthread_detach(t);
  return 1;
}
int addonslocal_estado(char *nome, size_t tam) {
  int e;
  pthread_mutex_lock(&trava);
  e = estado;
  if (nome && tam) snprintf(nome, tam, "%s", nomeNovo);
  if (e != ADL_TRABALHANDO) estado = ADL_NADA;
  pthread_mutex_unlock(&trava);
  return e;
}
void addonslocal_salvar(void) {
  AddonRemoto *v = calloc(ADL_MAX, sizeof *v);
  if (!v) return;
  gravar(v, lerAtual(v, ADL_MAX));
  free(v);
}
int addonslocal_remover_desligados(void) {
  AddonRemoto *v = calloc(ADL_MAX, sizeof *v);
  int n, w = 0;
  if (!v) return 0;
  n = lerAtual(v, ADL_MAX);
  for (int i = 0; i < n; i++) if (v[i].ativo) v[w++] = v[i];
  if (w != n && gravar(v, w)) aplicar(v, w);
  free(v);
  printf("[addons] convidado: %d addon(s) desligado(s) removido(s)\n", n - w);
  fflush(stdout);
  return n - w;
}
