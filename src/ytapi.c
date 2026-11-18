#include "ytapi.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include "perfis.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char apiBase[160] = "https://www.googleapis.com/youtube/v3";
static char oauthBase[160] = "https://oauth2.googleapis.com";
void yt_definir_base(const char *api, const char *oauth) {
  if (api) snprintf(apiBase, sizeof apiBase, "%s", api);
  if (oauth) snprintf(oauthBase, sizeof oauthBase, "%s", oauth);
}

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static char chave[64], clienteId[160], clienteSegredo[96];
static char acesso[2048], renovar[512], codigoDisp[512];
static long long acessoAte;
static int perfilLido = -1, perfilLogin = -1;

// ---------------------------------------------------------------- small helpers
int yt_id_valido(const char *id) {
  if (!id || strlen(id) != 11) return 0;
  for (; *id; id++) if (!isalnum((unsigned char)*id) && *id != '-' && *id != '_') return 0;
  return 1;
}
static int idSeguro(const char *id) {
  if (!id || !*id || strlen(id) > 38) return 0;
  for (; *id; id++) if (!isalnum((unsigned char)*id) && *id != '-' && *id != '_') return 0;
  return 1;
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
void yt_duracao(const char *iso, char *d, size_t n) {
  int h = 0, m = 0, s = 0, v = 0, tempo = 0;
  d[0] = 0;
  if (!iso || iso[0] != 'P') return;
  for (const char *p = iso + 1; *p; p++) {
    if (*p == 'T') { tempo = 1; continue; }
    if (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); continue; }
    if (*p == 'D') h += v * 24;
    else if (tempo && *p == 'H') h += v;
    else if (tempo && *p == 'M') m = v;
    else if (tempo && *p == 'S') s = v;
    v = 0;
  }
  if (!h && !m && !s) return;   // P0D: a live stream
  if (h) snprintf(d, n, "%d:%02d:%02d", h, m, s);
  else snprintf(d, n, "%d:%02d", m, s);
}
static const char *objeto(const char *ini, const char *fim, const char *chave_, const char **f) {
  // First "chave":{ ... } in [ini,fim).
  char pad[48];
  const char *p;
  snprintf(pad, sizeof pad, "\"%s\"", chave_);
  for (p = ini; (p = strstr(p, pad)) && (!fim || p < fim); p++) {
    const char *v = p + strlen(pad);
    while (*v == ' ' || *v == ':' || *v == '\n' || *v == '\t' || *v == '\r') v++;
    if (*v == '{') { *f = js_fim(v); return *f ? v : NULL; }
  }
  return NULL;
}
static void melhorThumb(const char *ini, const char *fim, char *d, size_t n) {
  static const char *const tam[] = { "maxres", "standard", "high", "medium", "default" };
  const char *tf, *t = objeto(ini, fim, "thumbnails", &tf);
  d[0] = 0;
  for (int i = 0; t && i < 5 && !d[0]; i++) {
    const char *of, *o = objeto(t, tf, tam[i], &of);
    if (o) js_texto(o, of, "url", d, n);
  }
  if (strncmp(d, "https://", 8)) d[0] = 0;
}
int yt_parse_erro(const char *json, int http) {
  char motivo[64] = "";
  if (json) js_texto(json, NULL, "reason", motivo, sizeof motivo);
  if (strstr(motivo, "quota") || strstr(motivo, "rateLimit")) return YT_ERRO_COTA;
  if (strstr(motivo, "keyInvalid") || strstr(motivo, "API_KEY") || strstr(motivo, "keyExpired")) return YT_ERRO_CHAVE;
  if (http == 401) return YT_ERRO_LOGIN;
  if (http == 400 && json && strstr(json, "API key")) return YT_ERRO_CHAVE;
  if (http == 0) return YT_ERRO_REDE;
  return YT_ERRO_OUTRO;
}
int yt_parse_lista(const char *json, YtItem *out, int max, char *prox, size_t nProx) {
  const char *p;
  char kind[48] = "";
  int n = 0;
  if (prox && nProx) prox[0] = 0;
  if (!json || !strstr(json, "\"items\"")) return -1;
  if (prox) js_texto_raiz(json, "nextPageToken", prox, nProx);
  p = js_array(json, NULL, "items");
  while (p && *p == '{' && n < max) {
    const char *f = js_fim(p), *sn, *snf = NULL;
    YtItem it;
    if (!f) break;
    memset(&it, 0, sizeof it);
    it.views = -1;
    js_texto_raiz_em(p, f, "kind", kind, sizeof kind);
    sn = objeto(p, f, "snippet", &snf);
    if (sn) {
      char pub[32] = "";
      js_texto_raiz_em(sn, snf, "title", it.titulo, sizeof it.titulo);
      js_texto_raiz_em(sn, snf, "channelTitle", it.canal, sizeof it.canal);
      js_texto_raiz_em(sn, snf, "channelId", it.canalId, sizeof it.canalId);
      js_texto_raiz_em(sn, snf, "description", it.desc, sizeof it.desc);
      js_texto_raiz_em(sn, snf, "publishedAt", pub, sizeof pub);
      snprintf(it.publicado, sizeof it.publicado, "%.10s", pub);
      melhorThumb(sn, snf, it.thumb, sizeof it.thumb);
      {
        char live[16] = "";
        js_texto_raiz_em(sn, snf, "liveBroadcastContent", live, sizeof live);
        if (!strcmp(live, "live")) snprintf(it.duracao, sizeof it.duracao, "%s", "AO VIVO");
      }
    }
    if (strstr(kind, "searchResult")) {
      const char *idf, *ido = objeto(p, f, "id", &idf);
      char k2[48] = "";
      if (ido) {
        js_texto(ido, idf, "kind", k2, sizeof k2);
        if (strstr(k2, "video")) { it.tipo = YT_VIDEO; js_texto(ido, idf, "videoId", it.id, sizeof it.id); }
        else if (strstr(k2, "channel")) { it.tipo = YT_CANAL; js_texto(ido, idf, "channelId", it.id, sizeof it.id); }
        else if (strstr(k2, "playlist")) { it.tipo = YT_PLAYLIST; js_texto(ido, idf, "playlistId", it.id, sizeof it.id); }
      }
    } else if (strstr(kind, "playlistItem") || strstr(kind, "subscription")) {
      const char *rf, *r = sn ? objeto(sn, snf, "resourceId", &rf) : NULL;
      if (r) {
        if (strstr(kind, "subscription")) { it.tipo = YT_CANAL; js_texto(r, rf, "channelId", it.id, sizeof it.id); }
        else { it.tipo = YT_VIDEO; js_texto(r, rf, "videoId", it.id, sizeof it.id); }
      }
      if (strstr(kind, "playlistItem")) {
        // The video's own channel, not the playlist owner's.
        char vc[96] = "";
        if (sn && js_texto_raiz_em(sn, snf, "videoOwnerChannelTitle", vc, sizeof vc) && vc[0]) snprintf(it.canal, sizeof it.canal, "%s", vc);
      }
    } else if (strstr(kind, "playlist")) {
      const char *cf, *c = objeto(p, f, "contentDetails", &cf);
      it.tipo = YT_PLAYLIST;
      js_texto_raiz_em(p, f, "id", it.id, sizeof it.id);
      if (c) it.itens = (int)js_num(c, cf, "itemCount", 0);
    } else if (strstr(kind, "channel")) {
      it.tipo = YT_CANAL;
      js_texto_raiz_em(p, f, "id", it.id, sizeof it.id);
    } else {   // youtube#video
      const char *cf, *c = objeto(p, f, "contentDetails", &cf), *sf, *st = objeto(p, f, "statistics", &sf);
      char dur[32] = "", v[24] = "";
      it.tipo = YT_VIDEO;
      js_texto_raiz_em(p, f, "id", it.id, sizeof it.id);
      if (c && js_texto(c, cf, "duration", dur, sizeof dur) && strcmp(it.duracao, "AO VIVO")) yt_duracao(dur, it.duracao, sizeof it.duracao);
      if (st && js_texto(st, sf, "viewCount", v, sizeof v)) it.views = atoll(v);
    }
    if (idSeguro(it.id) && it.titulo[0] && strcmp(it.titulo, "Private video") && strcmp(it.titulo, "Deleted video")) out[n++] = it;
    p = js_prox(f);
  }
  return n;
}

// ---------------------------------------------------------------- credentials
static void arquivoConta(char *d, size_t n) { int p = perfis_ativo(); snprintf(d, n, "youtube-account-p%d.txt", p > 0 ? p : 1); }
static void lerChaveValor(const char *t, const char *k, char *d, size_t n) {
  const char *p;
  size_t kl = strlen(k);
  d[0] = 0;
  for (p = t; p && *p;) {
    const char *f = strchr(p, '\n');
    size_t len = f ? (size_t)(f - p) : strlen(p);
    if (len > kl && !strncmp(p, k, kl) && p[kl] == '=') snprintf(d, n, "%.*s", (int)(len - kl - 1), p + kl + 1);
    p = f ? f + 1 : NULL;
  }
}
static void carregarLocked(void) {
  char *t = dados_ler("youtube.txt"), nome[48], v[32];
  lerChaveValor(t ? t : "", "chave", chave, sizeof chave);
  lerChaveValor(t ? t : "", "cliente", clienteId, sizeof clienteId);
  lerChaveValor(t ? t : "", "segredo", clienteSegredo, sizeof clienteSegredo);
  free(t);
  arquivoConta(nome, sizeof nome);
  t = dados_ler(nome);
  lerChaveValor(t ? t : "", "renovar", renovar, sizeof renovar);
  lerChaveValor(t ? t : "", "acesso", acesso, sizeof acesso);
  lerChaveValor(t ? t : "", "ate", v, sizeof v);
  acessoAte = atoll(v);
  free(t);
  perfilLido = perfis_ativo();
}
static void garantirLocked(void) { if (perfilLido != perfis_ativo()) carregarLocked(); }
void yt_carregar(void) { pthread_mutex_lock(&trava); carregarLocked(); pthread_mutex_unlock(&trava); }
static int gravarConfigLocked(void) {
  char t[512];
  snprintf(t, sizeof t, "chave=%s\ncliente=%s\nsegredo=%s\n", chave, clienteId, clienteSegredo);
  return dados_gravar("youtube.txt", t);
}
static void gravarContaLocked(void) {
  char nome[48], t[3000];
  arquivoConta(nome, sizeof nome);
  if (!renovar[0]) { dados_apagar(nome); return; }
  snprintf(t, sizeof t, "renovar=%s\nacesso=%s\nate=%lld\n", renovar, acesso, acessoAte);
  dados_gravar(nome, t);
}
static int textoLimpo(const char *s, size_t max) {
  if (!s || strlen(s) >= max) return 0;
  for (; *s; s++) if ((unsigned char)*s <= ' ' || *s == '"' || *s == '\\') return 0;
  return 1;
}
int yt_tem_chave(void) { int r; pthread_mutex_lock(&trava); garantirLocked(); r = chave[0] != 0; pthread_mutex_unlock(&trava); return r; }
int yt_tem_cliente(void) { int r; pthread_mutex_lock(&trava); garantirLocked(); r = clienteId[0] && clienteSegredo[0]; pthread_mutex_unlock(&trava); return r; }
int yt_definir_chave(const char *c) {
  int ok;
  if (c && c[0] && !textoLimpo(c, sizeof chave)) return 0;
  pthread_mutex_lock(&trava); garantirLocked();
  snprintf(chave, sizeof chave, "%s", c ? c : "");
  ok = gravarConfigLocked();
  pthread_mutex_unlock(&trava);
  return ok;
}
int yt_definir_cliente(const char *id, const char *seg) {
  int ok;
  if ((id && id[0] && !textoLimpo(id, sizeof clienteId)) || (seg && seg[0] && !textoLimpo(seg, sizeof clienteSegredo))) return 0;
  pthread_mutex_lock(&trava); garantirLocked();
  if (id) snprintf(clienteId, sizeof clienteId, "%s", id);        // NULL keeps the saved half
  if (seg) snprintf(clienteSegredo, sizeof clienteSegredo, "%s", seg);
  ok = gravarConfigLocked();
  pthread_mutex_unlock(&trava);
  return ok;
}
int yt_logado(void) { int r; pthread_mutex_lock(&trava); garantirLocked(); r = renovar[0] != 0; pthread_mutex_unlock(&trava); return r; }
void yt_sair(void) {
  pthread_mutex_lock(&trava); garantirLocked();
  renovar[0] = acesso[0] = 0; acessoAte = 0;
  gravarContaLocked();
  pthread_mutex_unlock(&trava);
}

// ---------------------------------------------------------------- requests
static char *postarForm(const char *url, const char *corpo, int *st) {
  static const char *const cab[] = { "Content-Type: application/x-www-form-urlencoded", NULL };
  return rede_postar_st(url, 20, cab, corpo, st);
}
// A fresh access token (refreshes when it is about to expire). 1 ok.
static int tokenValido(char *tok, size_t n) {
  char corpo[1600], id[160], seg[96], rf[512], url[256];
  char *r;
  int st = 0;
  long long agora = (long long)time(NULL);
  pthread_mutex_lock(&trava); garantirLocked();
  if (!renovar[0]) { pthread_mutex_unlock(&trava); return 0; }
  if (acesso[0] && acessoAte - 60 > agora) { snprintf(tok, n, "%s", acesso); pthread_mutex_unlock(&trava); return 1; }
  snprintf(id, sizeof id, "%s", clienteId); snprintf(seg, sizeof seg, "%s", clienteSegredo); snprintf(rf, sizeof rf, "%s", renovar);
  pthread_mutex_unlock(&trava);
  {
    char e1[400], e2[200], e3[700];
    urlenc(id, e1, sizeof e1); urlenc(seg, e2, sizeof e2); urlenc(rf, e3, sizeof e3);
    snprintf(corpo, sizeof corpo, "client_id=%s&client_secret=%s&refresh_token=%s&grant_type=refresh_token", e1, e2, e3);
  }
  snprintf(url, sizeof url, "%s/token", oauthBase);
  r = postarForm(url, corpo, &st);
  if (!r) return 0;
  {
    char a[2048] = "";
    double exp = js_num(r, NULL, "expires_in", 0);
    js_texto(r, NULL, "access_token", a, sizeof a);
    free(r);
    if (!a[0]) { if (st == 400 || st == 401) { printf("[youtube] a conta precisa entrar de novo\n"); fflush(stdout); yt_sair(); } return 0; }
    pthread_mutex_lock(&trava);
    snprintf(acesso, sizeof acesso, "%s", a);
    acessoAte = agora + (long long)exp;
    gravarContaLocked();
    pthread_mutex_unlock(&trava);
    snprintf(tok, n, "%s", a);
    return 1;
  }
}
// GET <apiBase>/<caminho> with the key or the account. Body and status.
static char *api(const char *caminho, int conta, int *st, int *erro) {
  char url[1400], tok[2048], cabTok[2100], k[64];
  const char *cab[2] = { NULL, NULL };
  char *r;
  *erro = YT_OK;
  *st = 0;
  pthread_mutex_lock(&trava); garantirLocked(); snprintf(k, sizeof k, "%s", chave); pthread_mutex_unlock(&trava);
  if (conta) {
    if (!tokenValido(tok, sizeof tok)) { *erro = YT_ERRO_LOGIN; return NULL; }
    snprintf(cabTok, sizeof cabTok, "Authorization: Bearer %s", tok);
    cab[0] = cabTok;
    snprintf(url, sizeof url, "%s/%s", apiBase, caminho);
  } else {
    if (!k[0]) { *erro = YT_SEM_CHAVE; return NULL; }
    snprintf(url, sizeof url, "%s/%s&key=%s", apiBase, caminho, k);
  }
  r = rede_baixar_st(url, 15, cab[0] ? cab : NULL, st);
  if (!r || *st == 0) { *erro = YT_ERRO_REDE; free(r); return NULL; }
  if (*st >= 400) { *erro = yt_parse_erro(r, *st); free(r); return NULL; }
  return r;
}
static int lista(const char *caminho, int conta, YtItem *out, int max, int *n, char *prox, size_t np) {
  int st, e;
  char *r = api(caminho, conta, &st, &e);
  *n = 0;
  if (!r) return e;
  *n = yt_parse_lista(r, out, max, prox, np);
  free(r);
  if (*n < 0) { *n = 0; return YT_ERRO_OUTRO; }
  return YT_OK;
}
static void pag(char *d, size_t n, const char *p) { d[0] = 0; if (p && p[0] && idSeguro(p)) snprintf(d, n, "&pageToken=%s", p); }
int yt_em_alta(const char *regiao, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np) {
  char c[300], pg[64], rg[8] = "US";
  if (regiao && strlen(regiao) == 2 && isalpha((unsigned char)regiao[0]) && isalpha((unsigned char)regiao[1])) snprintf(rg, sizeof rg, "%s", regiao);
  pag(pg, sizeof pg, pagina);
  snprintf(c, sizeof c, "videos?part=snippet,contentDetails,statistics&chart=mostPopular&regionCode=%s&maxResults=%d%s", rg, max > 50 ? 50 : max, pg);
  return lista(c, 0, out, max, n, prox, np);
}
int yt_buscar(const char *q, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np) {
  char c[600], e[400], pg[64];
  urlenc(q, e, sizeof e);
  pag(pg, sizeof pg, pagina);
  snprintf(c, sizeof c, "search?part=snippet&type=video&maxResults=%d&q=%s%s", max > 50 ? 50 : max, e, pg);
  return lista(c, 0, out, max, n, prox, np);
}
int yt_buscar_musica(const char *q, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np) {
  char c[600], e[400], pg[64];
  urlenc(q, e, sizeof e);
  pag(pg, sizeof pg, pagina);
  snprintf(c, sizeof c, "search?part=snippet&type=video&videoCategoryId=10&maxResults=%d&q=%s%s", max > 50 ? 50 : max, e, pg);
  return lista(c, 0, out, max, n, prox, np);
}
int yt_detalhes(YtItem *itens, int n) {
  char c[900], ids[700] = "";
  size_t k = 0;
  int st, e, m;
  YtItem *d;
  char *r;
  for (int i = 0; i < n && i < 50; i++) if (itens[i].tipo == YT_VIDEO && yt_id_valido(itens[i].id))
    k += (size_t)snprintf(ids + k, sizeof ids - k, "%s%s", k ? "," : "", itens[i].id);
  if (!k) return YT_OK;
  snprintf(c, sizeof c, "videos?part=snippet,contentDetails,statistics&id=%s", ids);
  r = api(c, 0, &st, &e);
  if (!r) return e;
  d = calloc(50, sizeof *d);
  m = d ? yt_parse_lista(r, d, 50, NULL, 0) : 0;
  free(r);
  for (int i = 0; d && i < n; i++) for (int j = 0; j < m; j++) if (!strcmp(itens[i].id, d[j].id)) {
    if (d[j].duracao[0]) snprintf(itens[i].duracao, sizeof itens[i].duracao, "%s", d[j].duracao);
    itens[i].views = d[j].views;
    if (d[j].desc[0]) snprintf(itens[i].desc, sizeof itens[i].desc, "%s", d[j].desc);
  }
  free(d);
  return YT_OK;
}
int yt_video(const char *id, YtItem *out) {
  char c[200];
  int st, e, n;
  char *r;
  if (!yt_id_valido(id)) return YT_INDISPONIVEL;
  snprintf(c, sizeof c, "videos?part=snippet,contentDetails,statistics&id=%s", id);
  r = api(c, 0, &st, &e);
  if (!r) return e;
  n = yt_parse_lista(r, out, 1, NULL, 0);
  free(r);
  return n == 1 ? YT_OK : n == 0 ? YT_INDISPONIVEL : YT_ERRO_OUTRO;
}
int yt_inscricoes(const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np) {
  char c[200], pg[64];
  pag(pg, sizeof pg, pagina);
  snprintf(c, sizeof c, "subscriptions?part=snippet&mine=true&order=alphabetical&maxResults=%d%s", max > 50 ? 50 : max, pg);
  return lista(c, 1, out, max, n, prox, np);
}
int yt_playlists(YtItem *out, int max, int *n) {
  char c[200];
  snprintf(c, sizeof c, "playlists?part=snippet,contentDetails&mine=true&maxResults=%d", max > 50 ? 50 : max);
  return lista(c, 1, out, max, n, NULL, 0);
}
int yt_playlist_itens(const char *id, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np) {
  char c[300], pg[64];
  *n = 0;
  if (!idSeguro(id)) return YT_ERRO_OUTRO;
  pag(pg, sizeof pg, pagina);
  // The account's own playlists can be private: ask as the account when signed in.
  snprintf(c, sizeof c, "playlistItems?part=snippet&playlistId=%s&maxResults=%d%s", id, max > 50 ? 50 : max, pg);
  return lista(c, yt_logado(), out, max, n, prox, np);
}
int yt_canal_videos(const char *canalId, YtItem *out, int max, int *n) {
  // The channel's uploads playlist ("UC..." -> "UU..."), 1 unit, not a 100-unit search.
  char up[40];
  *n = 0;
  if (!idSeguro(canalId) || strncmp(canalId, "UC", 2)) return YT_ERRO_OUTRO;
  snprintf(up, sizeof up, "UU%s", canalId + 2);
  return yt_playlist_itens(up, NULL, out, max, n, NULL, 0);
}

// ---------------------------------------------------------------- device sign-in
int yt_login_iniciar(char *codigo, size_t nc, char *urlUser, size_t nu, int *intervalo) {
  char corpo[600], url[256], e1[400], id[160];
  char *r;
  int st = 0;
  pthread_mutex_lock(&trava); garantirLocked(); snprintf(id, sizeof id, "%s", clienteId); pthread_mutex_unlock(&trava);
  if (!id[0]) return YT_SEM_CHAVE;
  urlenc(id, e1, sizeof e1);
  static const char escopo[] = "https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fyoutube.readonly";
  snprintf(corpo, sizeof corpo, "client_id=%s&scope=%s", e1, escopo);
  snprintf(url, sizeof url, "%s/device/code", oauthBase);
  r = postarForm(url, corpo, &st);
  if (!r) return YT_ERRO_REDE;
  {
    char dc[512] = "";
    js_texto(r, NULL, "device_code", dc, sizeof dc);
    js_texto(r, NULL, "user_code", codigo, nc);
    if (!js_texto(r, NULL, "verification_url", urlUser, nu)) js_texto(r, NULL, "verification_uri", urlUser, nu);
    if (intervalo) *intervalo = (int)js_num(r, NULL, "interval", 5);
    free(r);
    if (!dc[0] || !codigo[0]) return st == 401 || st == 400 ? YT_ERRO_CHAVE : YT_ERRO_OUTRO;
    pthread_mutex_lock(&trava); snprintf(codigoDisp, sizeof codigoDisp, "%s", dc); perfilLogin = perfis_ativo(); pthread_mutex_unlock(&trava);
  }
  return YT_OK;
}
int yt_login_conferir(void) {
  char corpo[1600], url[256], id[160], seg[96], dc[512], e1[400], e2[200], e3[700];
  char *r;
  int st = 0;
  pthread_mutex_lock(&trava);
  snprintf(id, sizeof id, "%s", clienteId); snprintf(seg, sizeof seg, "%s", clienteSegredo); snprintf(dc, sizeof dc, "%s", codigoDisp);
  pthread_mutex_unlock(&trava);
  if (!dc[0]) return -1;
  urlenc(id, e1, sizeof e1); urlenc(seg, e2, sizeof e2); urlenc(dc, e3, sizeof e3);
  snprintf(corpo, sizeof corpo, "client_id=%s&client_secret=%s&device_code=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-type%%3Adevice_code",
           e1, e2, e3);
  snprintf(url, sizeof url, "%s/token", oauthBase);
  r = postarForm(url, corpo, &st);
  if (!r) return 0;   // network hiccup: keep waiting
  {
    char a[2048] = "", rf[512] = "", erro[64] = "";
    double exp = js_num(r, NULL, "expires_in", 0);
    js_texto(r, NULL, "access_token", a, sizeof a);
    js_texto(r, NULL, "refresh_token", rf, sizeof rf);
    js_texto(r, NULL, "error", erro, sizeof erro);
    free(r);
    if (a[0] && rf[0]) {
      pthread_mutex_lock(&trava);
      if (perfilLogin != perfis_ativo()) {   // the profile changed while the phone was signing in
        codigoDisp[0] = 0;
        pthread_mutex_unlock(&trava);
        printf("[youtube] perfil trocado durante a entrada; descartada\n");
        fflush(stdout);
        return -1;
      }
      garantirLocked();
      snprintf(acesso, sizeof acesso, "%s", a);
      snprintf(renovar, sizeof renovar, "%s", rf);
      acessoAte = (long long)time(NULL) + (long long)exp;
      codigoDisp[0] = 0;
      gravarContaLocked();
      pthread_mutex_unlock(&trava);
      printf("[youtube] conta conectada neste perfil\n");
      fflush(stdout);
      return 1;
    }
    if (!strcmp(erro, "authorization_pending") || !strcmp(erro, "slow_down")) return 0;
    pthread_mutex_lock(&trava); codigoDisp[0] = 0; pthread_mutex_unlock(&trava);
    printf("[youtube] entrada recusada ou expirada (%s)\n", erro[0] ? erro : "?");
    fflush(stdout);
    return -1;
  }
}
