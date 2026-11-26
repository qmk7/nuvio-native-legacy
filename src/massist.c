// QR TIME + MUSIC ASSISTANT: see massist.h.
#include "massist.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARQ "music-assistant.txt"

// ---------------------------------------------------------------- config
int massist_ler(char *url, size_t nu, char *token, size_t nt) {
  char *t = dados_ler(ARQ), *l2;
  if (url && nu) url[0] = 0;
  if (token && nt) token[0] = 0;
  if (!t) return 0;
  l2 = strchr(t, '\n');
  if (l2) *l2++ = 0;
  if (url) snprintf(url, nu, "%s", t);
  if (token && l2) { l2[strcspn(l2, "\r\n")] = 0; snprintf(token, nt, "%s", l2); }
  free(t);
  return url ? url[0] != 0 : 1;
}
int massist_configurado(void) {
  char u[256], k[1024];
  return massist_ler(u, sizeof u, k, sizeof k) && u[0] && k[0];
}
int massist_gravar(const char *url, const char *token) {
  char b[1400], u[256];
  size_t n;
  snprintf(u, sizeof u, "%s", url ? url : "");
  n = strlen(u);
  while (n && (u[n - 1] == '/' || u[n - 1] == ' ')) u[--n] = 0;   // "http://x:8095/" -> no slash
  if (n && !strstr(u, "://")) {                                      // "192.168.1.5:8095" -> http://
    char t[256];
    snprintf(t, sizeof t, "http://%s", u);
    snprintf(u, sizeof u, "%s", t);
  }
  snprintf(b, sizeof b, "%s\n%s\n", u, token ? token : "");
  return dados_gravar(ARQ, b);
}

#define ARQ_ALTO "music-assistant-player.txt"
int massist_alto_ler(char *id, size_t ni, char *nome, size_t nn) {
  char *t = dados_ler(ARQ_ALTO), *l2;
  if (ni) id[0] = 0;
  if (nn) nome[0] = 0;
  if (!t) return 0;
  l2 = strchr(t, '\n');
  if (l2) *l2++ = 0;
  snprintf(id, ni, "%s", t);
  if (l2) { l2[strcspn(l2, "\r\n")] = 0; snprintf(nome, nn, "%s", l2); }
  free(t);
  return id[0] != 0;
}
int massist_alto_gravar(const char *id, const char *nome) {
  char b[300];
  snprintf(b, sizeof b, "%s\n%s\n", id ? id : "", nome ? nome : "");
  return dados_gravar(ARQ_ALTO, b);
}

// ---------------------------------------------------------------- transport
static char *postarRede(const char *url, const char *corpo, const char *token, int *status) {
  char auth[1100];
  const char *cab[3];
  snprintf(auth, sizeof auth, "Authorization: Bearer %s", token);
  cab[0] = "Content-Type: application/json";
  cab[1] = auth;
  cab[2] = NULL;
  return rede_postar_st(url, 15, cab, corpo, status);
}
static char *(*transporte)(const char *, const char *, const char *, int *) = postarRede;
void massist_teste_transporte(char *(*f)(const char *, const char *, const char *, int *)) {
  transporte = f ? f : postarRede;
}

static size_t esc(char *d, size_t n, const char *s) {
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
// One command; *res = the answer (malloc'd; the "result" inside when the
// server wraps it). MA_OK or an MA_* error.
static int comando(const char *cmd, const char *args, char **res) {
  char url[300], token[1024], *corpo, *r;
  int st = 0;
  size_t n;
  *res = NULL;
  if (!massist_ler(url, sizeof url, token, sizeof token) || !url[0] || !token[0]) return MA_SEM_CONFIG;
  n = strlen(cmd) + strlen(args ? args : "{}") + 64;
  if (!(corpo = malloc(n))) return MA_REDE;
  snprintf(corpo, n, "{\"command\":\"%s\",\"args\":%s}", cmd, args ? args : "{}");
  strncat(url, "/api", sizeof url - strlen(url) - 1);
  r = transporte(url, corpo, token, &st);
  if (!r) r = transporte(url, corpo, token, &st);   // the first call after a quiet while can drop: once more
  free(corpo);
  memset(token, 0, sizeof token);
  if (!r) return MA_REDE;
  if (st == 401 || st == 403) { free(r); return MA_TOKEN; }
  if (st < 200 || st >= 300 || strstr(r, "\"error_code\"")) {
    printf("[massist] %s: erro %d\n", cmd, st);
    fflush(stdout);
    free(r);
    return MA_SERVIDOR;
  }
  {
    // {"message_id":..,"result":X} -> X; a bare answer stays as it is.
    const char *p = r;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p == '{' && js_tem(p, NULL, "result") && js_tem(p, NULL, "message_id")) {
      const char *k = strstr(p, "\"result\"");
      if (k && (k = strchr(k + 8, ':'))) {
        char *c;
        k++;
        while (*k == ' ') k++;
        c = strdup(k);
        if (c) { char *f = strrchr(c, '}'); if (f) *f = 0; free(r); r = c; }
      }
    }
  }
  *res = r;
  return MA_OK;
}

// ---------------------------------------------------------------- parsing
static void imagemDe(const char *p, const char *f, char *dst, size_t n) {
  const char *im;
  dst[0] = 0;
  // metadata.images[] {type, path, provider, remotely_accessible}: the first
  // remote "thumb" (YouTube Music's are plain https links).
  for (im = js_array(p, f, "images"); im && *im == '{'; ) {
    const char *e = js_fim(im);
    char path[512] = "";
    if (!e) break;
    js_texto(im, e, "path", path, sizeof path);
    if (!strncmp(path, "http://", 7) || !strncmp(path, "https://", 8)) { snprintf(dst, n, "%s", path); return; }
    im = js_prox(e);
  }
  {
    char x[512] = "";
    if (js_texto_raiz_em(p, f, "image", x, sizeof x) && (!strncmp(x, "http", 4))) snprintf(dst, n, "%s", x);
  }
}
static int lerItem(const char *p, const char *f, MaItem *it) {
  const char *a;
  memset(it, 0, sizeof *it);
  js_texto_raiz_em(p, f, "uri", it->uri, sizeof it->uri);
  js_texto_raiz_em(p, f, "item_id", it->item_id, sizeof it->item_id);
  js_texto_raiz_em(p, f, "provider", it->provider, sizeof it->provider);
  js_texto_raiz_em(p, f, "media_type", it->tipo, sizeof it->tipo);
  js_texto_raiz_em(p, f, "name", it->nome, sizeof it->nome);
  it->dur = (int)js_num(p, f, "duration", 0);
  if ((a = js_array(p, f, "artists")) && *a == '{') {
    const char *e = js_fim(a);
    if (e) js_texto(a, e, "name", it->sub, sizeof it->sub);
  }
  if (!it->sub[0]) js_texto_raiz_em(p, f, "owner", it->sub, sizeof it->sub);
  imagemDe(p, f, it->imagem, sizeof it->imagem);
  return it->uri[0] && it->nome[0];
}
// First element of the ROOT object's array `chave` (js_array would also find
// a nested one: every track has its own "artists").
static const char *arrayRaiz(const char *j, const char *chave) {
  size_t kn = strlen(chave);
  int prof = 0;
  for (const char *p = j; *p; p++) {
    if (*p == '"') {
      const char *q = p + 1;
      while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
      if (!*q) return NULL;
      if (prof == 1 && (size_t)(q - p - 1) == kn && !strncmp(p + 1, chave, kn)) {
        const char *v = q + 1;
        while (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t') v++;
        if (*v == ':') {
          v++;
          while (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t') v++;
          if (*v != '[') return NULL;
          v++;
          while (*v == ' ' || *v == '\n' || *v == '\r' || *v == '\t') v++;
          return *v == ']' ? NULL : v;
        }
      }
      p = q;
    } else if (*p == '{' || *p == '[') prof++;
    else if (*p == '}' || *p == ']') prof--;
  }
  return NULL;
}
// Items of `chave` ("tracks"...) in `json`, or of the root array / "items"
// when chave is NULL.
int massist_ler_itens(const char *json, const char *chave, MaItem *out, int max) {
  const char *p;
  int n = 0;
  if (!json) return 0;
  if (chave) p = arrayRaiz(json, chave);
  else if (!(p = js_raiz_array(json))) p = arrayRaiz(json, "items");
  for (; p && *p == '{' && n < max; ) {
    const char *f = js_fim(p);
    if (!f) break;
    if (lerItem(p, f, &out[n])) n++;
    p = js_prox(f);
  }
  return n;
}
int massist_ler_players(const char *json, MaPlayer *out, int max) {
  const char *p;
  int n = 0;
  if (!json || !(p = js_raiz_array(json))) return 0;
  for (; p && *p == '{' && n < max; ) {
    const char *f = js_fim(p);
    char v[16] = "";
    MaPlayer *x = &out[n];
    if (!f) break;
    memset(x, 0, sizeof *x);
    js_texto_raiz_em(p, f, "player_id", x->id, sizeof x->id);
    if (!js_texto_raiz_em(p, f, "display_name", x->nome, sizeof x->nome) || !x->nome[0])
      js_texto_raiz_em(p, f, "name", x->nome, sizeof x->nome);
    js_texto_raiz_em(p, f, "state", x->estado, sizeof x->estado);
    x->disponivel = !strstr(p, "\"available\": false") && !strstr(p, "\"available\":false");
    if (js_texto_raiz_em(p, f, "type", v, sizeof v)) x->grupo = !strcmp(v, "group");
    if (x->id[0] && x->nome[0]) n++;
    p = js_prox(f);
  }
  return n;
}

int massist_ler_agora(const char *json, const char *player, MaAgora *a) {
  const char *p;
  memset(a, 0, sizeof *a);
  if (!json || !(p = js_raiz_array(json))) return 0;
  for (; p && *p == '{'; ) {
    const char *f = js_fim(p);
    char id[96] = "";
    if (!f) break;
    js_texto_raiz_em(p, f, "player_id", id, sizeof id);
    if (!strcmp(id, player)) {
      const char *m = strstr(p, "\"current_media\"");
      js_texto_raiz_em(p, f, "state", a->estado, sizeof a->estado);
      a->pos = js_num(p, f, "elapsed_time", 0);
      if (m && m < f && (m = strchr(m, '{')) && m < f) {
        const char *mf = js_fim(m);
        if (mf && mf <= f) {
          js_texto(m, mf, "title", a->titulo, sizeof a->titulo);
          js_texto(m, mf, "artist", a->artista, sizeof a->artista);
          js_texto(m, mf, "image_url", a->imagem, sizeof a->imagem);
          a->dur = (int)js_num(m, mf, "duration", 0);
          if (a->pos <= 0) a->pos = js_num(m, mf, "elapsed_time", 0);
        }
      }
      return 1;
    }
    p = js_prox(f);
  }
  return 0;
}
int massist_agora(const char *player, MaAgora *a) {
  char *r = NULL;
  int e = comando("players/all", "{}", &r);
  if (e == MA_OK && !massist_ler_agora(r, player, a)) memset(a, 0, sizeof *a);
  free(r);
  return e;
}

// ---------------------------------------------------------------- commands
int massist_testar(char *desc, size_t n) {
  char *r = NULL;
  int e = comando("players/all", "{}", &r), np = 0;
  if (e == MA_OK) {
    MaPlayer pl[32];
    np = massist_ler_players(r, pl, 32);
    snprintf(desc, n, "Music Assistant · %d", np);
  }
  free(r);
  return e;
}
int massist_biblioteca(const char *tipo, int offset, MaItem *out, int max, int *n) {
  char cmd[64], args[96], *r;
  int e;
  *n = 0;
  // "favorites": the tracks marked favourite (liked songs on YouTube Music).
  int fav = !strcmp(tipo, "favorites");
  snprintf(cmd, sizeof cmd, "music/%s/library_items", fav ? "tracks" : tipo);
  snprintf(args, sizeof args, "{\"limit\":%d,\"offset\":%d%s}", max, offset, fav ? ",\"favorite\":true" : "");
  if ((e = comando(cmd, args, &r)) == MA_OK) *n = massist_ler_itens(r, NULL, out, max);
  free(r);
  return e;
}
int massist_recentes(MaItem *out, int max, int *n) {
  char args[64], *r;
  int e;
  *n = 0;
  snprintf(args, sizeof args, "{\"limit\":%d}", max);
  if ((e = comando("music/recently_played_items", args, &r)) == MA_OK) *n = massist_ler_itens(r, NULL, out, max);
  free(r);
  return e;
}
int massist_buscar(const char *q, MaItem *out, int max, int *n) {
  static const char *const T[] = { "tracks", "artists", "albums", "playlists" };
  char eq[400], args[600], *r;
  int e;
  *n = 0;
  esc(eq, sizeof eq, q);
  snprintf(args, sizeof args, "{\"search_query\":\"%s\",\"media_types\":[\"track\",\"artist\",\"album\",\"playlist\"],\"limit\":12}", eq);
  if ((e = comando("music/search", args, &r)) == MA_OK)
    for (int i = 0; i < 4 && *n < max; i++) *n += massist_ler_itens(r, T[i], out + *n, max - *n);
  free(r);
  return e;
}
int massist_faixas_de(const MaItem *pai, MaItem *out, int max, int *n) {
  char a[160], b[96], args[400], *r;
  const char *cmd = !strcmp(pai->tipo, "album") ? "music/albums/album_tracks" : "music/playlists/playlist_tracks";
  int e;
  *n = 0;
  esc(a, sizeof a, pai->item_id);
  esc(b, sizeof b, pai->provider);
  snprintf(args, sizeof args, "{\"item_id\":\"%s\",\"provider_instance_id_or_domain\":\"%s\"}", a, b);
  if ((e = comando(cmd, args, &r)) == MA_OK) *n = massist_ler_itens(r, NULL, out, max);
  free(r);
  return e;
}
int massist_players(MaPlayer *out, int max, int *n) {
  char *r;
  int e;
  *n = 0;
  if ((e = comando("players/all", "{}", &r)) == MA_OK) *n = massist_ler_players(r, out, max);
  free(r);
  return e;
}
int massist_tocar(const char *player, const char *uri, int radio) {
  char p[200], u[MA_URI * 2], args[1200], *r;
  int e;
  esc(p, sizeof p, player);
  esc(u, sizeof u, uri);
  snprintf(args, sizeof args, "{\"queue_id\":\"%s\",\"media\":[\"%s\"],\"option\":\"replace\",\"radio_mode\":%s}",
           p, u, radio ? "true" : "false");
  e = comando("player_queues/play_media", args, &r);
  free(r);
  return e;
}
int massist_comando(const char *player, const char *cmd) {
  static const char *const OK[] = { "play", "pause", "play_pause", "stop", "next", "previous", "volume_up", "volume_down", NULL };
  char c[64], p[200], args[300], *r;
  int e, ok = 0;
  for (int i = 0; OK[i]; i++) if (!strcmp(cmd, OK[i])) ok = 1;
  if (!ok) return MA_SERVIDOR;
  esc(p, sizeof p, player);
  snprintf(c, sizeof c, "players/cmd/%s", cmd);
  snprintf(args, sizeof args, "{\"player_id\":\"%s\"}", p);
  e = comando(c, args, &r);
  free(r);
  return e;
}
