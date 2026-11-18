#include "mcspotify.h"
#include "mccat.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static char contas[200] = "https://accounts.spotify.com", api[200] = "https://api.spotify.com/v1";
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static char clienteId[96], segredo[96], token[512];
static long long tokenAte;
static int lido;
void mcsp_definir_base(const char *c, const char *a) {
  if (c && c[0]) snprintf(contas, sizeof contas, "%s", c);
  if (a && a[0]) snprintf(api, sizeof api, "%s", a);
}
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
static void garantir(void) {
  char *t;
  if (lido) return;
  t = dados_ler("spotify.txt");
  lerKV(t ? t : "", "cliente", clienteId, sizeof clienteId);
  lerKV(t ? t : "", "segredo", segredo, sizeof segredo);
  free(t);
  lido = 1;
}
int mcsp_tem_chave(void) { int r; pthread_mutex_lock(&trava); garantir(); r = clienteId[0] && segredo[0]; pthread_mutex_unlock(&trava); return r; }
static int alnum(const char *s, size_t max) {
  if (!s || strlen(s) >= max) return 0;
  for (; *s; s++) if (!isalnum((unsigned char)*s)) return 0;
  return 1;
}
int mcsp_definir_chave(const char *id, const char *seg) {
  char t[300];
  int ok;
  if ((id && id[0] && !alnum(id, sizeof clienteId)) || (seg && seg[0] && !alnum(seg, sizeof segredo))) return 0;
  pthread_mutex_lock(&trava); garantir();
  if (id) snprintf(clienteId, sizeof clienteId, "%s", id);
  if (seg) snprintf(segredo, sizeof segredo, "%s", seg);
  token[0] = 0; tokenAte = 0;
  snprintf(t, sizeof t, "cliente=%s\nsegredo=%s\n", clienteId, segredo);
  ok = dados_gravar("spotify.txt", t);
  pthread_mutex_unlock(&trava);
  return ok;
}
int mcsp_id_playlist(const char *e, char *id, size_t n) {
  const char *p;
  size_t k = 0;
  id[0] = 0;
  if (!e) return 0;
  if ((p = strstr(e, "playlist/"))) p += 9;
  else if ((p = strstr(e, "playlist:"))) p += 9;
  else p = e;
  while (isalnum((unsigned char)p[k]) && k + 1 < n) { id[k] = p[k]; k++; }
  id[k] = 0;
  return k >= 10 && k <= 40;
}
static void b64(const unsigned char *s, size_t n, char *d, size_t dn) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t k = 0;
  for (size_t i = 0; i < n && k + 5 < dn; i += 3) {
    unsigned v = (unsigned)s[i] << 16 | (i + 1 < n ? (unsigned)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
    d[k++] = T[v >> 18 & 63]; d[k++] = T[v >> 12 & 63];
    d[k++] = i + 1 < n ? T[v >> 6 & 63] : '=';
    d[k++] = i + 2 < n ? T[v & 63] : '=';
  }
  d[k] = 0;
}
// Client-credentials token (cached until it expires). 1 ok.
static int pegarToken(char *tok, size_t n, int *erro) {
  char par[200], b[300], cab[340], url[260];
  const char *cabs[3] = { NULL, "Content-Type: application/x-www-form-urlencoded", NULL };
  int st = 0;
  char *r;
  long long agora = (long long)time(NULL);
  pthread_mutex_lock(&trava); garantir();
  if (token[0] && tokenAte - 60 > agora) { snprintf(tok, n, "%s", token); pthread_mutex_unlock(&trava); return 1; }
  if (!clienteId[0] || !segredo[0]) { pthread_mutex_unlock(&trava); *erro = MC_ERRO_LOGIN; return 0; }
  snprintf(par, sizeof par, "%s:%s", clienteId, segredo);
  pthread_mutex_unlock(&trava);
  b64((const unsigned char *)par, strlen(par), b, sizeof b);
  memset(par, 0, sizeof par);
  snprintf(cab, sizeof cab, "Authorization: Basic %s", b);
  cabs[0] = cab;
  snprintf(url, sizeof url, "%s/api/token", contas);
  r = rede_postar_st(url, 15, cabs, "grant_type=client_credentials", &st);
  memset(cab, 0, sizeof cab);
  if (!r) { *erro = MC_ERRO_REDE; return 0; }
  {
    char t[512] = "";
    double exp = js_num(r, NULL, "expires_in", 0);
    js_texto(r, NULL, "access_token", t, sizeof t);
    free(r);
    if (!t[0]) { *erro = st == 400 || st == 401 ? MC_ERRO_LOGIN : MC_ERRO_SERVIDOR; return 0; }
    pthread_mutex_lock(&trava);
    snprintf(token, sizeof token, "%s", t);
    tokenAte = agora + (long long)exp;
    pthread_mutex_unlock(&trava);
    snprintf(tok, n, "%s", t);
  }
  return 1;
}
static char *get(const char *url, int *erro) {
  char tok[512], cab[560];
  const char *cabs[2] = { NULL, NULL };
  int st = 0;
  char *r;
  if (!pegarToken(tok, sizeof tok, erro)) return NULL;
  snprintf(cab, sizeof cab, "Authorization: Bearer %s", tok);
  cabs[0] = cab;
  r = rede_baixar_st(url, 20, cabs, &st);
  memset(cab, 0, sizeof cab); memset(tok, 0, sizeof tok);
  if (!r || st == 0) { free(r); *erro = MC_ERRO_REDE; return NULL; }
  if (st == 401) { pthread_mutex_lock(&trava); token[0] = 0; pthread_mutex_unlock(&trava); free(r); *erro = MC_ERRO_LOGIN; return NULL; }
  if (st == 404 || st == 403) { free(r); *erro = MC_ERRO_NAO_SUPORTA; return NULL; }
  if (st >= 400) { free(r); *erro = MC_ERRO_SERVIDOR; return NULL; }
  *erro = MC_OK;
  return r;
}
int mcsp_playlist(const char *id, char *nome, size_t nn, SpFaixa *out, int max, int *n) {
  char url[700], e[64];
  int er;
  char *r;
  *n = 0;
  nome[0] = 0;
  if (!mcsp_id_playlist(id, e, sizeof e)) return MC_ERRO_OUTRO;
  snprintf(url, sizeof url, "%s/playlists/%s?fields=name", api, e);
  if (!(r = get(url, &er))) return er;
  js_texto_raiz(r, "name", nome, nn);
  free(r);
  for (int off = 0; *n < max; off += 100) {
    const char *p;
    int antes = *n, temMais;
    snprintf(url, sizeof url, "%s/playlists/%s/tracks?fields=items(track(name,duration_ms,album(name),artists(name))),next&limit=100&offset=%d", api, e, off);
    if (!(r = get(url, &er))) return *n ? MC_OK : er;
    for (p = js_array(r, NULL, "items"); p && *p == '{' && *n < max; ) {
      const char *f = js_fim(p), *t, *tf;
      if (!f) break;
      t = strstr(p, "\"track\"");
      if (t && t < f && (t = strchr(t, '{')) && t < f && (tf = js_fim(t))) {
        SpFaixa *x = &out[*n];
        const char *a, *al;
        memset(x, 0, sizeof *x);
        js_texto_raiz_em(t, tf, "name", x->titulo, sizeof x->titulo);
        x->dur = (int)(js_num(t, tf, "duration_ms", 0) / 1000);
        if ((a = js_array(t, tf, "artists")) && *a == '{') js_texto_raiz_em(a, js_fim(a), "name", x->artista, sizeof x->artista);
        if ((al = strstr(t, "\"album\"")) && al < tf && (al = strchr(al, '{')) && al < tf) js_texto_raiz_em(al, js_fim(al), "name", x->album, sizeof x->album);
        if (x->titulo[0]) (*n)++;   // local files and removed tracks come as null
      }
      p = js_prox(f);
    }
    {
      char prox[600] = "";
      temMais = js_texto_raiz(r, "next", prox, sizeof prox) || prox[0];   // a URL (string) or null
    }
    free(r);
    if (!temMais || *n == antes) break;
  }
  return MC_OK;
}
// Lowercase, ASCII letters/digits only; "(feat. ...)", "- Remastered" cut.
static void norm(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; s && *s && k + 1 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '(' || c == '[' || (c == '-' && s[1] == ' ')) break;
    if (isalnum(c)) d[k++] = (char)tolower(c);
    else if (c >= 0x80) d[k++] = (char)c;   // keep non-ASCII (Arabic, accents) as is
  }
  d[k] = 0;
}
int mcsp_mesmo(const char *a, const char *b) {
  char x[200], y[200];
  norm(a, x, sizeof x); norm(b, y, sizeof y);
  return x[0] && !strcmp(x, y);
}
int mcsp_importar(const char *entrada, McFaixa *ac, int max, int *achadas, int *total, char *nome, size_t nn) {
  SpFaixa *v = calloc(500, sizeof *v);
  McFaixa fx[10];
  McArtista ar[1];
  McAlbum al[1];
  int n = 0, r, m = 0;
  *achadas = *total = 0;
  if (!v) return MC_ERRO_OUTRO;
  r = mcsp_playlist(entrada, nome, nn, v, 500, &n);
  if (r != MC_OK) { free(v); return r; }
  *total = n;
  for (int i = 0; i < n && m < max; i++) {
    int a = 0, b = 0, c = 0;
    char q[160];
    // Search by the bare title: "Song (Remastered 2011)" / "Song - Live" -> "Song".
    snprintf(q, sizeof q, "%s", v[i].titulo);
    for (char *x = q; *x; x++) if (*x == '(' || *x == '[' || (*x == '-' && x > q && x[-1] == ' ' && x[1] == ' ')) { *x = 0; break; }
    for (size_t l = strlen(q); l && q[l - 1] == ' '; ) q[--l] = 0;
    if (!q[0] || mccat_buscar(q, ar, 0, &a, al, 0, &b, fx, 10, &c) != MC_OK) continue;
    for (int k = 0; k < c; k++)
      if (mcsp_mesmo(fx[k].titulo, v[i].titulo) && (mcsp_mesmo(fx[k].artista, v[i].artista) || !v[i].artista[0])) { ac[m++] = fx[k]; break; }
  }
  *achadas = m;
  printf("[spotify] playlist lida: %d de %d faixa(s) na sua biblioteca\n", m, n);
  fflush(stdout);
  free(v);
  return MC_OK;
}
