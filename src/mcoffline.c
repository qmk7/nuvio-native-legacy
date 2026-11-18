#include "mcoffline.h"
#include "dados.h"
#include "md5.h"
#include "mcfila.h"
#include "mcfontes.h"
#include "rede.h"
#include "js.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;

static void nomeArquivo(const McFaixa *f, char *d, size_t n) {
  char h[33], ext[12];
  md5_hex(f->id, strlen(f->id), h);
  snprintf(ext, sizeof ext, "%s", f->formato[0] ? f->formato : "bin");
  for (char *c = ext; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) *c = 'x';
  snprintf(d, n, "mo-%.16s.%s", h, ext);
}
// The index: one line per copy: "<file>\t<bytes>\t<track line>".
static char *lerIndice(void) { return dados_ler("music-offline.txt"); }
static int achar(const char *idx, const char *id, char *arq, size_t na, long long *tam) {
  McFaixa x;
  for (const char *p = idx; p && *p;) {
    const char *f = strchr(p, '\n'), *t1 = strchr(p, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    if (t2 && (!f || t2 < f)) {
      char lin[3000];
      snprintf(lin, sizeof lin, "%.*s", (int)((f ? f : p + strlen(p)) - t2 - 1), t2 + 1);
      if (mcfila_ler_linha(lin, &x) && !strcmp(x.id, id)) {
        if (arq) snprintf(arq, na, "%.*s", (int)(t1 - p), p);
        if (tam) *tam = atoll(t1 + 1);
        return 1;
      }
    }
    p = f ? f + 1 : NULL;
  }
  return 0;
}
long long mco_usado(void) {
  long long t = 0;
  char *idx;
  pthread_mutex_lock(&trava);
  idx = lerIndice();
  for (const char *p = idx; p && *p;) {
    const char *f = strchr(p, '\n'), *t1 = strchr(p, '\t');
    if (t1 && (!f || t1 < f)) t += atoll(t1 + 1);
    p = f ? f + 1 : NULL;
  }
  free(idx);
  pthread_mutex_unlock(&trava);
  return t;
}
int mco_caminho(const char *id, char *url, size_t n) {
  char arq[64], cam[600];
  char *idx;
  int ok;
  struct stat st;
  pthread_mutex_lock(&trava);
  idx = lerIndice();
  ok = achar(idx, id, arq, sizeof arq, NULL);
  free(idx);
  pthread_mutex_unlock(&trava);
  if (!ok || !dados_caminho(cam, sizeof cam, arq) || stat(cam, &st) != 0) return 0;
  snprintf(url, n, "file://%s", cam);
  return 1;
}
typedef struct { FILE *f; long long n, teto; } Sorvo;
static int gravar(const char *d, size_t n, void *u) {
  Sorvo *s = u;
  if (s->n + (long long)n > s->teto) return 0;
  if (fwrite(d, 1, n, s->f) != n) return 0;
  s->n += (long long)n;
  return 1;
}
int mco_baixar(const McFaixa *f) {
  McFonte fo;
  char url[4096], cab[700], arq[64], cam[600], tmp[620], lin[3000];
  const char *loc, *cabs[2] = { NULL, NULL };
  char k[16];
  Sorvo s;
  int st = 0, ok;
  if (mco_caminho(f->id, url, sizeof url)) return MCOF_OK;
  if (!mcfontes_partir(f->id, k, sizeof k, &loc) || !mcfontes_achar(k, &fo)) return MCOF_FONTE;
  cab[0] = 0;
  if (fo.tipo == MCS_SUBSONIC) ss_url_download(&fo.ss, loc, url, sizeof url);
  else { dav_url(&fo.dv, loc, url, sizeof url); dav_cabecalho(&fo.dv, cab, sizeof cab); if (cab[0]) cabs[0] = cab; }
  nomeArquivo(f, arq, sizeof arq);
  if (!dados_caminho(cam, sizeof cam, arq)) return MCOF_DISCO;
  snprintf(tmp, sizeof tmp, "%s.part", cam);
  s.f = fopen(tmp, "wb");
  if (!s.f) return MCOF_DISCO;
  s.n = 0;
  s.teto = MCO_TETO - mco_usado();
  if (s.teto <= 0) { fclose(s.f); remove(tmp); return MCOF_SEM_ESPACO; }
  ok = rede_baixar_fluxo(url, 600, cabs[0] ? cabs : NULL, gravar, &s, s.teto + 1, &st, NULL);
  memset(url, 0, sizeof url);
  memset(cab, 0, sizeof cab);
  fclose(s.f);
  // A Subsonic error comes back as JSON/XML with HTTP 200: a tiny body that is not audio.
  if (ok && s.n < 4096) {
    char *b = NULL;
    FILE *x = fopen(tmp, "rb");
    if (x) { b = calloc(1, 4097); if (b) { size_t r = fread(b, 1, 4096, x); (void)r; } fclose(x); }
    if (b && strstr(b, "subsonic-response")) {
      int cod = (int)js_num(b, NULL, "code", 0);
      free(b);
      remove(tmp);
      return cod == 50 ? MCOF_SEM_PERMISSAO : MCOF_FONTE;
    }
    free(b);
  }
  if (!ok || s.n <= 0) {
    remove(tmp);
    if (s.n >= s.teto) return MCOF_SEM_ESPACO;
    return st == 401 || st == 403 ? MCOF_SEM_PERMISSAO : MCOF_REDE;
  }
  if (rename(tmp, cam) != 0) { remove(tmp); return MCOF_DISCO; }
  pthread_mutex_lock(&trava);
  {
    char *idx = lerIndice();
    size_t cap = (idx ? strlen(idx) : 0) + 3200;
    char *novo = malloc(cap);
    if (novo && mcfila_linha(f, lin, sizeof lin)) {
      snprintf(novo, cap, "%s%s\t%lld\t%s\n", idx ? idx : "", arq, s.n, lin);
      dados_gravar("music-offline.txt", novo);
    }
    free(novo);
    free(idx);
  }
  pthread_mutex_unlock(&trava);
  printf("[musica] copia offline salva (%lld KiB)\n", s.n / 1024);
  fflush(stdout);
  return MCOF_OK;
}
int mco_apagar(const char *id) {
  char arq[64], cam[600];
  char *idx, *novo;
  int ok;
  pthread_mutex_lock(&trava);
  idx = lerIndice();
  ok = achar(idx, id, arq, sizeof arq, NULL);
  if (ok) {
    size_t k = 0;
    novo = calloc(1, strlen(idx) + 1);
    for (const char *p = idx; novo && p && *p;) {
      const char *f = strchr(p, '\n');
      size_t len = f ? (size_t)(f - p) + 1 : strlen(p);
      if (strncmp(p, arq, strlen(arq)) || p[strlen(arq)] != '\t') { memcpy(novo + k, p, len); k += len; }
      p = f ? f + 1 : NULL;
    }
    if (novo) { if (k) dados_gravar("music-offline.txt", novo); else dados_apagar("music-offline.txt"); }
    free(novo);
    if (dados_caminho(cam, sizeof cam, arq)) remove(cam);
  }
  free(idx);
  pthread_mutex_unlock(&trava);
  return ok;
}
int mco_n(void) {
  int n = 0;
  char *idx;
  pthread_mutex_lock(&trava);
  idx = lerIndice();
  for (char *p = idx; p && *p; p++) if (*p == '\n') n++;
  free(idx);
  pthread_mutex_unlock(&trava);
  return n;
}
int mco_item(int i, McFaixa *out) {
  int k = 0, ok = 0;
  char *idx;
  pthread_mutex_lock(&trava);
  idx = lerIndice();
  for (const char *p = idx; p && *p && !ok;) {
    const char *f = strchr(p, '\n'), *t1 = strchr(p, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
    if (k++ == i && t2 && (!f || t2 < f)) {
      char lin[3000];
      snprintf(lin, sizeof lin, "%.*s", (int)((f ? f : p + strlen(p)) - t2 - 1), t2 + 1);
      ok = mcfila_ler_linha(lin, out);
    }
    p = f ? f + 1 : NULL;
  }
  free(idx);
  pthread_mutex_unlock(&trava);
  return ok;
}
