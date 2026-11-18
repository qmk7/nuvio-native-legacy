#include "mcui.h"
#include "mccat.h"
#include "mcfila.h"
#include "mcfmt.h"
#include "mcfontes.h"
#include "mclfm.h"
#include "mclrc.h"
#include "mcoffline.h"
#include "mcplayer.h"
#include "mcspotify.h"
#include "ytapi.h"
#include "ondever.h"
#include "video.h"
#include "esmaecer.h"
#include "dados.h"
#include "perfis.h"
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

#define PAG_MAX 500
#define PILHA 6
#define LOTE 60

// ---------------------------------------------------------------- rows and pages
enum { L_FAIXA, L_ALBUM, L_ARTISTA, L_PLAYLIST, L_GENERO, L_PASTA, L_FONTE, L_ACAO, L_TITULO, L_INFO, L_YTVIDEO, L_YTLISTA };
enum {
  A_NADA, A_RETOMAR, A_TOCAR_TUDO, A_ALEATORIO_TUDO, A_ADD_SUBSONIC, A_ADD_WEBDAV, A_LFM_CHAVE, A_LFM_SEGREDO,
  A_LFM_CONECTAR, A_LFM_SAIR, A_YTMUSIC, A_FESTA, A_LRCLIB, A_FILA_ALEATORIO, A_FILA_REPETIR, A_FILA_LIMPAR,
  A_FILA_SALVAR, A_NOVA_BUSCA, A_SIMILAR, A_TOCANDO, A_SP_ID, A_SP_SEGREDO, A_SP_IMPORTAR, A_YT_BUSCA, A_YT_LISTA_TOCAR, A_YTM_ABRIR
};
typedef struct {
  int tipo, acao;
  McFaixa f;                 // L_FAIXA
  char ref[MC_ID];           // id of the album / artist / playlist / genre / folder href / source key
  char t1[200], t2[220];
  char capa[96];
  char thumb[256], lista[64];   // YouTube rows: https thumbnail, playlist id
} Linha;
enum {
  P_INICIO, P_RECENTES, P_ARTISTAS, P_ALBUNS, P_FAIXAS, P_GENEROS, P_PLAYLISTS, P_FAVORITOS, P_OFFLINE, P_FILA,
  P_BUSCAR, P_SERVICOS, P_AJUSTES, P_SECOES,
  P_ALBUM = 100, P_ARTISTA, P_PLAYLIST, P_GENERO, P_PASTA, P_YTM, P_YTM_BUSCA, P_YTM_LISTA
};
static const char *const SECOES[P_SECOES] = {
  "Início", "Tocadas recentemente", "Artistas", "Álbuns", "Faixas", "Gêneros", "Playlists", "Favoritos",
  "Offline", "Fila", "Buscar", "Serviços de música", "Ajustes"
};
typedef struct {
  int tipo;
  char arg[MC_ID], arg2[MC_ID], titulo[200];
  Linha *v; int n;
  int foco, carregando, erro, pedido, offset, mais;
  unsigned gen;
} Pagina;

enum { Z_MENU, Z_LISTA, Z_TOCANDO, Z_FORM, Z_ACOES };
static Pagina pilha[PILHA];
static int nPilha, secao, zona = Z_MENU, sair, abertoUmaVez, zonaAntesTocando = Z_LISTA;
static unsigned geracao;
static int festaLig, lrclibLig = 1;

static Pagina *topo(void) { return nPilha ? &pilha[nPilha - 1] : NULL; }
static Linha *novaLinha(Pagina *pg) {
  if (!pg->v) pg->v = calloc(PAG_MAX, sizeof *pg->v);
  if (!pg->v || pg->n >= PAG_MAX) return NULL;
  memset(&pg->v[pg->n], 0, sizeof pg->v[0]);
  return &pg->v[pg->n++];
}
static void titulo(Pagina *pg, const char *t) {
  Linha *l = novaLinha(pg);
  if (l) { l->tipo = L_TITULO; snprintf(l->t1, sizeof l->t1, "%s", t); }
}
static void acao(Pagina *pg, int a, const char *t1, const char *t2) {
  Linha *l = novaLinha(pg);
  if (l) { l->tipo = L_ACAO; l->acao = a; snprintf(l->t1, sizeof l->t1, "%s", t1); snprintf(l->t2, sizeof l->t2, "%s", t2 ? t2 : ""); }
}
static void info(Pagina *pg, const char *t) {
  Linha *l = novaLinha(pg);
  if (l) { l->tipo = L_INFO; snprintf(l->t1, sizeof l->t1, "%s", t); }
}
static void durTexto(int s, char *d, size_t n) {
  if (s <= 0) { d[0] = 0; return; }
  if (s >= 3600) snprintf(d, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
  else snprintf(d, n, "%d:%02d", s / 60, s % 60);
}
static void linhaFaixa(Linha *l, const McFaixa *f) {
  char d[16], fmt[64];
  l->tipo = L_FAIXA;
  l->f = *f;
  snprintf(l->t1, sizeof l->t1, "%s", f->titulo);
  durTexto(f->dur, d, sizeof d);
  mcfmt_descrever(f, fmt, sizeof fmt);
  {
    const char *selo = mcfmt_selo(mcfmt_classe(f));
    snprintf(l->t2, sizeof l->t2, "%s%s%s%s%s%s%s%s%s", f->artista, f->album[0] ? "  ·  " : "", f->album, d[0] ? "  ·  " : "", d,
             fmt[0] ? "  ·  " : "", fmt, selo[0] ? "  ·  " : "", selo);
  }
  snprintf(l->ref, sizeof l->ref, "%s", f->id);
  snprintf(l->capa, sizeof l->capa, "%s", f->capa);
}
static void linhaAlbum(Linha *l, const McAlbum *a) {
  l->tipo = L_ALBUM;
  snprintf(l->ref, sizeof l->ref, "%s", a->id);
  snprintf(l->t1, sizeof l->t1, "%s", a->nome);
  if (a->ano) snprintf(l->t2, sizeof l->t2, "%s  ·  %d  ·  %d %s", a->artista, a->ano, a->faixas, i18n("faixas"));
  else snprintf(l->t2, sizeof l->t2, "%s  ·  %d %s", a->artista, a->faixas, i18n("faixas"));
  snprintf(l->capa, sizeof l->capa, "%s", a->capa);
}

// ---------------------------------------------------------------- worker
typedef struct { Pagina *alvo; unsigned gen; int tipo, offset, anexar; char arg[MC_ID]; } Pedido;
typedef struct { Pagina *alvo; unsigned gen; int anexar, erro, mais; Linha *v; int n; char titulo[200]; } Resultado;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static Resultado fila[8]; static int nFila;

static void *fioCarga(void *u) {
  Pedido p = *(Pedido *)u;
  Pagina tmp;
  Resultado r;
  int e = MC_OK, n = 0;
  free(u);
  memset(&tmp, 0, sizeof tmp);
  memset(&r, 0, sizeof r);
  switch (p.tipo) {
    case P_INICIO: {
      McAlbum *al = calloc(12, sizeof *al);
      if (al && mccat_albuns("newest", 0, al, 12, &n) == MC_OK && n) {
        titulo(&tmp, i18n("Adicionados recentemente"));
        for (int i = 0; i < n; i++) { Linha *l = novaLinha(&tmp); if (l) linhaAlbum(l, &al[i]); }
      }
      if (al && mccat_albuns("frequent", 0, al, 12, &n) == MC_OK && n) {
        titulo(&tmp, i18n("Mais tocados"));
        for (int i = 0; i < n; i++) { Linha *l = novaLinha(&tmp); if (l) linhaAlbum(l, &al[i]); }
      }
      free(al);
      break; }
    case P_ARTISTAS: {
      McArtista *ar = calloc(PAG_MAX, sizeof *ar);
      if (ar) e = mccat_artistas(ar, PAG_MAX, &n);
      for (int i = 0; ar && i < n; i++) {
        Linha *l = novaLinha(&tmp);
        if (!l) break;
        l->tipo = L_ARTISTA;
        snprintf(l->ref, sizeof l->ref, "%s", ar[i].id);
        snprintf(l->t1, sizeof l->t1, "%s", ar[i].nome);
        snprintf(l->t2, sizeof l->t2, "%d %s", ar[i].albuns, i18n("álbuns"));
        snprintf(l->capa, sizeof l->capa, "%s", ar[i].capa);
      }
      free(ar);
      break; }
    case P_ALBUNS: case P_ARTISTA: {
      McAlbum *al = calloc(LOTE, sizeof *al);
      if (!al) { e = MC_ERRO_OUTRO; break; }
      if (p.tipo == P_ALBUNS) { e = mccat_albuns("alphabeticalByName", p.offset, al, LOTE, &n); r.mais = n == LOTE; }
      else {
        char bio[700], img[8];
        e = mccat_artista_albuns(p.arg, al, LOTE, &n);
        if (!p.anexar && mclfm_tem_chave() && n && mclfm_artista(al[0].artista, bio, sizeof bio, img, sizeof img) == MC_OK) {
          Linha *l = novaLinha(&tmp);
          if (l) { l->tipo = L_INFO; snprintf(l->t1, sizeof l->t1, "%.190s", bio); snprintf(l->t2, sizeof l->t2, "%s", bio); }
        }
      }
      for (int i = 0; i < n; i++) { Linha *l = novaLinha(&tmp); if (l) linhaAlbum(l, &al[i]); }
      if (p.tipo == P_ARTISTA && !p.anexar && n && mclfm_tem_chave()) {
        char sim[6][128];
        int ns = mclfm_similares(al[0].artista, sim, 6);
        if (ns) titulo(&tmp, i18n("Artistas parecidos (Last.fm)"));
        for (int i = 0; i < ns; i++) acao(&tmp, A_SIMILAR, sim[i], i18n("Buscar na sua biblioteca"));
      }
      free(al);
      break; }
    case P_ALBUM: case P_FAIXAS: case P_GENERO: case P_PLAYLIST: case P_FAVORITOS: {
      McFaixa *v = calloc(PAG_MAX, sizeof *v);
      McAlbum info;
      int max = p.tipo == P_FAIXAS || p.tipo == P_GENERO ? LOTE : PAG_MAX - 2;
      if (!v) { e = MC_ERRO_OUTRO; break; }
      memset(&info, 0, sizeof info);
      if (p.tipo == P_ALBUM) e = mccat_album(p.arg, &info, v, max, &n);
      else if (p.tipo == P_FAIXAS) e = mccat_faixas(p.offset, v, max, &n);
      else if (p.tipo == P_GENERO) e = mccat_genero_faixas(p.arg, p.offset, v, max, &n);
      else if (p.tipo == P_PLAYLIST) e = mccat_playlist(p.arg, v, max, &n);
      else e = mccat_favoritos(v, max, &n);
      r.mais = (p.tipo == P_FAIXAS || p.tipo == P_GENERO) && n == LOTE;
      if (!p.anexar && n && p.tipo != P_FAIXAS && p.tipo != P_GENERO) {
        acao(&tmp, A_TOCAR_TUDO, i18n("Tocar tudo"), NULL);
        acao(&tmp, A_ALEATORIO_TUDO, i18n("Aleatório"), NULL);
      }
      if (p.tipo == P_ALBUM && info.nome[0]) snprintf(r.titulo, sizeof r.titulo, "%s — %s", info.nome, info.artista);
      for (int i = 0; i < n; i++) { Linha *l = novaLinha(&tmp); if (l) linhaFaixa(l, &v[i]); }
      free(v);
      break; }
    case P_GENEROS: {
      McGenero *g = calloc(300, sizeof *g);
      if (g) e = mccat_generos(g, 300, &n);
      for (int i = 0; g && i < n; i++) {
        Linha *l = novaLinha(&tmp);
        if (!l) break;
        l->tipo = L_GENERO;
        snprintf(l->ref, sizeof l->ref, "%s", g[i].nome);
        snprintf(l->t1, sizeof l->t1, "%s", g[i].nome);
        snprintf(l->t2, sizeof l->t2, "%d %s  ·  %d %s", g[i].faixas, i18n("faixas"), g[i].albuns, i18n("álbuns"));
      }
      free(g);
      break; }
    case P_PLAYLISTS: {
      McPlaylist *pl = calloc(200, sizeof *pl);
      if (pl) e = mccat_playlists(pl, 200, &n);
      for (int i = 0; pl && i < n; i++) {
        Linha *l = novaLinha(&tmp);
        if (!l) break;
        l->tipo = L_PLAYLIST;
        snprintf(l->ref, sizeof l->ref, "%s", pl[i].id);
        snprintf(l->t1, sizeof l->t1, "%s", pl[i].nome);
        snprintf(l->t2, sizeof l->t2, "%d %s  ·  %s", pl[i].faixas, i18n("faixas"),
                 !strncmp(pl[i].id, "lc:", 3) ? i18n("neste aparelho") : i18n("no servidor"));
      }
      free(pl);
      break; }
    case P_BUSCAR: {
      McArtista ar[10];
      McAlbum *al = calloc(20, sizeof *al);
      McFaixa *fx = calloc(60, sizeof *fx);
      int na = 0, nl = 0, nf = 0;
      if (al && fx) e = mccat_buscar(p.arg, ar, 10, &na, al, 20, &nl, fx, 60, &nf);
      if (na) titulo(&tmp, i18n("Artistas"));
      for (int i = 0; i < na; i++) {
        Linha *l = novaLinha(&tmp);
        if (!l) break;
        l->tipo = L_ARTISTA;
        snprintf(l->ref, sizeof l->ref, "%s", ar[i].id);
        snprintf(l->t1, sizeof l->t1, "%s", ar[i].nome);
      }
      if (nl) titulo(&tmp, i18n("Álbuns"));
      for (int i = 0; i < nl; i++) { Linha *l = novaLinha(&tmp); if (l) linhaAlbum(l, &al[i]); }
      if (nf) titulo(&tmp, i18n("Faixas"));
      for (int i = 0; i < nf; i++) { Linha *l = novaLinha(&tmp); if (l) linhaFaixa(l, &fx[i]); }
      n = na + nl + nf;
      free(al); free(fx);
      break; }
    case P_YTM: case P_YTM_BUSCA: case P_YTM_LISTA: {
      YtItem *v = calloc(50, sizeof *v);
      char prox[64];
      if (!v) { e = MC_ERRO_OUTRO; break; }
      yt_carregar();
      if (!yt_tem_chave()) {
        info(&tmp, i18n("Para buscar música do YouTube, adicione a chave da YouTube Data API em YouTube > Conta."));
        acao(&tmp, A_YTMUSIC, i18n("Abrir a seção YouTube"), NULL);
        free(v);
        break;
      }
      if (p.tipo == P_YTM) {
        acao(&tmp, A_YT_BUSCA, i18n("Buscar música no YouTube"), i18n("Clipes, áudios oficiais e envios do YouTube Music"));
        if (yt_logado()) {
          int r = yt_playlists(v, 50, &n);
          titulo(&tmp, i18n("Suas playlists (YouTube e YouTube Music)"));
          if (r != YT_OK) info(&tmp, i18n("Não foi possível ler as suas playlists agora."));
          for (int i = 0; i < n; i++) {
            Linha *l = novaLinha(&tmp);
            if (!l) break;
            l->tipo = L_YTLISTA;
            snprintf(l->ref, sizeof l->ref, "%s", v[i].id);
            snprintf(l->t1, sizeof l->t1, "%s", v[i].titulo);
            snprintf(l->t2, sizeof l->t2, i18n("Playlist · %d vídeo(s)"), v[i].itens);
            snprintf(l->thumb, sizeof l->thumb, "%s", v[i].thumb);
          }
        } else info(&tmp, i18n("Entre com a sua conta Google em YouTube > Conta para ver as suas playlists aqui."));
        info(&tmp, i18n("Toca no app oficial do YouTube (aba Música). Qualidade do YouTube Music: até 256 kbps AAC, com perda."));
      } else {
        int r = p.tipo == P_YTM_BUSCA ? yt_buscar_musica(p.arg, NULL, v, 40, &n, prox, sizeof prox)
                                      : yt_playlist_itens(p.arg, NULL, v, 50, &n, prox, sizeof prox);
        if (r == YT_OK && n) yt_detalhes(v, n);
        if (r != YT_OK) e = r == YT_ERRO_REDE ? MC_ERRO_REDE : r == YT_ERRO_LOGIN ? MC_ERRO_LOGIN : MC_ERRO_SERVIDOR;
        if (p.tipo == P_YTM_LISTA && n) acao(&tmp, A_YT_LISTA_TOCAR, i18n("Tocar a playlist no app do YouTube"), NULL);
        for (int i = 0; i < n; i++) {
          Linha *l = novaLinha(&tmp);
          if (!l) break;
          l->tipo = L_YTVIDEO;
          snprintf(l->ref, sizeof l->ref, "%s", v[i].id);
          if (p.tipo == P_YTM_LISTA) snprintf(l->lista, sizeof l->lista, "%.63s", p.arg);
          snprintf(l->t1, sizeof l->t1, "%s", v[i].titulo);
          snprintf(l->t2, sizeof l->t2, "%s%s%s", v[i].canal, v[i].duracao[0] ? "  ·  " : "", v[i].duracao);
          snprintf(l->thumb, sizeof l->thumb, "%s", v[i].thumb);
        }
      }
      free(v);
      break; }
    case P_PASTA: {
      DavItem *pa = calloc(200, sizeof *pa);
      McFaixa *ar = calloc(300, sizeof *ar);
      int np = 0, nar = 0;
      if (pa && ar) {
        char fonte[16];
        snprintf(fonte, sizeof fonte, "%.10s", p.arg);
        e = mccat_pasta(fonte, p.arg + 11, pa, 200, &np, ar, 300, &nar);
        if (e == MC_OK && nar) mccat_tags(ar, nar > 60 ? 60 : nar);   // tags for what is on screen first
        for (int i = 0; i < np; i++) {
          Linha *l = novaLinha(&tmp);
          if (!l) break;
          l->tipo = L_PASTA;
          snprintf(l->ref, sizeof l->ref, "%s:%s", fonte, pa[i].href);
          snprintf(l->t1, sizeof l->t1, "%s", pa[i].nome);
          snprintf(l->t2, sizeof l->t2, "%s", i18n("Pasta"));
        }
        if (nar) { acao(&tmp, A_TOCAR_TUDO, i18n("Tocar esta pasta"), NULL); acao(&tmp, A_ALEATORIO_TUDO, i18n("Aleatório"), NULL); }
        for (int i = 0; i < nar; i++) { Linha *l = novaLinha(&tmp); if (l) linhaFaixa(l, &ar[i]); }
        n = np + nar;
      }
      free(pa); free(ar);
      break; }
  }
  r.alvo = p.alvo; r.gen = p.gen; r.anexar = p.anexar; r.erro = e;
  r.v = tmp.v; r.n = tmp.n;
  printf("[musica] pagina %d: %d linha(s), erro %d\n", p.tipo, tmp.n, e);
  fflush(stdout);
  pthread_mutex_lock(&trava);
  if (nFila < 8) fila[nFila++] = r; else free(r.v);
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void pedir(Pagina *pg, int anexar) {
  pthread_t t;
  Pedido *p = calloc(1, sizeof *p);
  if (!p) return;
  if (!anexar) { pg->gen = ++geracao; pg->n = 0; pg->foco = 0; pg->offset = 0; pg->mais = 0; }
  pg->carregando = 1; pg->erro = MC_OK; pg->pedido = 1;
  p->alvo = pg; p->gen = pg->gen; p->tipo = pg->tipo; p->offset = pg->offset; p->anexar = anexar;
  snprintf(p->arg, sizeof p->arg, "%s", pg->arg);
  if (pthread_create(&t, NULL, fioCarga, p) != 0) { free(p); pg->carregando = 0; pg->erro = MC_ERRO_OUTRO; return; }
  pthread_detach(t);
}
static int pulavel(const Linha *l) { return l->tipo == L_TITULO || (l->tipo == L_INFO && !l->t2[0]); }
static void ajustarFoco(Pagina *pg, int dir) {
  if (!pg->n) { pg->foco = 0; return; }
  if (pg->foco >= pg->n) pg->foco = pg->n - 1;
  while (pg->foco >= 0 && pg->foco < pg->n && pulavel(&pg->v[pg->foco])) pg->foco += dir ? dir : 1;
  if (pg->foco >= pg->n) { pg->foco = pg->n - 1; while (pg->foco > 0 && pulavel(&pg->v[pg->foco])) pg->foco--; }
  if (pg->foco < 0) { pg->foco = 0; while (pg->foco < pg->n - 1 && pulavel(&pg->v[pg->foco])) pg->foco++; }
}
static void aplicar(void) {
  Resultado r[8];
  int n;
  pthread_mutex_lock(&trava);
  n = nFila; memcpy(r, fila, sizeof r[0] * (size_t)n); nFila = 0;
  pthread_mutex_unlock(&trava);
  for (int i = 0; i < n; i++) {
    Pagina *pg = r[i].alvo;
    if (pg->gen != r[i].gen) { free(r[i].v); continue; }
    pg->carregando = 0;
    pg->erro = r[i].erro;
    pg->mais = r[i].mais;
    if (r[i].titulo[0]) snprintf(pg->titulo, sizeof pg->titulo, "%s", r[i].titulo);
    for (int k = 0; k < r[i].n; k++) { Linha *l = novaLinha(pg); if (l) *l = r[i].v[k]; }
    free(r[i].v);
    ajustarFoco(pg, 1);
  }
}

// ---------------------------------------------------------------- local pages
static void construirLocal(Pagina *pg) {
  char b[300];
  int atualRow = -1, focoAntes = pg->foco, mesmaPagina = pg->pedido && pg->tipo == P_FILA;
  pg->n = 0;
  pg->carregando = 0; pg->erro = MC_OK; pg->pedido = 1;
  switch (pg->tipo) {
    case P_RECENTES:
      if (!mch_n()) info(pg, i18n("Nada tocado ainda neste perfil."));
      for (int i = 0; i < mch_n(); i++) { Linha *l = novaLinha(pg); if (l) linhaFaixa(l, mch_item(i)); }
      break;
    case P_OFFLINE: {
      long long u = mco_usado();
      snprintf(b, sizeof b, i18n("%d faixa(s) · %.1f de %.0f GB neste aparelho"), mco_n(), (double)u / (1 << 30), (double)MCO_TETO / (1 << 30));
      info(pg, b);
      if (!mco_n()) info(pg, i18n("Para guardar uma faixa, abra as opções dela (→) e escolha \"Baixar para ouvir offline\"."));
      else { acao(pg, A_TOCAR_TUDO, i18n("Tocar tudo"), NULL); }
      for (int i = 0; i < mco_n(); i++) { McFaixa f; Linha *l; if (mco_item(i, &f) && (l = novaLinha(pg))) linhaFaixa(l, &f); }
      break; }
    case P_FILA: {
      static const char *const REP[] = { "Repetir: não", "Repetir: todas", "Repetir: uma" };
      acao(pg, A_FILA_ALEATORIO, i18n(mcq_eh_aleatorio() ? "Aleatório: ligado" : "Aleatório: desligado"), NULL);
      acao(pg, A_FILA_REPETIR, i18n(REP[mcq_repeticao()]), NULL);
      if (mcq_n()) { acao(pg, A_FILA_SALVAR, i18n("Salvar a fila como playlist"), NULL); acao(pg, A_FILA_LIMPAR, i18n("Limpar a fila"), NULL); }
      snprintf(b, sizeof b, i18n("%d faixa(s) na fila"), mcq_n());
      titulo(pg, b);
      for (int i = 0; i < mcq_n(); i++) {
        Linha *l = novaLinha(pg);
        if (!l) break;
        linhaFaixa(l, mcq_item(i));
        if (i == mcq_atual()) { char t[220]; snprintf(t, sizeof t, "♪ %s", l->t1); snprintf(l->t1, sizeof l->t1, "%s", t); atualRow = pg->n - 1; }
      }
      break; }
    case P_SERVICOS: {
      McFonte f;
      titulo(pg, i18n("Seus servidores"));
      for (int i = 0; mcfontes_copiar(i, &f); i++) {
        Linha *l = novaLinha(pg);
        if (!l) break;
        l->tipo = L_FONTE;
        snprintf(l->ref, sizeof l->ref, "%s", f.chave);
        snprintf(l->t1, sizeof l->t1, "%s", f.nome);
        snprintf(l->t2, sizeof l->t2, "%s  ·  %s", f.tipo == MCS_SUBSONIC ? "Navidrome / Subsonic" : "WebDAV",
                 f.tipo == MCS_SUBSONIC ? f.ss.url : f.dv.url);
      }
      acao(pg, A_ADD_SUBSONIC, i18n("Adicionar Navidrome / Subsonic"), i18n("Endereço, usuário e senha"));
      acao(pg, A_ADD_WEBDAV, i18n("Adicionar pasta WebDAV"), i18n("Nextcloud, NAS, rclone…"));
      titulo(pg, "Last.fm");
      acao(pg, A_LFM_CHAVE, i18n("Chave da API do Last.fm"), i18n(mclfm_tem_chave() ? "definida" : "não definida"));
      acao(pg, A_LFM_SEGREDO, i18n("Segredo compartilhado do Last.fm"), NULL);
      if (mclfm_conectado()) {
        char u[96], t[200];
        mclfm_usuario(u, sizeof u);
        snprintf(t, sizeof t, "%s: %s", i18n("Conectado como"), u);
        acao(pg, A_LFM_SAIR, i18n("Desconectar o Last.fm"), t);
      } else acao(pg, A_LFM_CONECTAR, i18n("Conectar o Last.fm (scrobbling)"), i18n("Aprovação pelo celular"));
      titulo(pg, i18n("Outros serviços"));
      {
        Linha *l = novaLinha(pg);
        if (l) { l->tipo = L_ACAO; l->acao = A_YTM_ABRIR; snprintf(l->t1, sizeof l->t1, "YouTube Music");
                 snprintf(l->t2, sizeof l->t2, "%s", i18n("Busca e as suas playlists; toca no app oficial do YouTube")); }
      }
      acao(pg, A_SP_ID, i18n("Spotify: Client ID"), i18n(mcsp_tem_chave() ? "definido" : "não definido"));
      acao(pg, A_SP_SEGREDO, i18n("Spotify: Client secret"), NULL);
      acao(pg, A_SP_IMPORTAR, i18n("Importar uma playlist pública do Spotify"),
           i18n("Só os nomes: as faixas encontradas tocam do seu servidor. O Spotify não permite tocar fora dos apps dele."));
      break; }
    case P_AJUSTES: {
      McSaidaInfo si;
      acao(pg, A_FESTA, i18n(festaLig ? "Modo festa: ligado" : "Modo festa: desligado"), i18n("Continua com faixas parecidas quando a fila acaba"));
      acao(pg, A_LRCLIB, i18n(lrclibLig ? "Letras do LRCLIB: ligadas" : "Letras do LRCLIB: desligadas"), i18n("Quando o servidor não tem a letra"));
      titulo(pg, i18n("Qualidade de áudio"));
      info(pg, i18n("As faixas vão como o arquivo original (sem conversão do servidor)."));
      mcp_saida(&si);
      snprintf(b, sizeof b, "%s: %s", i18n("Saída"), mcp_backend()[0] ? mcp_backend() : "-");
      info(pg, b);
      info(pg, i18n("Na TV, o som é decodificado pela própria TV. O formato que sai pelo HDMI/eARC é definido nas configurações de som da TV e não é visível para apps: confira no visor do receptor."));
      titulo(pg, i18n("Para a melhor qualidade na TV LG + barra de som eARC"));
      info(pg, i18n("1. Ajustes da TV > Som > Saída de som: HDMI (eARC)."));
      info(pg, i18n("2. Som > Ajustes adicionais > Saída de som digital: Pass Through (ou Automático)."));
      info(pg, i18n("3. Som > Ajustes adicionais > eARC: ligado. Na barra de som, a entrada HDMI eARC."));
      info(pg, i18n("4. Prefira faixas Lossless ou Hi-Res (selo na lista). MP3, AAC e YouTube Music têm perda."));
      info(pg, i18n("5. Confira o formato recebido no visor da barra de som."));
      break; }
  }
  // The queue opens on the playing track; a rebuild of the same page keeps the focus.
  if (pg->tipo == P_FILA) pg->foco = mesmaPagina ? focoAntes : (atualRow >= 0 ? atualRow : 0);
  ajustarFoco(pg, 1);
  printf("[musica] pagina %d: %d linha(s) (local)\n", pg->tipo, pg->n);
  fflush(stdout);
}
static int ehLocal(int tipo) { return tipo == P_RECENTES || tipo == P_OFFLINE || tipo == P_FILA || tipo == P_SERVICOS || tipo == P_AJUSTES; }
static void carregar(Pagina *pg) {
  if (ehLocal(pg->tipo)) construirLocal(pg);
  else if (pg->tipo == P_BUSCAR && !pg->arg[0]) { pg->n = 0; pg->pedido = 1; acao(pg, A_NOVA_BUSCA, i18n("Buscar artistas, álbuns e faixas"), NULL); }
  else if (pg->tipo == P_INICIO) {
    // "Continue" first (this profile's saved queue), then the servers' rows.
    pg->n = 0; pg->foco = 0; pg->offset = 0; pg->mais = 0; pg->gen = ++geracao;
    if (mcq_faixa_atual()) {
      char t[300];
      snprintf(t, sizeof t, "%s: %s — %s", i18n("Continuar"), mcq_faixa_atual()->titulo, mcq_faixa_atual()->artista);
      acao(pg, A_RETOMAR, t, i18n("De onde parou, na mesma fila"));
    }
    pedir(pg, 1);
  } else pedir(pg, 0);
}
static void empilhar(int tipo, const char *arg, const char *t) {
  Pagina *pg;
  if (nPilha == PILHA) {   // drop the oldest page above the section
    Linha *v = pilha[1].v;
    memmove(&pilha[1], &pilha[2], sizeof pilha[0] * (PILHA - 2));
    pilha[PILHA - 1].v = v;
    nPilha--;
  }
  pg = &pilha[nPilha++];
  pg->tipo = tipo;
  pg->n = 0;
  pg->foco = 0; pg->pedido = 0; pg->carregando = 0; pg->erro = MC_OK; pg->mais = 0; pg->offset = 0;
  pg->gen = ++geracao;
  snprintf(pg->arg, sizeof pg->arg, "%s", arg ? arg : "");
  snprintf(pg->titulo, sizeof pg->titulo, "%s", t ? t : "");
  carregar(pg);
  zona = Z_LISTA;
}
static void abrirSecao(int s) {
  for (int i = 0; i < nPilha; i++) pilha[i].gen = ++geracao;
  nPilha = 0;
  secao = s;
  empilhar(s, "", i18n(SECOES[s]));
}

// ---------------------------------------------------------------- playing
static void tocarLista(const Pagina *pg, int desde, int aleatorio) {
  McFaixa *v = malloc(sizeof *v * (size_t)(pg->n ? pg->n : 1));
  int n = 0, ini = 0;
  if (!v) return;
  for (int i = 0; i < pg->n; i++) if (pg->v[i].tipo == L_FAIXA) { if (i == desde) ini = n; v[n++] = pg->v[i].f; }
  if (!n) { free(v); return; }
  mcq_aleatorio(0);
  mcq_definir(v, n, ini);
  if (aleatorio) { mcq_aleatorio(1); mcq_ir(0); }
  free(v);
  mcp_tocar(mcq_atual());
  mcq_salvar();
  zonaAntesTocando = zona == Z_TOCANDO ? zonaAntesTocando : zona;
  zona = Z_TOCANDO;
}

// ---------------------------------------------------------------- track actions
enum { AC_TOCAR, AC_A_SEGUIR, AC_FILA, AC_FAV, AC_PLAYLIST, AC_ALBUM, AC_ARTISTA, AC_BAIXAR, AC_APAGAR_OFF,
       AC_FILA_REMOVER, AC_FILA_CIMA, AC_FILA_BAIXO, AC_LISTA_REMOVER, AC_LOCAL_EDITAR, AC_LOCAL_REMOVER, AC_N };
static int acoes[AC_N], nAcoes, focoAcao;
static Linha linhaAcao;
static int linhaAcaoIdx;
static const char *rotuloAcao(int a) {
  switch (a) {
    case AC_TOCAR: return linhaAcao.tipo == L_FAIXA ? "Tocar" : "Tocar tudo";
    case AC_A_SEGUIR: return "Tocar a seguir";
    case AC_FILA: return "Adicionar à fila";
    case AC_FAV: return linhaAcao.f.fav || mcv_fav(linhaAcao.f.id) ? "Remover dos favoritos" : "Favoritar";
    case AC_PLAYLIST: return "Adicionar a uma playlist local";
    case AC_ALBUM: return "Ir para o álbum";
    case AC_ARTISTA: return "Ir para o artista";
    case AC_BAIXAR: return "Baixar para ouvir offline";
    case AC_APAGAR_OFF: return "Apagar a cópia offline";
    case AC_FILA_REMOVER: return "Remover da fila";
    case AC_FILA_CIMA: return "Mover para cima";
    case AC_FILA_BAIXO: return "Mover para baixo";
    case AC_LISTA_REMOVER: return "Remover desta playlist";
    case AC_LOCAL_EDITAR: return "Editar servidor";
    case AC_LOCAL_REMOVER: return "Remover servidor";
  }
  return "";
}
static void abrirAcoes(const Linha *l, int idx) {
  Pagina *pg = topo();
  char u[16];
  linhaAcao = *l; linhaAcaoIdx = idx;
  nAcoes = 0; focoAcao = 0;
  if (l->tipo == L_FAIXA) {
    acoes[nAcoes++] = AC_TOCAR; acoes[nAcoes++] = AC_A_SEGUIR; acoes[nAcoes++] = AC_FILA; acoes[nAcoes++] = AC_FAV;
    acoes[nAcoes++] = AC_PLAYLIST;
    if (l->f.albumId[0]) acoes[nAcoes++] = AC_ALBUM;
    if (l->f.artistaId[0]) acoes[nAcoes++] = AC_ARTISTA;
    acoes[nAcoes++] = mco_caminho(l->f.id, (char[700]){0}, 700) ? AC_APAGAR_OFF : AC_BAIXAR;
    if (pg && pg->tipo == P_FILA) { acoes[nAcoes++] = AC_FILA_REMOVER; acoes[nAcoes++] = AC_FILA_CIMA; acoes[nAcoes++] = AC_FILA_BAIXO; }
    if (pg && pg->tipo == P_PLAYLIST && !strncmp(pg->arg, "lc:", 3)) acoes[nAcoes++] = AC_LISTA_REMOVER;
  } else if (l->tipo == L_ALBUM || l->tipo == L_PLAYLIST) {
    acoes[nAcoes++] = AC_TOCAR; acoes[nAcoes++] = AC_A_SEGUIR; acoes[nAcoes++] = AC_FILA;
  } else if (l->tipo == L_FONTE) {
    acoes[nAcoes++] = AC_LOCAL_EDITAR; acoes[nAcoes++] = AC_LOCAL_REMOVER;
  }
  (void)u;
  if (nAcoes) zona = Z_ACOES;
}
// Album / playlist actions need its tracks: fetched on a worker.
typedef struct { int acao; char ref[MC_ID]; int tipo; } PedidoLote;
static McFaixa *lote; static int nLote, lotePronto, loteAcao;
static void *fioLote(void *u) {
  PedidoLote p = *(PedidoLote *)u;
  McFaixa *v = calloc(PAG_MAX, sizeof *v);
  int n = 0;
  free(u);
  if (v) { if (p.tipo == L_ALBUM) mccat_album(p.ref, NULL, v, PAG_MAX, &n); else mccat_playlist(p.ref, v, PAG_MAX, &n); }
  pthread_mutex_lock(&trava);
  free(lote);
  lote = v; nLote = n; lotePronto = 1; loteAcao = p.acao;
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void pedirLote(int ac) {
  pthread_t t;
  PedidoLote *p = calloc(1, sizeof *p);
  if (!p) return;
  p->acao = ac; p->tipo = linhaAcao.tipo;
  snprintf(p->ref, sizeof p->ref, "%s", linhaAcao.ref);
  if (pthread_create(&t, NULL, fioLote, p) != 0) { free(p); return; }
  pthread_detach(t);
}
typedef struct { McFaixa f; int liga; int baixar; } PedidoFav;
static int favResultado = -99, offResultado = -99;
static void *fioFav(void *u) {
  PedidoFav p = *(PedidoFav *)u;
  int r;
  free(u);
  if (p.baixar) { r = mco_baixar(&p.f); pthread_mutex_lock(&trava); offResultado = r; pthread_mutex_unlock(&trava); }
  else { r = mccat_favoritar(&p.f, p.liga); pthread_mutex_lock(&trava); favResultado = r == MC_OK ? p.liga : -1; pthread_mutex_unlock(&trava); }
  return NULL;
}
static void emFio(const McFaixa *f, int liga, int baixar) {
  pthread_t t;
  if (!baixar && !strncmp(f->id, "dv", 2)) {   // local favourite: here, not on a worker
    if (mcv_fav(f->id) != (liga != 0)) mcv_alternar(f);
    favResultado = liga;
    return;
  }
  PedidoFav *p = calloc(1, sizeof *p);
  if (!p) return;
  p->f = *f; p->liga = liga; p->baixar = baixar;
  if (pthread_create(&t, NULL, fioFav, p) != 0) { free(p); return; }
  pthread_detach(t);
}
static int tecladoCampo = -1;
enum { TC_BUSCA = 1, TC_LFM_CHAVE, TC_LFM_SEGREDO, TC_NOVA_LISTA, TC_FILA_LISTA, TC_FORM, TC_SP_ID, TC_SP_SEGREDO, TC_SP_URL, TC_YT_BUSCA };
static void executarAcao(int a) {
  Pagina *pg = topo();
  zona = Z_LISTA;
  switch (a) {
    case AC_TOCAR:
      if (linhaAcao.tipo == L_FAIXA && pg) tocarLista(pg, linhaAcaoIdx, 0);
      else pedirLote(AC_TOCAR);
      break;
    case AC_A_SEGUIR: case AC_FILA:
      if (linhaAcao.tipo == L_FAIXA) {
        if (a == AC_A_SEGUIR) mcq_tocar_a_seguir(&linhaAcao.f); else mcq_adicionar(&linhaAcao.f);
        mcq_salvar();
        ilha_avisar("mcfila", ILHA_INFO, NULL, i18n(a == AC_A_SEGUIR ? "Toca a seguir." : "Adicionada à fila."), 2500u, 0);
      } else pedirLote(a);
      break;
    case AC_FAV: {
      int liga = !(linhaAcao.f.fav || mcv_fav(linhaAcao.f.id));
      emFio(&linhaAcao.f, liga, 0);
      if (pg && linhaAcaoIdx < pg->n) pg->v[linhaAcaoIdx].f.fav = liga;
      break; }
    case AC_PLAYLIST:
      tecladoCampo = TC_NOVA_LISTA;
      teclado_contexto(i18n("Música · Playlist local"));
      teclado_abrir_com(i18n("Nome da playlist"), i18n("Uma nova, ou o nome de uma que já existe"), 60, NULL, NULL);
      teclado_aceitar_utf8(1);
      break;
    case AC_ALBUM: empilhar(P_ALBUM, linhaAcao.f.albumId, linhaAcao.f.album); break;
    case AC_ARTISTA: empilhar(P_ARTISTA, linhaAcao.f.artistaId, linhaAcao.f.artista); break;
    case AC_BAIXAR:
      emFio(&linhaAcao.f, 0, 1);
      ilha_avisar("mcoff", ILHA_INFO, NULL, i18n("Baixando o arquivo original…"), 3000u, 0);
      break;
    case AC_APAGAR_OFF:
      mco_apagar(linhaAcao.f.id);
      if (pg && pg->tipo == P_OFFLINE) construirLocal(pg);
      break;
    case AC_FILA_REMOVER: case AC_FILA_CIMA: case AC_FILA_BAIXO: {
      int i = 0;
      for (int k = 0; pg && k < linhaAcaoIdx; k++) i += pg->v[k].tipo == L_FAIXA;
      if (a == AC_FILA_REMOVER && !mcq_remover(i)) ilha_avisar("mcfila", ILHA_INFO, NULL, i18n("A faixa que está tocando não sai da fila."), 3000u, 0);
      if (a == AC_FILA_CIMA) mcq_mover(i, i - 1);
      if (a == AC_FILA_BAIXO) mcq_mover(i, i + 1);
      mcq_salvar();
      if (pg) { int f = pg->foco; construirLocal(pg); pg->foco = f + (a == AC_FILA_CIMA ? -1 : a == AC_FILA_BAIXO ? 1 : 0); ajustarFoco(pg, 1); }
      break; }
    case AC_LISTA_REMOVER: {
      int i = 0, li = atoi(pg->arg + 3);
      for (int k = 0; k < linhaAcaoIdx; k++) i += pg->v[k].tipo == L_FAIXA;
      mcl_remover(li, i);
      pedir(pg, 0);
      break; }
    case AC_LOCAL_REMOVER:
      mcfontes_remover(linhaAcao.ref);
      if (pg) construirLocal(pg);
      break;
    case AC_LOCAL_EDITAR: {
      extern void mcui_form_abrir(int tipo, const char *chave);
      McFonte f;
      if (mcfontes_achar(linhaAcao.ref, &f)) mcui_form_abrir(f.tipo, f.chave);
      break; }
  }
}

typedef struct { char nome[100]; McFaixa *v; int n; } PedidoSalvar;
static int salvarResultado = -99;
static void *fioSalvar(void *u) {
  PedidoSalvar *p = u;
  int srv = 0, r = mccat_salvar_playlist(p->nome, p->v, p->n, &srv);
  pthread_mutex_lock(&trava);
  salvarResultado = r == MC_OK ? srv : -1;
  pthread_mutex_unlock(&trava);
  free(p->v);
  free(p);
  return NULL;
}
void mcui_salvar_fila(const char *nome) {
  pthread_t t;
  PedidoSalvar *p = calloc(1, sizeof *p);
  if (!p || !mcq_n() || !(p->v = malloc(sizeof *p->v * (size_t)mcq_n()))) { if (p) free(p->v); free(p); return; }
  snprintf(p->nome, sizeof p->nome, "%s", nome);
  for (int i = 0; i < mcq_n(); i++) p->v[i] = *mcq_item(i);
  p->n = mcq_n();
  if (pthread_create(&t, NULL, fioSalvar, p) != 0) { free(p->v); free(p); return; }
  pthread_detach(t);
}

typedef struct { char url[200]; } PedidoSp;
static McFaixa *spAchadas; static int spN, spTotal, spPronto, spErro, spFio;
static char spNome[160];
static void *fioSpotify(void *u) {
  PedidoSp p = *(PedidoSp *)u;
  McFaixa *v = calloc(500, sizeof *v);
  int a = 0, t = 0, r = MC_ERRO_OUTRO;
  char nome[160] = "";
  free(u);
  if (v) r = mcsp_importar(p.url, v, 500, &a, &t, nome, sizeof nome);
  pthread_mutex_lock(&trava);
  free(spAchadas);
  spAchadas = v; spN = a; spTotal = t; spErro = r; spPronto = 1; spFio = 0;
  snprintf(spNome, sizeof spNome, "%s", nome);
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void importarSpotify(const char *url) {
  pthread_t t;
  PedidoSp *p;
  if (spFio || !(p = calloc(1, sizeof *p))) return;
  snprintf(p->url, sizeof p->url, "%s", url);
  spFio = 1;
  if (pthread_create(&t, NULL, fioSpotify, p) != 0) { free(p); spFio = 0; return; }
  pthread_detach(t);
  ilha_avisar("mcsp", ILHA_INFO, NULL, i18n("Lendo a playlist do Spotify e procurando na sua biblioteca…"), 4000u, 0);
}

// ---------------------------------------------------------------- source form
enum { FC_NOME, FC_URL, FC_USUARIO, FC_SENHA, FC_TESTAR, FC_SALVAR, FC_N };
static McFonte form;
static int formFoco, formTestando, formResultado = -99;
static char formDesc[160], formChaveAntiga[16];
void mcui_form_abrir(int tipo, const char *chave) {
  memset(&form, 0, sizeof form);
  form.tipo = tipo;
  formChaveAntiga[0] = 0;
  if (chave && mcfontes_achar(chave, &form)) snprintf(formChaveAntiga, sizeof formChaveAntiga, "%s", chave);
  formFoco = FC_URL; formDesc[0] = 0; formResultado = -99;
  zona = Z_FORM;
}
static char *formCampo(int c, size_t *n) {
  int ss = form.tipo == MCS_SUBSONIC;
  switch (c) {
    case FC_NOME: *n = sizeof form.nome; return form.nome;
    case FC_URL: *n = ss ? sizeof form.ss.url : sizeof form.dv.url; return ss ? form.ss.url : form.dv.url;
    case FC_USUARIO: *n = ss ? sizeof form.ss.usuario : sizeof form.dv.usuario; return ss ? form.ss.usuario : form.dv.usuario;
    case FC_SENHA: *n = ss ? sizeof form.ss.senha : sizeof form.dv.senha; return ss ? form.ss.senha : form.dv.senha;
  }
  *n = 0;
  return NULL;
}
static void *fioTestar(void *u) {
  McFonte f = *(McFonte *)u;
  char d[160];
  int ss = f.tipo == MCS_SUBSONIC, r;
  free(u);
  r = mccat_testar(f.tipo, ss ? f.ss.url : f.dv.url, ss ? f.ss.usuario : f.dv.usuario, ss ? f.ss.senha : f.dv.senha, d, sizeof d);
  pthread_mutex_lock(&trava);
  formResultado = r; snprintf(formDesc, sizeof formDesc, "%s", d); formTestando = 0;
  pthread_mutex_unlock(&trava);
  memset(&f, 0, sizeof f);
  return NULL;
}
static int formSalvarDepois;
static void formTestar(int salvar) {
  pthread_t t;
  McFonte *c = malloc(sizeof *c);
  if (!c || formTestando) { free(c); return; }
  *c = form;
  formTestando = 1; formResultado = -99; formSalvarDepois = salvar;
  if (pthread_create(&t, NULL, fioTestar, c) != 0) { free(c); formTestando = 0; return; }
  pthread_detach(t);
}

// ---------------------------------------------------------------- Last.fm sign-in
static int lfmEstado;          // 0 idle, 1 starting, 2 waiting for approval
static char lfmUrl[300];
static GLuint texQr; static char qrDe[300];
static Uint32 lfmProximo, lfmDesde;
static int lfmFio, lfmResultado = -99;
static void *fioLfm(void *u) {
  int r;
  if (u) { char url[300]; r = mclfm_entrar_iniciar(url, sizeof url); pthread_mutex_lock(&trava); if (r == MC_OK) snprintf(lfmUrl, sizeof lfmUrl, "%s", url); lfmResultado = r == MC_OK ? 10 : -1; }
  else { r = mclfm_entrar_conferir(); pthread_mutex_lock(&trava); lfmResultado = r; }
  lfmFio = 0;
  pthread_mutex_unlock(&trava);
  return NULL;
}
static void lfmDisparar(int iniciar) {
  pthread_t t;
  if (lfmFio) return;
  lfmFio = 1;
  if (pthread_create(&t, NULL, fioLfm, iniciar ? (void *)1 : NULL) != 0) lfmFio = 0; else pthread_detach(t);
}

// ---------------------------------------------------------------- public
void mcui_abrir(void) {
  sair = 0;
  mcfontes_carregar();
  mclfm_carregar();
  if (!abertoUmaVez) {
    abertoUmaVez = 1;
    if (!mcq_n()) mcq_carregar();
    mcp_definir_saida(mcsaida_padrao());
    mcp_definir_ganchos(mccat_ganchos());
    {
      // Phase 8: music preferences are per profile; the pre-phase-8 file
      // (one for the whole TV) is read only when this profile has none yet.
      char nome[40];
      snprintf(nome, sizeof nome, "music-settings-p%d.txt", perfis_ativo() > 0 ? perfis_ativo() : 1);
      char *t = dados_ler(nome);
      if (!t) t = dados_ler("music-settings.txt");
      festaLig = t && strstr(t, "festa=1");
      mcp_festa(festaLig);
      free(t);
    }
    abrirSecao(mcfontes_n() ? P_INICIO : P_SERVICOS);
    zona = Z_MENU;
  } else if (topo() && ehLocal(topo()->tipo)) construirLocal(topo());
}
void mcui_perfil_trocou(void) {
  mcp_descartar();
  mcfontes_carregar();
  mclfm_carregar();
  mcfila_perfil_trocou();
  for (int i = 0; i < nPilha; i++) pilha[i].gen = ++geracao;
  nPilha = 0;
  abertoUmaVez = 0;
}
int mcui_quer_sair(void) { int v = sair; sair = 0; return v; }
int mcui_teste_secao(void) { return secao; }
int mcui_teste_n(void) { Pagina *pg = topo(); return pg ? pg->n : 0; }
int mcui_teste_zona(void) { return zona; }
void mcui_fundo(Uint32 agora) { if (abertoUmaVez) mcp_atualizar(agora); }

static void ativar(Pagina *pg, int i) {
  Linha *l = &pg->v[i];
  switch (l->tipo) {
    case L_FAIXA: tocarLista(pg, i, 0); break;
    case L_ALBUM: empilhar(P_ALBUM, l->ref, l->t1); break;
    case L_ARTISTA: empilhar(P_ARTISTA, l->ref, l->t1); break;
    case L_PLAYLIST: empilhar(P_PLAYLIST, l->ref, l->t1); break;
    case L_GENERO: empilhar(P_GENERO, l->ref, l->t1); break;
    case L_PASTA: empilhar(P_PASTA, l->ref, l->t1); break;
    case L_YTLISTA: empilhar(P_YTM_LISTA, l->ref, l->t1); break;
    case L_YTVIDEO: {
      int e = ondever_youtube_lista(l->ref, l->lista[0] ? l->lista : NULL);
      ilha_avisar("mcyt", ILHA_INFO, NULL, i18n(e == ONDE_INFO ? "O YouTube abre no app da própria TV, que não está disponível neste aparelho." :
                                                "Abrindo no app oficial do YouTube…"), 4000u, 0);
      break; }
    case L_FONTE: {
      McFonte f;
      if (mcfontes_achar(l->ref, &f)) {
        if (f.tipo == MCS_WEBDAV) { char a[MC_ID]; snprintf(a, sizeof a, "%s:", f.chave); empilhar(P_PASTA, a, f.nome); }
        else abrirSecao(P_ALBUNS);
      }
      break; }
    case L_ACAO:
      switch (l->acao) {
        case A_TOCAR_TUDO: tocarLista(pg, 0, 0); break;
        case A_ALEATORIO_TUDO: tocarLista(pg, 0, 1); break;
        case A_RETOMAR: mcp_retomar(); zonaAntesTocando = zona; zona = Z_TOCANDO; break;
        case A_ADD_SUBSONIC: mcui_form_abrir(MCS_SUBSONIC, NULL); break;
        case A_ADD_WEBDAV: mcui_form_abrir(MCS_WEBDAV, NULL); break;
        case A_LFM_CHAVE: case A_LFM_SEGREDO:
          tecladoCampo = l->acao == A_LFM_CHAVE ? TC_LFM_CHAVE : TC_LFM_SEGREDO;
          teclado_contexto("Last.fm");
          teclado_abrir_com(l->acao == A_LFM_CHAVE ? "Chave da API do Last.fm" : "Segredo compartilhado do Last.fm",
                            "last.fm/api/account/create · use \"Digitar pelo celular\"", 40, "abcdefghijklmnopqrstuvwxyz0123456789", NULL);
          teclado_permitir_vazio(1);
          if (l->acao == A_LFM_SEGREDO) teclado_mascarar(1);
          break;
        case A_LFM_CONECTAR:
          if (!mclfm_tem_chave()) { ilha_avisar("mclfm", ILHA_INFO, NULL, i18n("Preencha a chave e o segredo do Last.fm antes."), 4000u, 0); break; }
          lfmEstado = 1; lfmUrl[0] = 0; lfmDesde = SDL_GetTicks(); lfmResultado = -99;
          lfmDisparar(1);
          break;
        case A_LFM_SAIR: mclfm_sair(); construirLocal(pg); break;
        case A_SP_ID: case A_SP_SEGREDO: case A_SP_IMPORTAR:
          tecladoCampo = l->acao == A_SP_ID ? TC_SP_ID : l->acao == A_SP_SEGREDO ? TC_SP_SEGREDO : TC_SP_URL;
          if (tecladoCampo == TC_SP_URL && !mcsp_tem_chave()) {
            tecladoCampo = -1;
            ilha_avisar("mcsp", ILHA_INFO, NULL, i18n("Preencha o Client ID e o Client secret do Spotify antes."), 4000u, 0);
            break;
          }
          teclado_contexto("Spotify");
          if (tecladoCampo == TC_SP_URL)
            teclado_abrir_com(i18n("Link da playlist do Spotify"), i18n("Compartilhar > Copiar link · use \"Digitar pelo celular\""), 120,
                              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-_?=&", NULL);
          else {
            teclado_abrir_com(l->acao == A_SP_ID ? "Spotify: Client ID" : "Spotify: Client secret",
                              "developer.spotify.com/dashboard · use \"Digitar pelo celular\"", 40, "abcdefghijklmnopqrstuvwxyz0123456789", NULL);
            teclado_permitir_vazio(1);
            if (l->acao == A_SP_SEGREDO) teclado_mascarar(1);
          }
          break;
        case A_YTM_ABRIR: empilhar(P_YTM, "", "YouTube Music"); break;
        case A_YT_BUSCA:
          tecladoCampo = TC_YT_BUSCA;
          teclado_contexto("YouTube Music");
          teclado_abrir_com(i18n("Buscar música no YouTube"), i18n("Artista, música ou álbum"), 100, NULL, NULL);
          teclado_aceitar_utf8(1);
          break;
        case A_YT_LISTA_TOCAR:
          for (int k = 0; k < pg->n; k++) if (pg->v[k].tipo == L_YTVIDEO) { ativar(pg, k); break; }
          break;
        case A_YTMUSIC: { extern void app_ir_youtube(void) __attribute__((weak)); if (app_ir_youtube) app_ir_youtube(); break; }
        case A_FESTA: festaLig = !festaLig; mcp_festa(festaLig); { char nome[40]; snprintf(nome, sizeof nome, "music-settings-p%d.txt", perfis_ativo() > 0 ? perfis_ativo() : 1);
          dados_gravar(nome, festaLig ? "festa=1\n" : "festa=0\n"); } construirLocal(pg); break;
        case A_LRCLIB: lrclibLig = !lrclibLig; construirLocal(pg); break;
        case A_FILA_ALEATORIO: mcq_aleatorio(!mcq_eh_aleatorio()); mcq_salvar(); construirLocal(pg); break;
        case A_FILA_REPETIR: mcq_repetir((mcq_repeticao() + 1) % 3); mcq_salvar(); construirLocal(pg); break;
        case A_FILA_LIMPAR: mcp_parar(); mcq_limpar(); mcq_salvar(); construirLocal(pg); break;
        case A_FILA_SALVAR:
          tecladoCampo = TC_FILA_LISTA;
          teclado_contexto(i18n("Música · Salvar a fila"));
          teclado_abrir_com(i18n("Nome da playlist"), NULL, 60, NULL, NULL);
          teclado_aceitar_utf8(1);
          break;
        case A_NOVA_BUSCA:
          tecladoCampo = TC_BUSCA;
          teclado_contexto(i18n("Música"));
          teclado_abrir_com(i18n("Buscar música"), i18n("Artista, álbum ou faixa"), 60, NULL, pg->arg);
          teclado_aceitar_utf8(1);
          break;
        case A_SIMILAR: {
          Pagina *b;
          abrirSecao(P_BUSCAR);
          b = topo();
          snprintf(b->arg, sizeof b->arg, "%s", l->t1);
          b->n = 0;
          b->gen = ++geracao;
          acao(b, A_NOVA_BUSCA, i18n("Nova busca"), NULL);
          pedir(b, 1);
          break; }
      }
      break;
  }
}

// ---------------------------------------------------------------- Now Playing state
static McLinha letra[MCLRC_MAX]; static int nLetra, letraSinc, verLetra, focoTocando = 2, focoBarra;
static char letraDe[MC_ID];
static int letraPronta;
static char letraNova[8000];
static void *fioLetra(void *u) {
  McFaixa f = *(McFaixa *)u;
  char *t = malloc(8000);
  free(u);
  if (!t) return NULL;
  t[0] = 0;
  if (mccat_letra(&f, t, 8000) != MC_OK || (!lrclibLig && !t[0])) t[0] = 0;
  pthread_mutex_lock(&trava);
  snprintf(letraNova, sizeof letraNova, "%s", t);
  letraPronta = 1;
  pthread_mutex_unlock(&trava);
  free(t);
  return NULL;
}
static void pedirLetra(const McFaixa *f) {
  pthread_t t;
  McFaixa *c = malloc(sizeof *c);
  if (!c) return;
  *c = *f;
  snprintf(letraDe, sizeof letraDe, "%s", f->id);
  nLetra = 0;
  if (pthread_create(&t, NULL, fioLetra, c) != 0) { free(c); return; }
  pthread_detach(t);
}
enum { TB_ALEAT, TB_ANT, TB_PLAY, TB_PROX, TB_REPETIR, TB_FAV, TB_LETRA, TB_FILA, TB_TELA, TB_N };

// SCREEN OFF while the music plays (webOS stops an app's sound when the app
// is closed, so the music keeps going with the app open and the screen off).
// 1 = the TV turned its panel off (LG tvpower service); 2 = the TV refused,
// so the app draws pure black (OLED pixels off). Any key turns it back on and
// does nothing else.
#if !defined(__APPLE__) && !defined(NV_LINUX_DESKTOP) && !defined(NV_TPK) && !defined(NV_ANDROID) && !defined(__EMSCRIPTEN__)
#define MC_WEBOS 1
#endif
static volatile int telaOff;
static Uint32 telaToque;
#ifdef MC_WEBOS
static void respTela(const char *payload, void *u) {
  (void)u;
  if (telaOff && payload && strstr(payload, "\"returnValue\":true")) telaOff = 1;
  printf("[musica] tvpower turnOffScreen: %s\n", payload && strstr(payload, "\"returnValue\":true") ? "aceito" : "recusado (tela preta do app)");
  fflush(stdout);
}
#endif
static void desligarTela(void) {
  telaOff = 2;
  telaToque = SDL_GetTicks();
  printf("[musica] tela desligada; a musica continua\n");
  fflush(stdout);
#ifdef MC_WEBOS
  video_luna("luna://com.webos.service.tvpower/power/turnOffScreen", "{}", respTela, NULL);
#endif
}
static void ligarTela(void) {
#ifdef MC_WEBOS
  if (telaOff == 1) video_luna("luna://com.webos.service.tvpower/power/turnOnScreen", "{}", NULL, NULL);
#endif
  telaOff = 0;
  printf("[musica] tela ligada\n");
  fflush(stdout);
}

void mcui_evento(const SDL_Event *e) {
  SDL_Keycode k;
  Pagina *pg = topo();
  int voltar, ok;
  if (teclado_aberto()) { teclado_evento(e); return; }
  if (e->type != SDL_KEYDOWN) return;
  if (telaOff) { ligarTela(); return; }   // the key only wakes the screen
  k = e->key.keysym.sym;
  voltar = k == SDLK_AC_BACK || k == SDLK_ESCAPE || k == SDLK_BACKSPACE;
  ok = k == SDLK_RETURN || k == SDLK_KP_ENTER;
  // Media keys anywhere in the Music Center.
  if (k == SDLK_PAUSE || k == SDLK_AUDIOPLAY) { mcp_alternar_pausa(); return; }
  if (k == SDLK_AUDIOSTOP) { mcp_parar(); return; }
  if (k == SDLK_AUDIONEXT) { mcp_proxima(); return; }
  if (k == SDLK_AUDIOPREV) { mcp_anterior(); return; }
  if (lfmEstado) { if (voltar) lfmEstado = 0; return; }
  if (zona == Z_ACOES) {
    if (voltar || k == SDLK_LEFT) { zona = Z_LISTA; return; }
    if (k == SDLK_UP && focoAcao > 0) focoAcao--;
    if (k == SDLK_DOWN && focoAcao + 1 < nAcoes) focoAcao++;
    if (ok) executarAcao(acoes[focoAcao]);
    return;
  }
  if (zona == Z_FORM) {
    if (voltar) { zona = Z_LISTA; if (pg) construirLocal(pg); return; }
    if (k == SDLK_UP && formFoco > 0) formFoco--;
    if (k == SDLK_DOWN && formFoco + 1 < FC_N) formFoco++;
    if (ok) {
      if (formFoco <= FC_SENHA) {
        static const char URLA[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:/.-_~?=&%@+#[]";
        static const char TUDO[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
        size_t n;
        char *c = formCampo(formFoco, &n);
        static const char *const ROT[] = { "Nome (opcional)", "Endereço do servidor", "Usuário", "Senha" };
        tecladoCampo = TC_FORM;
        teclado_contexto(form.tipo == MCS_SUBSONIC ? "Navidrome / Subsonic" : "WebDAV");
        teclado_abrir_com(ROT[formFoco], formFoco == FC_URL ? (form.tipo == MCS_SUBSONIC ? "https://music.example.com" : "https://cloud.example.com/remote.php/dav/files/me/Music/") : NULL,
                          (int)n - 1, formFoco == FC_URL ? URLA : formFoco == FC_NOME ? NULL : TUDO, c);
        teclado_permitir_vazio(1);
        if (formFoco == FC_NOME) teclado_aceitar_utf8(1);
        if (formFoco == FC_SENHA) teclado_mascarar(1);
      } else formTestar(formFoco == FC_SALVAR);
    }
    return;
  }
  if (zona == Z_TOCANDO) {
    if (voltar) { zona = zonaAntesTocando == Z_TOCANDO ? Z_LISTA : zonaAntesTocando; verLetra = 0; return; }
    if (focoBarra) {
      if (k == SDLK_LEFT) mcp_buscar(mcp_pos() - 10);
      else if (k == SDLK_RIGHT) mcp_buscar(mcp_pos() + 10);
      else if (k == SDLK_DOWN) focoBarra = 0;
      else if (ok) mcp_alternar_pausa();
      return;
    }
    if (k == SDLK_LEFT && focoTocando > 0) focoTocando--;
    else if (k == SDLK_RIGHT && focoTocando + 1 < TB_N) focoTocando++;
    else if (k == SDLK_UP) focoBarra = 1;
    else if (ok) {
      const McFaixa *f = mcq_faixa_atual();
      switch (focoTocando) {
        case TB_ALEAT: mcq_aleatorio(!mcq_eh_aleatorio()); mcq_salvar(); break;
        case TB_ANT: mcp_anterior(); break;
        case TB_PLAY: mcp_alternar_pausa(); break;
        case TB_PROX: mcp_proxima(); break;
        case TB_REPETIR: mcq_repetir((mcq_repeticao() + 1) % 3); mcq_salvar(); break;
        case TB_FAV: if (f) { int liga = !(f->fav || mcv_fav(f->id)); emFio(f, liga, 0); ((McFaixa *)f)->fav = liga; } break;
        case TB_LETRA: verLetra = !verLetra; if (verLetra && f && strcmp(letraDe, f->id)) pedirLetra(f); break;
        case TB_FILA: abrirSecao(P_FILA); zona = Z_LISTA; break;
        case TB_TELA: desligarTela(); break;
      }
    }
    return;
  }
  if (zona == Z_MENU) {
    if (voltar) { sair = 1; return; }
    if (k == SDLK_UP && secao > 0) abrirSecao(secao - 1), zona = Z_MENU;
    else if (k == SDLK_DOWN && secao + 1 < P_SECOES) abrirSecao(secao + 1), zona = Z_MENU;
    else if ((k == SDLK_RIGHT || ok) && pg) {
      zona = Z_LISTA;
      if (pg->tipo == P_BUSCAR && !pg->arg[0] && ok) ativar(pg, 0);
    } else if (k == SDLK_DOWN && mcp_estado() != MCP_PARADO) { zonaAntesTocando = Z_MENU; zona = Z_TOCANDO; }
    return;
  }
  // list
  if (!pg) { zona = Z_MENU; return; }
  if (voltar) {
    if (nPilha > 1) { nPilha--; pilha[nPilha].gen = ++geracao; if (ehLocal(topo()->tipo)) construirLocal(topo()); return; }
    zona = Z_MENU;
    return;
  }
  if (k == SDLK_LEFT) { zona = Z_MENU; return; }
  if (k == SDLK_UP) { int f = pg->foco; pg->foco--; ajustarFoco(pg, -1); if (pg->foco == f && f <= 0) zona = Z_MENU; return; }
  if (k == SDLK_DOWN) {
    if (pg->foco + 1 < pg->n) { pg->foco++; ajustarFoco(pg, 1); }
    else if (mcp_estado() != MCP_PARADO) { zonaAntesTocando = Z_LISTA; zona = Z_TOCANDO; }
    return;
  }
  if (k == SDLK_RIGHT && pg->n) { abrirAcoes(&pg->v[pg->foco], pg->foco); return; }
  if (ok) {
    if (pg->n) ativar(pg, pg->foco);
    else if (!pg->carregando && pg->erro) pedir(pg, 0);   // retry after an error
  }
}

static const char *mensagem(int e) {
  switch (e) {
    case MC_ERRO_REDE: return "Sem conexão com o servidor de música. Pressione OK para tentar de novo.";
    case MC_ERRO_LOGIN: return "O servidor recusou o usuário ou a senha. Confira em Serviços de música.";
    case MC_ERRO_NAO_SUPORTA: return "Nenhum servidor de música configurado para isto. Adicione um em Serviços de música.";
    case MC_ERRO_SERVIDOR: return "O servidor de música respondeu com um erro. Pressione OK para tentar de novo.";
    default: return "Algo deu errado. Pressione OK para tentar de novo.";
  }
}

void mcui_atualizar(float dt, Uint32 agora) {
  Pagina *pg;
  aplicar();
  // Screen off: keep the app's own screensaver from drawing (posters, a clock)
  // over the black, by telling it someone is here every 10 s.
  if (telaOff && agora - telaToque > 10000u) { telaToque = agora; esmaecer_entrada(agora, 0); }
  if (tecladoCampo >= 0) {
    int r;
    if (teclado_aberto()) teclado_atualizar(dt, agora);
    r = teclado_resultado();
    if (r == TECLADO_PRONTO) {
      const char *t = teclado_texto();
      pg = topo();
      switch (tecladoCampo) {
        case TC_BUSCA:
          if (pg && t[0]) { snprintf(pg->arg, sizeof pg->arg, "%s", t); pg->n = 0; pg->gen = ++geracao; acao(pg, A_NOVA_BUSCA, i18n("Nova busca"), NULL); pedir(pg, 1); }
          break;
        case TC_LFM_CHAVE: case TC_LFM_SEGREDO: {
          int okk = tecladoCampo == TC_LFM_CHAVE ? mclfm_definir_chave(t, NULL) : mclfm_definir_chave(NULL, t);
          teclado_esquecer();
          ilha_avisar("mclfm", ILHA_INFO, NULL, i18n(okk ? "Salvo neste aparelho." : "Valor inválido: use só letras e números."), 3000u, 0);
          if (pg) construirLocal(pg);
          break; }
        case TC_NOVA_LISTA: case TC_FILA_LISTA: {
          int li = -1;
          if (!t[0]) break;
          for (int i = 0; i < mcl_n(); i++) if (!strcmp(mcl_nome(i), t)) li = i;
          if (tecladoCampo == TC_NOVA_LISTA) {
            if (li < 0) li = mcl_criar(t);
            if (li >= 0 && mcl_adicionar(li, &linhaAcao.f)) ilha_avisar("mclista", ILHA_INFO, NULL, i18n("Adicionada à playlist."), 2500u, 0);
          } else {
            // On the server when the whole queue is from one server: network, so a worker.
            extern void mcui_salvar_fila(const char *nome);
            mcui_salvar_fila(t);
          }
          break; }
        case TC_SP_ID: case TC_SP_SEGREDO: {
          int okk = tecladoCampo == TC_SP_ID ? mcsp_definir_chave(t, NULL) : mcsp_definir_chave(NULL, t);
          teclado_esquecer();
          ilha_avisar("mcsp", ILHA_INFO, NULL, i18n(okk ? "Salvo neste aparelho." : "Valor inválido: use só letras e números."), 3000u, 0);
          if (pg) construirLocal(pg);
          break; }
        case TC_YT_BUSCA:
          if (t[0]) empilhar(P_YTM_BUSCA, t, t);
          break;
        case TC_SP_URL: {
          char id[64];
          if (!mcsp_id_playlist(t, id, sizeof id)) { ilha_avisar("mcsp", ILHA_INFO, NULL, i18n("Isso não parece um link de playlist do Spotify."), 4000u, 0); break; }
          importarSpotify(t);
          break; }
        case TC_FORM: {
          size_t n;
          char *c = formCampo(formFoco, &n);
          if (c) snprintf(c, n, "%s", t);
          teclado_esquecer();
          if (formFoco < FC_SENHA) formFoco++;
          break; }
      }
    }
    if (r != TECLADO_NADA) tecladoCampo = -1;
  }
  // Worker results: favourites, offline, album/playlist batches, the form test, lyrics, Last.fm.
  {
    int fr, orr, lp = 0, la = 0, nl = 0, ft = -99, lr = -99;
    McFaixa *lv = NULL;
    pthread_mutex_lock(&trava);
    fr = favResultado; favResultado = -99;
    orr = offResultado; offResultado = -99;
    if (lotePronto) { lp = 1; lv = lote; nl = nLote; la = loteAcao; lote = NULL; lotePronto = 0; }
    if (!formTestando && formResultado != -99) { ft = formResultado; formResultado = -99; }
    if (lfmResultado != -99) { lr = lfmResultado; lfmResultado = -99; }
    if (spPronto) {
      McFaixa *v = spAchadas;
      int a = spN, tot = spTotal, er = spErro;
      char nome[160], msg[300];
      snprintf(nome, sizeof nome, "%s", spNome);
      spAchadas = NULL; spPronto = 0;
      pthread_mutex_unlock(&trava);
      if (er != MC_OK) snprintf(msg, sizeof msg, "%s", i18n(er == MC_ERRO_LOGIN ? "O Spotify recusou o Client ID ou o secret." :
                                                       er == MC_ERRO_NAO_SUPORTA ? "Playlist não encontrada ou não pública (o Spotify não libera as playlists dele para apps novos)." :
                                                       "Não foi possível ler a playlist do Spotify."));
      else {
        if (a) {
          char t2[200];
          int li;
          snprintf(t2, sizeof t2, "%.150s (Spotify)", nome[0] ? nome : "Playlist");
          li = mcl_criar(t2);
          for (int i = 0; li >= 0 && i < a; i++) mcl_adicionar(li, &v[i]);
        }
        snprintf(msg, sizeof msg, i18n("%d de %d faixas encontradas na sua biblioteca; salvas em Playlists."), a, tot);
      }
      free(v);
      ilha_avisar("mcsp", ILHA_INFO, NULL, msg, 6000u, 0);
      pthread_mutex_lock(&trava);
    }
    if (salvarResultado != -99) {
      int sr = salvarResultado;
      salvarResultado = -99;
      pthread_mutex_unlock(&trava);
      ilha_avisar("mclista", ILHA_INFO, NULL, i18n(sr < 0 ? "Não foi possível salvar a playlist." : sr ? "Playlist salva no servidor." : "Playlist salva neste aparelho."), 3000u, 0);
      pthread_mutex_lock(&trava);
    }
    if (letraPronta) { letraPronta = 0; nLetra = mclrc_parse(letraNova, letra, MCLRC_MAX, &letraSinc); }
    pthread_mutex_unlock(&trava);
    if (fr != -99) ilha_avisar("mcfav", ILHA_INFO, NULL, i18n(fr < 0 ? "Não foi possível mudar o favorito." : fr ? "Nos favoritos." : "Fora dos favoritos."), 2500u, 0);
    if (orr != -99)
      ilha_avisar("mcoff", ILHA_INFO, NULL, i18n(orr == MCOF_OK ? "Salva para ouvir offline." : orr == MCOF_SEM_PERMISSAO ? "O servidor não permite baixar para este usuário." :
                                                orr == MCOF_SEM_ESPACO ? "Sem espaço para cópias offline (2 GB)." : "Não foi possível baixar a faixa."), 4000u, 0);
    if (lp) {
      if (nl) {
        if (la == AC_TOCAR) { mcq_aleatorio(0); mcq_definir(lv, nl, 0); mcp_tocar(0); mcq_salvar(); zonaAntesTocando = Z_LISTA; zona = Z_TOCANDO; }
        else {
          if (la == AC_A_SEGUIR) for (int i = nl - 1; i >= 0; i--) mcq_tocar_a_seguir(&lv[i]);
          else for (int i = 0; i < nl; i++) mcq_adicionar(&lv[i]);
          mcq_salvar();
          ilha_avisar("mcfila", ILHA_INFO, NULL, i18n(la == AC_A_SEGUIR ? "Toca a seguir." : "Adicionada à fila."), 2500u, 0);
        }
      }
      free(lv);
    }
    if (ft != -99) {
      if (ft == MC_OK && formSalvarDepois) {
        if (formChaveAntiga[0] && strcmp(formChaveAntiga, form.chave)) mcfontes_remover(formChaveAntiga);
        if (mcfontes_salvar(&form)) {
          ilha_avisar("mcform", ILHA_INFO, NULL, i18n("Servidor salvo."), 3000u, 0);
          zona = Z_LISTA;
          if (topo()) construirLocal(topo());
        }
      }
      if (ft != MC_OK) snprintf(formDesc, sizeof formDesc, "%s", i18n(mensagem(ft)));
      else if (!formSalvarDepois) { char d[160]; snprintf(d, sizeof d, "%s ✓ %s", i18n("Conectado:"), formDesc); snprintf(formDesc, sizeof formDesc, "%s", d); }
    }
    if (lr != -99) {
      if (lr == 10) { lfmEstado = 2; lfmProximo = agora + 4000; }
      else if (lr == 1) { lfmEstado = 0; ilha_avisar("mclfm", ILHA_INFO, NULL, i18n("Last.fm conectado a este perfil."), 3500u, 0); if (topo()) construirLocal(topo()); }
      else if (lr == -1) { lfmEstado = 0; ilha_avisar("mclfm", ILHA_INFO, NULL, i18n("O Last.fm não aprovou a conexão. Confira a chave e o segredo."), 5000u, 0); }
    }
  }
  if (lfmEstado == 2 && (int)(agora - lfmProximo) >= 0) {
    lfmProximo = agora + 4000;
    if (agora - lfmDesde > 10u * 60u * 1000u) lfmEstado = 0; else lfmDisparar(0);
  }
  // Lyrics follow the track.
  {
    const McFaixa *f = mcq_faixa_atual();
    if (verLetra && f && strcmp(letraDe, f->id)) pedirLetra(f);
  }
  pg = topo();
  if (pg && zona == Z_LISTA && pg->mais && !pg->carregando && pg->foco + 6 >= pg->n && pg->n < PAG_MAX) { pg->offset += LOTE; pedir(pg, 1); }
}

// ---------------------------------------------------------------- drawing
static void texto(TxtEstilo s, const char *t, float x, float y, float a) { TxtLinha l = txt_linha(s, t, 240, 242, 248, 255); txt_desenhar_alpha(l, x, y, a); }
static void corta(TxtEstilo s, const char *t, float x, float y, float w, float a) { TxtLinha l = txt_linha_corta(s, t, 240, 242, 248, 255, w); txt_desenhar_alpha(l, x, y, a); }
static void capa(const char *id, const char *c, float x, float y, float s) {
  char url[1200];
  GLuint tx = 0;
  if (c && c[0]) { mccat_capa(id, c, s > 200 ? 600 : 300, url, sizeof url); if (url[0]) tx = tex_obter_larg(url, s); }
  if (tx) gfx_textura((GfxRect){ x, y, s, s }, tx);
  else { gfx_cor((GfxRect){ x, y, s, s }, 0.12f, 1, 1, 1, 0.07f); texto(TXT_CALLOUT, "♪", x + s * 0.5f - 10, y + s * 0.5f - 16, 0.35f); }
}
static void barraMini(float y) {
  const McFaixa *f = mcq_faixa_atual();
  char b[300], p[16], d[16];
  float x = NV_MARGEM_X, w = NV_TELA_W - 2 * NV_MARGEM_X, pct;
  if (!f || mcp_estado() == MCP_PARADO) return;
  gfx_cor((GfxRect){ 0, y - 12, NV_TELA_W, NV_TELA_H - y + 12 }, 0, 0.09f, 0.095f, 0.11f, 0.97f);
  capa(f->id, f->capa, x, y, 64);
  durTexto((int)mcp_pos(), p, sizeof p);
  durTexto((int)mcp_dur(), d, sizeof d);
  snprintf(b, sizeof b, "%s%s — %s", mcp_estado() == MCP_PAUSADO ? i18n("(pausado) ") : mcp_estado() == MCP_CARREGANDO ? "… " : "♪ ", f->titulo, f->artista);
  corta(TXT_BODY, b, x + 84, y + 4, w - 360, 1);
  snprintf(b, sizeof b, "%s / %s", p[0] ? p : "0:00", d[0] ? d : "--:--");
  { TxtLinha l = txt_linha(TXT_CAPTION, b, 200, 202, 210, 255); txt_desenhar_alpha(l, x + w - (float)l.w, y + 10, 0.8f); }
  pct = mcp_dur() > 0 ? (float)(mcp_pos() / mcp_dur()) : 0;
  gfx_cor((GfxRect){ x + 84, y + 50, w - 84, 4 }, 0, 1, 1, 1, 0.15f);
  gfx_cor((GfxRect){ x + 84, y + 50, (w - 84) * (pct > 1 ? 1 : pct), 4 }, 0, 1, 1, 1, 0.85f);
}
static void desenharTocando(Uint32 agora) {
  const McFaixa *f = mcq_faixa_atual();
  float x = NV_MARGEM_X, y = NV_MARGEM_Y + 20, w = NV_TELA_W - 2 * NV_MARGEM_X, s = 480;
  char b[400], p[16], d[16];
  McSaidaInfo si;
  (void)agora;
  if (!f) { texto(TXT_BODY, i18n("Nada tocando."), x, y, 0.7f); return; }
  capa(f->id, f->capa, x, y, s);
  {
    float cx = x + s + 64, cw = w - s - 64, cy = y;
    if (verLetra) {
      int atual = letraSinc ? mclrc_linha_em(letra, nLetra, (int)(mcp_pos() * 1000)) : -1, de;
      corta(TXT_CALLOUT, f->titulo, cx, cy, cw, 0.8f);
      cy += 60;
      if (!nLetra) texto(TXT_BODY, i18n(strcmp(letraDe, f->id) ? "" : "Sem letra para esta faixa."), cx, cy, 0.6f);
      de = atual > 3 ? atual - 3 : 0;
      for (int i = de; i < nLetra && cy < y + s - 10; i++) {
        int foco = i == atual;
        TxtLinha l = txt_linha_corta(foco ? TXT_TITULO2 : TXT_BODY, letra[i].texto[0] ? letra[i].texto : "♪", 240, 242, 248, 255, cw);
        txt_desenhar_alpha(l, cx, cy, foco ? 1 : (letraSinc ? 0.45f : 0.85f));
        cy += foco ? 64 : 46;
      }
    } else {
      cy += txt_bloco_corta(TXT_TITULO1, f->titulo, 240, 242, 248, cx, cy, cw, 72, 1, 2) + 10;
      corta(TXT_TITULO2, f->artista, cx, cy, cw, 0.85f); cy += 60;
      corta(TXT_CALLOUT, f->album, cx, cy, cw, 0.65f); cy += 70;
      mcfmt_descrever(f, b, sizeof b);
      {
        char t[300];
        const char *selo = mcfmt_selo(mcfmt_classe(f));
        snprintf(t, sizeof t, "%s: %s%s%s", i18n("Arquivo"), b[0] ? b : i18n("formato não informado pela fonte"), selo[0] ? "  ·  " : "", selo);
        texto(TXT_CAPTION, t, cx, cy, 0.75f);
      }
      cy += 40;
      mcp_saida(&si);
      if (si.medido) snprintf(b, sizeof b, "%s: %s · %d Hz / %d-bit / %dch%s", i18n("Saída"), si.backend, si.taxa, si.bits, si.canais,
                              si.convertido ? i18n(" (convertido)") : "");
      else snprintf(b, sizeof b, "%s: %s%s%s", i18n("Saída"), si.backend[0] ? si.backend : mcp_backend(), si.codec[0] ? " · " : "", si.codec);
      texto(TXT_CAPTION, b, cx, cy, 0.75f);
      cy += 40;
      if (si.nota[0]) txt_bloco_corta(TXT_CAPTION, si.nota, 200, 202, 210, cx, cy, cw, 34, 0.55f, 3);
    }
  }
  y += s + 50;
  durTexto((int)mcp_pos(), p, sizeof p);
  durTexto((int)(mcp_dur() > 0 ? mcp_dur() : f->dur), d, sizeof d);
  {
    float pct = mcp_dur() > 0 ? (float)(mcp_pos() / mcp_dur()) : 0;
    gfx_cor((GfxRect){ x, y, w, focoBarra ? 10 : 6 }, 0, 1, 1, 1, 0.15f);
    gfx_cor((GfxRect){ x, y, w * (pct > 1 ? 1 : pct), focoBarra ? 10 : 6 }, 0, 1, 1, 1, focoBarra ? 1 : 0.8f);
    texto(TXT_CAPTION, p[0] ? p : "0:00", x, y + 20, 0.7f);
    { TxtLinha l = txt_linha(TXT_CAPTION, d[0] ? d : "--:--", 240, 242, 248, 255); txt_desenhar_alpha(l, x + w - (float)l.w, y + 20, 0.7f); }
    if (focoBarra) texto(TXT_CAPTION, i18n("Esquerda/direita: 10 s"), x + w * 0.5f - 120, y + 20, 0.7f);
  }
  y += 80;
  {
    static const char *const REP[] = { "Repetir", "Repetir todas", "Repetir uma" };
    const char *rot[TB_N];
    float bx = x;
    rot[TB_ALEAT] = mcq_eh_aleatorio() ? "Aleatório ✓" : "Aleatório";
    rot[TB_ANT] = "Anterior"; rot[TB_PLAY] = mcp_estado() == MCP_TOCANDO ? "Pausar" : "Tocar"; rot[TB_PROX] = "Próxima";
    rot[TB_REPETIR] = REP[mcq_repeticao()];
    rot[TB_FAV] = f->fav || mcv_fav(f->id) ? "♥" : "♡";
    rot[TB_LETRA] = verLetra ? "Letra da música ✓" : "Letra da música";
    rot[TB_FILA] = "Fila";
    rot[TB_TELA] = "Desligar a tela";
    for (int i = 0; i < TB_N; i++) {
      int foco = !focoBarra && i == focoTocando;
      TxtLinha l = txt_linha(TXT_CALLOUT, i18n(rot[i]), foco ? 20 : 240, foco ? 20 : 242, foco ? 24 : 248, 255);
      float bw = (float)l.w + 44;
      gfx_cor((GfxRect){ bx, y, bw, 60 }, 0.5f, 1, 1, 1, foco ? 0.92f : 0.1f);
      txt_desenhar_alpha(l, bx + 22, y + 14, 1);
      bx += bw + 14;
    }
  }
  if (mcp_estado() == MCP_ERRO) texto(TXT_CAPTION, i18n("Esta faixa não pôde ser tocada."), x, y + 80, 0.8f);
  else if (mcp_estado() == MCP_CARREGANDO) texto(TXT_CAPTION, i18n("Carregando…"), x, y + 80, 0.6f);
}
static void desenharForm(float x, float y, float w) {
  static const char *const ROT[] = { "Nome (opcional)", "Endereço do servidor", "Usuário", "Senha", "Testar conexão", "Testar e salvar" };
  texto(TXT_TITULO2, form.tipo == MCS_SUBSONIC ? "Navidrome / Subsonic" : "WebDAV", x, y, 1);
  y += 80;
  for (int i = 0; i < FC_N; i++) {
    int foco = i == formFoco;
    size_t n;
    char *c = formCampo(i, &n);
    char v[300] = "";
    if (c) { if (i == FC_SENHA) { size_t l = strlen(c); for (size_t k = 0; k < l && k < 24; k++) v[k] = '*'; v[l < 24 ? l : 24] = 0; } else snprintf(v, sizeof v, "%s", c); }
    if (foco) gfx_cor((GfxRect){ x - 12, y - 8, w + 24, 64 }, 0.25f, 1, 1, 1, 0.14f);
    texto(TXT_BODY, i18n(ROT[i]), x + 8, y + 6, foco ? 1 : 0.8f);
    if (v[0]) corta(TXT_BODY, v, x + 420, y + 6, w - 440, 0.75f);
    y += 72;
  }
  y += 20;
  if (formTestando) texto(TXT_CAPTION, i18n("Conectando…"), x, y, 0.7f);
  else if (formDesc[0]) corta(TXT_CAPTION, formDesc, x, y, w, 0.8f);
  txt_bloco_corta(TXT_CAPTION, i18n("A senha fica só neste aparelho e nunca vai para o registro do app. Com Navidrome/Subsonic, ela não viaja: vai um token com sal."),
                  200, 202, 210, x, y + 60, w, 34, 0.55f, 3);
}
void mcui_desenhar(Uint32 agora) {
  float mx = NV_MARGEM_X, y0 = NV_MARGEM_Y, menuW = 380, x = mx + menuW + 40, w = NV_TELA_W - x - NV_MARGEM_X;
  Pagina *pg = topo();
  int tocando = mcp_estado() != MCP_PARADO;
  float base = NV_TELA_H - (tocando ? 110 : 40);
  if (telaOff) { gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0, 0, 0, 1); return; }   // pure black: OLED pixels off
  gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0, 0.05f, 0.052f, 0.064f, 1);
  if (zona == Z_TOCANDO) { desenharTocando(agora); if (teclado_aberto()) teclado_desenhar(agora); return; }
  if (lfmEstado) {
    texto(TXT_TITULO2, i18n("Conectar o Last.fm"), mx, y0, 1);
    if (lfmEstado == 1 || !lfmUrl[0]) texto(TXT_BODY, i18n("Pedindo um código ao Last.fm…"), mx, y0 + 90, 0.8f);
    else {
      if (strcmp(qrDe, lfmUrl)) { snprintf(qrDe, sizeof qrDe, "%s", lfmUrl); qr_textura(&texQr, lfmUrl); }
      if (texQr) gfx_textura((GfxRect){ mx, y0 + 90, 360, 360 }, texQr);
      txt_bloco_corta(TXT_CALLOUT, i18n("Leia o código com o celular, entre no Last.fm e toque em \"Yes, allow access\". Esta tela continua sozinha."),
                      240, 242, 248, mx + 400, y0 + 100, NV_TELA_W - mx * 2 - 400, 46, 1, 4);
      texto(TXT_CAPTION, i18n("Voltar cancela."), mx + 400, y0 + 320, 0.6f);
    }
    return;
  }
  // Sections.
  texto(TXT_TITULO2, i18n("Música"), mx, y0, 1);
  for (int i = 0; i < P_SECOES; i++) {
    float sy = y0 + 80 + (float)i * 58;
    int foco = zona == Z_MENU && i == secao;
    if (i == secao) gfx_cor((GfxRect){ mx - 12, sy - 6, menuW, 52 }, 0.4f, 1, 1, 1, foco ? 0.9f : 0.12f);
    { TxtLinha l = txt_linha_corta(TXT_CALLOUT, i18n(SECOES[i]), foco ? 20 : 240, foco ? 20 : 242, foco ? 24 : 248, 255, menuW - 30); txt_desenhar_alpha(l, mx + 8, sy + 6, i == secao ? 1 : 0.7f); }
  }
  if (zona == Z_FORM) { desenharForm(x, y0, w); if (teclado_aberto()) teclado_desenhar(agora); barraMini(NV_TELA_H - 90); return; }
  if (pg) {
    float y = y0, rh = 96;
    int vis, de;
    corta(TXT_TITULO2, pg->titulo, x, y, w, 1);
    y += 80;
    vis = (int)((base - y) / rh);
    if (vis < 1) vis = 1;
    de = pg->foco >= vis ? pg->foco - vis + 1 : 0;
    if (!pg->n) {
      const char *m = pg->carregando ? "Carregando…" : pg->erro ? mensagem(pg->erro) : !mcfontes_n() && !ehLocal(pg->tipo) ?
                      "Nenhum servidor de música ainda. Adicione um Navidrome, Subsonic ou WebDAV em Serviços de música." : "Nada por aqui.";
      txt_bloco_corta(TXT_BODY, i18n(m), 240, 242, 248, x, y + 10, w, 44, 0.75f, 3);
    }
    for (int i = de; i < pg->n && i < de + vis; i++) {
      const Linha *l = &pg->v[i];
      int foco = zona == Z_LISTA && i == pg->foco;
      float ry = y + (float)(i - de) * rh;
      if (l->tipo == L_TITULO) { texto(TXT_CALLOUT, l->t1, x, ry + 40, 0.6f); continue; }
      if (foco) gfx_cor((GfxRect){ x - 12, ry - 6, w + 24, rh - 8 }, 0.25f, 1, 1, 1, 0.13f);
      if (l->tipo == L_FAIXA || l->tipo == L_ALBUM || l->tipo == L_ARTISTA || l->tipo == L_PLAYLIST) {
        capa(l->tipo == L_FAIXA ? l->f.id : l->ref, l->capa, x, ry, 80);
        corta(TXT_BODY, l->t1, x + 100, ry + 8, w - 120, foco ? 1 : 0.88f);
        corta(TXT_CAPTION, l->t2, x + 100, ry + 50, w - 120, 0.6f);
        if (l->tipo == L_FAIXA && (l->f.fav || mcv_fav(l->f.id))) texto(TXT_CAPTION, "♥", x + w - 30, ry + 10, 0.8f);
      } else if (l->tipo == L_YTVIDEO || l->tipo == L_YTLISTA) {
        GLuint tx = l->thumb[0] ? tex_obter_larg(l->thumb, 142) : 0;
        if (tx) gfx_textura((GfxRect){ x, ry, 142, 80 }, tx);
        else gfx_cor((GfxRect){ x, ry, 142, 80 }, 0.12f, 1, 1, 1, 0.07f);
        corta(TXT_BODY, l->t1, x + 162, ry + 8, w - 180, foco ? 1 : 0.88f);
        corta(TXT_CAPTION, l->t2, x + 162, ry + 50, w - 180, 0.6f);
      } else if (l->tipo == L_INFO) {
        txt_bloco_corta(TXT_CAPTION, l->t1, 200, 202, 210, x, ry + 8, w, 32, 0.7f, 2);
      } else {
        corta(TXT_BODY, l->t1, x + 8, ry + 8, w - 20, foco ? 1 : 0.88f);
        if (l->t2[0]) corta(TXT_CAPTION, l->t2, x + 8, ry + 50, w - 20, 0.6f);
      }
    }
    if (pg->n && pg->carregando) texto(TXT_CAPTION, i18n("Carregando mais…"), x, base - 30, 0.6f);
    if (zona == Z_LISTA && pg->n && (pg->v[pg->foco].tipo == L_FAIXA || pg->v[pg->foco].tipo == L_ALBUM))
      texto(TXT_CAPTION, i18n("OK toca · → mais opções"), x + w - 300, y0 + 16, 0.5f);
  }
  if (zona == Z_ACOES) {
    float ax = NV_TELA_W - 620, ay = 180, aw = 560;
    gfx_cor((GfxRect){ ax - 24, ay - 24, aw + 48, 80 + (float)nAcoes * 64 }, 0.3f, 0.12f, 0.125f, 0.14f, 0.98f);
    corta(TXT_CALLOUT, linhaAcao.t1, ax, ay, aw, 0.9f);
    for (int i = 0; i < nAcoes; i++) {
      int foco = i == focoAcao;
      float yy = ay + 60 + (float)i * 64;
      if (foco) gfx_cor((GfxRect){ ax - 10, yy - 6, aw + 20, 56 }, 0.25f, 1, 1, 1, 0.9f);
      { TxtLinha l = txt_linha(TXT_BODY, i18n(rotuloAcao(acoes[i])), foco ? 20 : 240, foco ? 20 : 242, foco ? 24 : 248, 255); txt_desenhar_alpha(l, ax, yy + 4, 1); }
    }
  }
  if (tocando) barraMini(NV_TELA_H - 90);
  if (teclado_aberto()) teclado_desenhar(agora);
}
