#include "esporteui.h"
#include "espn.h"
#include "esporte.h"
#include "tsdb.h"
#include "guia.h"
#include "epg.h"
#include "xtepg.h"
#include "lembrete.h"
#include "gfx.h"
#include "text.h"
#include "layout.h"
#include "idioma.h"
#include "ilha.h"
#include "horafmt.h"
#include "teclado.h"
#include "tex_cache.h"
#include "perfis.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EV_MAX 1500
#define REFRESH_VIVO_MS (60u * 1000u)
#define REFRESH_MS (10u * 60u * 1000u)

// ---------------------------------------------------------------- data (worker)
typedef struct { char liga[32]; time_t quando; int falhou, n; } LigaEstado;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static EspEvento *evs; static int nEvs;               // published (under trava)
static LigaEstado estados[ESPN_LIGAS_MAX]; static int nEstados;
static int fioVivo, carregando;
static unsigned revisao;
static Uint32 ultimaCarga;

typedef struct { EspEvento *v; int n; } Coleta;
static int juntar(const EspEvento *e, void *u) {
  Coleta *c = u;
  for (int i = 0; i < c->n; i++) if (!strcmp(c->v[i].id, e->id)) return 1;
  if (c->n < EV_MAX) c->v[c->n++] = *e;
  return 1;
}
static void *fio(void *u) {
  (void)u;
  EspEvento *v = malloc(sizeof(EspEvento) * EV_MAX);
  LigaEstado est[ESPN_LIGAS_MAX];
  int ne = 0;
  const EspLiga *ls;
  int nl = espn_ligas(&ls);
  char tl[64][32];
  int nt = esporte_times_seguidos(tl, NULL, NULL, 64);
  Coleta c = { v, 0 };
  time_t agora = time(NULL);
  if (!v) goto fim;
  for (int i = 0; i < nl && ne < ESPN_LIGAS_MAX; i++) {
    int quer = esporte_segue_liga(ls[i].chave);
    for (int k = 0; k < nt && !quer; k++) quer = !strcmp(tl[k], ls[i].chave);   // a followed team's league
    if (!quer) continue;
    memset(&est[ne], 0, sizeof est[ne]);
    snprintf(est[ne].liga, sizeof est[ne].liga, "%s", ls[i].chave);
    {
      int antes = c.n;
      int r = espn_buscar(ls[i].chave, agora, juntar, &c, &est[ne].quando, &est[ne].falhou);
      est[ne].n = r < 0 ? 0 : c.n - antes;
      if (r < 0) est[ne].falhou = 1;
    }
    ne++;
  }
  pthread_mutex_lock(&trava);
  free(evs);
  evs = v; nEvs = c.n; v = NULL;
  memcpy(estados, est, sizeof est[0] * (size_t)ne); nEstados = ne;
  revisao++;
  pthread_mutex_unlock(&trava);
  printf("[esporte] %d evento(s) de %d liga(s)\n", c.n, ne);
  fflush(stdout);
fim:
  free(v);
  pthread_mutex_lock(&trava);
  fioVivo = 0; carregando = 0;
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void carregar(void) {
  pthread_t t;
  pthread_mutex_lock(&trava);
  if (fioVivo) { pthread_mutex_unlock(&trava); return; }
  fioVivo = 1; carregando = 1;
  pthread_mutex_unlock(&trava);
  if (pthread_create(&t, NULL, fio, NULL) != 0) {
    pthread_mutex_lock(&trava); fioVivo = 0; carregando = 0; pthread_mutex_unlock(&trava);
    return;
  }
  pthread_detach(t);
}

// ---------------------------------------------------------------- view state
enum { ABA_MEUS, ABA_TODOS, ABA_VIVO, ABA_PROX, ABA_FIM, ABA_LIGAS, ABA_TIMES, ABA_FAV, ABA_N };
static const char *const ABAS[ABA_N] = { "Meus esportes", "Todos", "Ao vivo", "Próximos", "Encerrados", "Ligas", "Times", "Favoritos" };
enum { Z_ABAS, Z_FILTROS, Z_LISTA, Z_DETALHE, Z_ESCOLHER };
enum { F_ESPORTE, F_LIGA, F_DIA, F_PLACAR, F_N };
static int aba, zona = Z_LISTA, foco, filtroFoco, sair, abertoUmaVez;
static int fEsporte;            // 0 all, 1 soccer, 2 basketball, 3 tennis
static int fLiga = -1;          // index into the catalog, -1 all
static int fDia = 99;           // day offset -7..7, 99 all
static float rolagem;
static unsigned visto = ~0u;
static Uint32 relogioTela;
// Cached view: indexes into a private copy of the events (UI thread only).
static EspEvento *vista; static int nVista, capVista;
static int ord[EV_MAX]; static int nOrd;
// Teams tab
typedef struct { char liga[32], id[16], nome[64], logo[256]; } TimeView;
static TimeView timesV[400]; static int nTimesV;
// Match page
static EspEvento atual; static int temAtual;
typedef struct { int tipo; char canal[80], nome[96]; char texto[200]; } Acao;
enum { AC_ASSISTIR, AC_SUGESTAO, AC_ESCOLHER, AC_DESLIGAR, AC_PRINCIPAL, AC_LEMBRETE, AC_SEGUIR_CASA, AC_SEGUIR_FORA };
static Acao acoes[40]; static int nAcoes, focoAcao;
// Channel picker
static GuiaCanalResumo *canaisTodos; static int nCanaisTodos;
static int achados[200], nAchados, focoAchado, tecladoAberto;
static char busca[64];
// Hand-off to the router
static CatItem pedido; static int temPedido;
static char abrirPend[24];

static const char *esporteDe(const char *liga) { const EspLiga *l = espn_liga(liga); return l ? l->esporte : ""; }
static const char *nomeLiga(const char *liga) { const EspLiga *l = espn_liga(liga); return l ? l->nome : liga; }
static int diaDe(time_t t) {
  // Local day offset from today.
  time_t agora = time(NULL);
  struct tm a, b;
  localtime_r(&agora, &a);
  localtime_r(&t, &b);
  a.tm_hour = b.tm_hour = 12; a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
  return (int)((mktime(&b) - mktime(&a)) / 86400);
}
static int passaFiltro(const EspEvento *e) {
  static const char *const esp[] = { "", "soccer", "basketball", "tennis" };
  const EspLiga *ls;
  espn_ligas(&ls);
  if (fEsporte && strcmp(esporteDe(e->liga), esp[fEsporte])) return 0;
  if (fLiga >= 0 && strcmp(e->liga, ls[fLiga].chave)) return 0;
  if (fDia != 99 && diaDe(e->ini) != fDia) return 0;
  return 1;
}
static int cmpIni(const void *a, const void *b) {
  time_t x = vista[*(const int *)a].ini, y = vista[*(const int *)b].ini;
  return (x > y) - (x < y);
}
static int cmpIniDesc(const void *a, const void *b) { return -cmpIni(a, b); }
static int cmpMeus(const void *a, const void *b) {
  const EspEvento *x = &vista[*(const int *)a], *y = &vista[*(const int *)b];
  int px = x->estado == ESP_AO_VIVO ? 0 : x->estado == ESP_PRE ? 1 : 2, py = y->estado == ESP_AO_VIVO ? 0 : y->estado == ESP_PRE ? 1 : 2;
  if (px != py) return px - py;
  return px == 2 ? (x->ini < y->ini) - (x->ini > y->ini) : (x->ini > y->ini) - (x->ini < y->ini);
}
static void montar(void) {
  time_t agora = time(NULL);
  pthread_mutex_lock(&trava);
  if (nEvs > capVista) {
    EspEvento *t = realloc(vista, sizeof *t * (size_t)nEvs);
    if (t) { vista = t; capVista = nEvs; }
  }
  nVista = nEvs <= capVista ? nEvs : capVista;
  if (nVista) memcpy(vista, evs, sizeof *vista * (size_t)nVista);
  visto = revisao;
  pthread_mutex_unlock(&trava);
  nOrd = 0;
  for (int i = 0; i < nVista && nOrd < EV_MAX; i++) {
    const EspEvento *e = &vista[i];
    int ok;
    if (!passaFiltro(e)) continue;
    switch (aba) {
      case ABA_MEUS: ok = esporte_e_meu(e) || (esporte_segue_liga(e->liga) &&
                     (e->estado == ESP_AO_VIVO || (e->estado == ESP_PRE && e->ini - agora < 36 * 3600)));
                     break;
      case ABA_TODOS: ok = 1; break;
      case ABA_VIVO: ok = e->estado == ESP_AO_VIVO; break;
      case ABA_PROX: ok = e->estado == ESP_PRE; break;
      case ABA_FIM: ok = e->estado == ESP_FIM; break;
      case ABA_FAV: ok = esporte_e_meu(e); break;
      default: ok = 0;
    }
    if (ok) ord[nOrd++] = i;
  }
  qsort(ord, (size_t)nOrd, sizeof *ord, aba == ABA_FIM ? cmpIniDesc : aba == ABA_MEUS ? cmpMeus : cmpIni);
  // Teams seen in the data (Teams tab).
  nTimesV = 0;
  for (int i = 0; i < nVista; i++) for (int s = 0; s < 2; s++) {
    const EspTime *t = s ? &vista[i].fora : &vista[i].casa;
    int k;
    if (!t->id[0] || (fLiga >= 0 && !passaFiltro(&vista[i]))) continue;
    for (k = 0; k < nTimesV && !(!strcmp(timesV[k].id, t->id) && !strcmp(timesV[k].liga, vista[i].liga)); k++) {}
    if (k == nTimesV && nTimesV < 400) {
      snprintf(timesV[k].liga, sizeof timesV[k].liga, "%s", vista[i].liga);
      snprintf(timesV[k].id, sizeof timesV[k].id, "%s", t->id);
      snprintf(timesV[k].nome, sizeof timesV[k].nome, "%s", t->nome);
      snprintf(timesV[k].logo, sizeof timesV[k].logo, "%s", t->logo);
      nTimesV++;
    }
  }
}
static int nLinhas(void) {
  const EspLiga *ls;
  if (aba == ABA_LIGAS) return espn_ligas(&ls);
  if (aba == ABA_TIMES) return nTimesV;
  return nOrd;
}

// ---------------------------------------------------------------- match page
static int progsGuia(const char *canal, time_t de, time_t ate, char (*t)[128], time_t *ini, time_t *fim, int max, void *u) {
  EpgProg p[16];
  int n = guia_programa_faixa(canal, de, ate, p, max < 16 ? max : 16);
  (void)u;
  for (int i = 0; i < n; i++) {
    snprintf(t[i], 128, "%s", p[i].titulo ? p[i].titulo : "");
    ini[i] = p[i].ini; fim[i] = p[i].fim;
  }
  return n;
}
static void titulo(const EspEvento *e, char *d, size_t n) { snprintf(d, n, "%s × %s", e->casa.nome, e->fora.nome); }
static void canalReminder(const EspEvento *e, char *canal, size_t n, char *nome, size_t nn) {
  EspCanal c[1];
  if (esporte_canais(e->id, c, 1) == 1) { snprintf(canal, n, "%s", c[0].canal); snprintf(nome, nn, "%s", c[0].nome); }
  else { snprintf(canal, n, "esporte:%s", e->id); snprintf(nome, nn, "%s", "Sport Center"); }
}
static void montarAcoes(void) {
  EspCanal ligados[ESP_CANAIS_MAX];
  int nl = esporte_canais(atual.id, ligados, ESP_CANAIS_MAX);
  nAcoes = 0;
  for (int i = 0; i < nl && nAcoes < 40; i++) {
    Acao *a = &acoes[nAcoes++];
    a->tipo = AC_ASSISTIR;
    snprintf(a->canal, sizeof a->canal, "%s", ligados[i].canal);
    snprintf(a->nome, sizeof a->nome, "%s", ligados[i].nome);
    snprintf(a->texto, sizeof a->texto, "%s %s%s", i18n("Assistir em"), ligados[i].nome, i ? i18n(" (alternativo)") : "");
  }
  // Suggestions from the guide (UI thread: the guide data lives here).
  {
    GuiaCanalResumo *cr;
    int n = guia_canais_resumo(NULL, 0);
    cr = n > 0 ? malloc(sizeof *cr * (size_t)n) : NULL;
    if (cr) {
      EspCanalInfo *ci = malloc(sizeof *ci * (size_t)n);
      n = guia_canais_resumo(cr, n);
      if (ci) {
        EspSugestao s[8];
        int ns;
        for (int i = 0; i < n; i++) { ci[i].id = cr[i].id; ci[i].nome = cr[i].nome; ci[i].categoria = cr[i].categoria; }
        ns = esporte_sugerir(&atual, ci, n, progsGuia, NULL, s, 8);
        for (int i = 0; i < ns && nAcoes < 40; i++) {
          int ja = 0;
          for (int k = 0; k < nl; k++) ja |= !strcmp(ligados[k].canal, s[i].canal);
          if (ja) continue;
          Acao *a = &acoes[nAcoes++];
          a->tipo = AC_SUGESTAO;
          snprintf(a->canal, sizeof a->canal, "%s", s[i].canal);
          snprintf(a->nome, sizeof a->nome, "%s", s[i].nome);
          snprintf(a->texto, sizeof a->texto, "%s · %s · %s", s[i].nome,
                   i18n(s[i].certeza == ESP_PASSA ? "Passa este jogo" : "Talvez"), i18n(s[i].motivo));
        }
        free(ci);
      }
      free(cr);
    }
  }
  { Acao *a = &acoes[nAcoes++]; a->tipo = AC_ESCOLHER; a->canal[0] = 0; snprintf(a->texto, sizeof a->texto, "%s", i18n("Escolher um canal…")); }
  for (int i = 0; i < nl && nAcoes < 38; i++) {
    Acao *a;
    if (i) {
      a = &acoes[nAcoes++];
      a->tipo = AC_PRINCIPAL;
      snprintf(a->canal, sizeof a->canal, "%s", ligados[i].canal);
      snprintf(a->texto, sizeof a->texto, "%s: %s", i18n("Tornar principal"), ligados[i].nome);
    }
    a = &acoes[nAcoes++];
    a->tipo = AC_DESLIGAR;
    snprintf(a->canal, sizeof a->canal, "%s", ligados[i].canal);
    snprintf(a->texto, sizeof a->texto, "%s: %s", i18n("Desvincular"), ligados[i].nome);
  }
  if (atual.estado == ESP_PRE && nAcoes < 40) {
    char canal[80], nome[96], t[128];
    Acao *a = &acoes[nAcoes++];
    canalReminder(&atual, canal, sizeof canal, nome, sizeof nome);
    titulo(&atual, t, sizeof t);
    a->tipo = AC_LEMBRETE;
    snprintf(a->texto, sizeof a->texto, "%s", i18n(lembrete_achar(canal, atual.ini, t) >= 0 ? "Lembrete: ligado" : "Lembrete: desligado"));
  }
  if (atual.casa.id[0] && nAcoes < 40) {
    Acao *a = &acoes[nAcoes++];
    a->tipo = AC_SEGUIR_CASA;
    snprintf(a->texto, sizeof a->texto, "%s %s", i18n(esporte_segue_time(atual.liga, atual.casa.id) ? "Deixar de seguir" : "Seguir"), atual.casa.nome);
  }
  if (atual.fora.id[0] && nAcoes < 40) {
    Acao *a = &acoes[nAcoes++];
    a->tipo = AC_SEGUIR_FORA;
    snprintf(a->texto, sizeof a->texto, "%s %s", i18n(esporte_segue_time(atual.liga, atual.fora.id) ? "Deixar de seguir" : "Seguir"), atual.fora.nome);
  }
  if (focoAcao >= nAcoes) focoAcao = nAcoes ? nAcoes - 1 : 0;
}
// TheSportsDB channels for the open match, looked up on a worker thread.
static pthread_mutex_t travaTv = PTHREAD_MUTEX_INITIALIZER;
static char tvEvento[24], tvResultado[512];
static int tvPronto, tvFio;
static void *fioTv(void *u) {
  EspEvento e = *(EspEvento *)u;
  TsdbTv tv[TSDB_TV_MAX];
  char r[512] = "";
  size_t k = 0;
  int n;
  free(u);
  n = tsdb_canais(&e, tv, TSDB_TV_MAX);
  for (int i = 0; i < n && k + 8 < sizeof r; i++)
    k += (size_t)snprintf(r + k, sizeof r - k, "%s%s|%s", i ? ";" : "", tv[i].canal, tv[i].pais);
  pthread_mutex_lock(&travaTv);
  if (!strcmp(tvEvento, e.id)) { snprintf(tvResultado, sizeof tvResultado, "%s", r); tvPronto = 1; }
  tvFio = 0;
  pthread_mutex_unlock(&travaTv);
  return NULL;
}
static void pedirTv(const EspEvento *e) {
  pthread_t t;
  EspEvento *c;
  pthread_mutex_lock(&travaTv);
  if (tvFio) { pthread_mutex_unlock(&travaTv); return; }
  snprintf(tvEvento, sizeof tvEvento, "%s", e->id);
  tvPronto = 0; tvResultado[0] = 0;
  pthread_mutex_unlock(&travaTv);
  c = malloc(sizeof *c);
  if (!c) return;
  *c = *e;
  pthread_mutex_lock(&travaTv); tvFio = 1; pthread_mutex_unlock(&travaTv);
  if (pthread_create(&t, NULL, fioTv, c) != 0) { free(c); pthread_mutex_lock(&travaTv); tvFio = 0; pthread_mutex_unlock(&travaTv); return; }
  pthread_detach(t);
}
static void abrirDetalhe(const EspEvento *e) {
  atual = *e; temAtual = 1;
  zona = Z_DETALHE; focoAcao = 0;
  montarAcoes();
  pedirTv(e);
}
static void assistir(const char *canal, const char *nome) {
  if (guia_item_do_canal(canal, nome, "", &pedido)) {
    temPedido = 1;
    printf("[esporte] assistir: evento %s no canal %s\n", atual.id, canal);
    fflush(stdout);
  } else ilha_avisar("esporte", ILHA_INFO, NULL, i18n("Este canal não está na sua lista agora."), 4000u, 0);
}
static void acionar(const Acao *a) {
  char t[128], canal[80], nome[96];
  switch (a->tipo) {
    case AC_ASSISTIR: assistir(a->canal, a->nome); return;
    case AC_SUGESTAO:
      esporte_ligar(atual.id, a->canal, a->nome);
      ilha_avisar("esporte", ILHA_INFO, NULL, i18n("Canal vinculado ao jogo."), 3000u, 0);
      break;
    case AC_ESCOLHER:
      tecladoAberto = 1;
      busca[0] = 0;
      teclado_abrir_com("Procurar canal", "Parte do nome do canal (ex.: SSC, beIN).", 48, NULL, NULL);
      return;
    case AC_DESLIGAR: esporte_desligar(atual.id, a->canal); break;
    case AC_PRINCIPAL: esporte_tornar_principal(atual.id, a->canal); break;
    case AC_LEMBRETE:
      canalReminder(&atual, canal, sizeof canal, nome, sizeof nome);
      titulo(&atual, t, sizeof t);
      {
        int r = lembrete_alternar(canal, nome, t, "", atual.ini, atual.ini + 2 * 3600);
        ilha_avisar("esporte", ILHA_INFO, NULL, i18n(r == 1 ? "Lembrete marcado: aviso um minuto antes." :
                                                     r == 0 ? "Lembrete cancelado." : "Lista de lembretes cheia."), 3500u, 0);
      }
      break;
    case AC_SEGUIR_CASA: esporte_alternar_time(atual.liga, atual.casa.id, atual.casa.nome); break;
    case AC_SEGUIR_FORA: esporte_alternar_time(atual.liga, atual.fora.id, atual.fora.nome); break;
  }
  montarAcoes();
}
static void buscarCanais(const char *q) {
  char a[64], b[160];
  int n;
  nAchados = 0; focoAchado = 0;
  free(canaisTodos);
  n = guia_canais_resumo(NULL, 0);
  canaisTodos = n > 0 ? malloc(sizeof *canaisTodos * (size_t)n) : NULL;
  nCanaisTodos = canaisTodos ? guia_canais_resumo(canaisTodos, n) : 0;
  snprintf(a, sizeof a, "%s", q);
  for (char *c = a; *c; c++) *c = (char)tolower((unsigned char)*c);
  for (int i = 0; i < nCanaisTodos && nAchados < 200; i++) {
    snprintf(b, sizeof b, "%s", canaisTodos[i].nome ? canaisTodos[i].nome : "");
    for (char *c = b; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (!a[0] || strstr(b, a)) achados[nAchados++] = i;
  }
  zona = Z_ESCOLHER;
}

// ---------------------------------------------------------------- public
void esporteui_abrir(void) {
  sair = 0;
  if (!abertoUmaVez) { abertoUmaVez = 1; aba = ABA_MEUS; zona = Z_ABAS; }
  if (zona == Z_DETALHE || zona == Z_ESCOLHER) zona = Z_LISTA;
  tecladoAberto = 0;
  esporte_carregar();
  carregar();
  // The channel list and its guide feed the match page's suggestions.
  guia_carregar();
  ultimaCarga = SDL_GetTicks();
  visto = ~0u;
}
void esporteui_perfil_trocou(void) {
  pthread_mutex_lock(&trava);
  nEvs = 0; revisao++;
  pthread_mutex_unlock(&trava);
  abertoUmaVez = 0; temAtual = 0;
}
int esporteui_quer_sair(void) { int v = sair; sair = 0; return v; }
int esporteui_pediu_canal(CatItem *it) {
  if (!temPedido) return 0;
  temPedido = 0;
  if (it) *it = pedido;
  return 1;
}
void esporteui_abrir_evento(const char *id) { snprintf(abrirPend, sizeof abrirPend, "%s", id ? id : ""); }
int esporteui_teste_n(void) { return nLinhas(); }
int esporteui_teste_aba(void) { return aba; }

static void ciclarFiltro(int f, int dir) {
  const EspLiga *ls;
  int nl = espn_ligas(&ls);
  switch (f) {
    case F_ESPORTE: fEsporte = (fEsporte + dir + 4) % 4; break;
    case F_LIGA: fLiga += dir; if (fLiga < -1) fLiga = nl - 1; if (fLiga >= nl) fLiga = -1; break;
    case F_DIA: if (fDia == 99) fDia = dir > 0 ? -7 : 7; else { fDia += dir; if (fDia > 7 || fDia < -7) fDia = 99; } break;
    case F_PLACAR: esporte_definir_esconder_placar(!esporte_esconder_placar()); break;
  }
  foco = 0; rolagem = 0;
  montar();
}
void esporteui_evento(const SDL_Event *e) {
  SDL_Keycode k;
  if (tecladoAberto) { teclado_evento(e); return; }
  if (e->type != SDL_KEYDOWN) return;
  k = e->key.keysym.sym;
  int voltar = k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE;
  int ok = k == SDLK_RETURN || k == SDLK_KP_ENTER;
  if (zona == Z_ESCOLHER) {
    if (voltar) { zona = Z_DETALHE; return; }
    if (k == SDLK_UP && focoAchado > 0) focoAchado--;
    if (k == SDLK_DOWN && focoAchado + 1 < nAchados) focoAchado++;
    if (ok && nAchados) {
      const GuiaCanalResumo *c = &canaisTodos[achados[focoAchado]];
      esporte_ligar(atual.id, c->id, c->nome);
      ilha_avisar("esporte", ILHA_INFO, NULL, i18n("Canal vinculado ao jogo."), 3000u, 0);
      zona = Z_DETALHE;
      montarAcoes();
    }
    return;
  }
  if (zona == Z_DETALHE) {
    if (voltar) { zona = Z_LISTA; montar(); return; }
    if (k == SDLK_UP && focoAcao > 0) focoAcao--;
    if (k == SDLK_DOWN && focoAcao + 1 < nAcoes) focoAcao++;
    if (ok && nAcoes) acionar(&acoes[focoAcao]);
    return;
  }
  if (voltar) { sair = 1; return; }
  if (zona == Z_ABAS) {
    if (k == SDLK_LEFT && aba > 0) { aba--; foco = 0; rolagem = 0; montar(); }
    else if (k == SDLK_RIGHT && aba + 1 < ABA_N) { aba++; foco = 0; rolagem = 0; montar(); }
    else if (k == SDLK_DOWN) zona = aba == ABA_LIGAS ? Z_LISTA : Z_FILTROS;
    else if (k == SDLK_LEFT && aba == 0) sair = 1;
    return;
  }
  if (zona == Z_FILTROS) {
    if (k == SDLK_LEFT && filtroFoco > 0) filtroFoco--;
    else if (k == SDLK_RIGHT && filtroFoco + 1 < F_N) filtroFoco++;
    else if (k == SDLK_UP) zona = Z_ABAS;
    else if (k == SDLK_DOWN) zona = Z_LISTA;
    else if (ok) ciclarFiltro(filtroFoco, 1);
    return;
  }
  // list
  if (k == SDLK_UP) { if (foco > 0) foco--; else zona = aba == ABA_LIGAS ? Z_ABAS : Z_FILTROS; return; }
  if (k == SDLK_DOWN && foco + 1 < nLinhas()) { foco++; return; }
  if (ok && nLinhas()) {
    if (aba == ABA_LIGAS) {
      const EspLiga *ls;
      espn_ligas(&ls);
      esporte_definir_liga(ls[foco].chave, !esporte_segue_liga(ls[foco].chave));
      carregar();
    } else if (aba == ABA_TIMES) {
      esporte_alternar_time(timesV[foco].liga, timesV[foco].id, timesV[foco].nome);
    } else abrirDetalhe(&vista[ord[foco]]);
  }
}
void esporteui_atualizar(float dt, Uint32 agora) {
  int vivo = 0;
  (void)dt;
  if (tecladoAberto) {
    int r;
    teclado_atualizar(dt, agora);
    r = teclado_resultado();
    if (r == TECLADO_PRONTO) { snprintf(busca, sizeof busca, "%s", teclado_texto()); tecladoAberto = 0; buscarCanais(busca); }
    else if (r == TECLADO_CANCELOU) tecladoAberto = 0;
  }
  if (visto != revisao) {
    montar();
    // A newer score for the open match page.
    if (temAtual) for (int i = 0; i < nVista; i++) if (!strcmp(vista[i].id, atual.id)) {
      char tp[512];
      snprintf(tp, sizeof tp, "%s", atual.tvPais);
      atual = vista[i];
      snprintf(atual.tvPais, sizeof atual.tvPais, "%s", tp);
      break;
    }
  }
  // The guide's EPG only advances while someone drives it (the guide screen,
  // the player): the Sport Center does too, so suggestions have programmes.
  {
    static int tinhaCanais;
    int nc = guia_canais_resumo(NULL, 0);
    if (nc > 0 && !tinhaCanais) guia_carregar();   // registers the providers' own guides
    tinhaCanais = nc > 0;
    epg_passo();
    xtepg_passo();
  }
  // Channels (or their guide) arrived after the page opened: suggest again.
  {
    static int nCanaisVisto = -1;
    static Uint32 epgVisto;
    int nc = guia_canais_resumo(NULL, 0);
    if (zona == Z_DETALHE && temAtual && (nc != nCanaisVisto || agora - epgVisto > 5000u)) {
      if (nc != nCanaisVisto) { nCanaisVisto = nc; montarAcoes(); }
      else { int ns = 0; for (int i = 0; i < nAcoes; i++) ns += acoes[i].tipo == AC_SUGESTAO; if (!ns && nc > 0) montarAcoes(); }
      epgVisto = agora;
    }
  }
  // TheSportsDB answered for the open match: its channels join the page.
  {
    int pronto = 0;
    char r[512];
    pthread_mutex_lock(&travaTv);
    if (tvPronto && temAtual && !strcmp(tvEvento, atual.id)) { pronto = 1; snprintf(r, sizeof r, "%s", tvResultado); tvPronto = 0; }
    pthread_mutex_unlock(&travaTv);
    if (pronto && strcmp(r, atual.tvPais)) { snprintf(atual.tvPais, sizeof atual.tvPais, "%s", r); montarAcoes(); }
  }
  if (abrirPend[0]) {
    for (int i = 0; i < nVista; i++) if (!strcmp(vista[i].id, abrirPend)) { abrirDetalhe(&vista[i]); abrirPend[0] = 0; break; }
  }
  for (int i = 0; i < nVista && !vivo; i++) vivo = vista[i].estado == ESP_AO_VIVO;
  if (agora - ultimaCarga > (vivo ? REFRESH_VIVO_MS : REFRESH_MS)) { ultimaCarga = agora; carregar(); }
  if (foco >= nLinhas()) foco = nLinhas() ? nLinhas() - 1 : 0;
  relogioTela = agora;
}

// ---------------------------------------------------------------- drawing
static void texto(TxtEstilo s, const char *t, float x, float y, float a, int r, int g, int b) {
  TxtLinha l = txt_linha(s, t, r, g, b, 255);
  txt_desenhar_alpha(l, x, y, a);
}
static void textoCorta(TxtEstilo s, const char *t, float x, float y, float w, float a) {
  TxtLinha l = txt_linha_corta(s, t, 240, 242, 248, 255, (int)w);
  txt_desenhar_alpha(l, x, y, a);
}
static void logo(const char *url, float x, float y, float s) {
  GLuint tx = url && url[0] ? tex_obter(url) : 0;
  if (tx) gfx_textura((GfxRect){ x, y, s, s }, tx);
  else gfx_cor((GfxRect){ x, y, s, s }, 0.5f, 1, 1, 1, 0.08f);
}
static void horaDe(time_t t, char *d, size_t n) {
  struct tm m;
  char h[16];
  localtime_r(&t, &m);
  hora_tela(h, sizeof h, &m);
  int dd = diaDe(t);
  if (dd == 0) snprintf(d, n, "%s", h);
  else if (dd == 1) snprintf(d, n, "%s %s", i18n("Amanhã"), h);
  else if (dd == -1) snprintf(d, n, "%s %s", i18n("Ontem"), h);
  else snprintf(d, n, "%02d/%02d %s", m.tm_mday, m.tm_mon + 1, h);
}
static void estadoTexto(const EspEvento *e, char *d, size_t n) {
  if (e->estado == ESP_AO_VIVO) snprintf(d, n, "%s %s", i18n("AO VIVO"), e->relogio[0] ? e->relogio : e->detalhe);
  else if (e->estado == ESP_FIM) snprintf(d, n, "%s", i18n("Encerrado"));
  else if (e->estado == ESP_ADIADO) snprintf(d, n, "%s", i18n("Adiado"));
  else horaDe(e->ini, d, n);
}
static void placarTexto(const EspEvento *e, char *d, size_t n) {
  if (e->estado == ESP_PRE || e->estado == ESP_ADIADO || (!e->casa.placar[0] && !e->fora.placar[0])) snprintf(d, n, "%s", "vs");
  else if (esporte_esconder_placar()) snprintf(d, n, "%s", "• – •");
  else snprintf(d, n, "%s – %s", e->casa.placar, e->fora.placar);
}
static void rodape(float y) {
  char t[200] = "", h[16];
  time_t mais = 0;
  int falhas = 0, total;
  pthread_mutex_lock(&trava);
  total = nEstados;
  for (int i = 0; i < nEstados; i++) { if (estados[i].quando > mais) mais = estados[i].quando; falhas += estados[i].falhou; }
  pthread_mutex_unlock(&trava);
  if (mais) { struct tm m; localtime_r(&mais, &m); hora_tela(h, sizeof h, &m); }
  else snprintf(h, sizeof h, "--:--");
  if (carregando && !mais) snprintf(t, sizeof t, "%s", i18n("Carregando jogos…"));
  else if (total && falhas == total) snprintf(t, sizeof t, i18n("ESPN não respondeu · dados de %s"), h);
  else snprintf(t, sizeof t, i18n("Dados: ESPN (fonte pública não oficial) · atualizado %s"), h);
  texto(TXT_CAPTION, t, NV_MARGEM_X, y, 0.6f, 200, 202, 210);
}
void esporteui_desenhar(Uint32 agora) {
  float x = NV_MARGEM_X, y = NV_MARGEM_Y, w = NV_TELA_W - 2 * NV_MARGEM_X;
  char b[200];
  (void)agora;
  gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0.055f, 0.058f, 0.068f, 1);
  if (temAtual && (zona == Z_DETALHE || zona == Z_ESCOLHER)) {
    const EspEvento *e = &atual;
    texto(TXT_CAPTION, nomeLiga(e->liga), x, y, 0.7f, 200, 202, 210);
    y += 50;
    logo(e->casa.logo, x, y, 120);
    logo(e->fora.logo, x + w - 120, y, 120);
    texto(TXT_TITULO2, e->casa.nome, x + 140, y + 20, 1, 240, 242, 248);
    { TxtLinha l = txt_linha(TXT_TITULO2, e->fora.nome, 240, 242, 248, 255); txt_desenhar_alpha(l, x + w - 140 - (float)l.w, y + 20, 1); }
    placarTexto(e, b, sizeof b);
    { TxtLinha l = txt_linha(TXT_TITULO1, b, 255, 255, 255, 255); txt_desenhar_alpha(l, x + (w - (float)l.w) * 0.5f, y + 10, 1); }
    estadoTexto(e, b, sizeof b);
    { TxtLinha l = txt_linha(TXT_CALLOUT, b, 255, 120, 110, 255); txt_desenhar_alpha(l, x + (w - (float)l.w) * 0.5f, y + 90, e->estado == ESP_AO_VIVO ? 1 : 0.7f); }
    y += 170;
    if (e->tvPais[0]) {
      char t[512], *it, *sp, lin[300] = "";
      size_t k = 0;
      int c = 0;
      snprintf(t, sizeof t, "%s", e->tvPais);
      for (it = strtok_r(t, ";", &sp); it && c < 4; it = strtok_r(NULL, ";", &sp), c++) {
        char *bar = strchr(it, '|');
        if (bar) *bar = 0;
        k += (size_t)snprintf(lin + k, sizeof lin - k, "%s%s%s%s%s", c ? "  ·  " : "", it, bar && bar[1] ? " (" : "",
                              bar && bar[1] ? bar + 1 : "", bar && bar[1] ? ")" : "");
        if (k >= sizeof lin) break;
      }
      snprintf(b, sizeof b, "%s: %s", i18n("Transmissão (TheSportsDB)"), lin);
      texto(TXT_CAPTION, b, x, y, 0.7f, 200, 202, 210);
      y += 40;
    }
    if (e->local[0] || e->tv[0]) {
      snprintf(b, sizeof b, "%s%s%s%s", e->local, e->local[0] && e->tv[0] ? "  ·  " : "", e->tv[0] ? "TV: " : "", e->tv);
      texto(TXT_CAPTION, b, x, y, 0.6f, 200, 202, 210);
      y += 44;
    }
    if (zona == Z_ESCOLHER) {
      snprintf(b, sizeof b, "%s \"%s\": %d", i18n("Canais com"), busca, nAchados);
      texto(TXT_CALLOUT, b, x, y, 0.85f, 240, 242, 248);
      y += 52;
      for (int i = 0; i < nAchados && y < NV_TELA_H - 120; i++) {
        int f = i == focoAchado;
        int de = focoAchado > 8 ? focoAchado - 8 : 0;
        if (i < de) continue;
        if (f) gfx_cor((GfxRect){ x, y - 6, w, 50 }, 0.3f, 1, 1, 1, 0.12f);
        textoCorta(TXT_BODY, canaisTodos[achados[i]].nome, x + 16, y, w - 32, f ? 1 : 0.75f);
        y += 54;
      }
      if (!nAchados) texto(TXT_BODY, i18n("Nenhum canal com esse nome na sua lista."), x, y, 0.7f, 240, 242, 248);
    } else {
      texto(TXT_CALLOUT, i18n("Onde assistir"), x, y, 0.85f, 240, 242, 248);
      y += 52;
      for (int i = 0; i < nAcoes && y < NV_TELA_H - 120; i++) {
        int f = i == focoAcao;
        int de = focoAcao > 8 ? focoAcao - 8 : 0;
        if (i < de) continue;
        if (f) gfx_cor((GfxRect){ x, y - 6, w, 50 }, 0.3f, 1, 1, 1, 0.12f);
        textoCorta(TXT_BODY, acoes[i].texto, x + 16, y, w - 32, f ? 1 : 0.75f);
        y += 54;
      }
      {
        int temLigado = 0, temSug = 0;
        for (int i = 0; i < nAcoes; i++) { temLigado |= acoes[i].tipo == AC_ASSISTIR; temSug |= acoes[i].tipo == AC_SUGESTAO; }
        if (!temLigado && !temSug)
          texto(TXT_CAPTION, i18n("Nenhuma sugestão no guia (os títulos podem estar em outro idioma). Use \"Escolher um canal…\"."),
                x, NV_TELA_H - 110, 0.6f, 200, 202, 210);
      }
    }
    rodape(NV_TELA_H - 64);
    if (tecladoAberto) teclado_desenhar(agora);
    return;
  }
  texto(TXT_TITULO2, "Sport Center", x, y, 1, 240, 242, 248);
  y += 80;
  // Tabs.
  {
    float tx = x;
    for (int i = 0; i < ABA_N; i++) {
      TxtLinha l = txt_linha(TXT_CALLOUT, i18n(ABAS[i]), 240, 242, 248, 255);
      float lw = (float)l.w + 40;
      int f = zona == Z_ABAS && i == aba;
      gfx_cor((GfxRect){ tx, y, lw, 52 }, 0.5f, 1, 1, 1, i == aba ? (f ? 0.9f : 0.18f) : 0.06f);
      if (f) { TxtLinha d = txt_linha(TXT_CALLOUT, i18n(ABAS[i]), 20, 20, 24, 255); txt_desenhar_alpha(d, tx + 20, y + 10, 1); }
      else txt_desenhar_alpha(l, tx + 20, y + 10, i == aba ? 1 : 0.7f);
      tx += lw + 12;
    }
    y += 72;
  }
  // Filters.
  if (aba != ABA_LIGAS) {
    static const char *const esp[] = { "Todos os esportes", "Futebol", "Basquete", "Tênis" };
    const EspLiga *ls;
    char fv[F_N][96];
    float tx = x;
    espn_ligas(&ls);
    snprintf(fv[F_ESPORTE], sizeof fv[0], "%s", i18n(esp[fEsporte]));
    snprintf(fv[F_LIGA], sizeof fv[0], "%s", fLiga < 0 ? i18n("Todas as ligas") : ls[fLiga].nome);
    if (fDia == 99) snprintf(fv[F_DIA], sizeof fv[0], "%s", i18n("Todos os dias"));
    else if (fDia == 0) snprintf(fv[F_DIA], sizeof fv[0], "%s", i18n("Hoje"));
    else if (fDia == 1) snprintf(fv[F_DIA], sizeof fv[0], "%s", i18n("Amanhã"));
    else if (fDia == -1) snprintf(fv[F_DIA], sizeof fv[0], "%s", i18n("Ontem"));
    else snprintf(fv[F_DIA], sizeof fv[0], i18n(fDia > 0 ? "Em %d dias" : "Há %d dias"), fDia > 0 ? fDia : -fDia);
    snprintf(fv[F_PLACAR], sizeof fv[0], "%s", i18n(esporte_esconder_placar() ? "Placares ocultos" : "Placares visíveis"));
    for (int i = 0; i < F_N; i++) {
      TxtLinha l = txt_linha(TXT_CAPTION, fv[i], 240, 242, 248, 255);
      float lw = (float)l.w + 32;
      int f = zona == Z_FILTROS && i == filtroFoco;
      gfx_cor((GfxRect){ tx, y, lw, 44 }, 0.5f, 1, 1, 1, f ? 0.3f : 0.08f);
      txt_desenhar_alpha(l, tx + 16, y + 9, f ? 1 : 0.75f);
      tx += lw + 10;
    }
    y += 64;
  }
  // Rows.
  {
    float rh = 92, area = NV_TELA_H - 90 - y;
    int visiveis = (int)(area / rh), de = foco >= visiveis ? foco - visiveis + 1 : 0, n = nLinhas();
    if (!n) {
      texto(TXT_BODY, i18n(carregando ? "Carregando jogos…" :
                           aba == ABA_VIVO ? "Nenhum jogo ao vivo agora." :
                           aba == ABA_MEUS || aba == ABA_FAV ? "Siga times em \"Times\" ou ligas em \"Ligas\" para montar a sua lista." :
                           "Nenhum jogo com estes filtros."), x, y + 20, 0.7f, 240, 242, 248);
    }
    for (int i = de; i < n && i < de + visiveis; i++) {
      int f = zona == Z_LISTA && i == foco;
      float ry = y + (float)(i - de) * rh;
      if (f) gfx_cor((GfxRect){ x - 12, ry - 8, w + 24, rh - 8 }, 0.25f, 1, 1, 1, 0.12f);
      if (aba == ABA_LIGAS) {
        const EspLiga *ls;
        espn_ligas(&ls);
        snprintf(b, sizeof b, "%s %s", esporte_segue_liga(ls[i].chave) ? "★" : "☆", ls[i].nome);
        texto(TXT_BODY, b, x + 8, ry + 16, f ? 1 : 0.8f, 240, 242, 248);
        continue;
      }
      if (aba == ABA_TIMES) {
        logo(timesV[i].logo, x, ry + 6, 56);
        snprintf(b, sizeof b, "%s %s  ·  %s", esporte_segue_time(timesV[i].liga, timesV[i].id) ? "★" : "☆",
                 timesV[i].nome, nomeLiga(timesV[i].liga));
        texto(TXT_BODY, b, x + 72, ry + 16, f ? 1 : 0.8f, 240, 242, 248);
        continue;
      }
      {
        const EspEvento *e = &vista[ord[i]];
        EspCanal c[1];
        estadoTexto(e, b, sizeof b);
        texto(TXT_CAPTION, b, x, ry + 22, 0.85f, e->estado == ESP_AO_VIVO ? 255 : 200, e->estado == ESP_AO_VIVO ? 120 : 202,
              e->estado == ESP_AO_VIVO ? 110 : 210);
        logo(e->casa.logo, x + 230, ry + 10, 52);
        textoCorta(TXT_BODY, e->casa.curto, x + 296, ry + 18, 420, f ? 1 : 0.85f);
        placarTexto(e, b, sizeof b);
        texto(TXT_CALLOUT, b, x + 740, ry + 16, 1, 255, 255, 255);
        logo(e->fora.logo, x + 860, ry + 10, 52);
        textoCorta(TXT_BODY, e->fora.curto, x + 926, ry + 18, 420, f ? 1 : 0.85f);
        snprintf(b, sizeof b, "%s", nomeLiga(e->liga));
        if (esporte_canais(e->id, c, 1) == 1) snprintf(b, sizeof b, "%s  ·  📺 %s", nomeLiga(e->liga), c[0].nome);
        textoCorta(TXT_CAPTION, b, x + 1370, ry + 22, w - 1370, 0.6f);
      }
    }
  }
  rodape(NV_TELA_H - 64);
}
