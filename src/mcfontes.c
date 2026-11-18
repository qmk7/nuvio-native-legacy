#include "mcfontes.h"
#include "dados.h"
#include "md5.h"
#include "perfis.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static McFonte fontes[MCF_MAX];
static int nFontes, perfilLido = -1;

static void arquivo(char *d, size_t n) { int p = perfis_ativo(); snprintf(d, n, "musica-fontes-p%d.txt", p > 0 ? p : 1); }
// Tabs and newlines never reach the file (fields are tab-separated).
static int limpo(const char *s) { for (; s && *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') return 0; return 1; }

static void carregarLocked(void) {
  char nome[48], *t, *lin, *sp;
  arquivo(nome, sizeof nome);
  nFontes = 0;
  perfilLido = perfis_ativo();
  t = dados_ler(nome);
  for (lin = t ? strtok_r(t, "\n", &sp) : NULL; lin && nFontes < MCF_MAX; lin = strtok_r(NULL, "\n", &sp)) {
    char *c[6] = { 0 };
    int k = 0;
    McFonte *f = &fontes[nFontes];
    for (char *p = lin; k < 6; k++) { c[k] = p; p = strchr(p, '\t'); if (!p) { k++; break; } *p++ = 0; }
    if (k < 6) continue;
    memset(f, 0, sizeof *f);
    f->tipo = atoi(c[0]);
    snprintf(f->chave, sizeof f->chave, "%s", c[1]);
    snprintf(f->nome, sizeof f->nome, "%s", c[2]);
    if (f->tipo == MCS_SUBSONIC) {
      snprintf(f->ss.url, sizeof f->ss.url, "%s", c[3]); snprintf(f->ss.usuario, sizeof f->ss.usuario, "%s", c[4]); snprintf(f->ss.senha, sizeof f->ss.senha, "%s", c[5]);
    } else if (f->tipo == MCS_WEBDAV) {
      snprintf(f->dv.url, sizeof f->dv.url, "%s", c[3]); snprintf(f->dv.usuario, sizeof f->dv.usuario, "%s", c[4]); snprintf(f->dv.senha, sizeof f->dv.senha, "%s", c[5]);
    } else continue;
    if (f->chave[0]) nFontes++;
  }
  free(t);
}
static void garantir(void) { if (perfilLido != perfis_ativo()) carregarLocked(); }
static int gravarLocked(void) {
  char nome[48];
  size_t cap = 512 + (size_t)nFontes * 1200, k = 0;
  char *t = malloc(cap);
  int ok;
  if (!t) return 0;
  t[0] = 0;
  for (int i = 0; i < nFontes; i++) {
    const McFonte *f = &fontes[i];
    const char *u = f->tipo == MCS_SUBSONIC ? f->ss.url : f->dv.url, *us = f->tipo == MCS_SUBSONIC ? f->ss.usuario : f->dv.usuario,
               *pw = f->tipo == MCS_SUBSONIC ? f->ss.senha : f->dv.senha;
    k += (size_t)snprintf(t + k, cap - k, "%d\t%s\t%s\t%s\t%s\t%s\n", f->tipo, f->chave, f->nome, u, us, pw);
  }
  arquivo(nome, sizeof nome);
  ok = nFontes ? dados_gravar(nome, t) : (dados_apagar(nome), 1);
  free(t);
  return ok;
}
void mcfontes_carregar(void) { pthread_mutex_lock(&trava); carregarLocked(); pthread_mutex_unlock(&trava); }
int mcfontes_n(void) { int n; pthread_mutex_lock(&trava); garantir(); n = nFontes; pthread_mutex_unlock(&trava); return n; }
int mcfontes_copiar(int i, McFonte *out) {
  int ok;
  pthread_mutex_lock(&trava); garantir();
  ok = i >= 0 && i < nFontes;
  if (ok && out) *out = fontes[i];
  pthread_mutex_unlock(&trava);
  return ok;
}
int mcfontes_partir(const char *id, char *fonte, size_t nf, const char **local) {
  const char *c = id ? strchr(id, ':') : NULL;
  if (!c || c - id != 10 || (strncmp(id, "ss", 2) && strncmp(id, "dv", 2))) return 0;
  snprintf(fonte, nf, "%.*s", (int)(c - id), id);
  if (local) *local = c + 1;
  return 1;
}
int mcfontes_achar(const char *chave, McFonte *out) {
  char k[16];
  int ok = 0;
  if (!chave) return 0;
  if (!mcfontes_partir(chave, k, sizeof k, NULL)) snprintf(k, sizeof k, "%.10s", chave);
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < nFontes && !ok; i++) if (!strcmp(fontes[i].chave, k)) { ok = 1; if (out) *out = fontes[i]; }
  pthread_mutex_unlock(&trava);
  return ok;
}
int mcfontes_salvar(McFonte *f) {
  const char *u = f->tipo == MCS_SUBSONIC ? f->ss.url : f->dv.url, *us = f->tipo == MCS_SUBSONIC ? f->ss.usuario : f->dv.usuario,
             *pw = f->tipo == MCS_SUBSONIC ? f->ss.senha : f->dv.senha;
  char base[600], h[33];
  int i, ok;
  if ((f->tipo != MCS_SUBSONIC && f->tipo != MCS_WEBDAV) || !u[0] || !limpo(u) || !limpo(us) || !limpo(pw) || !limpo(f->nome)) return 0;
  if (strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) return 0;
  snprintf(base, sizeof base, "%d|%s|%s", f->tipo, u, us);
  md5_hex(base, strlen(base), h);
  snprintf(f->chave, sizeof f->chave, "%s%.8s", f->tipo == MCS_SUBSONIC ? "ss" : "dv", h);
  if (!f->nome[0]) {
    const char *p = strstr(u, "://");
    snprintf(f->nome, sizeof f->nome, "%.40s", p ? p + 3 : u);
    if (strchr(f->nome, '/')) *strchr(f->nome, '/') = 0;
  }
  pthread_mutex_lock(&trava); garantir();
  for (i = 0; i < nFontes && strcmp(fontes[i].chave, f->chave); i++) {}
  if (i == nFontes) { if (nFontes == MCF_MAX) { pthread_mutex_unlock(&trava); return 0; } nFontes++; }
  fontes[i] = *f;
  ok = gravarLocked();
  pthread_mutex_unlock(&trava);
  return ok;
}
int mcfontes_remover(const char *chave) {
  int ok = 0;
  pthread_mutex_lock(&trava); garantir();
  for (int i = 0; i < nFontes; i++) if (!strcmp(fontes[i].chave, chave)) {
    memmove(&fontes[i], &fontes[i + 1], sizeof fontes[0] * (size_t)(nFontes - i - 1));
    nFontes--;
    ok = gravarLocked();
    break;
  }
  pthread_mutex_unlock(&trava);
  return ok;
}
