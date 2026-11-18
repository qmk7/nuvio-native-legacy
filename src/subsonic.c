#include "subsonic.h"
#include "md5.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SS_VERSAO "1.16.1"
#define SS_CLIENTE "qrtime"
#define SS_PRAZO 20

static char salTeste[32];
static pthread_mutex_t travaSal = PTHREAD_MUTEX_INITIALIZER;
void ss_teste_sal(const char *sal) {
  pthread_mutex_lock(&travaSal);
  snprintf(salTeste, sizeof salTeste, "%s", sal ? sal : "");
  pthread_mutex_unlock(&travaSal);
}

// ---------------------------------------------------------------- helpers
static void urlenc(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; s && *s && k + 4 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') d[k++] = (char)c;
    else k += (size_t)snprintf(d + k, n - k, "%%%02X", c);
  }
  d[k] = 0;
}
static void novoSal(char s[17]) {
  static const char hx[] = "0123456789abcdef";
  unsigned char b[8];
  int ok = 0;
  pthread_mutex_lock(&travaSal);
  if (salTeste[0]) { snprintf(s, 17, "%s", salTeste); pthread_mutex_unlock(&travaSal); return; }
  pthread_mutex_unlock(&travaSal);
  {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { ok = read(fd, b, sizeof b) == (ssize_t)sizeof b; close(fd); }
  }
  if (!ok) { unsigned v = (unsigned)time(NULL) ^ (unsigned)(size_t)s; for (int i = 0; i < 8; i++) { v = v * 1103515245u + 12345u; b[i] = (unsigned char)(v >> 16); } }
  for (int i = 0; i < 8; i++) { s[i * 2] = hx[b[i] >> 4]; s[i * 2 + 1] = hx[b[i] & 15]; }
  s[16] = 0;
}
// <url>/rest/<metodo>?<auth>&v=..&c=..&f=json<extra>. `legado`: p=enc:<hex>.
static __thread const char *salFixo;   // covers: a stable salt (see ss_url_capa)
static void montar(const SsServidor *s, const char *metodo, const char *extra, int legado, char *url, size_t n) {
  char u[300], base[260], aut[200];
  size_t bl;
  snprintf(base, sizeof base, "%s", s->url);
  bl = strlen(base);
  while (bl && base[bl - 1] == '/') base[--bl] = 0;
  urlenc(s->usuario, u, sizeof u);
  if (legado) {
    static const char hx[] = "0123456789abcdef";
    size_t k = 0;
    k += (size_t)snprintf(aut, sizeof aut, "p=enc:");
    for (const unsigned char *p = (const unsigned char *)s->senha; *p && k + 3 < sizeof aut; p++) { aut[k++] = hx[*p >> 4]; aut[k++] = hx[*p & 15]; }
    aut[k] = 0;
  } else {
    char sal[17], mat[160], tok[33];
    if (salFixo) snprintf(sal, sizeof sal, "%s", salFixo); else novoSal(sal);
    snprintf(mat, sizeof mat, "%s%s", s->senha, sal);
    md5_hex(mat, strlen(mat), tok);
    snprintf(aut, sizeof aut, "t=%s&s=%s", tok, sal);
  }
  snprintf(url, n, "%s/rest/%s?u=%s&%s&v=" SS_VERSAO "&c=" SS_CLIENTE "&f=json%s", base, metodo, u, aut, extra ? extra : "");
}
void ss_redigir(const char *url, char *d, size_t n) {
  size_t k = 0;
  const char *p = url;
  while (p && *p && k + 1 < n) {
    if ((*p == '?' || *p == '&') && (!strncmp(p + 1, "u=", 2) || !strncmp(p + 1, "t=", 2) || !strncmp(p + 1, "s=", 2) ||
                                     !strncmp(p + 1, "p=", 2) || !strncmp(p + 1, "apiKey=", 7))) {
      const char *eq = strchr(p, '=');
      size_t pre = (size_t)(eq - p) + 1;
      if (k + pre + 4 >= n) break;
      memcpy(d + k, p, pre); k += pre;
      memcpy(d + k, "***", 3); k += 3;
      p = eq + 1;
      while (*p && *p != '&') p++;
      continue;
    }
    d[k++] = *p++;
  }
  d[k] = 0;
}
void ss_url_stream(const SsServidor *s, const char *id, char *url, size_t n) {
  char e[200], x[260];
  urlenc(id, e, sizeof e);
  // format=raw + maxBitRate=0: the original file, never a transcode.
  snprintf(x, sizeof x, "&id=%s&format=raw&maxBitRate=0", e);
  montar(s, "stream", x, 0, url, n);
}
void ss_url_capa(const SsServidor *s, const char *capaId, int tamanho, char *url, size_t n) {
  char e[200], x[260];
  if (!capaId || !capaId[0]) { url[0] = 0; return; }
  urlenc(capaId, e, sizeof e);
  snprintf(x, sizeof x, "&id=%s&size=%d", e, tamanho > 0 ? tamanho : 600);
  {
    char m[400], h[33];
    snprintf(m, sizeof m, "qrtcover|%s|%s|%s", capaId, s->usuario, s->senha);
    md5_hex(m, strlen(m), h);
    h[16] = 0;
    salFixo = h;
    montar(s, "getCoverArt", x, 0, url, n);
    salFixo = NULL;
    memset(m, 0, sizeof m);
  }
}

// Depth-1 number/flag of an object [o, fim): js_num would match a nested key
// first (OpenSubsonic adds "artists":[{"id":..}], "replayGain":{..}).
static const char *valorRaiz(const char *o, const char *fim, const char *chave) {
  int prof = 0;
  size_t kl = strlen(chave);
  for (const char *p = o; *p && (!fim || p < fim); p++) {
    if (*p == '"') {
      const char *ini = p + 1, *q = ini;
      while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
      if (!*q) return NULL;
      if (prof == 1 && (size_t)(q - ini) == kl && !strncmp(ini, chave, kl)) {
        const char *v = q + 1;
        while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
        if (*v == ':') { v++; while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++; return v; }
      }
      p = q;
      continue;
    }
    if (*p == '{' || *p == '[') prof++;
    else if (*p == '}' || *p == ']') { prof--; if (prof <= 0) return NULL; }
  }
  return NULL;
}
static long long numR(const char *o, const char *fim, const char *chave) {
  const char *v = valorRaiz(o, fim, chave);
  if (!v) return 0;
  if (*v == '"') v++;   // some servers quote numbers
  return (*v == '-' || isdigit((unsigned char)*v)) ? atoll(v) : 0;
}
static int textoR(const char *o, const char *fim, const char *chave, char *d, size_t n) {
  d[0] = 0;
  if (js_texto_raiz_em(o, fim, chave, d, n)) return 1;
  // Numeric ids (older Subsonic servers): keep them as text.
  {
    const char *v = valorRaiz(o, fim, chave);
    if (v && (isdigit((unsigned char)*v) || *v == '-')) {
      size_t k = 0;
      while ((isdigit((unsigned char)*v) || *v == '-') && k + 1 < n) d[k++] = *v++;
      d[k] = 0;
      return 1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------- parsers
int ss_parse_estado(const char *json, int *codigo, char *msg, size_t nmsg) {
  const char *r, *rf, *e;
  char st[16] = "";
  if (codigo) *codigo = 0;
  if (msg && nmsg) msg[0] = 0;
  if (!json || !(r = strstr(json, "\"subsonic-response\""))) return MC_ERRO_OUTRO;
  r = strchr(r, '{');
  if (!r || !(rf = js_fim(r))) return MC_ERRO_OUTRO;
  js_texto_raiz_em(r, rf, "status", st, sizeof st);
  if (!strcmp(st, "ok")) return MC_OK;
  e = valorRaiz(r, rf, "error");
  if (e && *e == '{') {
    const char *ef = js_fim(e);
    int c = (int)numR(e, ef, "code");
    if (codigo) *codigo = c;
    if (msg && nmsg) js_texto_raiz_em(e, ef, "message", msg, nmsg);
    if (c == 40 || c == 41 || c == 44) return MC_ERRO_LOGIN;
    if (c == 30 || c == 70) return c == 70 ? MC_ERRO_NAO_SUPORTA : MC_ERRO_SERVIDOR;
    if (c == 0 || c == 10) return MC_ERRO_SERVIDOR;
  }
  return MC_ERRO_SERVIDOR;
}
int ss_parse_faixa(const char *o, const char *fim, McFaixa *f) {
  char v[32];
  memset(f, 0, sizeof *f);
  textoR(o, fim, "id", f->id, sizeof f->id);
  js_texto_raiz_em(o, fim, "title", f->titulo, sizeof f->titulo);
  js_texto_raiz_em(o, fim, "artist", f->artista, sizeof f->artista);
  js_texto_raiz_em(o, fim, "album", f->album, sizeof f->album);
  textoR(o, fim, "albumId", f->albumId, sizeof f->albumId);
  textoR(o, fim, "artistId", f->artistaId, sizeof f->artistaId);
  textoR(o, fim, "coverArt", f->capa, sizeof f->capa);
  js_texto_raiz_em(o, fim, "genre", f->genero, sizeof f->genero);
  js_texto_raiz_em(o, fim, "suffix", f->formato, sizeof f->formato);
  js_texto_raiz_em(o, fim, "contentType", f->mime, sizeof f->mime);
  for (char *c = f->formato; *c; c++) *c = (char)tolower((unsigned char)*c);
  f->faixa = (int)numR(o, fim, "track");
  f->disco = (int)numR(o, fim, "discNumber");
  f->ano = (int)numR(o, fim, "year");
  f->dur = (int)numR(o, fim, "duration");
  f->kbps = (int)numR(o, fim, "bitRate");
  f->tam = numR(o, fim, "size");
  // OpenSubsonic: only what the server measured; absent stays 0.
  f->taxa = (int)numR(o, fim, "samplingRate");
  f->bits = (int)numR(o, fim, "bitDepth");
  f->canais = (int)numR(o, fim, "channelCount");
  f->fav = js_texto_raiz_em(o, fim, "starred", v, sizeof v) && v[0];
  {
    char tipo[16] = "";
    if (js_texto_raiz_em(o, fim, "type", tipo, sizeof tipo) && tipo[0] && strcmp(tipo, "music")) return 0;   // podcasts, videos
    if (valorRaiz(o, fim, "isVideo") && !strncmp(valorRaiz(o, fim, "isVideo"), "true", 4)) return 0;
  }
  return f->id[0] && f->titulo[0];
}
int ss_parse_faixas(const char *json, const char *chave, McFaixa *out, int max) {
  const char *p = json ? js_array(json, NULL, chave) : NULL;
  int n = 0;
  while (p && *p == '{' && n < max) {
    const char *f = js_fim(p);
    if (!f) break;
    if (ss_parse_faixa(p, f, &out[n])) n++;
    p = js_prox(f);
  }
  return n;
}
static int parseAlbum(const char *o, const char *fim, McAlbum *a) {
  char v[32];
  memset(a, 0, sizeof *a);
  textoR(o, fim, "id", a->id, sizeof a->id);
  if (!js_texto_raiz_em(o, fim, "name", a->nome, sizeof a->nome)) js_texto_raiz_em(o, fim, "title", a->nome, sizeof a->nome);
  js_texto_raiz_em(o, fim, "artist", a->artista, sizeof a->artista);
  textoR(o, fim, "artistId", a->artistaId, sizeof a->artistaId);
  textoR(o, fim, "coverArt", a->capa, sizeof a->capa);
  js_texto_raiz_em(o, fim, "genre", a->genero, sizeof a->genero);
  a->ano = (int)numR(o, fim, "year");
  a->faixas = (int)numR(o, fim, "songCount");
  a->dur = (int)numR(o, fim, "duration");
  a->fav = js_texto_raiz_em(o, fim, "starred", v, sizeof v) && v[0];
  return a->id[0] && a->nome[0];
}
static int parseAlbuns(const char *json, const char *chave, McAlbum *out, int max) {
  const char *p = js_array(json, NULL, chave);
  int n = 0;
  while (p && *p == '{' && n < max) {
    const char *f = js_fim(p);
    if (!f) break;
    if (parseAlbum(p, f, &out[n])) n++;
    p = js_prox(f);
  }
  return n;
}
static int parseArtista(const char *o, const char *fim, McArtista *a) {
  char v[32];
  memset(a, 0, sizeof *a);
  textoR(o, fim, "id", a->id, sizeof a->id);
  js_texto_raiz_em(o, fim, "name", a->nome, sizeof a->nome);
  textoR(o, fim, "coverArt", a->capa, sizeof a->capa);
  a->albuns = (int)numR(o, fim, "albumCount");
  a->fav = js_texto_raiz_em(o, fim, "starred", v, sizeof v) && v[0];
  return a->id[0] && a->nome[0];
}

// ---------------------------------------------------------------- requests
// GET a method; on token refusal (41) over https, once more with the legacy
// form. Returns the body (caller frees) or NULL with *erro set.
static char *chamar(const SsServidor *s, const char *metodo, const char *extra, int *erro) {
  char url[2048], red[2048];
  int st = 0, cod = 0, legado = 0;
  char *r;
  *erro = MC_OK;
  if (!s || !s->url[0] || !s->usuario[0]) { *erro = MC_ERRO_LOGIN; return NULL; }
de_novo:
  montar(s, metodo, extra, legado, url, sizeof url);
  r = rede_baixar_st(url, SS_PRAZO, NULL, &st);
  if (!r || st == 0) {
    ss_redigir(url, red, sizeof red);
    printf("[subsonic] sem resposta: %s\n", red);
    fflush(stdout);
    free(r);
    *erro = MC_ERRO_REDE;
    return NULL;
  }
  *erro = ss_parse_estado(r, &cod, NULL, 0);
  if (*erro == MC_ERRO_OUTRO && st == 401) *erro = MC_ERRO_LOGIN;
  if (*erro == MC_ERRO_OUTRO && st >= 400) *erro = st == 404 ? MC_ERRO_NAO_SUPORTA : MC_ERRO_SERVIDOR;
  if (*erro == MC_ERRO_LOGIN && cod == 41 && !legado && !strncmp(s->url, "https://", 8)) {
    free(r);
    legado = 1;
    goto de_novo;
  }
  if (*erro != MC_OK) {
    printf("[subsonic] %s: erro %d (http %d, codigo %d)\n", metodo, *erro, st, cod);
    fflush(stdout);
    free(r);
    return NULL;
  }
  return r;
}
int ss_ping(const SsServidor *s, SsInfo *info) {
  int e;
  char *r = chamar(s, "ping", NULL, &e);
  if (info) memset(info, 0, sizeof *info);
  if (!r) return e;
  if (info) {
    const char *o = strchr(strstr(r, "\"subsonic-response\""), '{'), *of = o ? js_fim(o) : NULL;
    const char *v;
    js_texto_raiz_em(o, of, "type", info->tipo, sizeof info->tipo);
    js_texto_raiz_em(o, of, "serverVersion", info->versao, sizeof info->versao);
    js_texto_raiz_em(o, of, "version", info->apiVersao, sizeof info->apiVersao);
    v = valorRaiz(o, of, "openSubsonic");
    info->openSubsonic = v && !strncmp(v, "true", 4);
    if (!info->tipo[0]) snprintf(info->tipo, sizeof info->tipo, "subsonic");
  }
  free(r);
  return MC_OK;
}
int ss_artistas(const SsServidor *s, McArtista *out, int max, int *n) {
  int e;
  char *r = chamar(s, "getArtists", NULL, &e);
  const char *ix;
  *n = 0;
  if (!r) return e;
  // artists.index[].artist[]
  ix = js_array(r, NULL, "index");
  while (ix && *ix == '{' && *n < max) {
    const char *ixf = js_fim(ix), *p;
    if (!ixf) break;
    p = js_array(ix, ixf, "artist");
    while (p && p < ixf && *p == '{' && *n < max) {
      const char *f = js_fim(p);
      if (!f) break;
      if (parseArtista(p, f, &out[*n])) (*n)++;
      p = js_prox(f);
    }
    ix = js_prox(ixf);
  }
  free(r);
  return MC_OK;
}
static void param(char *x, size_t n, const char *nome, const char *v) {
  char e[400];
  size_t k = strlen(x);
  urlenc(v, e, sizeof e);
  snprintf(x + k, n - k, "&%s=%s", nome, e);
}
int ss_artista_albuns(const SsServidor *s, const char *id, McAlbum *out, int max, int *n) {
  char x[300] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", id);
  r = chamar(s, "getArtist", x, &e);
  *n = 0;
  if (!r) return e;
  *n = parseAlbuns(r, "album", out, max);
  free(r);
  return MC_OK;
}
int ss_albuns(const SsServidor *s, const char *tipo, const char *genero, int offset, McAlbum *out, int max, int *n) {
  char x[400] = "";
  int e;
  char *r;
  param(x, sizeof x, "type", tipo && tipo[0] ? tipo : "newest");
  if (genero && genero[0]) param(x, sizeof x, "genre", genero);
  snprintf(x + strlen(x), sizeof x - strlen(x), "&size=%d&offset=%d", max > 500 ? 500 : max, offset < 0 ? 0 : offset);
  r = chamar(s, "getAlbumList2", x, &e);
  *n = 0;
  if (!r) return e;
  *n = parseAlbuns(r, "album", out, max);
  free(r);
  return MC_OK;
}
int ss_album(const SsServidor *s, const char *id, McAlbum *info, McFaixa *out, int max, int *n) {
  char x[300] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", id);
  r = chamar(s, "getAlbum", x, &e);
  *n = 0;
  if (!r) return e;
  if (info) {
    const char *a = valorRaiz(strchr(strstr(r, "\"subsonic-response\""), '{'), NULL, "album");
    if (a && *a == '{') parseAlbum(a, js_fim(a), info);
  }
  *n = ss_parse_faixas(r, "song", out, max);
  free(r);
  return MC_OK;
}
int ss_generos(const SsServidor *s, McGenero *out, int max, int *n) {
  int e;
  char *r = chamar(s, "getGenres", NULL, &e);
  const char *p;
  *n = 0;
  if (!r) return e;
  p = js_array(r, NULL, "genre");
  while (p && *p == '{' && *n < max) {
    const char *f = js_fim(p);
    McGenero *g = &out[*n];
    if (!f) break;
    memset(g, 0, sizeof *g);
    js_texto_raiz_em(p, f, "value", g->nome, sizeof g->nome);
    g->faixas = (int)numR(p, f, "songCount");
    g->albuns = (int)numR(p, f, "albumCount");
    if (g->nome[0]) (*n)++;
    p = js_prox(f);
  }
  free(r);
  return MC_OK;
}
int ss_genero_faixas(const SsServidor *s, const char *genero, int offset, McFaixa *out, int max, int *n) {
  char x[400] = "";
  int e;
  char *r;
  param(x, sizeof x, "genre", genero);
  snprintf(x + strlen(x), sizeof x - strlen(x), "&count=%d&offset=%d", max > 500 ? 500 : max, offset < 0 ? 0 : offset);
  r = chamar(s, "getSongsByGenre", x, &e);
  *n = 0;
  if (!r) return e;
  *n = ss_parse_faixas(r, "song", out, max);
  free(r);
  return MC_OK;
}
static int parsePlaylist(const char *o, const char *fim, McPlaylist *p) {
  const char *v;
  memset(p, 0, sizeof *p);
  textoR(o, fim, "id", p->id, sizeof p->id);
  js_texto_raiz_em(o, fim, "name", p->nome, sizeof p->nome);
  js_texto_raiz_em(o, fim, "owner", p->dono, sizeof p->dono);
  textoR(o, fim, "coverArt", p->capa, sizeof p->capa);
  p->faixas = (int)numR(o, fim, "songCount");
  p->dur = (int)numR(o, fim, "duration");
  v = valorRaiz(o, fim, "public");
  p->publica = v && !strncmp(v, "true", 4);
  return p->id[0] && p->nome[0];
}
int ss_playlists(const SsServidor *s, McPlaylist *out, int max, int *n) {
  int e;
  char *r = chamar(s, "getPlaylists", NULL, &e);
  const char *p;
  *n = 0;
  if (!r) return e;
  p = js_array(r, NULL, "playlist");
  while (p && *p == '{' && *n < max) {
    const char *f = js_fim(p);
    if (!f) break;
    if (parsePlaylist(p, f, &out[*n])) (*n)++;
    p = js_prox(f);
  }
  free(r);
  return MC_OK;
}
int ss_playlist(const SsServidor *s, const char *id, McPlaylist *info, McFaixa *out, int max, int *n) {
  char x[300] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", id);
  r = chamar(s, "getPlaylist", x, &e);
  *n = 0;
  if (!r) return e;
  if (info) {
    const char *a = valorRaiz(strchr(strstr(r, "\"subsonic-response\""), '{'), NULL, "playlist");
    if (a && *a == '{') parsePlaylist(a, js_fim(a), info);
  }
  *n = ss_parse_faixas(r, "entry", out, max);
  free(r);
  return MC_OK;
}
int ss_playlist_criar(const SsServidor *s, const char *nome, const char *const *ids, int n, char *novoId, size_t nn) {
  size_t cap = 400 + (size_t)n * 220;
  char *x = calloc(1, cap);
  int e;
  char *r;
  if (novoId && nn) novoId[0] = 0;
  if (!x) return MC_ERRO_OUTRO;
  param(x, cap, "name", nome);
  for (int i = 0; i < n; i++) param(x, cap, "songId", ids[i]);
  r = chamar(s, "createPlaylist", x, &e);
  free(x);
  if (!r) return e;
  if (novoId && nn) {
    const char *a = valorRaiz(strchr(strstr(r, "\"subsonic-response\""), '{'), NULL, "playlist");
    if (a && *a == '{') textoR(a, js_fim(a), "id", novoId, nn);
  }
  free(r);
  return MC_OK;
}
int ss_playlist_editar(const SsServidor *s, const char *id, const char *nome,
                       const char *const *add, int nAdd, const int *remover, int nRem) {
  size_t cap = 500 + (size_t)nAdd * 220 + (size_t)nRem * 40;
  char *x = calloc(1, cap);
  int e;
  char *r;
  if (!x) return MC_ERRO_OUTRO;
  param(x, cap, "playlistId", id);
  if (nome && nome[0]) param(x, cap, "name", nome);
  for (int i = 0; i < nAdd; i++) param(x, cap, "songIdToAdd", add[i]);
  for (int i = 0; i < nRem; i++) { size_t k = strlen(x); snprintf(x + k, cap - k, "&songIndexToRemove=%d", remover[i]); }
  r = chamar(s, "updatePlaylist", x, &e);
  free(x);
  if (!r) return e;
  free(r);
  return MC_OK;
}
int ss_playlist_apagar(const SsServidor *s, const char *id) {
  char x[300] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", id);
  r = chamar(s, "deletePlaylist", x, &e);
  if (!r) return e;
  free(r);
  return MC_OK;
}
int ss_buscar(const SsServidor *s, const char *q, int offsetFaixas,
              McArtista *ar, int maxAr, int *nAr, McAlbum *al, int maxAl, int *nAl,
              McFaixa *fx, int maxFx, int *nFx) {
  char x[600] = "";
  int e;
  char *r;
  const char *sr, *srf;
  *nAr = *nAl = *nFx = 0;
  param(x, sizeof x, "query", q);
  snprintf(x + strlen(x), sizeof x - strlen(x), "&artistCount=%d&albumCount=%d&songCount=%d&songOffset=%d",
           maxAr, maxAl, maxFx, offsetFaixas < 0 ? 0 : offsetFaixas);
  r = chamar(s, "search3", x, &e);
  if (!r) return e;
  sr = valorRaiz(strchr(strstr(r, "\"subsonic-response\""), '{'), NULL, "searchResult3");
  srf = sr && *sr == '{' ? js_fim(sr) : NULL;
  if (srf) {
    const char *p = valorRaiz(sr, srf, "artist");
    if (p && *p == '[') for (p = p + 1; *p && *p != '{' && *p != ']'; p++) {}
    while (p && *p == '{' && *nAr < maxAr) { const char *f = js_fim(p); if (!f) break; if (parseArtista(p, f, &ar[*nAr])) (*nAr)++; p = js_prox(f); }
    p = valorRaiz(sr, srf, "album");
    if (p && *p == '[') for (p = p + 1; *p && *p != '{' && *p != ']'; p++) {}
    while (p && *p == '{' && *nAl < maxAl) { const char *f = js_fim(p); if (!f) break; if (parseAlbum(p, f, &al[*nAl])) (*nAl)++; p = js_prox(f); }
    p = valorRaiz(sr, srf, "song");
    if (p && *p == '[') for (p = p + 1; *p && *p != '{' && *p != ']'; p++) {}
    while (p && *p == '{' && *nFx < maxFx) { const char *f = js_fim(p); if (!f) break; if (ss_parse_faixa(p, f, &fx[*nFx])) (*nFx)++; p = js_prox(f); }
  }
  free(r);
  return MC_OK;
}
int ss_aleatorias(const SsServidor *s, const char *genero, McFaixa *out, int max, int *n) {
  char x[300] = "";
  int e;
  char *r;
  snprintf(x, sizeof x, "&size=%d", max > 500 ? 500 : max);
  if (genero && genero[0]) param(x, sizeof x, "genre", genero);
  r = chamar(s, "getRandomSongs", x, &e);
  *n = 0;
  if (!r) return e;
  *n = ss_parse_faixas(r, "song", out, max);
  free(r);
  return MC_OK;
}
int ss_favoritar(const SsServidor *s, const char *faixaId, int liga) {
  char x[300] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", faixaId);
  r = chamar(s, liga ? "star" : "unstar", x, &e);
  if (!r) return e;
  free(r);
  return MC_OK;
}
int ss_favoritos(const SsServidor *s, McFaixa *out, int max, int *n) {
  int e;
  char *r = chamar(s, "getStarred2", NULL, &e);
  *n = 0;
  if (!r) return e;
  *n = ss_parse_faixas(r, "song", out, max);
  free(r);
  return MC_OK;
}
int ss_scrobble(const SsServidor *s, const char *faixaId, long long quandoMs, int submissao) {
  char x[400] = "";
  int e;
  char *r;
  param(x, sizeof x, "id", faixaId);
  snprintf(x + strlen(x), sizeof x - strlen(x), "&time=%lld&submission=%s", quandoMs, submissao ? "true" : "false");
  r = chamar(s, "scrobble", x, &e);
  if (!r) return e;
  free(r);
  return MC_OK;
}
int ss_letra(const SsServidor *s, const McFaixa *f, char *lrc, size_t n) {
  char x[600] = "";
  int e;
  char *r;
  size_t k = 0;
  lrc[0] = 0;
  // OpenSubsonic structured lyrics first (synced when the server has them).
  param(x, sizeof x, "id", f->id);
  r = chamar(s, "getLyricsBySongId", x, &e);
  if (r) {
    const char *l = js_array(r, NULL, "structuredLyrics");
    if (l && *l == '{') {
      const char *lf = js_fim(l), *v = valorRaiz(l, lf, "synced"), *p;
      int sinc = v && !strncmp(v, "true", 4);
      for (p = js_array(l, lf, "line"); p && p < lf && *p == '{'; ) {
        const char *pf = js_fim(p);
        char t[400] = "";
        if (!pf) break;
        js_texto_raiz_em(p, pf, "value", t, sizeof t);
        if (sinc) {
          long long ms = numR(p, pf, "start");
          k += (size_t)snprintf(lrc + k, n > k ? n - k : 0, "[%02lld:%02lld.%02lld]%s\n", ms / 60000, (ms / 1000) % 60, (ms % 1000) / 10, t);
        } else k += (size_t)snprintf(lrc + k, n > k ? n - k : 0, "%s\n", t);
        if (k >= n) { lrc[n - 1] = 0; break; }
        p = js_prox(pf);
      }
    }
    free(r);
    if (lrc[0]) return MC_OK;
  }
  // Legacy getLyrics (by artist + title).
  x[0] = 0;
  param(x, sizeof x, "artist", f->artista);
  param(x, sizeof x, "title", f->titulo);
  r = chamar(s, "getLyrics", x, &e);
  if (!r) return e == MC_OK ? MC_ERRO_NAO_SUPORTA : e;
  {
    const char *a = valorRaiz(strchr(strstr(r, "\"subsonic-response\""), '{'), NULL, "lyrics");
    if (a && *a == '{') js_texto_linhas(a, js_fim(a), "value", lrc, n);
  }
  free(r);
  return lrc[0] ? MC_OK : MC_ERRO_NAO_SUPORTA;
}
