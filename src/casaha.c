// QR TIME FOR HOME ASSISTANT (and Alexa through it): "play this film" from
// outside the TV. Home Assistant posts to the settings page's fixed link
// (ajremoto.c) on the home network:
//   POST http://<tv>:8098/<token>/tocar   q=<title or tt id>&tipo=filme|serie
//   POST http://<tv>:8098/<token>/tecla   k=up|down|left|right|ok|back|play
// A title is turned into its IMDb id by Cinemeta's search (the TITLE goes
// out, nothing else); then the title page opens and plays with the automatic
// source, the same path as "Watch" on a Continue watching card.
// Waking the TV and opening the app is Home Assistant's part (LG webOS TV
// integration: Wake-on-LAN, then launch com.qrtime.tv).
#include "casaha.h"
#include "iptvvod.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static char pronto[24];
static int fioVivo;
static char cineBase[160] = "https://v3-cinemeta.strem.io";
void casaha_teste_base(const char *b) { snprintf(cineBase, sizeof cineBase, "%s", b ? b : ""); }

static int ehTt(const char *s) {
  size_t i;
  if (!s || s[0] != 't' || s[1] != 't') return 0;
  for (i = 2; isdigit((unsigned char)s[i]); i++) {}
  return i >= 4 && i <= 12 && !s[i];
}
// Best Cinemeta answer for `q`: the first whose normalised name is exactly
// the query, else the first one (Cinemeta ranks by popularity). 1 = found.
int casaha_escolher(const char *json, const char *q, char *imdb, size_t n) {
  char nq[128], primeiro[24] = "";
  const char *p;
  iptvvod_normalizar(q, nq, sizeof nq);
  for (p = js_array(json ? json : "", NULL, "metas"); p && *p == '{'; ) {
    const char *f = js_fim(p);
    char nome[200] = "", nn[128], id[24] = "";
    if (!f) break;
    js_texto_raiz_em(p, f, "name", nome, sizeof nome);
    if (!js_texto_raiz_em(p, f, "imdb_id", id, sizeof id) || !id[0]) js_texto_raiz_em(p, f, "id", id, sizeof id);
    if (ehTt(id)) {
      iptvvod_normalizar(nome, nn, sizeof nn);
      if (!strcmp(nn, nq)) { snprintf(imdb, n, "%s", id); return 1; }
      if (!primeiro[0]) snprintf(primeiro, sizeof primeiro, "%s", id);
    }
    p = js_prox(f);
  }
  if (primeiro[0]) { snprintf(imdb, n, "%s", primeiro); return 1; }
  return 0;
}
typedef struct { char q[200]; char tipo[16]; } Pedido;
static int buscar(const char *tipo, const char *q, char *imdb, size_t n) {
  char url[600], e[400];
  size_t k = 0;
  char *r;
  int ok;
  for (const char *s = q; *s && k + 4 < sizeof e; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') e[k++] = (char)c;
    else k += (size_t)snprintf(e + k, sizeof e - k, "%%%02X", c);
  }
  e[k] = 0;
  snprintf(url, sizeof url, "%s/catalog/%s/top/search=%s.json", cineBase, tipo, e);
  r = rede_baixar(url, 10);
  ok = casaha_escolher(r, q, imdb, n);
  free(r);
  return ok;
}
static void *fio(void *u) {
  Pedido *p = u;
  char id[24] = "";
  int ok = 0;
  if (ehTt(p->q)) { snprintf(id, sizeof id, "%s", p->q); ok = 1; }
  else if (!strcmp(p->tipo, "serie")) ok = buscar("series", p->q, id, sizeof id);
  else ok = buscar("movie", p->q, id, sizeof id) || (strcmp(p->tipo, "filme") && buscar("series", p->q, id, sizeof id));
  printf("[casa] pedido de reproducao: %s\n", ok ? id : "titulo nao encontrado");
  fflush(stdout);
  pthread_mutex_lock(&trava);
  if (ok) snprintf(pronto, sizeof pronto, "%s", id);
  fioVivo = 0;
  pthread_mutex_unlock(&trava);
  free(p);
  return NULL;
}
int casaha_tocar(const char *tipo, const char *q) {
  Pedido *p;
  pthread_t t;
  if (!q || !q[0]) return 0;
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); return 0; }
  fioVivo = 1;
  pthread_mutex_unlock(&trava);
  if (!(p = calloc(1, sizeof *p))) { fioVivo = 0; return 0; }
  snprintf(p->q, sizeof p->q, "%s", q);
  snprintf(p->tipo, sizeof p->tipo, "%s", tipo ? tipo : "");
  if (pthread_create(&t, NULL, fio, p)) { free(p); fioVivo = 0; return 0; }
  pthread_detach(t);
  return 1;
}
int casaha_pegar(char *imdb, size_t n) {
  int r = 0;
  pthread_mutex_lock(&trava);
  if (pronto[0]) { snprintf(imdb, n, "%s", pronto); pronto[0] = 0; r = 1; }
  pthread_mutex_unlock(&trava);
  return r;
}
