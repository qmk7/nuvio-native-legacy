#include "tsdb.h"
#include "esporte.h"
#include "js.h"
#include "rede.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static char base[160] = "https://www.thesportsdb.com/api/v1/json";
static char chave[64] = "123";
void tsdb_definir_base(const char *b) { snprintf(base, sizeof base, "%s", b ? b : ""); }
void tsdb_definir_chave(const char *k) { snprintf(chave, sizeof chave, "%s", k && k[0] ? k : "123"); }
int tsdb_chave_pessoal(void) { return strcmp(chave, "123") != 0; }

static int regiao(const char *pais) {
  static const char *const me[] = { "saudi", "arab", "middle east", "mena", "qatar", "emirates", "uae", "kuwait",
                                    "bahrain", "oman", "jordan", "egypt", "iraq", "lebanon", "morocco", "algeria",
                                    "tunisia", "libya", "syria", "yemen", "palestine", "sudan", NULL };
  char p[64];
  size_t i;
  for (i = 0; pais[i] && i + 1 < sizeof p; i++) p[i] = (char)(pais[i] >= 'A' && pais[i] <= 'Z' ? pais[i] + 32 : pais[i]);
  p[i] = 0;
  for (int k = 0; me[k]; k++) if (strstr(p, me[k])) return 1;
  return 0;
}
int tsdb_parse_tv(const char *json, TsdbTv *out, int max) {
  const char *p = json ? js_array(json, NULL, "tvevent") : NULL;
  int n = 0;
  while (p && *p == '{' && n < max) {
    const char *f = js_fim(p);
    TsdbTv t;
    if (!f) break;
    memset(&t, 0, sizeof t);
    js_texto(p, f, "strChannel", t.canal, sizeof t.canal);
    js_texto(p, f, "strCountry", t.pais, sizeof t.pais);
    if (t.canal[0]) {
      int dup = 0;
      for (int i = 0; i < n; i++) dup |= !strcasecmp(out[i].canal, t.canal) && !strcasecmp(out[i].pais, t.pais);
      if (!dup) out[n++] = t;
    }
    p = js_prox(f);
  }
  // Middle East first, otherwise the provider's order.
  for (int i = 1; i < n; i++) {
    TsdbTv v = out[i];
    int j = i;
    if (!regiao(v.pais)) continue;
    while (j > 0 && !regiao(out[j - 1].pais)) { out[j] = out[j - 1]; j--; }
    out[j] = v;
  }
  return n;
}
int tsdb_achar_evento(const char *json, const EspEvento *e, char *id, size_t n) {
  const char *p = json ? js_array(json, NULL, "events") : NULL;
  while (p && *p == '{') {
    const char *f = js_fim(p);
    char casa[96] = "", fora[96] = "", ts[40] = "", eid[24] = "";
    time_t t;
    if (!f) break;
    js_texto(p, f, "strHomeTeam", casa, sizeof casa);
    js_texto(p, f, "strAwayTeam", fora, sizeof fora);
    js_texto(p, f, "strTimestamp", ts, sizeof ts);
    js_texto(p, f, "idEvent", eid, sizeof eid);
    t = espn_instante(ts);
    if (!t && ts[0]) { char z[48]; snprintf(z, sizeof z, "%sZ", ts); t = espn_instante(z); }   // some omit the "Z"
    if (eid[0] && (!t || (t > e->ini - 3 * 3600 && t < e->ini + 3 * 3600))) {
      int a = esporte_time_em(&e->casa, casa) || esporte_time_em(&e->casa, fora);
      int b = esporte_time_em(&e->fora, casa) || esporte_time_em(&e->fora, fora);
      if (a && b) { snprintf(id, n, "%s", eid); return 1; }
    }
    p = js_prox(f);
  }
  return 0;
}

// Session cache: event -> channels (also "none"), and day lists per sport.
#define TSDB_CACHE 64
static struct { char evento[24]; int n; TsdbTv tv[TSDB_TV_MAX]; } cache[TSDB_CACHE];
static int nCache;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static long long ultimoPedido;   // ms: requests at least 2.1 s apart (free key ~30/min)

static char *pedir(const char *caminho) {
  char url[400];
  struct timespec ts;
  long long agora;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  agora = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  pthread_mutex_lock(&trava);
  long long espera = ultimoPedido + 2100 - agora;
  ultimoPedido = espera > 0 ? agora + espera : agora;
  pthread_mutex_unlock(&trava);
  if (espera > 0 && espera < 60000) usleep((useconds_t)espera * 1000);
  snprintf(url, sizeof url, "%s/%s/%s", base, chave, caminho);
  return rede_baixar(url, 15);
}
int tsdb_canais(const EspEvento *e, TsdbTv *out, int max) {
  const EspLiga *l = espn_liga(e->liga);
  char cam[160], id[24] = "", dia[16];
  char *c;
  int n;
  struct tm g;
  if (!l) return 0;
  pthread_mutex_lock(&trava);
  for (int i = 0; i < nCache; i++) if (!strcmp(cache[i].evento, e->id)) {
    n = cache[i].n < max ? cache[i].n : max;
    memcpy(out, cache[i].tv, sizeof *out * (size_t)n);
    pthread_mutex_unlock(&trava);
    return n;
  }
  pthread_mutex_unlock(&trava);
  gmtime_r(&e->ini, &g);
  snprintf(dia, sizeof dia, "%04d-%02d-%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);
  snprintf(cam, sizeof cam, "eventsday.php?d=%s&s=%s", dia, !strcmp(l->esporte, "soccer") ? "Soccer" :
           !strcmp(l->esporte, "basketball") ? "Basketball" : "Tennis");
  c = pedir(cam);
  if (!c) return -1;
  n = tsdb_achar_evento(c, e, id, sizeof id);
  free(c);
  if (n) {
    snprintf(cam, sizeof cam, "lookuptv.php?id=%s", id);
    c = pedir(cam);
    if (!c) return -1;
    n = tsdb_parse_tv(c, out, max);
    free(c);
  }
  pthread_mutex_lock(&trava);
  {
    int i = nCache < TSDB_CACHE ? nCache++ : (int)(e->ini % TSDB_CACHE);
    snprintf(cache[i].evento, sizeof cache[i].evento, "%s", e->id);
    cache[i].n = n < TSDB_TV_MAX ? n : TSDB_TV_MAX;
    memcpy(cache[i].tv, out, sizeof *out * (size_t)cache[i].n);
  }
  pthread_mutex_unlock(&trava);
  printf("[esporte] TheSportsDB: %d canal(is) para o evento %s\n", n, e->id);
  fflush(stdout);
  return n;
}
