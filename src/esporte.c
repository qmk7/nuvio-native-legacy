#include "esporte.h"
#include "dados.h"
#include "perfis.h"
#include "perfisessao.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIMES_MAX 128
#define LIGAS_SET_MAX 64
#define LIG_MAX 256
#define NAO_MAX 256
typedef struct { char liga[32], id[16], nome[64]; } Time;
typedef struct { char liga[32]; int on; } LigaSet;
typedef struct { char evento[24], canal[80], nome[96]; } Lig;
typedef struct { char evento[24], canal[80]; } Nao;
static Time times[TIMES_MAX]; static int nTimes;
static LigaSet ligas[LIGAS_SET_MAX]; static int nLigas;
static Lig ligs[LIG_MAX]; static int nLigs;
static Nao naos[NAO_MAX]; static int nNaos;
static int spoiler, perfilLido = -1;
static uint64_t sessaoLida;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;

static void arquivo(char *d, size_t n) { int p = perfis_ativo(); snprintf(d, n, "esporte-p%d.txt", p > 0 ? p : 1); }
static void limpar(char *s) { for (; *s; s++) if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' '; }
static void gravarLocked(void) {
  size_t cap = 64 + (size_t)(nTimes + nLigas + nLigs + nNaos) * 260, k = 0;
  char *t = malloc(cap), nome[40];
  if (!t) return;
  t[0] = 0;
  k += (size_t)snprintf(t + k, cap - k, "spoiler\t%d\n", spoiler);
  for (int i = 0; i < nTimes; i++) k += (size_t)snprintf(t + k, cap - k, "time\t%s\t%s\t%s\n", times[i].liga, times[i].id, times[i].nome);
  for (int i = 0; i < nLigas; i++) k += (size_t)snprintf(t + k, cap - k, "liga\t%s\t%d\n", ligas[i].liga, ligas[i].on);
  for (int i = 0; i < nLigs; i++) k += (size_t)snprintf(t + k, cap - k, "canal\t%s\t%s\t%s\n", ligs[i].evento, ligs[i].canal, ligs[i].nome);
  for (int i = 0; i < nNaos; i++) k += (size_t)snprintf(t + k, cap - k, "refused\t%s\t%s\n", naos[i].evento, naos[i].canal);
  arquivo(nome, sizeof nome);
  if (nv_perfil_sessao_travar(sessaoLida)) { dados_gravar(nome, t); nv_perfil_sessao_destravar(); }
  free(t);
}
static void garantirLocked(void) {
  char nome[40], *t, *p;
  int perfil = perfis_ativo();
  uint64_t sessao = nv_perfil_sessao_ler();
  if (perfil == perfilLido && sessao == sessaoLida) return;
  perfilLido = perfil; sessaoLida = sessao;
  nTimes = nLigas = nLigs = nNaos = 0; spoiler = 0;
  arquivo(nome, sizeof nome);
  t = dados_ler(nome);
  for (p = t; p && *p;) {
    char *fim = strchr(p, '\n'), *c[5];
    int nc = 0;
    if (fim) *fim = 0;
    c[nc++] = p;
    for (char *q = p; *q && nc < 5; q++) if (*q == '\t') { *q = 0; c[nc++] = q + 1; }
    if (!strcmp(c[0], "spoiler") && nc >= 2) spoiler = atoi(c[1]) != 0;
    else if (!strcmp(c[0], "time") && nc >= 4 && nTimes < TIMES_MAX) {
      snprintf(times[nTimes].liga, sizeof times[0].liga, "%s", c[1]);
      snprintf(times[nTimes].id, sizeof times[0].id, "%s", c[2]);
      snprintf(times[nTimes].nome, sizeof times[0].nome, "%s", c[3]);
      nTimes++;
    } else if (!strcmp(c[0], "liga") && nc >= 3 && nLigas < LIGAS_SET_MAX) {
      snprintf(ligas[nLigas].liga, sizeof ligas[0].liga, "%s", c[1]);
      ligas[nLigas++].on = atoi(c[2]) != 0;
    } else if (!strcmp(c[0], "canal") && nc >= 4 && nLigs < LIG_MAX) {
      snprintf(ligs[nLigs].evento, sizeof ligs[0].evento, "%s", c[1]);
      snprintf(ligs[nLigs].canal, sizeof ligs[0].canal, "%s", c[2]);
      snprintf(ligs[nLigs].nome, sizeof ligs[0].nome, "%s", c[3]);
      nLigs++;
    } else if (!strcmp(c[0], "refused") && nc >= 3 && nNaos < NAO_MAX) {
      snprintf(naos[nNaos].evento, sizeof naos[0].evento, "%s", c[1]);
      snprintf(naos[nNaos].canal, sizeof naos[0].canal, "%s", c[2]);
      nNaos++;
    }
    p = fim ? fim + 1 : NULL;
  }
  free(t);
}
void esporte_carregar(void) {
  pthread_mutex_lock(&trava);
  perfilLido = -1;
  garantirLocked();
  pthread_mutex_unlock(&trava);
}

int esporte_segue_liga(const char *liga) {
  int r = -1;
  const EspLiga *l = espn_liga(liga);
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nLigas; i++) if (!strcmp(ligas[i].liga, liga)) r = ligas[i].on;
  pthread_mutex_unlock(&trava);
  return r >= 0 ? r : (l ? l->padrao : 0);
}
void esporte_definir_liga(const char *liga, int seguir) {
  int i;
  if (!espn_liga(liga)) return;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (i = 0; i < nLigas && strcmp(ligas[i].liga, liga); i++) {}
  if (i == nLigas && nLigas < LIGAS_SET_MAX) { snprintf(ligas[nLigas].liga, sizeof ligas[0].liga, "%s", liga); nLigas++; }
  if (i < nLigas) ligas[i].on = !!seguir;
  gravarLocked();
  pthread_mutex_unlock(&trava);
}
int esporte_segue_time(const char *liga, const char *id) {
  int r = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nTimes && !r; i++) r = !strcmp(times[i].id, id) && (!liga || !strcmp(times[i].liga, liga));
  pthread_mutex_unlock(&trava);
  return r;
}
void esporte_alternar_time(const char *liga, const char *id, const char *nome) {
  int i;
  if (!liga || !id || !id[0]) return;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (i = 0; i < nTimes && !(!strcmp(times[i].id, id) && !strcmp(times[i].liga, liga)); i++) {}
  if (i < nTimes) times[i] = times[--nTimes];
  else if (nTimes < TIMES_MAX) {
    snprintf(times[nTimes].liga, sizeof times[0].liga, "%s", liga);
    snprintf(times[nTimes].id, sizeof times[0].id, "%s", id);
    snprintf(times[nTimes].nome, sizeof times[0].nome, "%s", nome ? nome : "");
    limpar(times[nTimes].nome);
    nTimes++;
  }
  gravarLocked();
  pthread_mutex_unlock(&trava);
}
int esporte_times_seguidos(char (*l)[32], char (*ids)[16], char (*nomes)[64], int max) {
  int n = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nTimes && n < max; i++, n++) {
    if (l) snprintf(l[n], 32, "%s", times[i].liga);
    if (ids) snprintf(ids[n], 16, "%s", times[i].id);
    if (nomes) snprintf(nomes[n], 64, "%s", times[i].nome);
  }
  pthread_mutex_unlock(&trava);
  return n;
}
int esporte_esconder_placar(void) { int r; pthread_mutex_lock(&trava); garantirLocked(); r = spoiler; pthread_mutex_unlock(&trava); return r; }
void esporte_definir_esconder_placar(int on) {
  pthread_mutex_lock(&trava); garantirLocked(); spoiler = !!on; gravarLocked(); pthread_mutex_unlock(&trava);
}
int esporte_e_meu(const EspEvento *e) {
  return esporte_segue_time(e->liga, e->casa.id) || esporte_segue_time(e->liga, e->fora.id);
}

int esporte_canais(const char *evento, EspCanal *out, int max) {
  int n = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nLigs && n < max; i++) if (!strcmp(ligs[i].evento, evento)) {
    snprintf(out[n].canal, sizeof out[n].canal, "%s", ligs[i].canal);
    snprintf(out[n].nome, sizeof out[n].nome, "%s", ligs[i].nome);
    n++;
  }
  pthread_mutex_unlock(&trava);
  return n;
}
int esporte_ligar(const char *evento, const char *canal, const char *nome) {
  int i, ok = 0, ja = 0;
  if (!evento || !evento[0] || !canal || !canal[0] || strlen(canal) >= sizeof ligs[0].canal) return 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (i = 0; i < nLigs; i++) if (!strcmp(ligs[i].evento, evento) && !strcmp(ligs[i].canal, canal)) ja = 1;
  { int k = 0; for (i = 0; i < nLigs; i++) k += !strcmp(ligs[i].evento, evento); if (k >= ESP_CANAIS_MAX) ja = 2; }
  if (!ja && nLigs < LIG_MAX) {
    snprintf(ligs[nLigs].evento, sizeof ligs[0].evento, "%s", evento);
    snprintf(ligs[nLigs].canal, sizeof ligs[0].canal, "%s", canal);
    snprintf(ligs[nLigs].nome, sizeof ligs[0].nome, "%s", nome ? nome : canal);
    limpar(ligs[nLigs].nome);
    nLigs++;
    // A channel linked by hand is no longer "not this one".
    { int w = 0; for (i = 0; i < nNaos; i++) if (strcmp(naos[i].evento, evento) || strcmp(naos[i].canal, canal)) naos[w++] = naos[i]; nNaos = w; }
    gravarLocked();
    ok = 1;
  }
  pthread_mutex_unlock(&trava);
  return ok || ja == 1;
}
int esporte_tornar_principal(const char *evento, const char *canal) {
  int i, primeiro = -1, ok = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (i = 0; i < nLigs; i++) if (!strcmp(ligs[i].evento, evento)) {
    if (primeiro < 0) primeiro = i;
    if (!strcmp(ligs[i].canal, canal)) {
      Lig t = ligs[i];
      memmove(&ligs[primeiro + 1], &ligs[primeiro], sizeof *ligs * (size_t)(i - primeiro));
      ligs[primeiro] = t;
      ok = 1;
      break;
    }
  }
  if (ok) gravarLocked();
  pthread_mutex_unlock(&trava);
  return ok;
}
int esporte_desligar(const char *evento, const char *canal) {
  int w = 0, saiu = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nLigs; i++) {
    if (!strcmp(ligs[i].evento, evento) && !strcmp(ligs[i].canal, canal)) { saiu = 1; continue; }
    ligs[w++] = ligs[i];
  }
  nLigs = w;
  if (nNaos < NAO_MAX) {
    snprintf(naos[nNaos].evento, sizeof naos[0].evento, "%s", evento);
    snprintf(naos[nNaos].canal, sizeof naos[0].canal, "%s", canal);
    nNaos++;
  }
  gravarLocked();
  pthread_mutex_unlock(&trava);
  return saiu;
}
int esporte_recusado(const char *evento, const char *canal) {
  int r = 0;
  pthread_mutex_lock(&trava);
  garantirLocked();
  for (int i = 0; i < nNaos && !r; i++) r = !strcmp(naos[i].evento, evento) && !strcmp(naos[i].canal, canal);
  pthread_mutex_unlock(&trava);
  return r;
}

// ---- matching ----
static void dobrar(const char *s, char *d, size_t n) {
  size_t k = 0;
  int sep = 1;
  for (; s && *s && k + 2 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c >= 0x80) { d[k++] = (char)(c < 0x80 ? tolower(c) : c); sep = 0; }
    else if (!sep) { d[k++] = ' '; sep = 1; }
  }
  while (k && d[k - 1] == ' ') k--;
  d[k] = 0;
}
static int palavraEm(const char *w, const char *texto) {
  size_t n = strlen(w);
  for (const char *p = texto; (p = strstr(p, w)) != NULL; p++) {
    int ini = p == texto || p[-1] == ' ', fim = p[n] == 0 || p[n] == ' ';
    if (ini && fim) return 1;
  }
  return 0;
}
static int ehClube(const char *w) {
  static const char *const z[] = { "fc", "sc", "cf", "ac", "afc", "cd", "club", "fk", "sk", "sv", "the", NULL };
  for (int i = 0; z[i]; i++) if (!strcmp(w, z[i])) return 1;
  return 0;
}
// All significant words of `nome` appear in `texto` (both folded).
static int nomeEm(const char *nome, const char *texto) {
  char n[96], *w, *sp;
  int algum = 0;
  dobrar(nome, n, sizeof n);
  for (w = strtok_r(n, " ", &sp); w; w = strtok_r(NULL, " ", &sp)) {
    if (ehClube(w) || strlen(w) < 2) continue;
    algum = 1;
    if (!palavraEm(w, texto)) return 0;
  }
  return algum;
}
int esporte_time_em(const EspTime *t, const char *texto) {
  char d[512];
  dobrar(texto, d, sizeof d);
  if (!d[0]) return 0;
  if (t->curto[0] && nomeEm(t->curto, d)) return 1;
  if (t->nome[0] && nomeEm(t->nome, d)) return 1;
  if (strlen(t->abrev) >= 3) { char a[16]; dobrar(t->abrev, a, sizeof a); if (palavraEm(a, d)) return 1; }
  return 0;
}
static int cmpSug(const void *a, const void *b) {
  return ((const EspSugestao *)b)->pontos - ((const EspSugestao *)a)->pontos;
}
int esporte_sugerir(const EspEvento *e, const EspCanalInfo *canais, int n, EspProgsFn progs, void *u,
                    EspSugestao *out, int max) {
  int k = 0;
  char tv[8][48];
  int ntv = 0;
  // ESPN's broadcasters ("beIN SPORTS, SSC") as folded names.
  {
    char t[160], *w, *sp;
    snprintf(t, sizeof t, "%s", e->tv);
    for (w = strtok_r(t, ",", &sp); w && ntv < 8; w = strtok_r(NULL, ",", &sp)) {
      while (*w == ' ') w++;
      dobrar(w, tv[ntv], sizeof tv[0]);
      if (strlen(tv[ntv]) >= 3) ntv++;
    }
  }
  for (int i = 0; i < n && k < max; i++) {
    EspSugestao s;
    char nomeC[160];
    if (!canais[i].id || !canais[i].id[0] || esporte_recusado(e->id, canais[i].id)) continue;
    memset(&s, 0, sizeof s);
    snprintf(s.canal, sizeof s.canal, "%s", canais[i].id);
    snprintf(s.nome, sizeof s.nome, "%s", canais[i].nome ? canais[i].nome : "");
    dobrar(canais[i].nome, nomeC, sizeof nomeC);
    // 2. The channel's own name names both sides.
    if (esporte_time_em(&e->casa, canais[i].nome) && esporte_time_em(&e->fora, canais[i].nome)) {
      s.pontos = 150; s.certeza = ESP_PASSA;
      snprintf(s.motivo, sizeof s.motivo, "%s", "nome do canal");
    }
    // 1 / 3. Programmes on air at kick-off.
    if (progs) {
      char tit[16][128];
      time_t pi[16], pf[16];
      int np = progs(canais[i].id, e->ini - 45 * 60, e->ini + 150 * 60, tit, pi, pf, 16, u);
      for (int j = 0; j < np; j++) {
        int noAr = pi[j] <= e->ini + 15 * 60 && pf[j] > e->ini;
        int a, b, p = 0, cer = 0;
        if (!noAr) continue;
        a = esporte_time_em(&e->casa, tit[j]);
        b = esporte_time_em(&e->fora, tit[j]);
        if (a && b) { p = 160; cer = ESP_PASSA; }
        else if (a || b) { p = 80; cer = ESP_TALVEZ; }
        else {
          for (int t = 0; t < ntv && !p; t++) if (strstr(nomeC, tv[t])) { p = 60; cer = ESP_TALVEZ; }
        }
        if (p > s.pontos) {
          s.pontos = p; s.certeza = cer;
          snprintf(s.motivo, sizeof s.motivo, "%s", tit[j]);
        }
      }
    }
    if (s.pontos > 0) out[k++] = s;
  }
  qsort(out, (size_t)k, sizeof *out, cmpSug);
  return k;
}
