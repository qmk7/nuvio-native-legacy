// QR TIME DOORBELL / CAMERA ALERTS through EZVIZ's open platform.
// The person creates a developer app at open.ezvizlife.com (AppKey + Secret)
// and types both on the phone page (Settings > Accounts... > Phone > Home).
// Then, while QR TIME is open, every 15 s:
//   POST <api>/api/lapp/token/get        appKey, appSecret -> accessToken (7 days), areaDomain
//   POST <areaDomain>/api/lapp/alarm/list accessToken, pageSize -> the latest alarms
// A new alarm (doorbell press, motion, person...) is a notice on the TV with
// the device name and, when the alarm picture is not encrypted, the picture.
// Nothing is sent except the key/secret to EZVIZ and the token back to it.
// UNVERIFIED: written against EZVIZ's published API; needs a real account and
// doorbell to confirm (the live RTSP picture is not done: the TV's media
// pipeline needs a URL it can open, and EZVIZ's stream is encrypted by default).
#include "ezviz.h"
#include "celapps.h"
#include "dados.h"
#include "idioma.h"
#include "ilha.h"
#include "js.h"
#include "rede.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char base[200] = "https://open.ezvizlife.com";
void ezviz_definir_base(const char *b) { snprintf(base, sizeof base, "%s", b && b[0] ? b : "https://open.ezvizlife.com"); }

int ezviz_parse_token(const char *json, char *tok, size_t nt, char *area, size_t na, long long *expira) {
  char cod[16] = "";
  const char *d;
  if (tok && nt) tok[0] = 0;
  if (area && na) area[0] = 0;
  if (!json) return 0;
  js_texto_raiz(json, "code", cod, sizeof cod);
  if (strcmp(cod, "200") || !(d = strstr(json, "\"data\"")) || !(d = strchr(d, '{'))) return 0;
  js_texto_raiz_em(d, js_fim(d), "accessToken", tok, nt);
  js_texto_raiz_em(d, js_fim(d), "areaDomain", area, na);
  if (expira) *expira = (long long)js_num(d, js_fim(d), "expireTime", 0);
  return tok && tok[0];
}
int ezviz_parse_alarmes(const char *json, EzAlarme *out, int max) {
  char cod[16] = "";
  const char *p;
  int n = 0;
  if (!json) return -1;
  js_texto_raiz(json, "code", cod, sizeof cod);
  if (strcmp(cod, "200")) return -1;
  for (p = js_array(json, NULL, "data"); p && *p == '{' && n < max; ) {
    const char *f = js_fim(p);
    EzAlarme *a = &out[n];
    if (!f) break;
    memset(a, 0, sizeof *a);
    js_texto_raiz_em(p, f, "alarmId", a->id, sizeof a->id);
    js_texto_raiz_em(p, f, "deviceSerial", a->serial, sizeof a->serial);
    js_texto_raiz_em(p, f, "alarmName", a->nome, sizeof a->nome);
    js_texto_raiz_em(p, f, "alarmPicUrl", a->foto, sizeof a->foto);
    a->tipo = (int)js_num(p, f, "alarmType", 0);
    a->quando = (long long)js_num(p, f, "alarmTime", 0);
    a->cifrada = (int)js_num(p, f, "isEncrypt", 0);
    if (a->id[0]) n++;
    p = js_prox(f);
  }
  return n;
}

// ---------------------------------------------------------------- polling
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int fio, temNovo;
static EzAlarme novo;
static char ultimoId[64];
static char tok[128], area[200];
static long long tokAte;

static int credenciais(char *k, size_t nk, char *s, size_t ns) {
  char *t = dados_ler("ezviz.txt"), *tab;
  int ok = 0;
  if (t && (tab = strchr(t, '\t'))) {
    *tab = 0;
    snprintf(k, nk, "%s", t);
    snprintf(s, ns, "%s", tab + 1);
    s[strcspn(s, "\r\n")] = 0;
    ok = k[0] && s[0];
  }
  if (t) { memset(t, 0, strlen(t)); free(t); }
  return ok;
}
int ezviz_configurado(void) { char k[96], s[96]; int r = credenciais(k, sizeof k, s, sizeof s); memset(s, 0, sizeof s); return r; }
int ezviz_definir(const char *chave, const char *segredo) {
  char b[260];
  if (!chave || !chave[0]) { pthread_mutex_lock(&trava); tok[0] = 0; pthread_mutex_unlock(&trava); return dados_apagar("ezviz.txt"), 1; }
  if (!segredo || !segredo[0] || strchr(chave, '\t') || strchr(segredo, '\t') || strlen(chave) > 90 || strlen(segredo) > 90) return 0;
  snprintf(b, sizeof b, "%s\t%s\n", chave, segredo);
  { int r = dados_gravar("ezviz.txt", b); memset(b, 0, sizeof b);
    pthread_mutex_lock(&trava); tok[0] = 0; ultimoId[0] = 0; pthread_mutex_unlock(&trava); return r; }
}

static void *fioPassar(void *u) {
  char k[96], s[96], corpo[400], url[300], t[128], a[200];
  const char *cabs[2] = { "Content-Type: application/x-www-form-urlencoded", NULL };
  EzAlarme al[5];
  int st = 0, n;
  char *r;
  (void)u;
  pthread_mutex_lock(&trava);
  snprintf(t, sizeof t, "%s", tok); snprintf(a, sizeof a, "%s", area);
  if (tokAte && time(NULL) * 1000LL > tokAte - 3600000LL) t[0] = 0;
  pthread_mutex_unlock(&trava);
  if (!t[0]) {
    long long ate = 0;
    if (!credenciais(k, sizeof k, s, sizeof s)) goto fim;
    snprintf(url, sizeof url, "%s/api/lapp/token/get", base);
    snprintf(corpo, sizeof corpo, "appKey=%s&appSecret=%s", k, s);
    r = rede_postar_st(url, 15, cabs, corpo, &st);
    memset(corpo, 0, sizeof corpo); memset(s, 0, sizeof s);
    if (!ezviz_parse_token(r, t, sizeof t, a, sizeof a, &ate)) { printf("[ezviz] token recusado (HTTP %d)\n", st); free(r); goto fim; }
    free(r);
    pthread_mutex_lock(&trava);
    snprintf(tok, sizeof tok, "%s", t); snprintf(area, sizeof area, "%s", a[0] ? a : base); tokAte = ate;
    snprintf(a, sizeof a, "%s", area);
    pthread_mutex_unlock(&trava);
  }
  snprintf(url, sizeof url, "%s/api/lapp/alarm/list", a[0] ? a : base);
  snprintf(corpo, sizeof corpo, "accessToken=%s&pageSize=5&pageStart=0", t);
  r = rede_postar_st(url, 15, cabs, corpo, &st);
  memset(corpo, 0, sizeof corpo);
  n = ezviz_parse_alarmes(r, al, 5);
  free(r);
  if (n < 0) { pthread_mutex_lock(&trava); tok[0] = 0; pthread_mutex_unlock(&trava); printf("[ezviz] lista de alarmes falhou (HTTP %d)\n", st); goto fim; }
  pthread_mutex_lock(&trava);
  if (n > 0 && strcmp(al[0].id, ultimoId)) {
    int primeiro = !ultimoId[0];
    snprintf(ultimoId, sizeof ultimoId, "%s", al[0].id);
    // The first answer only sets the mark: old alarms are not announced.
    if (!primeiro && al[0].quando > (time(NULL) - 120) * 1000LL) { novo = al[0]; temNovo = 1; }
  }
  pthread_mutex_unlock(&trava);
fim:
  fflush(stdout);
  pthread_mutex_lock(&trava); fio = 0; pthread_mutex_unlock(&trava);
  return NULL;
}
void ezviz_passo(Uint32 agora) {
  static Uint32 ult;
  EzAlarme a;
  int tem = 0;
  pthread_mutex_lock(&trava);
  if (temNovo) { a = novo; temNovo = 0; tem = 1; }
  pthread_mutex_unlock(&trava);
  if (tem) {
    IlhaModal m;
    IlhaAvisoEx av;
    char txt[200];
    memset(&m, 0, sizeof m); memset(&av, 0, sizeof av);
    snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Campainha / câmera"));
    snprintf(m.titulo, sizeof m.titulo, "%s", a.nome[0] ? a.nome : i18n("Alguém na porta"));
    snprintf(m.linha, sizeof m.linha, "%s", a.serial);
    if (a.foto[0] && !a.cifrada && !strncmp(a.foto, "https://", 8)) snprintf(m.arte, sizeof m.arte, "%s", a.foto);
    else snprintf(m.icone, sizeof m.icone, "aj_door-open");
    m.nBotoes = 1;
    snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Fechar"));
    snprintf(txt, sizeof txt, "%s: %s", i18n("Campainha"), a.nome[0] ? a.nome : i18n("Alguém na porta"));
    av.chave = "ezviz"; av.tipo = ILHA_ERRO; av.icone = "aj_bell-ring"; av.texto = txt; av.ms = 20000u; av.modal = &m; av.abrir = 1; av.prior = ILHA_P1;
    ilha_avisar_ex(&av);
    printf("[ezviz] alarme novo (tipo %d)\n", a.tipo);
    fflush(stdout);
  }
  if (agora - ult < 15000u) return;
  ult = agora;
  if (!ezviz_configurado()) return;
  pthread_mutex_lock(&trava);
  if (fio) { pthread_mutex_unlock(&trava); return; }
  fio = 1;
  pthread_mutex_unlock(&trava);
  {
    pthread_t t;
    if (pthread_create(&t, NULL, fioPassar, NULL)) { fio = 0; return; }
    pthread_detach(t);
  }
}

// ---------------------------------------------------------------- phone page
static char *pagina(void) {
  char corpo[2400], s[4][220];
  const char *k[4] = { "Campainha ou câmera EZVIZ", "Crie um app de desenvolvedor em open.ezvizlife.com e cole aqui o AppKey e o Secret. A TV avisa a cada toque da campainha ou movimento, enquanto o QR TIME estiver aberto.",
                       "Salvar na TV", "Remover" };
  for (int i = 0; i < 4; i++) lanweb_escapar(s[i], sizeof s[i], i18n(k[i]));
  snprintf(corpo, sizeof corpo,
    "<h2>%s</h2><p><small>%s</small></p><label>AppKey</label><input id=\"k\" autocomplete=\"off\">"
    "<label>Secret</label><input id=\"s\" type=\"password\" autocomplete=\"off\"><button id=\"ok\">%s</button>"
    "<button class=\"s\" id=\"rm\">%s</button><div id=\"msg\" class=\"msg\"></div>", s[0], s[1], s[2], s[3]);
  return celapps_pagina("Casa", corpo,
    "function v(i){return document.getElementById(i).value.trim()}"
    "document.getElementById('ok').onclick=function(){post('ezviz',{chave:v('k'),segredo:v('s')}).then(function(){msg('\\u2713');document.getElementById('s').value=''})};"
    "document.getElementById('rm').onclick=function(){post('ezviz',{chave:''}).then(function(){msg('\\u2713')})};");
}
static void pedido(const LwPedido *p) {
  char k[96], s[96];
  if (strcmp(p->rota, "ezviz")) return;
  lanweb_campo(p->corpo, "chave", k, sizeof k);
  lanweb_campo(p->corpo, "segredo", s, sizeof s);
  if (ezviz_definir(k, s)) ilha_avisar("ezviz-cfg", ILHA_OK, "aj_door-open", i18n(k[0] ? "Campainha EZVIZ ligada" : "Campainha EZVIZ removida"), 3000u, 0);
  memset(k, 0, sizeof k); memset(s, 0, sizeof s);
}
void ezviz_iniciar(void) {
  CelAppDef d = { pagina, pedido, NULL, 1024 };
  celapps_definir(CA_CASA, &d);
}
