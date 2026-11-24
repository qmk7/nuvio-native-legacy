// Ver celapps.h.
#include "celapps.h"
#include "idioma.h"
#include "idiomacod.h"
#include "ilha.h"
#include "teclado.h"
#include "spotlight.h"
#include "mcfontes.h"
#include "mclfm.h"
#include "mcspotify.h"
#include "mcui.h"
#include "songid.h"
#include "player.h"
#include "mcfesta.h"
#include "qrbackup.h"
#include "perfilfoto.h"
#include "ezviz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ajremoto.h"

int ajustes_idioma(void);

static const char *const APP[CA_N] = { "controle", "musica", "festa", "backup", "foto", "casa", "ajustes" };
static const char *const NOME[CA_N] = { "Controle pelo celular", "Configurar música pelo celular", "Modo festa",
                                        "Backup e restauração", "Foto do perfil", "Campainha EZVIZ",
                                        "Ajustes da TV pelo celular" };
static const char *const DESC[CA_N] = {
  "Setas, OK, voltar e play no celular. Digite no celular para buscar na TV ou preencher o campo aberto.",
  "Navidrome, WebDAV, Last.fm e playlists do Spotify, digitados no celular.",
  "Os convidados pedem músicas pelo celular; você aprova na TV. Votos para pular.",
  "Guarda num arquivo protegido por senha o que a conta não sincroniza, e restaura.",
  "Escolha uma foto no celular para o perfil atual.",
  "AppKey e Secret do EZVIZ: a TV avisa quando tocam a campainha.",
  "Todos os ajustes da TV no celular ou no computador, num link fixo que você pode salvar.",
};
static const char *const ICONE[CA_N] = { "aj_gamepad-2", "aj_music", "aj_party-popper", "aj_hard-drive", "aj_image", "aj_door-open", "aj_sliders-horizontal" };
static CelAppDef defs[CA_N];

const char *celapps_nome(int a) { return a >= 0 && a < CA_N ? NOME[a] : ""; }
const char *celapps_desc(int a) { return a >= 0 && a < CA_N ? DESC[a] : ""; }
const char *celapps_icone(int a) { return a >= 0 && a < CA_N ? ICONE[a] : "aj_smartphone"; }
const char *celapps_url(int a) { return a >= 0 && a < CA_N ? lanweb_url(APP[a]) : ""; }
void celapps_renovar(int a) {
  if (a == CA_AJUSTES) ajremoto_renovar();   // the kept token changes too
  else if (a >= 0 && a < CA_N) lanweb_renovar(APP[a]);
}
void celapps_definir(int a, const CelAppDef *d) { if (a >= 0 && a < CA_N && d) defs[a] = *d; }

// ---------------------------------------------------------------- page skeleton
char *celapps_pagina(const char *titulo, const char *corpo, const char *js) {
  const char *iso = idioma_iso(ajustes_idioma());
  int rtl = iso && !strcmp(iso, "ar");
  char t[256];
  size_t n;
  char *b;
  lanweb_escapar(t, sizeof t, i18n(titulo));
  n = strlen(corpo) + (js ? strlen(js) : 0) + 4096;
  b = malloc(n);
  if (!b) return NULL;
  snprintf(b, n,
    "<!doctype html><html lang=\"%s\" dir=\"%s\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>QR TIME</title><style>"
    "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:#061532;color:#eef1f8}"
    "main{max-width:520px;margin:0 auto;padding:20px 16px 40px}h1{font-size:22px;margin:4px 0 16px}"
    "h2{font-size:16px;margin:22px 0 8px;color:#b9c3dc}.marca{font-weight:800;letter-spacing:.08em;color:#7fb2ff;font-size:13px}"
    "input,select,textarea{width:100%%;box-sizing:border-box;font-size:17px;padding:12px;border-radius:12px;border:1px solid #2b3b63;"
    "background:#0d2048;color:#fff;margin:4px 0 10px}button{font-size:17px;padding:14px 16px;border-radius:14px;border:0;"
    "background:#2f6bff;color:#fff;font-weight:600;width:100%%;margin:4px 0}button.s{background:#1a2c57}"
    ".grade{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.grade button{height:72px;font-size:26px;margin:0}"
    ".lin{display:flex;gap:10px}.lin button{flex:1}.msg{min-height:22px;color:#9fe0b0;margin:8px 0}"
    ".item{background:#0d2048;border-radius:12px;padding:12px;margin:8px 0;display:flex;gap:10px;align-items:center}"
    ".item div{flex:1}.item small{color:#9aa6c4;display:block}.item button{width:auto;margin:0}"
    "</style></head><body><main><div class=\"marca\">QR TIME</div><h1>%s</h1>%s</main><script>"
    "function post(r,o){var b=Object.keys(o||{}).map(function(k){return encodeURIComponent(k)+'='+encodeURIComponent(o[k])}).join('&');"
    "return fetch(r,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b}).then(function(x){return x.ok})}"
    "function estado(f){fetch('estado').then(function(x){return x.json()}).then(f).catch(function(){})}"
    "function msg(t){var m=document.getElementById('msg');if(m)m.textContent=t}"
    "%s</script></body></html>",
    iso ? iso : "en", rtl ? "rtl" : "ltr", t, corpo, js ? js : "");
  return b;
}

// ---------------------------------------------------------------- remote control
static char remotas[16][16];
static int nRemotas;
static void teclaRemota(const char *k) { if (nRemotas < 16) snprintf(remotas[nRemotas++], sizeof remotas[0], "%s", k); }
int celapps_tecla_remota(char *dst, size_t n) {
  if (!nRemotas) return 0;
  snprintf(dst, n, "%s", remotas[0]);
  memmove(remotas, remotas + 1, sizeof remotas[0] * (size_t)(--nRemotas));
  return 1;
}
static char *paginaControle(void) {
  char corpo[3000], s1[160], s2[160], s3[160], s4[160], s5[160];
  lanweb_escapar(s1, sizeof s1, i18n("Voltar"));
  lanweb_escapar(s2, sizeof s2, i18n("Play / pausa"));
  lanweb_escapar(s3, sizeof s3, i18n("Digite aqui"));
  lanweb_escapar(s4, sizeof s4, i18n("Buscar na TV"));
  lanweb_escapar(s5, sizeof s5, i18n("Enviar ao campo aberto"));
  snprintf(corpo, sizeof corpo,
    "<div class=\"grade\"><span></span><button data-k=\"up\">&#9650;</button><span></span>"
    "<button data-k=\"left\">&#9664;</button><button data-k=\"ok\">OK</button><button data-k=\"right\">&#9654;</button>"
    "<span></span><button data-k=\"down\">&#9660;</button><span></span></div>"
    "<div class=\"lin\" style=\"margin-top:10px\"><button class=\"s\" data-k=\"back\">%s</button>"
    "<button class=\"s\" data-k=\"play\">%s</button></div>"
    "<h2>%s</h2><input id=\"t\" autocomplete=\"off\"><div class=\"lin\"><button id=\"busca\">%s</button>"
    "<button class=\"s\" id=\"campo\">%s</button></div><div id=\"msg\" class=\"msg\"></div>",
    s1, s2, s3, s4, s5);
  return celapps_pagina("Controle pelo celular", corpo,
    "document.querySelectorAll('[data-k]').forEach(function(b){b.onclick=function(){post('tecla',{k:b.dataset.k});"
    "if(navigator.vibrate)navigator.vibrate(15)}});"
    "function txt(m){var v=document.getElementById('t').value;if(!v)return;post('texto',{t:v,modo:m}).then(function(){msg('\\u2713')})}"
    "document.getElementById('busca').onclick=function(){txt('busca')};"
    "document.getElementById('campo').onclick=function(){txt('campo')};");
}
// Home Assistant (ajremoto.c "tecla" route): the same keys as the phone remote.
int celapps_injetar_tecla(const char *k) {
  static const char *const OK[] = { "up", "down", "left", "right", "ok", "back", "play", NULL };
  for (int i = 0; k && OK[i]; i++) if (!strcmp(k, OK[i])) { teclaRemota(k); return 1; }
  return 0;
}
static void pedidoControle(const LwPedido *p) {
  char k[16], t[512], modo[16];
  static const char *const OK[] = { "up", "down", "left", "right", "ok", "back", "play", NULL };
  if (!strcmp(p->rota, "tecla") && lanweb_campo(p->corpo, "k", k, sizeof k)) {
    for (int i = 0; OK[i]; i++) if (!strcmp(k, OK[i])) { teclaRemota(k); return; }
    return;
  }
  if (!strcmp(p->rota, "texto") && lanweb_campo(p->corpo, "t", t, sizeof t) && t[0]) {
    lanweb_campo(p->corpo, "modo", modo, sizeof modo);
    printf("[controle] texto do celular (%zu bytes, %s)\n", strlen(t), modo);
    if (teclado_aberto()) { teclado_porta_colar(t); return; }
    if (!strcmp(modo, "campo")) { ilha_avisar("celcampo", ILHA_INFO, "aj_smartphone", i18n("Nenhum campo de texto aberto na TV"), 2500, 0); return; }
    if (player_aberto()) return;
    if (!spot_aberto()) spot_abrir(0);
    spot_texto_externo(t);
  }
}

// ---------------------------------------------------------------- music setup
static char *paginaMusica(void) {
  char corpo[7000], a[12][200];
  const char *k[12] = { "Servidor de música", "Tipo", "Nome", "Endereço", "Usuário", "Senha", "Salvar na TV",
                        "Chave de API", "Segredo compartilhado", "Playlist do Spotify", "Link público da playlist", "Importar" };
  for (int i = 0; i < 12; i++) lanweb_escapar(a[i], sizeof a[i], i18n(k[i]));
  snprintf(corpo, sizeof corpo,
    "<h2>%s</h2><label>%s</label><select id=\"tipo\"><option value=\"ss\">Navidrome / Subsonic</option>"
    "<option value=\"dv\">WebDAV</option></select><label>%s</label><input id=\"nome\" placeholder=\"Navidrome\">"
    "<label>%s</label><input id=\"url\" inputmode=\"url\" placeholder=\"https://music.example.com\">"
    "<label>%s</label><input id=\"usr\" autocomplete=\"off\"><label>%s</label><input id=\"pw\" type=\"password\" autocomplete=\"off\">"
    "<button id=\"fonte\">%s</button>"
    "<h2>Last.fm</h2><label>%s</label><input id=\"lk\" autocomplete=\"off\"><label>%s</label>"
    "<input id=\"ls\" type=\"password\" autocomplete=\"off\"><button id=\"lfm\">%s</button>"
    "<h2>%s</h2><label>Client ID</label><input id=\"sci\" autocomplete=\"off\"><label>Client secret</label>"
    "<input id=\"scs\" type=\"password\" autocomplete=\"off\"><label>%s</label><input id=\"sp\" inputmode=\"url\" placeholder=\"https://open.spotify.com/playlist/...\">"
    "<button id=\"spb\">%s</button>"
    "<h2>AudD</h2><label>API token</label><input id=\"aud\" type=\"password\" autocomplete=\"off\"><button class=\"s\" id=\"audb\">%s</button>"
    "<div id=\"msg\" class=\"msg\"></div>",
    a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[6], a[9], a[10], a[11], a[6]);
  return celapps_pagina("Configurar música pelo celular", corpo,
    "function v(i){return document.getElementById(i).value.trim()}"
    "document.getElementById('fonte').onclick=function(){post('fonte',{tipo:v('tipo'),nome:v('nome'),url:v('url'),usuario:v('usr'),senha:v('pw')})"
    ".then(function(){msg('\\u2713');document.getElementById('pw').value=''})};"
    "document.getElementById('lfm').onclick=function(){post('lastfm',{chave:v('lk'),segredo:v('ls')}).then(function(){msg('\\u2713');"
    "document.getElementById('ls').value=''})};"
    "document.getElementById('spb').onclick=function(){post('spotify',{url:v('sp'),id:v('sci'),segredo:v('scs')}).then(function(){msg('\\u2713');"
    "document.getElementById('scs').value=''})};"
    "document.getElementById('audb').onclick=function(){post('audd',{token:v('aud')}).then(function(){msg('\\u2713');document.getElementById('aud').value=''})};"
    "setInterval(function(){estado(function(e){if(e.msg)msg(e.msg)})},2000);");
}
static void pedidoMusica(const LwPedido *p) {
  char tipo[8], url[384];
  if (!strcmp(p->rota, "fonte")) {
    McFonte f;
    memset(&f, 0, sizeof f);
    lanweb_campo(p->corpo, "tipo", tipo, sizeof tipo);
    lanweb_campo(p->corpo, "url", url, sizeof url);
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) {
      lanweb_publicar("musica", "{\"msg\":\"\\u2716 URL\"}");
      return;
    }
    f.tipo = !strcmp(tipo, "dv") ? MCS_WEBDAV : MCS_SUBSONIC;
    lanweb_campo(p->corpo, "nome", f.nome, sizeof f.nome);
    if (!f.nome[0]) snprintf(f.nome, sizeof f.nome, "%s", f.tipo == MCS_WEBDAV ? "WebDAV" : "Navidrome");
    if (f.tipo == MCS_WEBDAV) {
      snprintf(f.dv.url, sizeof f.dv.url, "%s", url);
      lanweb_campo(p->corpo, "usuario", f.dv.usuario, sizeof f.dv.usuario);
      lanweb_campo(p->corpo, "senha", f.dv.senha, sizeof f.dv.senha);
    } else {
      snprintf(f.ss.url, sizeof f.ss.url, "%s", url);
      lanweb_campo(p->corpo, "usuario", f.ss.usuario, sizeof f.ss.usuario);
      lanweb_campo(p->corpo, "senha", f.ss.senha, sizeof f.ss.senha);
    }
    if (mcfontes_salvar(&f)) {
      char t[160];
      snprintf(t, sizeof t, "%s: %s", i18n("Fonte de música adicionada"), f.nome);
      ilha_avisar("celmusica", ILHA_OK, "aj_music", t, 3500, 0);
      lanweb_publicar("musica", "{\"msg\":\"\\u2713\"}");
      printf("[celapps] fonte de musica salva pelo celular (tipo %d)\n", f.tipo);
    }
    memset(&f, 0, sizeof f);
    return;
  }
  if (!strcmp(p->rota, "lastfm")) {
    char k[96], s[96];
    lanweb_campo(p->corpo, "chave", k, sizeof k);
    lanweb_campo(p->corpo, "segredo", s, sizeof s);
    if (k[0] && mclfm_definir_chave(k, s[0] ? s : NULL)) {
      ilha_avisar("celmusica", ILHA_OK, "aj_music", i18n("Chave do Last.fm salva"), 3000, 0);
      puts("[celapps] chave do Last.fm salva pelo celular");
    }
    memset(k, 0, sizeof k); memset(s, 0, sizeof s);
    return;
  }
  if (!strcmp(p->rota, "audd")) {
    char t[128];
    lanweb_campo(p->corpo, "token", t, sizeof t);
    if (songid_definir_chave(t)) ilha_avisar("celmusica", ILHA_OK, "aj_music", i18n(t[0] ? "Chave do AudD salva" : "Chave do AudD removida"), 3000, 0);
    memset(t, 0, sizeof t);
    return;
  }
  if (!strcmp(p->rota, "spotify")) {
    // Spotify's official Web API with the person's own developer app (client
    // id + secret, kept on this TV). Reading a playlist without a key would
    // mean scraping Spotify's pages: not done.
    char id[96], seg[96], e[64];
    lanweb_campo(p->corpo, "id", id, sizeof id);
    lanweb_campo(p->corpo, "segredo", seg, sizeof seg);
    if (id[0] || seg[0]) mcsp_definir_chave(id[0] ? id : NULL, seg[0] ? seg : NULL);
    memset(seg, 0, sizeof seg);
    lanweb_campo(p->corpo, "url", url, sizeof url);
    if (!url[0]) { if (id[0]) ilha_avisar("celmusica", ILHA_OK, "aj_music", i18n("Chave do Spotify salva"), 3000, 0); return; }
    if (!mcsp_id_playlist(url, e, sizeof e)) { lanweb_publicar("musica", "{\"msg\":\"\\u2716 URL\"}"); return; }
    if (!mcsp_tem_chave()) {
      ilha_avisar("celmusica", ILHA_ERRO, "aj_music", i18n("O Spotify pede o Client ID e o secret do seu app de desenvolvedor."), 4000, 0);
      lanweb_publicar("musica", "{\"msg\":\"\\u2716 Client ID\"}");
      return;
    }
    mcui_importar_spotify(url);
  }
}

// ---------------------------------------------------------------- dispatcher
static void iniciarDefs(void) {
  static int feito;
  if (feito) return;
  feito = 1;
  lanweb_porta_preferida(8098);   // every page, so bookmarked links survive restarts
  if (!defs[CA_CONTROLE].montar) defs[CA_CONTROLE] = (CelAppDef){ paginaControle, pedidoControle, NULL, 2048 };
  if (!defs[CA_MUSICA].montar) defs[CA_MUSICA] = (CelAppDef){ paginaMusica, pedidoMusica, NULL, 4096 };
  if (!defs[CA_FESTA].montar) mcfesta_iniciar();
  if (!defs[CA_BACKUP].montar) qrbackup_iniciar();
  if (!defs[CA_FOTO].montar) perfilfoto_iniciar();
  if (!defs[CA_CASA].montar) ezviz_iniciar();
  if (!defs[CA_AJUSTES].montar) ajremoto_iniciar();
}
int celapps_abrir(int a) {
  char *h;
  int ok;
  iniciarDefs();
  if (a < 0 || a >= CA_N || !defs[a].montar) return 0;
  if (a == CA_AJUSTES) return ajremoto_abrir();
  h = defs[a].montar();
  if (!h) return 0;
  ok = lanweb_registrar(APP[a], h, defs[a].maxCorpo);
  free(h);
  printf("[celapps] %s: %s\n", APP[a], ok ? "no ar" : "sem rede local");
  fflush(stdout);
  return ok;
}
void celapps_atualizar(Uint32 agora) {
  LwPedido p;
  int guarda = 0;
  iniciarDefs();
  while (guarda++ < 16 && lanweb_proximo(&p)) {
    for (int a = 0; a < CA_N; a++)
      if (!strcmp(p.app, APP[a]) && defs[a].pedido) { defs[a].pedido(&p); break; }
    lanweb_liberar(&p);
  }
  for (int a = 0; a < CA_N; a++) if (defs[a].quadro) defs[a].quadro(agora);
}
