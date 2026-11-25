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
static char pronto[64];
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
// the query, else the first one (Cinemeta ranks by popularity). 1 = found;
// *exato says which, *nota is that title's IMDb rating (0 unknown).
static int escolher(const char *json, const char *q, char *imdb, size_t n, int *exato, double *nota) {
  char nq[128], primeiro[24] = "";
  double notaPrimeiro = 0;
  const char *p;
  *exato = 0; *nota = 0;
  iptvvod_normalizar(q, nq, sizeof nq);
  for (p = js_array(json ? json : "", NULL, "metas"); p && *p == '{'; ) {
    const char *f = js_fim(p);
    char nome[200] = "", nn[128], id[24] = "", r[16] = "";
    if (!f) break;
    js_texto_raiz_em(p, f, "name", nome, sizeof nome);
    if (!js_texto_raiz_em(p, f, "imdb_id", id, sizeof id) || !id[0]) js_texto_raiz_em(p, f, "id", id, sizeof id);
    js_texto_raiz_em(p, f, "imdbRating", r, sizeof r);
    if (ehTt(id)) {
      iptvvod_normalizar(nome, nn, sizeof nn);
      if (!strcmp(nn, nq)) { snprintf(imdb, n, "%s", id); *exato = 1; *nota = atof(r); return 1; }
      if (!primeiro[0]) { snprintf(primeiro, sizeof primeiro, "%s", id); notaPrimeiro = atof(r); }
    }
    p = js_prox(f);
  }
  if (primeiro[0]) { snprintf(imdb, n, "%s", primeiro); *nota = notaPrimeiro; return 1; }
  return 0;
}
int casaha_escolher(const char *json, const char *q, char *imdb, size_t n) {
  int e; double r;
  return escolher(json, q, imdb, n, &e, &r);
}
// Movie or show with the same name ("Friends"): the exact one; both exact,
// the better-rated (the famous one); neither, the movie's first, then the show's.
int casaha_entre_tipos(const char *jFilme, const char *jSerie, const char *q, char *imdb, size_t n) {
  char a[24] = "", b[24] = "";
  int ea = 0, eb = 0;
  double na = 0, nb = 0;
  int ta = escolher(jFilme, q, a, sizeof a, &ea, &na), tb = escolher(jSerie, q, b, sizeof b, &eb, &nb);
  const char *r = NULL;
  if (ta && tb && ea && eb) r = nb > na ? b : a;
  else if (tb && eb) r = b;
  else if (ta && ea) r = a;
  else if (ta) r = a;
  else if (tb) r = b;
  if (!r) return 0;
  snprintf(imdb, n, "%s", r);
  return 1;
}
static int (*local)(const char *q, const char *tipo, char *id, size_t n);
void casaha_definir_local(int (*f)(const char *q, const char *tipo, char *id, size_t n)) { local = f; }
typedef struct { char q[200]; char tipo[16]; } Pedido;
static char *baixar(const char *tipo, const char *q) {
  char url[600], e[400];
  size_t k = 0;
  for (const char *s = q; *s && k + 4 < sizeof e; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.') e[k++] = (char)c;
    else k += (size_t)snprintf(e + k, sizeof e - k, "%%%02X", c);
  }
  e[k] = 0;
  snprintf(url, sizeof url, "%s/catalog/%s/top/search=%s.json", cineBase, tipo, e);
  return rede_baixar(url, 10);
}
static void *fio(void *u) {
  Pedido *p = u;
  char id[24] = "";
  int ok = 0;
  if (ehTt(p->q)) { snprintf(id, sizeof id, "%s", p->q); ok = 1; }
  else {
    char *jf = strcmp(p->tipo, "serie") ? baixar("movie", p->q) : NULL;
    char *js = strcmp(p->tipo, "filme") ? baixar("series", p->q) : NULL;
    ok = casaha_entre_tipos(jf, js, p->q, id, sizeof id);
    free(jf); free(js);
  }
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
  // What the person has (Continue watching, what they watched, their home
  // rows) comes first: "play Friends" means THEIR Friends, at their episode.
  { char id[64];
    if (local && local(q, tipo ? tipo : "", id, sizeof id)) {
      pthread_mutex_lock(&trava);
      snprintf(pronto, sizeof pronto, "%s", id);
      pthread_mutex_unlock(&trava);
      printf("[casa] pedido de reproducao: %s (da conta)\n", id); fflush(stdout);
      return 1;
    } }
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
int casaha_pendente(void) {
  int r;
  pthread_mutex_lock(&trava);
  r = fioVivo || pronto[0];
  pthread_mutex_unlock(&trava);
  return r;
}
int casaha_pegar(char *imdb, size_t n) {
  int r = 0;
  pthread_mutex_lock(&trava);
  if (pronto[0]) { snprintf(imdb, n, "%s", pronto); pronto[0] = 0; r = 1; }
  pthread_mutex_unlock(&trava);
  return r;
}
