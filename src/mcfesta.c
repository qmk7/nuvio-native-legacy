// QR TIME PARTY MODE (Nuvio One style): guests open a page on their phones
// (QR in Settings > Accounts, providers and data > Phone), search the TV's
// music sources and request songs. Each request reaches the TV as a notice
// with Approve / Decline; approved songs go to the end of the queue (and
// start playing when nothing is). Guests vote to skip the current song: half
// of the phones that took part (at least 2) skips it.
//
// Guests never see the server addresses, ids or passwords: only titles and
// artists, and an index into their own search results.
#include "mcfesta.h"
#include "celapps.h"
#include "mccat.h"
#include "mcfila.h"
#include "mcplayer.h"
#include "idioma.h"
#include "ilha.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONVIDADOS 12
#define RESULTADOS 10
#define PEDIDOS    24

typedef struct { char g[24], nome[40], cli[48]; int n, buscando; McFaixa r[RESULTADOS]; } Convidado;
typedef struct { int id, estado; McFaixa f; char quem[40]; } Pedido;   // estado: 0 waiting, 1 approved, 2 declined

static Convidado conv[CONVIDADOS];
static int nConv;
static Pedido ped[PEDIDOS];
static int nPed, proxId = 1;
static char votos[CONVIDADOS][48], votoFaixa[sizeof ((McFaixa *)0)->id];
static int nVotos, sujo = 1;
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int (*buscar)(const char *q, McFaixa *out, int max) = NULL;

// ---------------------------------------------------------------- helpers
static int buscarReal(const char *q, McFaixa *out, int max) {
  McArtista ar[2]; McAlbum al[2];
  int na = 0, nl = 0, nf = 0;
  if (mccat_buscar(q, ar, 0, &na, al, 0, &nl, out, max, &nf) < 0) return 0;
  return nf;
}
static Convidado *convidado(const char *g, const char *cli, int criar) {
  for (int i = 0; i < nConv; i++) if (!strcmp(conv[i].g, g)) return &conv[i];
  if (!criar || !g[0]) return NULL;
  if (nConv == CONVIDADOS) return NULL;
  memset(&conv[nConv], 0, sizeof conv[nConv]);
  snprintf(conv[nConv].g, sizeof conv[nConv].g, "%s", g);
  snprintf(conv[nConv].cli, sizeof conv[nConv].cli, "%s", cli);
  return &conv[nConv++];
}
static int telefones(void) {   // distinct phones (by address) that took part
  int n = 0;
  for (int i = 0; i < nConv; i++) {
    int novo = 1;
    for (int j = 0; j < i; j++) if (!strcmp(conv[i].cli, conv[j].cli)) novo = 0;
    n += novo;
  }
  return n;
}
int mcfesta_votos_precisa(void) { int n = telefones(); n = (n + 1) / 2; return n < 2 ? 2 : n; }
static void jsonTexto(char *d, size_t n, const char *s) {
  size_t k = 0;
  for (; s && *s && k + 7 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { d[k++] = '\\'; d[k++] = (char)c; }
    else if (c < 0x20) { k += (size_t)snprintf(d + k, n - k, "\\u%04x", c); }
    else d[k++] = (char)c;
  }
  d[k] = 0;
}
static void publicar(void) {
  size_t cap = 65536, u = 0;
  char *b = malloc(cap), t[200], a[160], q[60];
  const McFaixa *at = mcq_faixa_atual();
  if (!b) return;
  jsonTexto(t, sizeof t, at && mcp_estado() != MCP_PARADO ? at->titulo : "");
  jsonTexto(a, sizeof a, at && mcp_estado() != MCP_PARADO ? at->artista : "");
  u += (size_t)snprintf(b + u, cap - u, "{\"tocando\":{\"t\":\"%s\",\"a\":\"%s\"},\"votos\":%d,\"precisa\":%d,\"fila\":[",
                        t, a, nVotos, mcfesta_votos_precisa());
  for (int i = 0; i < nPed && u + 600 < cap; i++) {
    jsonTexto(t, sizeof t, ped[i].f.titulo); jsonTexto(a, sizeof a, ped[i].f.artista); jsonTexto(q, sizeof q, ped[i].quem);
    u += (size_t)snprintf(b + u, cap - u, "%s{\"t\":\"%s\",\"a\":\"%s\",\"quem\":\"%s\",\"e\":%d}", i ? "," : "", t, a, q, ped[i].estado);
  }
  u += (size_t)snprintf(b + u, cap - u, "],\"res\":{");
  for (int i = 0; i < nConv && u + 3000 < cap; i++) {
    u += (size_t)snprintf(b + u, cap - u, "%s\"%s\":{\"b\":%d,\"l\":[", i ? "," : "", conv[i].g, conv[i].buscando);
    for (int j = 0; j < conv[i].n && u + 400 < cap; j++) {
      jsonTexto(t, sizeof t, conv[i].r[j].titulo); jsonTexto(a, sizeof a, conv[i].r[j].artista);
      u += (size_t)snprintf(b + u, cap - u, "%s{\"t\":\"%s\",\"a\":\"%s\",\"d\":%d}", j ? "," : "", t, a, conv[i].r[j].dur);
    }
    u += (size_t)snprintf(b + u, cap - u, "]}");
  }
  snprintf(b + u, cap - u, "}}");
  lanweb_publicar("festa", b);
  free(b);
}

// ---------------------------------------------------------------- search worker
typedef struct { char g[24], q[128]; } Busca;
static void *fioBusca(void *arg) {
  Busca *bq = arg;
  McFaixa r[RESULTADOS];
  int n = buscar ? buscar(bq->q, r, RESULTADOS) : buscarReal(bq->q, r, RESULTADOS);
  pthread_mutex_lock(&trava);
  for (int i = 0; i < nConv; i++)
    if (!strcmp(conv[i].g, bq->g)) { conv[i].n = n < 0 ? 0 : n; memcpy(conv[i].r, r, sizeof *r * (size_t)conv[i].n); conv[i].buscando = 0; }
  sujo = 1;
  pthread_mutex_unlock(&trava);
  free(bq);
  return NULL;
}

// ---------------------------------------------------------------- requests
static void anunciar(const Pedido *p) {
  IlhaModal m;
  IlhaAvisoEx a;
  char chave[32], txt[300], f1[200];
  memset(&m, 0, sizeof m);
  memset(&a, 0, sizeof a);
  snprintf(chave, sizeof chave, "festa:%d", p->id);
  snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Modo festa"));
  snprintf(m.titulo, sizeof m.titulo, "%s", p->f.titulo);
  snprintf(m.linha, sizeof m.linha, "%s", p->f.artista);
  snprintf(m.nota, sizeof m.nota, "%s %s", i18n("Pedido de"), p->quem);
  snprintf(m.icone, sizeof m.icone, "aj_party-popper");
  m.nBotoes = 2;
  snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Aprovar"));
  snprintf(m.botao[1], sizeof m.botao[1], "%s", i18n("Recusar"));
  snprintf(txt, sizeof txt, i18n("%s pediu %s"), p->quem, ilha_forte(f1, sizeof f1, p->f.titulo));
  a.chave = chave; a.tipo = ILHA_ACENTO; a.icone = "aj_party-popper"; a.texto = txt; a.ms = 15000u;
  a.modal = &m; a.tecla = 1;
  ilha_avisar_ex(&a);
}
static int indicePedido(int id) { for (int i = 0; i < nPed; i++) if (ped[i].id == id) return i; return -1; }
int mcfesta_decidir(int id, int aprovar) {
  int i = indicePedido(id);
  if (i < 0 || ped[i].estado) return 0;
  ped[i].estado = aprovar ? 1 : 2;
  if (aprovar) {
    mcq_adicionar(&ped[i].f);
    if (mcp_estado() == MCP_PARADO) mcp_tocar(mcq_n() - 1);
  }
  printf("[festa] pedido %d %s\n", id, aprovar ? "aprovado" : "recusado");
  fflush(stdout);
  sujo = 1;
  return 1;
}
void mcfesta_acao(const char *chave, int botao) {
  int id;
  if (sscanf(chave, "festa:%d", &id) == 1 && (botao == 1 || botao == 2)) mcfesta_decidir(id, botao == 1);
}

static void votar(const char *cli) {
  const McFaixa *at = mcq_faixa_atual();
  if (!at || mcp_estado() == MCP_PARADO) return;
  if (strcmp(votoFaixa, at->id)) { snprintf(votoFaixa, sizeof votoFaixa, "%s", at->id); nVotos = 0; }
  for (int i = 0; i < nVotos; i++) if (!strcmp(votos[i], cli)) return;   // one vote per phone
  if (nVotos < CONVIDADOS) snprintf(votos[nVotos++], sizeof votos[0], "%s", cli);
  sujo = 1;
  if (nVotos >= mcfesta_votos_precisa()) {
    printf("[festa] votos para pular: %d de %d, pulando\n", nVotos, mcfesta_votos_precisa());
    fflush(stdout);
    ilha_avisar("festa-pular", ILHA_INFO, "aj_party-popper", i18n("Os convidados votaram: próxima música"), 3000u, 0);
    nVotos = 0; votoFaixa[0] = 0;
    mcp_proxima();
  }
}

void mcfesta_pedido(const LwPedido *p) {
  char g[24], nome[40], q[128], idx[8];
  Convidado *c;
  lanweb_campo(p->corpo, "g", g, sizeof g);
  lanweb_campo(p->corpo, "nome", nome, sizeof nome);
  for (char *s = g; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9'))) *s = 0;   // id: [a-z0-9]
  if (!g[0]) return;
  pthread_mutex_lock(&trava);
  c = convidado(g, p->cliente, 1);
  if (c && nome[0]) snprintf(c->nome, sizeof c->nome, "%s", nome);
  pthread_mutex_unlock(&trava);
  if (!c) return;
  if (!strcmp(p->rota, "buscar") && lanweb_campo(p->corpo, "q", q, sizeof q) && q[0]) {
    pthread_t t;
    Busca *bq;
    if (c->buscando) return;
    bq = calloc(1, sizeof *bq);
    if (!bq) return;
    snprintf(bq->g, sizeof bq->g, "%s", g); snprintf(bq->q, sizeof bq->q, "%s", q);
    c->buscando = 1; sujo = 1;
    if (pthread_create(&t, NULL, fioBusca, bq)) { free(bq); c->buscando = 0; return; }
    pthread_detach(t);
  } else if (!strcmp(p->rota, "pedir") && lanweb_campo(p->corpo, "i", idx, sizeof idx)) {
    int i = atoi(idx), pendentes = 0;
    for (int k = 0; k < nPed; k++) if (!ped[k].estado) pendentes++;
    if (i < 0 || i >= c->n || pendentes >= 8) return;   // no flooding the TV
    if (nPed == PEDIDOS) { memmove(ped, ped + 1, sizeof *ped * (PEDIDOS - 1)); nPed--; }
    memset(&ped[nPed], 0, sizeof ped[nPed]);
    ped[nPed].id = proxId++;
    ped[nPed].f = c->r[i];
    snprintf(ped[nPed].quem, sizeof ped[nPed].quem, "%s", c->nome[0] ? c->nome : i18n("Convidado"));
    printf("[festa] pedido %d recebido\n", ped[nPed].id);
    fflush(stdout);
    anunciar(&ped[nPed]);
    nPed++;
    sujo = 1;
  } else if (!strcmp(p->rota, "pular")) {
    votar(p->cliente);
  }
}

void mcfesta_quadro(Uint32 agora) {
  static Uint32 ult;
  static char tocandoAntes[sizeof ((McFaixa *)0)->id];
  const McFaixa *at = mcq_faixa_atual();
  if (!lanweb_url("festa")[0]) return;
  if (at && strcmp(at->id, tocandoAntes)) { snprintf(tocandoAntes, sizeof tocandoAntes, "%s", at->id); sujo = 1; }
  if (!sujo && agora - ult < 3000u) return;
  ult = agora;
  pthread_mutex_lock(&trava);
  sujo = 0;
  publicar();
  pthread_mutex_unlock(&trava);
}

static char *pagina(void) {
  char corpo[3500], s[10][200];
  const char *k[10] = { "Seu nome", "Buscar música", "Buscar", "Tocando agora", "Votar para pular", "Pedidos",
                        "Pedir", "Aguardando aprovação", "Aprovado", "Recusado" };
  for (int i = 0; i < 10; i++) lanweb_escapar(s[i], sizeof s[i], i18n(k[i]));
  snprintf(corpo, sizeof corpo,
    "<label>%s</label><input id=\"nome\" maxlength=\"30\">"
    "<h2>%s</h2><div id=\"toc\" class=\"item\"><div>-</div></div><button class=\"s\" id=\"pul\">%s <span id=\"vt\"></span></button>"
    "<h2>%s</h2><div class=\"lin\"><input id=\"q\" style=\"flex:2\"><button id=\"bq\" style=\"flex:1\">%s</button></div>"
    "<div id=\"res\"></div><h2>%s</h2><div id=\"fila\"></div><div id=\"msg\" class=\"msg\"></div>"
    "<script>var L={pedir:\"%s\",e:[\"%s\",\"%s\",\"%s\"]};</script>",
    s[0], s[3], s[4], s[1], s[2], s[5], s[6], s[7], s[8], s[9]);
  return celapps_pagina("Modo festa", corpo,
    "var g=localStorage.getItem('qrg');if(!g){g=Math.random().toString(36).slice(2,12);localStorage.setItem('qrg',g)}"
    "var nm=document.getElementById('nome');nm.value=localStorage.getItem('qrn')||'';"
    "nm.onchange=function(){localStorage.setItem('qrn',nm.value)};"
    "function f(o){o.g=g;o.nome=nm.value;return o}"
    "function el(t,c){var e=document.createElement(t);if(c)e.textContent=c;return e}"
    "document.getElementById('bq').onclick=function(){var q=document.getElementById('q').value.trim();if(q)post('buscar',f({q:q}))};"
    "document.getElementById('pul').onclick=function(){post('pular',f({}))};"
    "function draw(e){var t=document.getElementById('toc');t.innerHTML='';var d=el('div',e.tocando.t||'-');d.appendChild(el('small',e.tocando.a||''));t.appendChild(d);"
    "document.getElementById('vt').textContent=e.votos?'('+e.votos+'/'+e.precisa+')':'';"
    "var r=document.getElementById('res');r.innerHTML='';var m=e.res[g];if(m){if(m.b)r.appendChild(el('div','\\u2026'));"
    "m.l.forEach(function(x,i){var it=el('div');it.className='item';var d=el('div',x.t);d.appendChild(el('small',x.a));it.appendChild(d);"
    "var b=el('button',L.pedir);b.onclick=function(){post('pedir',f({i:i})).then(function(){msg('\\u2713')})};it.appendChild(b);r.appendChild(it)})}"
    "var fl=document.getElementById('fila');fl.innerHTML='';e.fila.slice().reverse().forEach(function(x){var it=el('div');it.className='item';"
    "var d=el('div',x.t);d.appendChild(el('small',x.a+' \\u00b7 '+x.quem+' \\u00b7 '+L.e[x.e]));it.appendChild(d);fl.appendChild(it)})}"
    "function loop(){estado(draw)}loop();setInterval(loop,1500);");
}

void mcfesta_iniciar(void) {
  CelAppDef d = { pagina, mcfesta_pedido, mcfesta_quadro, 1024 };
  celapps_definir(CA_FESTA, &d);
}
void mcfesta_teste_busca(int (*fn)(const char *, McFaixa *, int)) { buscar = fn; }
int mcfesta_teste_pedidos(void) { return nPed; }
