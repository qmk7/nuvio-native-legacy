#include "ytui.h"
#include "ponteiro.h"
#include "ytapi.h"
#include "ondever.h"
#include "gfx.h"
#include "text.h"
#include "layout.h"
#include "idioma.h"
#include "ilha.h"
#include "teclado.h"
#include "tex_cache.h"
#include "qrtex.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAG_MAX 200      // items kept per list (pages of 25 appended)
#define SUB_MAX 5        // channel / playlist pages stacked over a tab
#define YT_COLS 4        // videos per row of the grid

// ---------------------------------------------------------------- lists
enum { P_ALTA, P_BUSCA, P_INSC, P_PLAYLISTS, P_PLAYLIST, P_CANAL };
typedef struct {
  int tipo;
  char arg[200], titulo[200];
  YtItem *itens; int n;
  char prox[64];
  int erro, carregando, pedido;
  unsigned gen;
  int foco;
  float rolagem;
} Pagina;

enum { ABA_ALTA, ABA_BUSCA, ABA_INSC, ABA_PLAY, ABA_CONTA, ABA_N };
static const char *const ABAS[ABA_N] = { "Em alta", "Buscar", "Inscrições", "Playlists", "Conta" };
enum { Z_ABAS, Z_LISTA, Z_VIDEO, Z_CONTA };
static Pagina base[ABA_CONTA], sub[SUB_MAX];
static int nSub, aba, zona = Z_ABAS, sair, abertoUmaVez;
static unsigned geracao;
static char lancado[16];

// Results from the workers (the UI thread applies them).
typedef struct { Pagina *alvo; unsigned gen; int erro, anexar; YtItem *itens; int n; char prox[64]; } Resultado;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static Resultado fila[8]; static int nFila;

typedef struct { Pagina *alvo; unsigned gen; int tipo, anexar; char arg[200], pagina[64], regiao[8]; } Pedido;
static void *fioLista(void *u) {
  Pedido p = *(Pedido *)u;
  Resultado r;
  free(u);
  memset(&r, 0, sizeof r);
  r.alvo = p.alvo; r.gen = p.gen; r.anexar = p.anexar;
  r.itens = calloc(50, sizeof *r.itens);
  if (!r.itens) r.erro = YT_ERRO_OUTRO;
  else switch (p.tipo) {
    case P_ALTA:      r.erro = yt_em_alta(p.regiao, p.pagina, r.itens, 25, &r.n, r.prox, sizeof r.prox); break;
    case P_BUSCA:     r.erro = yt_buscar(p.arg, p.pagina, r.itens, 25, &r.n, r.prox, sizeof r.prox);
                      if (!r.erro && r.n) yt_detalhes(r.itens, r.n); break;   // durations, views: 1 unit
    case P_INSC:      r.erro = yt_inscricoes(p.pagina, r.itens, 50, &r.n, r.prox, sizeof r.prox); break;
    case P_PLAYLISTS: r.erro = yt_playlists(r.itens, 50, &r.n); break;
    case P_PLAYLIST:  r.erro = yt_playlist_itens(p.arg, p.pagina, r.itens, 25, &r.n, r.prox, sizeof r.prox);
                      if (!r.erro && r.n) yt_detalhes(r.itens, r.n); break;
    case P_CANAL:     r.erro = yt_canal_videos(p.arg, r.itens, 25, &r.n);
                      if (!r.erro && r.n) yt_detalhes(r.itens, r.n); break;
  }
  printf("[youtube] lista tipo=%d itens=%d erro=%d\n", p.tipo, r.n, r.erro);
  fflush(stdout);
  pthread_mutex_lock(&trava);
  if (nFila < 8) fila[nFila++] = r; else free(r.itens);
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void pedir(Pagina *pg, int anexar) {
  Pedido *p = calloc(1, sizeof *p);
  pthread_t t;
  if (!p) return;
  if (!anexar) { pg->gen = ++geracao; pg->n = 0; pg->prox[0] = 0; pg->foco = 0; pg->rolagem = 0; }
  pg->erro = YT_OK; pg->carregando = 1; pg->pedido = 1;
  p->alvo = pg; p->gen = pg->gen; p->tipo = pg->tipo; p->anexar = anexar;
  snprintf(p->arg, sizeof p->arg, "%s", pg->arg);
  if (anexar) snprintf(p->pagina, sizeof p->pagina, "%s", pg->prox);
  {
    char pais[16] = "";
    ondever_pais(pais, sizeof pais);
    snprintf(p->regiao, sizeof p->regiao, "%.2s", pais[0] ? pais : "US");
    for (char *c = p->regiao; *c; c++) *c = (char)toupper((unsigned char)*c);
  }
  if (pthread_create(&t, NULL, fioLista, p) != 0) { free(p); pg->carregando = 0; pg->erro = YT_ERRO_OUTRO; return; }
  pthread_detach(t);
}
static void aplicarResultados(void) {
  Resultado r[8];
  int n;
  pthread_mutex_lock(&trava);
  n = nFila; memcpy(r, fila, sizeof r[0] * (size_t)n); nFila = 0;
  pthread_mutex_unlock(&trava);
  for (int i = 0; i < n; i++) {
    Pagina *pg = r[i].alvo;
    if (pg->gen != r[i].gen) { free(r[i].itens); continue; }   // the page moved on
    pg->carregando = 0;
    pg->erro = r[i].erro;
    if (!pg->itens) pg->itens = calloc(PAG_MAX, sizeof *pg->itens);
    for (int k = 0; pg->itens && k < r[i].n && pg->n < PAG_MAX; k++) {
      int dup = 0;
      for (int j = 0; j < pg->n && !dup; j++) dup = pg->itens[j].tipo == r[i].itens[k].tipo && !strcmp(pg->itens[j].id, r[i].itens[k].id);
      if (!dup) pg->itens[pg->n++] = r[i].itens[k];
    }
    snprintf(pg->prox, sizeof pg->prox, "%s", r[i].erro ? "" : r[i].prox);
    free(r[i].itens);
  }
}
static Pagina *atual(void) { return nSub ? &sub[nSub - 1] : aba < ABA_CONTA ? &base[aba] : NULL; }
static void limparSub(void) { for (int i = 0; i < SUB_MAX; i++) { sub[i].gen = ++geracao; sub[i].n = 0; } nSub = 0; }
static void empilhar(int tipo, const char *arg, const char *titulo) {
  Pagina *pg;
  if (nSub == SUB_MAX) { // drop the oldest
    YtItem *v = sub[0].itens;
    memmove(sub, sub + 1, sizeof sub[0] * (SUB_MAX - 1));
    sub[SUB_MAX - 1].itens = v;
    nSub--;
  }
  pg = &sub[nSub++];
  pg->tipo = tipo;
  snprintf(pg->arg, sizeof pg->arg, "%s", arg);
  snprintf(pg->titulo, sizeof pg->titulo, "%s", titulo);
  pedir(pg, 0);
}

// ---------------------------------------------------------------- video page
static YtItem video; static int temVideo, erroVideo, carregandoVideo, focoVideo;
static unsigned genVideo;
static pthread_mutex_t travaV = PTHREAD_MUTEX_INITIALIZER;
static YtItem videoNovo; static int videoPronto, videoErroNovo; static unsigned videoGenNovo;
static void *fioVideo(void *u) {
  char id[16];
  YtItem v;
  int e;
  unsigned g;
  pthread_mutex_lock(&travaV);
  snprintf(id, sizeof id, "%s", (const char *)u);
  g = genVideo;
  pthread_mutex_unlock(&travaV);
  free(u);
  memset(&v, 0, sizeof v);
  e = yt_video(id, &v);
  pthread_mutex_lock(&travaV);
  videoNovo = v; videoErroNovo = e; videoGenNovo = g; videoPronto = 1;
  pthread_mutex_unlock(&travaV);
  return NULL;
}
static void abrirVideo(const YtItem *it) {
  pthread_t t;
  char *id = strdup(it->id);
  video = *it;
  temVideo = 1; erroVideo = YT_OK; focoVideo = 0; carregandoVideo = 1;
  zona = Z_VIDEO;
  pthread_mutex_lock(&travaV); genVideo++; pthread_mutex_unlock(&travaV);
  if (!id || pthread_create(&t, NULL, fioVideo, id) != 0) { free(id); carregandoVideo = 0; return; }
  pthread_detach(t);
}
static int acoesVideo(void) { return erroVideo == YT_INDISPONIVEL ? 0 : 1 + (!strncmp(video.canalId, "UC", 2)); }
static void assistir(void) {
  int e;
  snprintf(lancado, sizeof lancado, "%s", video.id);
  e = ondever_youtube_video(video.id);
  if (e == ONDE_INFO)
    ilha_avisar("ytapp", ILHA_INFO, NULL,
                i18n("O YouTube abre no app da própria TV, que não está disponível neste aparelho."), 6000u, 0);
  else if (e == ONDE_ABRIR)
    ilha_avisar("ytapp", ILHA_INFO, NULL, i18n("Abrindo no app oficial do YouTube…"), 3000u, 0);
}

// ---------------------------------------------------------------- account
enum { C_CHAVE, C_CLIENTE, C_SEGREDO, C_ENTRAR, C_N };
static int focoConta, tecladoCampo = -1;
static pthread_mutex_t travaL = PTHREAD_MUTEX_INITIALIZER;
static int loginEstado;              // 0 idle, 1 starting, 2 waiting for the phone, 3 done, -1 failed
static int loginErro, loginIntervalo, loginFio;
static char loginCodigo[32], loginUrl[128];
static Uint32 loginProximo, loginDesde;
static GLuint texQr; static char qrDe[128];
static void *fioLoginIniciar(void *u) {
  char c[32] = "", url[128] = "";
  int iv = 5, e;
  (void)u;
  e = yt_login_iniciar(c, sizeof c, url, sizeof url, &iv);
  pthread_mutex_lock(&travaL);
  if (loginEstado == 1) {
    if (e == YT_OK) { loginEstado = 2; snprintf(loginCodigo, sizeof loginCodigo, "%s", c); snprintf(loginUrl, sizeof loginUrl, "%s", url); loginIntervalo = iv < 2 ? 2 : iv > 30 ? 30 : iv; }
    else { loginEstado = -1; loginErro = e; }
  }
  loginFio = 0;
  pthread_mutex_unlock(&travaL);
  return NULL;
}
static void *fioLoginConferir(void *u) {
  int r;
  (void)u;
  r = yt_login_conferir();
  pthread_mutex_lock(&travaL);
  if (loginEstado == 2) { if (r == 1) loginEstado = 3; else if (r < 0) { loginEstado = -1; loginErro = YT_ERRO_LOGIN; } }
  loginFio = 0;
  pthread_mutex_unlock(&travaL);
  return NULL;
}
static void disparar(void *(*f)(void *)) {
  pthread_t t;
  pthread_mutex_lock(&travaL);
  if (loginFio) { pthread_mutex_unlock(&travaL); return; }
  loginFio = 1;
  pthread_mutex_unlock(&travaL);
  if (pthread_create(&t, NULL, f, NULL) != 0) { pthread_mutex_lock(&travaL); loginFio = 0; pthread_mutex_unlock(&travaL); return; }
  pthread_detach(t);
}
static void entrar(void) {
  if (!yt_tem_cliente()) {
    ilha_avisar("ytacct", ILHA_INFO, NULL, i18n("Preencha o ID e o segredo do cliente OAuth antes de entrar."), 5000u, 0);
    return;
  }
  pthread_mutex_lock(&travaL); loginEstado = 1; loginCodigo[0] = loginUrl[0] = 0; pthread_mutex_unlock(&travaL);
  loginDesde = SDL_GetTicks();
  disparar(fioLoginIniciar);
}
static void abrirCampo(int c) {
  static const char ALFA[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
  tecladoCampo = c;
  teclado_contexto("YouTube · Conta");
  teclado_abrir_com(c == C_CHAVE ? "Chave da YouTube Data API" : c == C_CLIENTE ? "ID do cliente OAuth" : "Segredo do cliente OAuth",
                    "Do seu projeto no Google Cloud. Use \"Digitar pelo celular\" para colar.",
                    c == C_CLIENTE ? 159 : 63, ALFA, NULL);
  teclado_permitir_vazio(1);
  // Not TECLADO_TIPO_SENHA: that type turns "type on the phone" off, and the
  // phone is how a 35-character secret gets pasted. Dots on screen instead.
  if (c == C_SEGREDO) teclado_mascarar(1);
}

// ---------------------------------------------------------------- public
void ytui_abrir(void) {
  sair = 0;
  yt_carregar();
  if (!abertoUmaVez) {
    abertoUmaVez = 1;
    aba = ABA_ALTA; zona = Z_ABAS;
    base[ABA_ALTA].tipo = P_ALTA; base[ABA_BUSCA].tipo = P_BUSCA;
    base[ABA_INSC].tipo = P_INSC; base[ABA_PLAY].tipo = P_PLAYLISTS;
  }
  if (zona == Z_VIDEO) zona = nSub || aba < ABA_CONTA ? Z_LISTA : Z_ABAS;
  {
    Pagina *pg = atual();
    if (pg && !pg->pedido && pg->tipo != P_BUSCA) pedir(pg, 0);
  }
}
void ytui_perfil_trocou(void) {
  // Subscriptions and playlists belong to the profile's account.
  for (int i = 0; i < ABA_CONTA; i++) { base[i].gen = ++geracao; base[i].n = 0; base[i].pedido = 0; base[i].erro = YT_OK; }
  limparSub();
  temVideo = 0;
  if (zona == Z_VIDEO) zona = Z_ABAS;
  pthread_mutex_lock(&travaL); loginEstado = 0; pthread_mutex_unlock(&travaL);
}
int ytui_quer_sair(void) { int v = sair; sair = 0; return v; }
int ytui_teste_n(void) { Pagina *pg = atual(); return pg ? pg->n : 0; }
int ytui_teste_aba(void) { return aba; }
int ytui_teste_zona(void) { return zona; }
int ytui_teste_erro(void) { Pagina *pg = atual(); return zona == Z_VIDEO ? erroVideo : pg ? pg->erro : 0; }
const char *ytui_teste_lancado(void) { return lancado; }

static void trocarAba(int nova) {
  aba = nova;
  limparSub();
  {
    Pagina *pg = atual();
    if (pg && !pg->pedido && pg->tipo != P_BUSCA) pedir(pg, 0);
  }
}
static void abrirBusca(void) {
  tecladoCampo = 99;
  teclado_contexto("YouTube");
  teclado_abrir_com("Buscar no YouTube", "Títulos, canais, assuntos", 100,
                    "abcdefghijklmnopqrstuvwxyz0123456789 -'", base[ABA_BUSCA].arg);
  teclado_aceitar_utf8(1);   // Arabic and accents from the phone or the system keyboard
}
static void escolher(const YtItem *it) {
  if (it->tipo == YT_VIDEO) abrirVideo(it);
  else if (it->tipo == YT_CANAL) empilhar(P_CANAL, it->id, it->titulo);
  else empilhar(P_PLAYLIST, it->id, it->titulo);
}
void ytui_evento(const SDL_Event *e) {
  SDL_Keycode k;
  Pagina *pg = atual();
  if (teclado_aberto()) { teclado_evento(e); return; }
  if (e->type != SDL_KEYDOWN) return;
  k = e->key.keysym.sym;
  int voltar = k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE;
  int ok = k == SDLK_RETURN || k == SDLK_KP_ENTER;
  int estadoL;
  pthread_mutex_lock(&travaL); estadoL = loginEstado; pthread_mutex_unlock(&travaL);
  if (zona == Z_VIDEO) {
    int na = acoesVideo();
    if (voltar) { zona = Z_LISTA; return; }
    if (k == SDLK_UP && focoVideo > 0) focoVideo--;
    if (k == SDLK_DOWN && focoVideo + 1 < na) focoVideo++;
    if (ok && na) {
      if (focoVideo == 0) assistir();
      else { zona = Z_LISTA; empilhar(P_CANAL, video.canalId, video.canal); }
    }
    return;
  }
  if (zona == Z_CONTA) {
    if (estadoL == 1 || estadoL == 2) {   // the sign-in card: Back cancels
      if (voltar) { pthread_mutex_lock(&travaL); loginEstado = 0; pthread_mutex_unlock(&travaL); }
      return;
    }
    if (voltar || (k == SDLK_UP && focoConta == 0)) { zona = Z_ABAS; return; }
    if (k == SDLK_UP) focoConta--;
    if (k == SDLK_DOWN && focoConta + 1 < C_N) focoConta++;
    if (ok) {
      if (focoConta == C_ENTRAR) {
        if (yt_logado()) { yt_sair(); ytui_perfil_trocou(); zona = Z_CONTA; ilha_avisar("ytacct", ILHA_INFO, NULL, i18n("Conta do YouTube desconectada deste perfil."), 4000u, 0); }
        else entrar();
      } else abrirCampo(focoConta);
    }
    return;
  }
  if (zona == Z_ABAS) {
    if (voltar) { sair = 1; return; }
    if (k == SDLK_LEFT && aba > 0) trocarAba(aba - 1);
    else if (k == SDLK_RIGHT && aba + 1 < ABA_N) trocarAba(aba + 1);
    else if (k == SDLK_DOWN || ok) {
      if (aba == ABA_CONTA) { zona = Z_CONTA; focoConta = 0; }
      else if (aba == ABA_BUSCA && (ok || !base[ABA_BUSCA].arg[0])) abrirBusca();
      else zona = Z_LISTA;
    }
    return;
  }
  // list
  if (!pg) { zona = Z_ABAS; return; }
  if (voltar) {
    if (nSub) { nSub--; sub[nSub].gen = ++geracao; return; }
    zona = Z_ABAS;
    return;
  }
  // The grid: Up/Down a row, Left/Right a column.
  if (k == SDLK_UP) { if (pg->foco >= YT_COLS) pg->foco -= YT_COLS; else if (!nSub) zona = Z_ABAS; return; }
  if (k == SDLK_DOWN) { if (pg->foco + YT_COLS < pg->n) pg->foco += YT_COLS; else if (pg->foco / YT_COLS < (pg->n - 1) / YT_COLS) pg->foco = pg->n - 1; return; }
  if (k == SDLK_LEFT) { if (pg->foco % YT_COLS) pg->foco--; return; }
  if (k == SDLK_RIGHT) { if (pg->foco % YT_COLS + 1 < YT_COLS && pg->foco + 1 < pg->n) pg->foco++; return; }
  if (ok) {
    if (pg->n) escolher(&pg->itens[pg->foco]);
    else if (pg->tipo == P_BUSCA && !nSub) abrirBusca();
    else if (!pg->carregando) pedir(pg, 0);   // retry after an error
  }
}
void ytui_atualizar(float dt, Uint32 agora) {
  Pagina *pg;
  aplicarResultados();
  if (tecladoCampo >= 0) {   // ours, open or just closed (the result is read once)
    int r;
    if (teclado_aberto()) teclado_atualizar(dt, agora);
    r = teclado_resultado();
    if (r == TECLADO_PRONTO) {
      const char *t = teclado_texto();
      if (tecladoCampo == 99) {
        if (t[0]) {
          snprintf(base[ABA_BUSCA].arg, sizeof base[ABA_BUSCA].arg, "%s", t);
          base[ABA_BUSCA].tipo = P_BUSCA;
          limparSub();
          pedir(&base[ABA_BUSCA], 0);
          zona = Z_LISTA;
        }
      } else {
        int okG = 1;
        if (tecladoCampo == C_CHAVE) okG = yt_definir_chave(t);
        else if (tecladoCampo == C_CLIENTE || tecladoCampo == C_SEGREDO) {
          // The other half stays as it is; the module never hands it back, so
          // only the field typed now changes.
          okG = tecladoCampo == C_CLIENTE ? yt_definir_cliente(t, NULL) : yt_definir_cliente(NULL, t);
        }
        teclado_esquecer();
        ilha_avisar("ytacct", ILHA_INFO, NULL, i18n(okG ? "Salvo neste aparelho." : "Valor inválido: use só letras, números, - _ ."), 3500u, 0);
        if (okG && tecladoCampo == C_CHAVE) for (int i = 0; i < ABA_CONTA; i++) { base[i].pedido = 0; base[i].n = 0; base[i].gen = ++geracao; }
      }
      tecladoCampo = -1;
    } else if (r == TECLADO_CANCELOU) tecladoCampo = -1;
  }
  // Sign-in: poll at the interval Google asked for; stop after 15 minutes.
  {
    int est;
    pthread_mutex_lock(&travaL); est = loginEstado; pthread_mutex_unlock(&travaL);
    if (est == 2 && agora - loginDesde > 15u * 60u * 1000u) { pthread_mutex_lock(&travaL); loginEstado = -1; loginErro = YT_ERRO_LOGIN; pthread_mutex_unlock(&travaL); }
    else if (est == 2 && (int)(agora - loginProximo) >= 0) { loginProximo = agora + (Uint32)loginIntervalo * 1000u; disparar(fioLoginConferir); }
    else if (est == 3) {
      pthread_mutex_lock(&travaL); loginEstado = 0; pthread_mutex_unlock(&travaL);
      for (int i = ABA_INSC; i <= ABA_PLAY; i++) { base[i].pedido = 0; base[i].n = 0; base[i].gen = ++geracao; }
      ilha_avisar("ytacct", ILHA_INFO, NULL, i18n("Conta do YouTube conectada a este perfil."), 4000u, 0);
    } else if (est == -1) {
      pthread_mutex_lock(&travaL); loginEstado = 0; est = loginErro; pthread_mutex_unlock(&travaL);
      ilha_avisar("ytacct", ILHA_INFO, NULL,
                  i18n(est == YT_ERRO_REDE ? "Sem conexão com o Google. Tente de novo." :
                       est == YT_ERRO_CHAVE ? "O Google recusou o cliente OAuth. Confira o ID e o tipo (TVs e dispositivos de entrada limitada)." :
                       "A entrada não foi concluída (recusada ou expirada)."), 6000u, 0);
    }
  }
  {
    int pronto = 0, e = 0;
    YtItem v;
    pthread_mutex_lock(&travaV);
    if (videoPronto && videoGenNovo == genVideo) { pronto = 1; v = videoNovo; e = videoErroNovo; }
    videoPronto = 0;
    pthread_mutex_unlock(&travaV);
    if (pronto && temVideo) {
      carregandoVideo = 0;
      erroVideo = e;
      if (e == YT_OK && !strcmp(v.id, video.id)) video = v;
      if (focoVideo >= acoesVideo()) focoVideo = 0;
    }
  }
  pg = atual();
  // The next page when the focus nears the end.
  if (pg && zona == Z_LISTA && pg->prox[0] && !pg->carregando && pg->foco + 2 * YT_COLS >= pg->n && pg->n < PAG_MAX) pedir(pg, 1);
}

// ---------------------------------------------------------------- drawing
static void texto(TxtEstilo s, const char *t, float x, float y, float a) {
  TxtLinha l = txt_linha(s, t, 240, 242, 248, 255);
  txt_desenhar_alpha(l, x, y, a);
}
static void corta(TxtEstilo s, const char *t, float x, float y, float w, float a) {
  TxtLinha l = txt_linha_corta(s, t, 240, 242, 248, 255, w);
  txt_desenhar_alpha(l, x, y, a);
}
static void miniatura(const char *url, float x, float y, float w, float h) {
  GLuint tx = url && url[0] ? tex_obter_larg(url, w) : 0;
  if (tx) gfx_textura((GfxRect){ x, y, w, h }, tx);
  else gfx_cor((GfxRect){ x, y, w, h }, 0.2f, 1, 1, 1, 0.08f);
}
static void views(long long v, char *d, size_t n) {
  char num[24];
  if (v < 0) { d[0] = 0; return; }
  if (v >= 1000000000LL) snprintf(num, sizeof num, "%.1fB", (double)v / 1e9);
  else if (v >= 1000000LL) snprintf(num, sizeof num, "%.1fM", (double)v / 1e6);
  else if (v >= 1000LL) snprintf(num, sizeof num, "%.1fK", (double)v / 1e3);
  else snprintf(num, sizeof num, "%lld", v);
  snprintf(d, n, i18n("%s visualizações"), num);
}
static void meta(const YtItem *it, char *d, size_t n) {
  char v[64];
  size_t k = 0;
  d[0] = 0;
  if (it->tipo == YT_CANAL) { snprintf(d, n, "%s", i18n("Canal")); return; }
  if (it->tipo == YT_PLAYLIST) { snprintf(d, n, i18n("Playlist · %d vídeo(s)"), it->itens); return; }
  if (it->canal[0]) k += (size_t)snprintf(d + k, n - k, "%s", it->canal);
  views(it->views, v, sizeof v);
  if (v[0] && k < n) k += (size_t)snprintf(d + k, n - k, "%s%s", k ? "  ·  " : "", v);
  if (it->publicado[0] && k < n) k += (size_t)snprintf(d + k, n - k, "%s%s", k ? "  ·  " : "", it->publicado);
  if (it->duracao[0] && k < n) snprintf(d + k, n - k, "%s%s", k ? "  ·  " : "", !strcmp(it->duracao, "AO VIVO") ? i18n("AO VIVO") : it->duracao);
}
static const char *mensagemErro(int e, int tipo) {
  switch (e) {
    case YT_SEM_CHAVE:   return "Adicione uma chave da YouTube Data API na aba Conta para buscar e ver vídeos.";
    case YT_ERRO_CHAVE:  return "O YouTube recusou a chave da API. Confira-a na aba Conta.";
    case YT_ERRO_COTA:   return "A cota diária da sua chave da API acabou. Tente de novo amanhã.";
    case YT_ERRO_REDE:   return "Sem conexão com o YouTube. Verifique a internet e pressione OK para tentar de novo.";
    case YT_ERRO_LOGIN:  return tipo == P_INSC || tipo == P_PLAYLISTS ? "Entre com a sua conta Google na aba Conta para ver inscrições e playlists."
                                                                     : "A sessão da conta expirou. Entre de novo na aba Conta.";
    case YT_INDISPONIVEL:return "Este vídeo não está disponível (removido, privado ou bloqueado na sua região).";
    default:             return "O YouTube não respondeu como esperado. Pressione OK para tentar de novo.";
  }
}
static void desenharVideo(float x, float y, float w) {
  char b[300];
  float tw = 768, th = 432;
  miniatura(video.thumb, x, y, tw, th);
  {
    float cx = x + tw + 48, cw = w - tw - 48, cy = y;
    cy += txt_bloco_corta(TXT_TITULO2, video.titulo, 240, 242, 248, cx, cy, cw, 56, 1, 3) + 20;
    meta(&video, b, sizeof b);
    corta(TXT_CALLOUT, b, cx, cy, cw, 0.75f);
    cy += 70;
    if (erroVideo) {
      txt_bloco_corta(TXT_BODY, i18n(mensagemErro(erroVideo, P_ALTA)), 255, 140, 130, cx, cy, cw, 40, 1, 3);
    } else {
      static const char *const ROT[] = { "Assistir no app oficial do YouTube", "Mais vídeos deste canal" };
      for (int i = 0; i < acoesVideo(); i++) {
        int f = i == focoVideo;
        TxtLinha l = txt_linha(TXT_CALLOUT, i18n(ROT[i]), f ? 20 : 240, f ? 20 : 242, f ? 24 : 248, 255);
        gfx_cor((GfxRect){ cx, cy, (float)l.w + 48, 60 }, 0.5f, 1, 1, 1, f ? 0.92f : 0.1f);
        txt_desenhar_alpha(l, cx + 24, cy + 14, 1);
        cy += 76;
      }
      txt_bloco_corta(TXT_CAPTION, i18n("O vídeo abre no app do YouTube da TV. O QR TIME não reproduz vídeos do YouTube."),
                      200, 202, 210, cx, cy + 8, cw, 34, 0.7f, 2);
    }
  }
  if (carregandoVideo) texto(TXT_CAPTION, i18n("Carregando…"), x, y + th + 24, 0.6f);
  else if (video.desc[0]) txt_bloco_corta(TXT_BODY, video.desc, 220, 222, 230, x, y + th + 32, w, 40, 0.8f, 6);
}
static void desenharConta(float x, float y, float w, Uint32 agora) {
  char b[300];
  int est;
  (void)agora;
  pthread_mutex_lock(&travaL); est = loginEstado; pthread_mutex_unlock(&travaL);
  if (est == 1 || est == 2) {
    char url[128], cod[32];
    pthread_mutex_lock(&travaL); snprintf(url, sizeof url, "%s", loginUrl); snprintf(cod, sizeof cod, "%s", loginCodigo); pthread_mutex_unlock(&travaL);
    texto(TXT_TITULO2, i18n("Entrar com a conta Google"), x, y, 1);
    y += 80;
    if (est == 1 || !url[0]) { texto(TXT_BODY, i18n("Pedindo um código ao Google…"), x, y, 0.8f); return; }
    if (strcmp(qrDe, url)) { snprintf(qrDe, sizeof qrDe, "%s", url); qr_textura(&texQr, url); }
    if (texQr) gfx_textura((GfxRect){ x, y, 320, 320 }, texQr);
    snprintf(b, sizeof b, i18n("No celular, abra %s e digite o código:"), url);
    txt_bloco_corta(TXT_CALLOUT, b, 240, 242, 248, x + 380, y, w - 380, 44, 1, 2);
    { TxtLinha l = txt_linha(TXT_TITULO1, cod, 255, 255, 255, 255); txt_desenhar_alpha(l, x + 380, y + 120, 1); }
    txt_bloco_corta(TXT_CAPTION, i18n("Só leitura (youtube.readonly): inscrições e playlists. Voltar cancela."),
                    200, 202, 210, x + 380, y + 240, w - 380, 34, 0.7f, 2);
    return;
  }
  texto(TXT_TITULO2, i18n("Conta e chaves do YouTube"), x, y, 1);
  y += 80;
  for (int i = 0; i < C_N; i++) {
    int f = i == focoConta;
    const char *rot = i == C_CHAVE ? "Chave da YouTube Data API" : i == C_CLIENTE ? "ID do cliente OAuth" :
                      i == C_SEGREDO ? "Segredo do cliente OAuth" : yt_logado() ? "Sair da conta Google" : "Entrar com a conta Google";
    const char *val = i == C_CHAVE ? (yt_tem_chave() ? "definida" : "não definida") :
                      i == C_CLIENTE || i == C_SEGREDO ? (yt_tem_cliente() ? "definido" : "não definido") :
                      yt_logado() ? "conectada neste perfil" : "não conectada";
    if (f) gfx_cor((GfxRect){ x - 12, y - 8, w + 24, 64 }, 0.25f, 1, 1, 1, 0.14f);
    texto(TXT_BODY, i18n(rot), x + 8, y + 6, f ? 1 : 0.8f);
    { TxtLinha l = txt_linha(TXT_CAPTION, i18n(val), 200, 202, 210, 255); txt_desenhar_alpha(l, x + w - (float)l.w - 8, y + 12, 0.7f); }
    y += 72;
  }
  y += 24;
  txt_bloco_corta(TXT_CAPTION,
                  i18n("Busca e Em alta usam a sua chave da YouTube Data API (Google Cloud > APIs e serviços > Credenciais). "
                       "Inscrições e playlists pedem um cliente OAuth do tipo \"TVs e dispositivos de entrada limitada\". "
                       "As chaves ficam só neste aparelho e nunca vão para o registro do app."),
                  200, 202, 210, x, y, w, 36, 0.7f, 5);
}
// ---- YouTube TV-style grid (QR TIME): four big 16:9 thumbnails per row,
// the duration (or LIVE) on the picture, title on two lines under it, the
// channel and views below; channels are a round avatar. Focus: white ring.
static void thumb169(const YtItem *it, char *d, size_t n) {
  // The 16:9 "mqdefault" frame of a video (the API's larger ones are 4:3
  // with black bars); other items keep the API's own picture.
  if (it->tipo == YT_VIDEO && it->id[0]) snprintf(d, n, "https://i.ytimg.com/vi/%s/mqdefault.jpg", it->id);
  else snprintf(d, n, "%s", it->thumb);
}
static void cartaoYt(const YtItem *it, float x, float y, float w, int f) {
  float h = w * 9.0f / 16.0f, raio = 12.0f / h;
  char url[300], b[300];
  GLuint tx;
  if (f) { gfx_anel_fora((GfxRect){ x, y, w, h }, raio, 4, 4, 1, 1, 1, 1); }
  if (it->tipo == YT_CANAL) {
    float a = h * 0.86f;
    gfx_cor((GfxRect){ x, y, w, h }, raio, 1, 1, 1, f ? 0.10f : 0.05f);
    tx = it->thumb[0] ? tex_obter_larg(it->thumb, a) : 0;
    if (tx) gfx_textura((GfxRect){ x + (w - a) * 0.5f, y + (h - a) * 0.5f, a, a }, tx);
    gfx_anel((GfxRect){ x + (w - a) * 0.5f, y + (h - a) * 0.5f, a, a }, 0.5f, 3, 0.055f, 0.058f, 0.068f, 1);   // round crop edge
  } else {
    thumb169(it, url, sizeof url);
    tx = url[0] ? tex_obter_larg(url, w) : 0;
    if (tx) gfx_textura((GfxRect){ x, y, w, h }, tx);
    else gfx_cor((GfxRect){ x, y, w, h }, raio, 1, 1, 1, 0.08f);
    gfx_anel((GfxRect){ x, y, w, h }, raio, 6, 0.055f, 0.058f, 0.068f, 1);   // rounded corners over the picture
    // Badge: duration / LIVE / the playlist's count.
    b[0] = 0;
    if (it->tipo == YT_PLAYLIST) snprintf(b, sizeof b, "▶ %d", it->itens);
    else if (!strcmp(it->duracao, "AO VIVO")) snprintf(b, sizeof b, "%s", i18n("AO VIVO"));
    else snprintf(b, sizeof b, "%s", it->duracao);
    if (b[0]) {
      int vivo = !strcmp(it->duracao, "AO VIVO");
      TxtLinha l = txt_linha(TXT_ILHA_SEG, b, 255, 255, 255, 255);
      float bw = (float)l.w + 16, bh = (float)l.h + 6, bx = x + w - bw - 10, by = y + h - bh - 10;
      gfx_cor((GfxRect){ bx, by, bw, bh }, 0.25f, vivo ? 0.8f : 0, vivo ? 0.07f : 0, vivo ? 0.07f : 0, vivo ? 1 : 0.8f);
      txt_desenhar_alpha(l, bx + 8, by + 3, 1);
    }
  }
  // Title (two lines), then channel · views · age.
  y += h + 14;
  y += txt_bloco_corta(TXT_ILHA_NOME, it->titulo, 240, 242, 248, x, y, w, 30, f ? 1 : 0.9f, 2) + 4;
  // The duration is already on the picture: channel · views · date only.
  { YtItem m = *it; m.duracao[0] = 0; meta(&m, b, sizeof b); }
  { TxtLinha l = txt_linha_corta(TXT_CAPTION2, b, 170, 174, 186, 255, (int)w); txt_desenhar_alpha(l, x, y, 1); }
}

// Magic Remote: hovering a tab / a card focuses it (the same variables the arrows move), a click is OK.
static void pFocarAba(int a, int b) { (void)b; if (a != aba) trocarAba(a); zona = Z_ABAS; }
static void pFocarCartao(int a, int b) { Pagina *pg = atual(); (void)b; if (pg && a >= 0 && a < pg->n) { pg->foco = a; zona = Z_LISTA; } }
void ytui_desenhar(Uint32 agora) {
  float x = NV_MARGEM_X, y = NV_MARGEM_Y, w = NV_TELA_W - 2 * NV_MARGEM_X;
  Pagina *pg = atual();
  char b[300];
  gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0.055f, 0.058f, 0.068f, 1);
  if (zona == Z_VIDEO && temVideo) {
    desenharVideo(x, y, w);
    if (teclado_aberto()) teclado_desenhar(agora);
    return;
  }
  // The red play mark + "YouTube", then the tabs as pills (the chosen one white).
  gfx_cor((GfxRect){ x, y + 8, 64, 44 }, 0.3f, 1.0f, 0.0f, 0.0f, 1);
  { TxtLinha l = txt_linha(TXT_CALLOUT, "▶", 255, 255, 255, 255); txt_desenhar_alpha(l, x + 32 - (float)l.w * 0.5f, y + 30 - (float)l.h * 0.5f, 1); }
  texto(TXT_TITULO2, "YouTube", x + 82, y, 1);
  y += 80;
  {
    float tx = x;
    for (int i = 0; i < ABA_N; i++) {
      int sel = i == aba, f = zona == Z_ABAS && i == aba;
      TxtLinha l = txt_linha(TXT_ILHA_NOME, i18n(ABAS[i]), sel ? 11 : 240, sel ? 12 : 242, sel ? 16 : 248, 255);
      float lw = (float)l.w + 40;
      GfxRect rr = { tx, y, lw, 52 };
      if (sel) gfx_cor(rr, 0.5f, 1, 1, 1, 1);
      else { gfx_cor(rr, 0.5f, 0.118f, 0.129f, 0.173f, 0.78f); gfx_anel(rr, 0.5f, 1, 1, 1, 1, 0.2f); }
      if (f) gfx_anel_fora(rr, 0.5f, 3, 3, 1, 1, 1, 1);
      txt_desenhar_alpha(l, tx + 20, y + (52 - (float)l.h) * 0.5f, sel || f ? 1 : 0.85f);
      ponteiro_alvo(rr.x, rr.y, rr.w, rr.h, pFocarAba, NULL, i, 0);
      tx += lw + 10;
    }
    y += 80;
  }
  if (aba == ABA_CONTA && !nSub) { desenharConta(x, y, w, agora); if (teclado_aberto()) teclado_desenhar(agora); return; }
  if (!pg) return;
  if (nSub) { corta(TXT_CALLOUT, pg->titulo, x, y, w, 0.9f); y += 52; }
  else if (pg->tipo == P_BUSCA && pg->arg[0]) { snprintf(b, sizeof b, "%s \"%s\"", i18n("Resultados para"), pg->arg); corta(TXT_CALLOUT, b, x, y, w, 0.9f); y += 52; }
  {
    const float gap = 28;
    float cw = (w - gap * (YT_COLS - 1)) / YT_COLS, rh = cw * 9.0f / 16.0f + 14 + 64 + 30 + gap, area = NV_TELA_H - 70 - y;
    int linhas = (int)(area / rh), lf = pg->foco / YT_COLS, l0;
    if (linhas < 1) linhas = 1;
    l0 = lf >= linhas ? lf - linhas + 1 : 0;
    if (!pg->n) {
      const char *m = pg->carregando ? "Carregando…" :
                      pg->erro ? mensagemErro(pg->erro, pg->tipo) :
                      pg->tipo == P_BUSCA && !pg->arg[0] && !nSub ? "Pressione OK para buscar vídeos no YouTube." :
                      !pg->pedido ? "" : "Nada por aqui.";
      if (m[0]) txt_bloco_corta(TXT_BODY, i18n(m), 240, 242, 248, x, y + 20, w, 40, 0.75f, 3);
    }
    for (int i = l0 * YT_COLS; i < pg->n && i < (l0 + linhas) * YT_COLS; i++) {
      float cx = x + (float)(i % YT_COLS) * (cw + gap), cy = y + (float)(i / YT_COLS - l0) * rh;
      cartaoYt(&pg->itens[i], cx, cy, cw, zona == Z_LISTA && i == pg->foco);
      ponteiro_alvo(cx, cy, cw, rh - gap, pFocarCartao, NULL, i, 0);
    }
    if (pg->n && pg->carregando) texto(TXT_CAPTION, i18n("Carregando mais…"), x, NV_TELA_H - 56, 0.6f);
    else if (pg->n && pg->erro) corta(TXT_CAPTION, i18n(mensagemErro(pg->erro, pg->tipo)), x, NV_TELA_H - 56, w, 0.7f);
  }
  if (teclado_aberto()) teclado_desenhar(agora);
}
