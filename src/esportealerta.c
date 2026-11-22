// QR TIME: alerts for the teams the profile follows in the Sport Center
// (Settings > TV ao vivo > Centro de Esportes > "Avisos dos seus times").
//   1. Each upcoming match of a followed team (next 48 h) gets a reminder
//      ONCE, through the guide reminder list (lembrete.h): the card one
//      minute before kick-off with "Watch" on the linked channel, or the
//      match page when no channel is linked. Cancelling it is respected: a
//      match is handled once (esporte-alertas-p<N>.txt).
//   2. With "reminders and goals", a goal in a followed team's live match is
//      a notice on the island, unless the profile hides scores.
// ESPN is asked from a worker every 15 min, every 60 s while a followed
// match is live or starts within 20 min. Only leagues of followed teams.
#include "esportealerta.h"
#include "ajustes.h"
#include "dados.h"
#include "espn.h"
#include "esporte.h"
#include "esportenomes.h"
#include "idioma.h"
#include "ilha.h"
#include "lembrete.h"
#include "ondever.h"
#include "perfis.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 24
#define MAXE 64
static char tLiga[MAXT][32], tId[MAXT][16];
static int nT;
static EspEvento ev[MAXE], antes[MAXE];
static int nEv, nAntes, fioVivo, pronto;
static time_t proxima;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;

typedef struct { EspEvento v[MAXE]; int n; char tl[MAXT][32], ti[MAXT][16]; int nt; } Coleta;
static int coletar(const EspEvento *e, void *u) {
  Coleta *c = u;
  if (c->n >= MAXE) return 0;
  for (int i = 0; i < c->nt; i++)
    if (!strcmp(c->tl[i], e->liga) && (!strcmp(c->ti[i], e->casa.id) || !strcmp(c->ti[i], e->fora.id))) { c->v[c->n++] = *e; break; }
  return 0;
}
static void *fio(void *arg) {
  char ligas[MAXT][32];
  int nl = 0;
  time_t agora = time(NULL), quando;
  Coleta *c = calloc(1, sizeof *c);
  (void)arg;
  pthread_mutex_lock(&trava);
  if (c) { c->nt = nT; memcpy(c->tl, tLiga, sizeof tLiga); memcpy(c->ti, tId, sizeof tId); }
  for (int i = 0; i < nT; i++) {
    int ja = 0;
    for (int j = 0; j < nl; j++) if (!strcmp(ligas[j], tLiga[i])) ja = 1;
    if (!ja) snprintf(ligas[nl++], sizeof ligas[0], "%s", tLiga[i]);
  }
  pthread_mutex_unlock(&trava);
  for (int j = 0; c && j < nl; j++) {
    int falhou = 0;
    espn_buscar(ligas[j], agora, coletar, c, &quando, &falhou);   // network: no lock held
  }
  pthread_mutex_lock(&trava);
  if (c) { memcpy(ev, c->v, sizeof *ev * (size_t)c->n); nEv = c->n; }
  pronto = 1; fioVivo = 0;
  free(c);
  pthread_mutex_unlock(&trava);
  return NULL;
}

// ---- matches already handled (per profile) ----
static int jaTratado(const char *id) {
  char n[48], *t, *p;
  int r = 0;
  snprintf(n, sizeof n, "esporte-alertas-p%d.txt", perfis_ativo());
  if (!(t = dados_ler(n))) return 0;
  for (p = t; (p = strstr(p, id)) != NULL; p++)
    if ((p == t || p[-1] == '\n') && (p[strlen(id)] == '\n' || !p[strlen(id)])) { r = 1; break; }
  free(t);
  return r;
}
static void marcarTratado(const char *id) {
  char n[48], *t, *novo;
  size_t l;
  snprintf(n, sizeof n, "esporte-alertas-p%d.txt", perfis_ativo());
  t = dados_ler(n);
  l = t ? strlen(t) : 0;
  if (l > 6000) { char *c = strchr(t + l - 4000, '\n'); memmove(t, c ? c + 1 : t, strlen(c ? c + 1 : t) + 1); l = strlen(t); }   // keep it short
  if ((novo = malloc(l + strlen(id) + 2))) {
    snprintf(novo, l + strlen(id) + 2, "%s%s\n", t ? t : "", id);
    dados_gravar(n, novo);
    free(novo);
  }
  free(t);
}

static void titulo(const EspEvento *e, char *d, size_t n) {
  snprintf(d, n, "%s × %s", esporte_nome_tela(e->casa.nome), esporte_nome_tela(e->fora.nome));
}
int esportealerta_lembrar(const EspEvento *e, time_t agora) {
  char canal[96], nome[120], t[200];
  EspCanal c[1];
  if (e->estado != ESP_PRE || e->ini <= agora || e->ini > agora + 48 * 3600 || jaTratado(e->id)) return 0;
  if (esporte_canais(e->id, c, 1) == 1) { snprintf(canal, sizeof canal, "%s", c[0].canal); snprintf(nome, sizeof nome, "%s", c[0].nome); }
  else { snprintf(canal, sizeof canal, "esporte:%s", e->id); snprintf(nome, sizeof nome, "%s", "Sport Center"); }
  titulo(e, t, sizeof t);
  marcarTratado(e->id);
  if (lembrete_achar(canal, e->ini, t) >= 0) return 0;
  if (lembrete_alternar(canal, nome, t, "", e->ini, e->ini + 2 * 3600) != 1) return 0;
  printf("[esporte] lembrete automatico: evento %s\n", e->id);
  fflush(stdout);
  return 1;
}
int esportealerta_gol(const EspEvento *velho, const EspEvento *novo, char *msg, size_t n) {
  if (!velho || novo->estado != ESP_AO_VIVO || strcmp(velho->id, novo->id)) return 0;
  if (!strcmp(velho->casa.placar, novo->casa.placar) && !strcmp(velho->fora.placar, novo->fora.placar)) return 0;
  if (!novo->casa.placar[0] || !novo->fora.placar[0]) return 0;
  if (atoi(novo->casa.placar) + atoi(novo->fora.placar) <= atoi(velho->casa.placar) + atoi(velho->fora.placar)) return 0;   // corrections down
  snprintf(msg, n, "%s %s – %s %s", esporte_nome_tela(novo->casa.curto[0] ? novo->casa.curto : novo->casa.nome), novo->casa.placar,
           novo->fora.placar, esporte_nome_tela(novo->fora.curto[0] ? novo->fora.curto : novo->fora.nome));
  return 1;
}

// A followed team's match just ended: highlights in the official YouTube app
// (a search there; nothing is fetched or played here). With scores hidden
// the notice does not say the score.
static char hlBusca[8][200], hlId[8][24];
static int hlN;
static void fimDeJogo(const EspEvento *e) {
  IlhaModal m;
  IlhaAvisoEx a;
  char chave[48], txt[260];
  int k = hlN++ % 8;
  snprintf(hlId[k], sizeof hlId[k], "%s", e->id);
  snprintf(hlBusca[k], sizeof hlBusca[k], "%s vs %s highlights", e->casa.nome, e->fora.nome);   // YouTube titles are mostly English
  memset(&m, 0, sizeof m); memset(&a, 0, sizeof a);
  snprintf(chave, sizeof chave, "esportehl:%s", e->id);
  snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Fim de jogo"));
  titulo(e, m.titulo, sizeof m.titulo);
  if (!esporte_esconder_placar()) snprintf(m.linha, sizeof m.linha, "%s – %s", e->casa.placar, e->fora.placar);
  snprintf(m.icone, sizeof m.icone, "aj_trophy");
  m.nBotoes = 2;
  snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Melhores momentos"));
  snprintf(m.botao[1], sizeof m.botao[1], "%s", i18n("Fechar"));
  snprintf(txt, sizeof txt, "%s: %s", i18n("Fim de jogo"), m.titulo);
  a.chave = chave; a.tipo = ILHA_INFO; a.icone = "aj_trophy"; a.texto = txt; a.ms = 12000u; a.modal = &m; a.tecla = 1;
  ilha_avisar_ex(&a);
  printf("[esporte] fim de jogo seguido (evento %s)\n", e->id);
  fflush(stdout);
}
void esportealerta_acao(const char *chave, int botao) {
  if (botao != 1 || strncmp(chave, "esportehl:", 10)) return;
  for (int i = 0; i < 8; i++) if (!strcmp(hlId[i], chave + 10)) { ondever_youtube_busca(hlBusca[i]); return; }
}

void esportealerta_passo(Uint32 agoraMs) {
  time_t agora = time(NULL);
  int modo = ajustes_esporte_alertas(), pr;
  (void)agoraMs;
  pthread_mutex_lock(&trava);
  pr = pronto;
  pthread_mutex_unlock(&trava);
  if (pr) {
    int quente = 0;
    pthread_mutex_lock(&trava);
    pronto = 0;
    for (int i = 0; i < nEv; i++) {
      const EspEvento *e = &ev[i];
      if (e->estado == ESP_AO_VIVO || (e->estado == ESP_PRE && e->ini - agora < 20 * 60 && e->ini > agora - 3600)) quente = 1;
      if (modo >= 1) esportealerta_lembrar(e, agora);
      for (int j = 0; modo >= 1 && j < nAntes; j++)
        if (!strcmp(antes[j].id, e->id) && antes[j].estado == ESP_AO_VIVO && e->estado == ESP_FIM) fimDeJogo(e);
      if (modo == 2 && !esporte_esconder_placar())
        for (int j = 0; j < nAntes; j++) {
          char m[200];
          if (esportealerta_gol(&antes[j], e, m, sizeof m)) {
            char t[260];
            snprintf(t, sizeof t, "%s  %s", i18n("Gol!"), m);
            ilha_avisar(e->id, ILHA_ACENTO, "aj_trophy", t, 7000u, 0);
            printf("[esporte] gol num jogo seguido (evento %s)\n", e->id);
            fflush(stdout);
          }
        }
    }
    memcpy(antes, ev, sizeof *ev * (size_t)nEv); nAntes = nEv;
    pthread_mutex_unlock(&trava);
    proxima = agora + (quente ? 60 : 15 * 60);
  }
  if (!modo || agora < proxima) return;
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); return; }
  {
    char ligas[MAXT][32], ids[MAXT][16], nomes[MAXT][64];
    nT = esporte_times_seguidos(ligas, ids, nomes, MAXT);
    for (int i = 0; i < nT; i++) { snprintf(tLiga[i], sizeof tLiga[i], "%s", ligas[i]); snprintf(tId[i], sizeof tId[i], "%s", ids[i]); }
  }
  proxima = agora + 15 * 60;
  if (!nT) { pthread_mutex_unlock(&trava); return; }
  fioVivo = 1;
  pthread_mutex_unlock(&trava);
  {
    pthread_t t;
    if (pthread_create(&t, NULL, fio, NULL)) { fioVivo = 0; return; }
    pthread_detach(t);
  }
}
void esportealerta_perfil_trocou(void) {
  pthread_mutex_lock(&trava);
  nAntes = 0; proxima = 0;
  pthread_mutex_unlock(&trava);
}
