#include "mcplayer.h"
#include "mcfila.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const McSaida *saida;
static McGanchos g;
static int estado = MCP_PARADO, erro, festa, contou, temProxima, retomarPausado;
static double inicioSeg;
static long long inicioMs;
static unsigned ultimoSalvo, ultimoAgora;
static McFaixa atual;
static char proximaId[MC_ID];

void mcp_definir_saida(const McSaida *s) { if (saida && saida != s && saida->parar) saida->parar(); saida = s; }
void mcp_definir_ganchos(const McGanchos *x) { if (x) g = *x; else memset(&g, 0, sizeof g); }
int mcp_estado(void) { return estado; }
int mcp_erro(void) { return erro; }
const char *mcp_backend(void) { return saida ? saida->nome : ""; }
double mcp_pos(void) { return saida && estado != MCP_PARADO ? saida->pos() : 0; }
double mcp_dur(void) {
  double d = saida && estado != MCP_PARADO ? saida->dur() : 0;
  return d > 0 ? d : atual.dur;
}
void mcp_saida(McSaidaInfo *out) {
  memset(out, 0, sizeof *out);
  if (saida && saida->info && estado != MCP_PARADO) saida->info(out);
}
static long long agoraMs(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

static int abrirAtual(double inicio) {
  const McFaixa *f = mcq_faixa_atual();
  char url[4096], cab[700];
  if (!f || !saida) { estado = MCP_PARADO; return 0; }
  atual = *f;
  contou = 0; temProxima = 0; proximaId[0] = 0;
  url[0] = cab[0] = 0;
  if (!g.url || !g.url(f, url, sizeof url, cab, sizeof cab) || !url[0]) {
    estado = MCP_ERRO; erro = MC_ERRO_OUTRO;
    printf("[musica] sem endereco para a faixa (fonte removida?)\n");
    fflush(stdout);
    return 0;
  }
  if (!saida->abrir(url, cab, f, inicio)) { estado = MCP_ERRO; erro = -1; return 0; }
  memset(url, 0, sizeof url);   // the URL may carry a token
  estado = MCP_CARREGANDO; erro = 0;
  inicioSeg = inicio;
  inicioMs = agoraMs();
  printf("[musica] tocar \"%s\" (%s) em %s\n", f->titulo, f->formato[0] ? f->formato : "?", saida->nome);
  fflush(stdout);
  mch_tocou(f);
  if (g.agora) g.agora(f);
  return 1;
}
int mcp_tocar(int i) {
  if (mcq_ir(i) < 0) return 0;
  retomarPausado = 0;
  return abrirAtual(0);
}
int mcp_retomar(void) {
  double s;
  if (!mcq_n() && !mcq_carregar()) return 0;
  s = mcq_posicao_salva();
  retomarPausado = 1;
  return abrirAtual(s > 3 ? s - 2 : 0);   // two seconds of context
}
void mcp_alternar_pausa(void) {
  if (!saida) return;
  if (estado == MCP_PARADO) { if (mcq_n()) abrirAtual(0); return; }
  if (estado == MCP_TOCANDO) { saida->pausar(1); estado = MCP_PAUSADO; mcq_posicao(saida->pos()); mcq_salvar(); }
  else if (estado == MCP_PAUSADO) { saida->pausar(0); estado = MCP_TOCANDO; }
}
int mcp_proxima(void) {
  if (mcq_proxima(0) < 0) return 0;
  retomarPausado = 0;
  return abrirAtual(0);
}
int mcp_anterior(void) {
  if (saida && estado != MCP_PARADO && saida->pos() > 3) { saida->buscar(0); return 1; }
  if (mcq_anterior() < 0) return 0;
  retomarPausado = 0;
  return abrirAtual(0);
}
void mcp_buscar(double seg) {
  double d = mcp_dur();
  if (!saida || estado == MCP_PARADO) return;
  if (seg < 0) seg = 0;
  if (d > 0 && seg > d - 1) seg = d - 1;
  saida->buscar(seg);
}
void mcp_parar(void) {
  if (saida && estado != MCP_PARADO) { mcq_posicao(saida->pos()); saida->parar(); }
  estado = MCP_PARADO;
  mcq_salvar();
}

// ---------------------------------------------------------------- party mode
static pthread_mutex_t travaF = PTHREAD_MUTEX_INITIALIZER;
static McFaixa festaNovas[25]; static int festaN, festaPronta, festaFio;
static void *fioFesta(void *u) {
  McFaixa base = *(McFaixa *)u, *v = malloc(sizeof(McFaixa) * 25);
  int n = 0;
  free(u);
  if (v && g.mais) n = g.mais(&base, v, 25);
  pthread_mutex_lock(&travaF);
  if (v) memcpy(festaNovas, v, sizeof(McFaixa) * (size_t)(n > 0 ? n : 0));
  festaN = n > 0 ? n : 0; festaPronta = 1; festaFio = 0;
  pthread_mutex_unlock(&travaF);
  free(v);
  return NULL;
}
void mcp_festa(int liga) { festa = liga != 0; }
int mcp_eh_festa(void) { return festa; }
static void festaPasso(void) {
  int pronta = 0, n = 0;
  McFaixa v[25];
  pthread_mutex_lock(&travaF);
  if (festaPronta) { pronta = 1; n = festaN; memcpy(v, festaNovas, sizeof(McFaixa) * (size_t)n); festaPronta = 0; }
  pthread_mutex_unlock(&travaF);
  if (pronta) {
    int add = 0;
    for (int i = 0; i < n; i++) {
      int dup = 0;
      for (int k = 0; k < mcq_n() && !dup; k++) dup = !strcmp(mcq_item(k)->id, v[i].id);
      if (!dup && mcq_adicionar(&v[i])) add++;
    }
    printf("[musica] modo festa: %d faixa(s) adicionada(s)\n", add);
    fflush(stdout);
    mcq_salvar();
  }
  // Two tracks left: ask for more (one request at a time).
  if (festa && g.mais && mcq_n() - mcq_atual() <= 2 && mcq_faixa_atual()) {
    pthread_t t;
    McFaixa *b;
    pthread_mutex_lock(&travaF);
    if (festaFio || festaPronta) { pthread_mutex_unlock(&travaF); return; }
    festaFio = 1;
    pthread_mutex_unlock(&travaF);
    b = malloc(sizeof *b);
    if (!b) { festaFio = 0; return; }
    *b = *mcq_faixa_atual();
    if (pthread_create(&t, NULL, fioFesta, b) != 0) { free(b); pthread_mutex_lock(&travaF); festaFio = 0; pthread_mutex_unlock(&travaF); return; }
    pthread_detach(t);
  }
}

// ---------------------------------------------------------------- per frame
void mcp_atualizar(unsigned agora) {
  int st;
  double p, d;
  ultimoAgora = agora;
  if (festa) festaPasso();
  if (!saida || estado == MCP_PARADO || estado == MCP_ERRO) return;
  if (saida->bombear) saida->bombear();
  st = saida->estado();
  if (st == MCO_ERRO) {
    printf("[musica] a saida falhou nesta faixa; seguindo para a proxima\n");
    fflush(stdout);
    erro = -1;
    if (mcq_proxima(1) >= 0) abrirAtual(0); else { estado = MCP_ERRO; mcq_salvar(); }
    return;
  }
  if (st == MCO_TRANSICAO) {   // gapless: the prepared track is already playing
    if (mcq_proxima(1) >= 0) {
      atual = *mcq_faixa_atual();
      contou = 0; temProxima = 0; proximaId[0] = 0;
      inicioMs = agoraMs();
      printf("[musica] sem pausa para \"%s\"\n", atual.titulo);
      fflush(stdout);
      mch_tocou(&atual);
      if (g.agora) g.agora(&atual);
    }
    return;
  }
  if (st == MCO_FIM) {
    if (mcq_proxima(1) >= 0) abrirAtual(0);
    else { estado = MCP_PARADO; mcq_posicao(0); mcq_salvar(); printf("[musica] fim da fila\n"); fflush(stdout); }
    return;
  }
  if (estado == MCP_CARREGANDO && st == MCO_TOCANDO) {
    estado = MCP_TOCANDO;
    if (retomarPausado) { saida->pausar(1); estado = MCP_PAUSADO; retomarPausado = 0; }
  }
  if (estado != MCP_TOCANDO) return;
  p = saida->pos();
  d = mcp_dur();
  // Played: half the track or 4 minutes (and at least 30 s long).
  if (!contou && d >= 30 && (p - inicioSeg >= d / 2 || p - inicioSeg >= 240)) {
    contou = 1;
    if (g.tocou) g.tocou(&atual, inicioMs);
  }
  // Gapless: hand the backend the next track 20 s before the end.
  if (saida->preparar && !temProxima && d > 0 && p > d - 20 && mcq_repeticao() != MCR_UMA) {
    int prox = mcq_atual() + 1;
    const McFaixa *nf = mcq_item(prox);
    char url[4096], cab[700];
    temProxima = 1;
    if (nf && g.url && g.url(nf, url, sizeof url, cab, sizeof cab)) {
      saida->preparar(url, cab, nf);
      snprintf(proximaId, sizeof proximaId, "%s", nf->id);
    }
    memset(url, 0, sizeof url);
  }
  if (agora - ultimoSalvo > 10000u) { ultimoSalvo = agora; mcq_posicao(p); mcq_salvar(); }
}
