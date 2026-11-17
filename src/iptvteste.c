#include "iptvteste.h"
#include "iptvfontes.h"
#include "m3u.h"
#include "stalker.h"
#include "idioma.h"
#include "perfisessao.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// One result at a time, owned by the worker while it runs; read under the lock.
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int estado = IPTVT_NADA, rodando;
static unsigned contaId;
static uint64_t sessaoTeste;
static char texto[200];

int iptvteste_texto_xtream(int veio, int http, const XtreamConta *c, long long agora,
                           char *msg, unsigned n) {
  if (!veio || !c) {
    if (http >= 400) snprintf(msg, n, i18n("erro HTTP %d do servidor"), http);
    else snprintf(msg, n, "%s", i18n("sem resposta do servidor"));
    return 0;
  }
  if (!c->auth) { snprintf(msg, n, "%s", i18n("recusada pelo servidor")); return 0; }
  switch (xtream_conta_aviso(c, agora)) {
    case XA_EXPIRADA:   snprintf(msg, n, "%s", i18n("vencida")); return 0;
    case XA_DESATIVADA: snprintf(msg, n, "%s", i18n("desativada pelo provedor")); return 0;
    default: break;
  }
  if (c->expira > 0) {
    time_t t = (time_t)c->expira;
    struct tm m;
    char d[16];
    localtime_r(&t, &m);
    strftime(d, sizeof d, "%d/%m/%Y", &m);
    if (c->maxConexoes > 0)
      snprintf(msg, n, i18n("ativa até %s · %d de %d telas"), d, c->conexoes, c->maxConexoes);
    else snprintf(msg, n, i18n("ativa até %s"), d);
  } else if (c->maxConexoes > 0)
    snprintf(msg, n, i18n("ativa · %d de %d telas"), c->conexoes, c->maxConexoes);
  else snprintf(msg, n, "%s", i18n("ativa"));
  return 1;
}

int iptvteste_texto_lista(int canais, char *msg, unsigned n) {
  if (canais > 0) { snprintf(msg, n, i18n("conectado · %d canais"), canais); return 1; }
  snprintf(msg, n, "%s", i18n("nenhum canal encontrado"));
  return 0;
}

typedef struct { unsigned id; int tipo; uint64_t sessao; } Pedido;
static Pedido pedido;

static void *fio(void *u) {
  Pedido p = *(Pedido *)u;
  char msg[200];
  int ok = 0;
  if (p.tipo == IPTV_XTREAM) {
    XtreamConta c;
    int http = 0, veio = xtream_fonte_testar(p.id, p.sessao, &c, &http);
    ok = iptvteste_texto_xtream(veio, http, &c, (long long)time(NULL), msg, sizeof msg);
  } else if (p.tipo == IPTV_M3U) {
    ok = iptvteste_texto_lista(m3u_fonte_testar(p.id, p.sessao), msg, sizeof msg);
  } else {
    ok = iptvteste_texto_lista(stalker_fonte_testar(p.id, p.sessao), msg, sizeof msg);
  }
  pthread_mutex_lock(&trava);
  rodando = 0;
  // A profile switch or a newer request while we worked: keep it out.
  if (contaId == p.id && sessaoTeste == p.sessao) {
    estado = ok ? IPTVT_OK : IPTVT_FALHOU;
    snprintf(texto, sizeof texto, "%s", msg);
  }
  pthread_mutex_unlock(&trava);
  printf("[iptv] connection test: account %u type %d -> %s\n", p.id, p.tipo, ok ? "ok" : "failed");
  fflush(stdout);
  return NULL;
}

int iptvteste_iniciar(unsigned id, int tipo) {
  pthread_t t;
  if (!id || (tipo != IPTV_XTREAM && tipo != IPTV_M3U && tipo != IPTV_STALKER)) return 0;
  pthread_mutex_lock(&trava);
  if (rodando) { pthread_mutex_unlock(&trava); return 0; }
  rodando = 1;
  contaId = id;
  sessaoTeste = nv_perfil_sessao_ler();
  estado = IPTVT_TESTANDO;
  texto[0] = 0;
  pedido = (Pedido){ id, tipo, sessaoTeste };
  pthread_mutex_unlock(&trava);
  if (pthread_create(&t, NULL, fio, &pedido) != 0) {
    pthread_mutex_lock(&trava);
    rodando = 0; estado = IPTVT_NADA;
    pthread_mutex_unlock(&trava);
    return 0;
  }
  pthread_detach(t);
  return 1;
}

int iptvteste_estado(unsigned id, char *msg, unsigned n) {
  int e;
  pthread_mutex_lock(&trava);
  e = (id == contaId && sessaoTeste == nv_perfil_sessao_ler()) ? estado : IPTVT_NADA;
  if (msg && n) snprintf(msg, n, "%s", e == IPTVT_OK || e == IPTVT_FALHOU ? texto : "");
  pthread_mutex_unlock(&trava);
  return e;
}

void iptvteste_esquecer(void) {
  pthread_mutex_lock(&trava);
  if (!rodando) { estado = IPTVT_NADA; contaId = 0; texto[0] = 0; }
  else contaId = 0;   // the running worker's result will be dropped
  pthread_mutex_unlock(&trava);
}
