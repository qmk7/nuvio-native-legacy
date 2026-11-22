// QR TIME AUTOMATIC ARABIC SUBTITLES: when a title has no Arabic subtitle,
// the player's "Legenda árabe (IA)" button takes the subtitle on screen
// (any language), translates it with DeepL (the person's own key, free tier
// 500 000 characters a month: a film is ~60 000) and shows the Arabic one.
// Only the subtitle TEXT goes to DeepL, in batches of 50 lines; timing stays.
// The result is kept (legenda-ar-<hash>.srt, regenerable cache) so the same
// subtitle is never translated twice. Key: Settings > Accounts, providers
// and data > DeepL. UNVERIFIED against DeepL here (no key in this build env).
#include "legtrad.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOTE 50

int legtrad_tem_chave(void) { char *t = dados_ler("deepl.txt"); int r = t && t[0] > ' '; free(t); return r; }
int legtrad_definir_chave(const char *k) {
  if (!k || !k[0]) return dados_apagar("deepl.txt"), 1;
  for (const char *p = k; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == ':')) return 0;
  return dados_gravar("deepl.txt", k);
}
// Free keys end in ":fx" and use api-free.deepl.com.
const char *legtrad_host(const char *chave) {
  size_t n = chave ? strlen(chave) : 0;
  return n > 3 && !strcmp(chave + n - 3, ":fx") ? "https://api-free.deepl.com/v2/translate" : "https://api.deepl.com/v2/translate";
}

static void urlenc(const char *s, char **o, size_t *n, size_t *cap) {
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (*n + 4 >= *cap) { *cap *= 2; *o = realloc(*o, *cap); if (!*o) return; }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') (*o)[(*n)++] = (char)c;
    else *n += (size_t)snprintf(*o + *n, *cap - *n, "%%%02X", c);
  }
  (*o)[*n] = 0;
}
char *legtrad_corpo(const LegendaCue *c, int n) {
  size_t cap = 4096, k = 0;
  char *o = malloc(cap);
  if (!o) return NULL;
  k = (size_t)snprintf(o, cap, "target_lang=AR&tag_handling=xml&preserve_formatting=1");
  for (int i = 0; i < n && o; i++) {
    if (k + 8 >= cap) { cap *= 2; o = realloc(o, cap); if (!o) return NULL; }
    memcpy(o + k, "&text=", 6); k += 6; o[k] = 0;
    urlenc(c[i].texto, &o, &k, &cap);
  }
  return o;
}
// {"translations":[{"detected_source_language":"EN","text":"..."},...]}
int legtrad_parse(const char *json, char (*out)[768], int max) {
  const char *p;
  int n = 0;
  if (!json) return -1;
  for (p = js_array(json, NULL, "translations"); p && *p == '{' && n < max; ) {
    const char *f = js_fim(p);
    if (!f) break;
    out[n][0] = 0;
    js_texto_raiz_em(p, f, "text", out[n], sizeof out[n]);
    n++;
    p = js_prox(f);
  }
  return n;
}
static void tempoSrt(char *d, size_t n, double t) {
  long ms = (long)(t * 1000.0 + 0.5);
  snprintf(d, n, "%02ld:%02ld:%02ld,%03ld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
}
char *legtrad_srt(const LegendaCue *c, int n) {
  size_t cap = 1024 + (size_t)n * 900, k = 0;
  char *o = malloc(cap);
  if (!o) return NULL;
  o[0] = 0;
  for (int i = 0; i < n; i++) {
    char a[16], b[16];
    tempoSrt(a, sizeof a, c[i].inicio); tempoSrt(b, sizeof b, c[i].fim);
    k += (size_t)snprintf(o + k, cap - k, "%d\n%s --> %s\n%s\n\n", i + 1, a, b, c[i].texto);
  }
  return o;
}
static unsigned long hashCues(const LegendaCue *c, int n) {
  unsigned long h = 5381;
  for (int i = 0; i < n; i++) for (const char *p = c[i].texto; *p; p++) h = h * 33 ^ (unsigned char)*p;
  return h ^ (unsigned long)n;
}

// ---------------------------------------------------------------- background
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int rodando, pronto, res;
static char *srtPronto;
typedef struct { LegendaCue *c; int n; } Trabalho;
static void *fio(void *arg) {
  Trabalho *w = arg;
  char nome[64], *chave = dados_ler("deepl.txt"), *cache, cab[200];
  const char *cabs[3] = { "Content-Type: application/x-www-form-urlencoded", NULL, NULL };
  int r = LT_OK;
  snprintf(nome, sizeof nome, "legenda-ar-%08lx.srt", hashCues(w->c, w->n));
  if ((cache = dados_ler(nome)) != NULL) { printf("[legtrad] da memoria (%d linhas)\n", w->n); goto fim; }
  if (!chave || !chave[0]) { r = LT_SEM_CHAVE; goto fim; }
  chave[strcspn(chave, "\r\n")] = 0;
  snprintf(cab, sizeof cab, "Authorization: DeepL-Auth-Key %s", chave);
  cabs[1] = cab;
  for (int i = 0; i < w->n && r == LT_OK; i += LOTE) {
    int m = w->n - i < LOTE ? w->n - i : LOTE, st = 0, got;
    char *corpo = legtrad_corpo(w->c + i, m), *resp;
    char (*tr)[768] = calloc((size_t)m, sizeof *tr);
    if (!corpo || !tr) { free(corpo); free(tr); r = LT_ERRO; break; }
    resp = rede_postar_st(legtrad_host(chave), 30, cabs, corpo, &st);
    free(corpo);
    got = resp && st >= 200 && st < 300 ? legtrad_parse(resp, tr, m) : -1;
    free(resp);
    if (got != m) r = st == 403 || st == 401 ? LT_CHAVE_RECUSADA : st == 456 ? LT_COTA : LT_REDE;
    else for (int j = 0; j < m; j++) snprintf(w->c[i + j].texto, sizeof w->c[i + j].texto, "%s", tr[j]);
    free(tr);
  }
  memset(cab, 0, sizeof cab);
  if (r == LT_OK) { cache = legtrad_srt(w->c, w->n); if (cache) dados_gravar(nome, cache); printf("[legtrad] %d linhas traduzidas\n", w->n); }
fim:
  if (chave) { memset(chave, 0, strlen(chave)); free(chave); }
  free(w->c); free(w);
  pthread_mutex_lock(&trava);
  free(srtPronto); srtPronto = r == LT_OK ? cache : NULL;
  if (r != LT_OK) free(cache);
  res = r; pronto = 1; rodando = 0;
  pthread_mutex_unlock(&trava);
  fflush(stdout);
  return NULL;
}
int legtrad_iniciar(void) {
  Trabalho *w;
  pthread_t t;
  LegendaCue *c;
  int n = legenda_copiar_cues(&c);
  if (n <= 0) return LT_SEM_LEGENDA;
  pthread_mutex_lock(&trava);
  if (rodando) { pthread_mutex_unlock(&trava); free(c); return LT_OCUPADO; }
  rodando = 1; pronto = 0;
  pthread_mutex_unlock(&trava);
  if (!(w = calloc(1, sizeof *w))) { free(c); rodando = 0; return LT_ERRO; }
  w->c = c; w->n = n;
  if (pthread_create(&t, NULL, fio, w)) { free(c); free(w); rodando = 0; return LT_ERRO; }
  pthread_detach(t);
  return LT_OK;
}
int legtrad_resultado(char **srt) {
  int r = -1;
  pthread_mutex_lock(&trava);
  if (pronto) { r = res; *srt = srtPronto; srtPronto = NULL; pronto = 0; }
  pthread_mutex_unlock(&trava);
  return r;
}
