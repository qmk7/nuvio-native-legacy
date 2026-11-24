// QR TIME SETTINGS FROM THE PHONE OR A COMPUTER: every setting of the TV's
// Settings screen, in the same sections and order, on a page of the home
// network (lanweb.c). Each row shows the value as the TV shows it and changes
// it with ◀ ▶, exactly like the remote's Left/Right (ajustes_remoto_mudar),
// risky changes included in the safe-mode journal.
//
// THE LINK STAYS THE SAME (bookmark it): the page's token is kept on this TV
// (ajustes-remoto.txt, 0600 like every data file) and the server tries port
// 8098 first. "New link" in Accounts › Phone makes a new token and the old
// bookmark stops working. Once opened, the page comes back by itself at every
// start of the app. Home network only: lanweb answers LAN addresses only.
#include "ajremoto.h"
#include "ajustes.h"
#include "casaha.h"
#include "celapps.h"
#include "dados.h"
#include "idioma.h"
#include "lanweb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARQ "ajustes-remoto.txt"
#define MAX_OPS 1400

static char token[16];
static int ops[MAX_OPS], nOps;

static int lerToken(void) {
  char *t = dados_ler(ARQ);
  size_t n;
  token[0] = 0;
  if (!t) return 0;
  n = strcspn(t, "\r\n");
  if (n < sizeof token) { memcpy(token, t, n); token[n] = 0; }
  free(t);
  return token[0] != 0;
}
static int novoToken(void) {
  char t[16];
  if (!lanweb_gerar_token(t, sizeof t)) return 0;
  snprintf(token, sizeof token, "%s", t);
  return dados_gravar(ARQ, token);
}

// JSON string body (no quotes) of `s`.
static size_t jsonEsc(char *d, size_t n, const char *s) {
  size_t k = 0;
  for (; s && *s && k + 7 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { d[k++] = '\\'; d[k++] = (char)c; }
    else if (c < 0x20) k += (size_t)snprintf(d + k, n - k, "\\u%04x", c);
    else d[k++] = (char)c;
  }
  d[k] = 0;
  return k;
}
static void publicar(void) {
  size_t cap = 512 + (size_t)nOps * 140, k = 0;
  char *j = malloc(cap), e[256];
  if (!j) return;
  k += (size_t)snprintf(j + k, cap - k, "{\"v\":{");
  for (int i = 0; i < nOps && k + 300 < cap; i++) {
    jsonEsc(e, sizeof e, ajustes_remoto_valor(ops[i]));
    k += (size_t)snprintf(j + k, cap - k, "%s\"%d\":\"%s\"", i ? "," : "", ops[i], e);
  }
  snprintf(j + k, cap - k, "}}");
  lanweb_publicar("ajustes", j);
  free(j);
}

static char *pagina(void) {
  size_t cap = 4096, k = 0;
  char *c = malloc(cap), esc[400];
  int tipo, op, aberto = 0;
  const char *tit;
  if (!c) return NULL;
  nOps = 0;
#define POR(...) do { int w_; for (;;) { w_ = snprintf(c + k, cap - k, __VA_ARGS__); if ((size_t)w_ < cap - k) break; \
    char *n_ = realloc(c, cap * 2); if (!n_) { free(c); return NULL; } c = n_; cap *= 2; } k += (size_t)w_; } while (0)
  lanweb_escapar(esc, sizeof esc, i18n("Procurar um ajuste"));
  POR("<input id=\"q\" placeholder=\"%s\" oninput=\"filtrar()\"><div id=\"msg\" class=\"msg\"></div>", esc);
  for (int i = 0; ajustes_remoto_item(i, &tipo, &tit, &op); i++) {
    if (tipo == 0) {
      if (aberto) POR("</details>");
      lanweb_escapar(esc, sizeof esc, tit ? tit : "");
      POR("<details class=\"sec\"><summary>%s</summary>", esc);
      aberto = 1;
    } else if (tipo == 1 && tit && tit[0]) {
      lanweb_escapar(esc, sizeof esc, tit);
      POR("<h2>%s</h2>", esc);
    } else if (tipo == 2 && nOps < MAX_OPS) {
      ops[nOps++] = op;
      lanweb_escapar(esc, sizeof esc, tit ? tit : "");
      POR("<div class=\"item aj\"><div><span>%s</span><small id=\"v%d\">…</small></div>"
          "<button class=\"s\" onclick=\"m(%d,-1)\">&#9664;</button><button onclick=\"m(%d,1)\">&#9654;</button></div>",
          esc, op, op, op);
    }
  }
  if (aberto) POR("</details>");
#undef POR
  {
    char *h = celapps_pagina("Ajustes da TV", c,
      "function pintar(e){if(!e||!e.v)return;for(var k in e.v){var x=document.getElementById('v'+k);if(x)x.textContent=e.v[k]}}"
      "function atualizar(){setTimeout(function(){estado(pintar)},350)}"
      "function m(o,d){post('mudar',{op:o,d:d}).then(atualizar)}"
      "function filtrar(){var q=document.getElementById('q').value.toLowerCase();"
      "document.querySelectorAll('details.sec').forEach(function(s){var algum=0;"
      "s.querySelectorAll('.aj').forEach(function(r){var ok=!q||r.textContent.toLowerCase().indexOf(q)>=0;r.style.display=ok?'':'none';if(ok)algum=1});"
      "s.style.display=algum?'':'none';if(q)s.open=algum})}"
      "post('ler',{}).then(atualizar);setInterval(function(){estado(pintar)},5000);"
      "document.head.insertAdjacentHTML('beforeend','<style>details.sec{background:#0a1a3c;border-radius:14px;margin:10px 0;padding:4px 10px}"
      "details.sec summary{font-size:17px;font-weight:700;padding:12px 4px;cursor:pointer}.aj span{display:block}"
      ".aj small{color:#7fb2ff;font-size:15px}.aj button{min-width:52px}</style>');");
    free(c);
    return h;
  }
}
static void pedido(const LwPedido *p) {
  char o[16], d[8], q[200], t[16];
  // Home Assistant / Alexa: play a title by name (or tt id), or press a key.
  if (!strcmp(p->rota, "tocar")) {
    lanweb_campo(p->corpo, "tipo", t, sizeof t);
    if (lanweb_campo(p->corpo, "q", q, sizeof q) && q[0]) casaha_tocar(t, q);
    return;
  }
  if (!strcmp(p->rota, "tecla")) {
    if (lanweb_campo(p->corpo, "k", t, sizeof t)) celapps_injetar_tecla(t);
    return;
  }
  if (!strcmp(p->rota, "mudar") && lanweb_campo(p->corpo, "op", o, sizeof o) && lanweb_campo(p->corpo, "d", d, sizeof d)) {
    int op = atoi(o), ok = 0;
    for (int i = 0; i < nOps; i++) if (ops[i] == op) ok = 1;   // only rows the page shows
    if (ok) ajustes_remoto_mudar(op, atoi(d));
  }
  publicar();
}
void ajremoto_passo(Uint32 agora);
static int ligar(void) {
  char *h = pagina();
  int ok;
  if (!h) return 0;
  if (!token[0] && !lerToken() && !novoToken()) { free(h); return 0; }
  ok = lanweb_registrar_token("ajustes", h, 1024, token);
  free(h);
  if (ok) publicar();
  printf("[ajremoto] pagina de ajustes %s\n", ok ? "no ar" : "sem rede local");
  fflush(stdout);
  return ok;
}
// celapps: CA_AJUSTES opens through ajremoto_abrir (fixed token); montar is
// only there so the dispatcher knows the page exists.
void ajremoto_iniciar(void) {
  CelAppDef d = { pagina, pedido, ajremoto_passo, 512 };
  celapps_definir(CA_AJUSTES, &d);
}
int ajremoto_abrir(void) { return ligar(); }
void ajremoto_renovar(void) { if (novoToken()) ligar(); }
void ajremoto_passo(Uint32 agora) {
  static int tentou;
  // Opened once before: back by itself ~10 s after the start (network up).
  if (tentou || agora < 10000u) return;
  tentou = 1;
  if (lerToken()) ligar();
}
