#include "mclfm.h"
#include "dados.h"
#include "js.h"
#include "md5.h"
#include "perfis.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char base[200] = "https://ws.audioscrobbler.com/2.0/";
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static char chave[64], segredo[64], sessao[96], usuario[96], token[96];
static int perfilLido = -1;
void mclfm_definir_base(const char *u) { if (u && u[0]) snprintf(base, sizeof base, "%s", u); }

static void lerKV(const char *t, const char *k, char *d, size_t n) {
  size_t kl = strlen(k);
  d[0] = 0;
  for (const char *p = t; p && *p;) {
    const char *f = strchr(p, '\n');
    size_t len = f ? (size_t)(f - p) : strlen(p);
    if (len > kl && !strncmp(p, k, kl) && p[kl] == '=') snprintf(d, n, "%.*s", (int)(len - kl - 1), p + kl + 1);
    p = f ? f + 1 : NULL;
  }
}
static void arqSessao(char *d, size_t n, const char *base_) { int p = perfis_ativo(); snprintf(d, n, "lastfm-%s-p%d.txt", base_, p > 0 ? p : 1); }
static void carregarLocked(void) {
  char nome[48], *t = dados_ler("lastfm.txt");
  lerKV(t ? t : "", "chave", chave, sizeof chave);
  lerKV(t ? t : "", "segredo", segredo, sizeof segredo);
  free(t);
  arqSessao(nome, sizeof nome, "sessao");
  t = dados_ler(nome);
  lerKV(t ? t : "", "sk", sessao, sizeof sessao);
  lerKV(t ? t : "", "usuario", usuario, sizeof usuario);
  free(t);
  perfilLido = perfis_ativo();
}
static void garantir(void) { if (perfilLido != perfis_ativo()) carregarLocked(); }
void mclfm_carregar(void) { pthread_mutex_lock(&trava); carregarLocked(); pthread_mutex_unlock(&trava); }
int mclfm_tem_chave(void) { int r; pthread_mutex_lock(&trava); garantir(); r = chave[0] && segredo[0]; pthread_mutex_unlock(&trava); return r; }
int mclfm_conectado(void) { int r; pthread_mutex_lock(&trava); garantir(); r = sessao[0] != 0; pthread_mutex_unlock(&trava); return r; }
void mclfm_usuario(char *d, size_t n) { pthread_mutex_lock(&trava); garantir(); snprintf(d, n, "%s", usuario); pthread_mutex_unlock(&trava); }
static int limpo(const char *s, size_t max) {
  if (!s || strlen(s) >= max) return 0;
  for (; *s; s++) if (!isalnum((unsigned char)*s)) return 0;   // Last.fm keys and secrets are hex
  return 1;
}
int mclfm_definir_chave(const char *k, const char *s) {
  char t[200];
  int ok;
  if ((k && k[0] && !limpo(k, sizeof chave)) || (s && s[0] && !limpo(s, sizeof segredo))) return 0;
  pthread_mutex_lock(&trava); garantir();
  if (k) snprintf(chave, sizeof chave, "%s", k);
  if (s) snprintf(segredo, sizeof segredo, "%s", s);
  snprintf(t, sizeof t, "chave=%s\nsegredo=%s\n", chave, segredo);
  ok = dados_gravar("lastfm.txt", t);
  pthread_mutex_unlock(&trava);
  return ok;
}
static void gravarSessaoLocked(void) {
  char nome[48], t[300];
  arqSessao(nome, sizeof nome, "sessao");
  if (!sessao[0]) { dados_apagar(nome); return; }
  snprintf(t, sizeof t, "sk=%s\nusuario=%s\n", sessao, usuario);
  dados_gravar(nome, t);
}
void mclfm_sair(void) {
  pthread_mutex_lock(&trava); garantir();
  sessao[0] = usuario[0] = token[0] = 0;
  gravarSessaoLocked();
  pthread_mutex_unlock(&trava);
}

// ---------------------------------------------------------------- signing
static int cmpStr(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
void mclfm_assinar(const char *const *pares, const char *seg, char out[33]) {
  const char *v[40];
  char buf[6000];
  size_t k = 0;
  int n = 0;
  for (; pares[n] && n < 40; n++) v[n] = pares[n];
  qsort(v, (size_t)n, sizeof *v, cmpStr);
  for (int i = 0; i < n && k < sizeof buf; i++) {
    const char *eq = strchr(v[i], '=');
    if (!eq) continue;
    // "format" and "callback" are not signed (Last.fm rule).
    if (!strncmp(v[i], "format=", 7) || !strncmp(v[i], "callback=", 9)) continue;
    k += (size_t)snprintf(buf + k, sizeof buf - k, "%.*s%s", (int)(eq - v[i]), v[i], eq + 1);
  }
  if (k < sizeof buf) k += (size_t)snprintf(buf + k, sizeof buf - k, "%s", seg);
  md5_hex(buf, k < sizeof buf ? k : sizeof buf - 1, out);
  memset(buf, 0, sizeof buf);
}
static void urlenc(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; s && *s && k + 4 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') d[k++] = (char)c;
    else k += (size_t)snprintf(d + k, n - k, "%%%02X", c);
  }
  d[k] = 0;
}
// Signed call: pares "k=v" (raw values); method included. POST for writes.
static char *chamar(const char *const *pares, int assinar, int post, int *st) {
  char k[64], s[64], sig[33], corpo[6000], url[6400];
  const char *v[44];
  size_t pos = 0;
  int n = 0;
  char *r;
  pthread_mutex_lock(&trava); garantir(); snprintf(k, sizeof k, "%s", chave); snprintf(s, sizeof s, "%s", segredo); pthread_mutex_unlock(&trava);
  if (!k[0]) return NULL;
  {
    static __thread char par[64];
    snprintf(par, sizeof par, "api_key=%s", k);
    v[n++] = par;
  }
  for (int i = 0; pares[i] && n < 40; i++) v[n++] = pares[i];
  v[n] = NULL;
  if (assinar) mclfm_assinar(v, s, sig);
  for (int i = 0; i < n; i++) {
    const char *eq = strchr(v[i], '=');
    char e[1200];
    if (!eq) continue;
    urlenc(eq + 1, e, sizeof e);
    pos += (size_t)snprintf(corpo + pos, sizeof corpo - pos, "%s%.*s=%s", pos ? "&" : "", (int)(eq - v[i]), v[i], e);
  }
  if (assinar) pos += (size_t)snprintf(corpo + pos, sizeof corpo - pos, "&api_sig=%s", sig);
  pos += (size_t)snprintf(corpo + pos, sizeof corpo - pos, "&format=json");
  memset(s, 0, sizeof s);
  if (post) {
    static const char *const cab[] = { "Content-Type: application/x-www-form-urlencoded", NULL };
    r = rede_postar_st(base, 15, cab, corpo, st);
  } else {
    snprintf(url, sizeof url, "%s?%s", base, corpo);
    r = rede_baixar_st(url, 15, NULL, st);
    memset(url, 0, sizeof url);
  }
  memset(corpo, 0, sizeof corpo);
  return r;
}
static int erroDe(const char *r) {
  double e = r ? js_num(r, NULL, "error", 0) : 0;
  return (int)e;
}

// ---------------------------------------------------------------- sign-in
int mclfm_entrar_iniciar(char *url, size_t n) {
  const char *p[] = { "method=auth.getToken", NULL };
  int st = 0;
  char *r, tk[96] = "", k[64];
  url[0] = 0;
  if (!mclfm_tem_chave()) return MC_ERRO_LOGIN;
  r = chamar(p, 1, 0, &st);
  if (!r) return MC_ERRO_REDE;
  js_texto(r, NULL, "token", tk, sizeof tk);
  {
    int e = erroDe(r);
    free(r);
    if (!tk[0]) return e == 10 || e == 26 || e == 13 ? MC_ERRO_LOGIN : MC_ERRO_SERVIDOR;
  }
  pthread_mutex_lock(&trava);
  snprintf(token, sizeof token, "%s", tk);
  snprintf(k, sizeof k, "%s", chave);
  pthread_mutex_unlock(&trava);
  snprintf(url, n, "https://www.last.fm/api/auth/?api_key=%s&token=%s", k, tk);
  return MC_OK;
}
int mclfm_entrar_conferir(void) {
  char tk[96], par[120], sk[96] = "", nome[96] = "";
  const char *p[3];
  int st = 0, e;
  char *r;
  pthread_mutex_lock(&trava); snprintf(tk, sizeof tk, "%s", token); pthread_mutex_unlock(&trava);
  if (!tk[0]) return -1;
  snprintf(par, sizeof par, "token=%s", tk);
  p[0] = "method=auth.getSession"; p[1] = par; p[2] = NULL;
  r = chamar(p, 1, 0, &st);
  if (!r) return 0;
  js_texto(r, NULL, "key", sk, sizeof sk);
  js_texto(r, NULL, "name", nome, sizeof nome);
  e = erroDe(r);
  free(r);
  if (sk[0]) {
    pthread_mutex_lock(&trava);
    snprintf(sessao, sizeof sessao, "%s", sk);
    snprintf(usuario, sizeof usuario, "%s", nome);
    token[0] = 0;
    gravarSessaoLocked();
    pthread_mutex_unlock(&trava);
    printf("[lastfm] conectado neste perfil\n");
    fflush(stdout);
    return 1;
  }
  if (e == 14) return 0;   // not yet authorized
  pthread_mutex_lock(&trava); token[0] = 0; pthread_mutex_unlock(&trava);
  return -1;
}

// ---------------------------------------------------------------- scrobbling
static int temSessao(char *sk, size_t n) { pthread_mutex_lock(&trava); garantir(); snprintf(sk, n, "%s", sessao); pthread_mutex_unlock(&trava); return sk[0] != 0; }
static int enviarFaixa(const char *metodo, const McFaixa *f, long long quandoS, char *skv) {
  char m[64], a[300], t[300], al[300], d[32], ts[32], sk[120];
  const char *p[8];
  int n = 0, st = 0, e;
  char *r;
  snprintf(m, sizeof m, "method=%s", metodo);
  snprintf(a, sizeof a, "artist=%s", f->artista);
  snprintf(t, sizeof t, "track=%s", f->titulo);
  snprintf(sk, sizeof sk, "sk=%s", skv);
  p[n++] = m; p[n++] = a; p[n++] = t; p[n++] = sk;
  if (f->album[0]) { snprintf(al, sizeof al, "album=%s", f->album); p[n++] = al; }
  if (f->dur > 0) { snprintf(d, sizeof d, "duration=%d", f->dur); p[n++] = d; }
  if (quandoS > 0) { snprintf(ts, sizeof ts, "timestamp=%lld", quandoS); p[n++] = ts; }
  p[n] = NULL;
  r = chamar(p, 1, 1, &st);
  memset(sk, 0, sizeof sk);
  if (!r) return MC_ERRO_REDE;
  e = erroDe(r);
  free(r);
  if (e == 9) { printf("[lastfm] sessao invalida; conecte de novo\n"); fflush(stdout); mclfm_sair(); return MC_ERRO_LOGIN; }
  if (e == 11 || e == 16 || e == 29 || st >= 500) return MC_ERRO_REDE;   // service offline / temporary / rate limit: retry later
  return e ? MC_ERRO_SERVIDOR : MC_OK;
}
void mclfm_agora(const McFaixa *f) {
  char sk[96];
  if (!f->artista[0] || !f->titulo[0] || !temSessao(sk, sizeof sk)) return;
  enviarFaixa("track.updateNowPlaying", f, 0, sk);
  memset(sk, 0, sizeof sk);
}
static pthread_mutex_t travaP = PTHREAD_MUTEX_INITIALIZER;
static void arqPend(char *d, size_t n) { arqSessao(d, n, "pendentes"); }
int mclfm_pendentes(void) {
  char nome[48], *t;
  int n = 0;
  arqPend(nome, sizeof nome);
  pthread_mutex_lock(&travaP);
  t = dados_ler(nome);
  for (char *p = t; p && *p; p++) if (*p == '\n') n++;
  free(t);
  pthread_mutex_unlock(&travaP);
  return n;
}
#include "mcfila.h"
void mclfm_tocou(const McFaixa *f, long long inicioMs) {
  char sk[96], nome[48], lin[2600], *t, *nova;
  size_t cap, k = 0;
  int ok = 1, pend = 0;
  if (!f->artista[0] || !f->titulo[0] || !temSessao(sk, sizeof sk)) return;
  arqPend(nome, sizeof nome);
  pthread_mutex_lock(&travaP);
  t = dados_ler(nome);
  cap = (t ? strlen(t) : 0) + 2700;
  nova = calloc(1, cap);
  // Older ones first (Last.fm keeps the order by timestamp anyway).
  for (char *p = t, *sp = NULL, *l = t ? strtok_r(p, "\n", &sp) : NULL; l; l = strtok_r(NULL, "\n", &sp)) {
    long long q = atoll(l);
    McFaixa x;
    char *tab = strchr(l, '\t');
    if (!tab || !mcfila_ler_linha(tab + 1, &x)) continue;
    if (ok && enviarFaixa("track.scrobble", &x, q, sk) == MC_ERRO_REDE) ok = 0;
    if (!ok && nova && pend < 500) { k += (size_t)snprintf(nova + k, cap - k, "%s\n", l); *tab = '\t'; pend++; }
  }
  if (ok && enviarFaixa("track.scrobble", f, inicioMs / 1000, sk) == MC_ERRO_REDE) ok = 0;
  if (!ok && nova && mcfila_linha(f, lin, sizeof lin)) { k += (size_t)snprintf(nova + k, cap - k, "%lld\t%s\n", inicioMs / 1000, lin); pend++; }
  if (pend) { dados_gravar(nome, nova); printf("[lastfm] %d scrobble(s) aguardando a rede\n", pend); }
  else dados_apagar(nome);
  fflush(stdout);
  pthread_mutex_unlock(&travaP);
  free(t);
  free(nova);
  memset(sk, 0, sizeof sk);
}

// ---------------------------------------------------------------- metadata
int mclfm_artista(const char *nome, char *bio, size_t nb, char *img, size_t ni) {
  char a[300];
  const char *p[4];
  int st = 0;
  char *r;
  bio[0] = img[0] = 0;
  snprintf(a, sizeof a, "artist=%s", nome);
  p[0] = "method=artist.getInfo"; p[1] = a; p[2] = "autocorrect=1"; p[3] = NULL;
  r = chamar(p, 0, 0, &st);
  if (!r) return mclfm_tem_chave() ? MC_ERRO_REDE : MC_ERRO_LOGIN;
  {
    const char *b = strstr(r, "\"bio\"");
    if (b) js_texto(b, NULL, "summary", bio, nb);
    // Strip the "<a href=...>Read more on Last.fm</a>" tail.
    { char *lt = strstr(bio, " <a href"); if (lt) *lt = 0; }
  }
  free(r);
  (void)ni;
  return bio[0] ? MC_OK : MC_ERRO_NAO_SUPORTA;
}
int mclfm_similares(const char *artista, char nomes[][128], int max) {
  char a[300], lim[32];
  const char *p[5];
  int st = 0, n = 0;
  char *r;
  const char *it;
  snprintf(a, sizeof a, "artist=%s", artista);
  snprintf(lim, sizeof lim, "limit=%d", max);
  p[0] = "method=artist.getSimilar"; p[1] = a; p[2] = lim; p[3] = "autocorrect=1"; p[4] = NULL;
  r = chamar(p, 0, 0, &st);
  if (!r) return 0;
  for (it = js_array(r, NULL, "artist"); it && *it == '{' && n < max; ) {
    const char *f = js_fim(it);
    if (!f) break;
    if (js_texto_raiz_em(it, f, "name", nomes[n], 128) && nomes[n][0]) n++;
    it = js_prox(f);
  }
  free(r);
  return n;
}
