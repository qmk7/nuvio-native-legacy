#include "mccat.h"
#include "mcfila.h"
#include "mcfmt.h"
#include "mcfontes.h"
#include "subsonic.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// Optional modules (Last.fm, LRCLIB): weak so the tests can link without them.
__attribute__((weak)) void mclfm_agora(const McFaixa *f) { (void)f; }
__attribute__((weak)) void mclfm_tocou(const McFaixa *f, long long inicioMs) { (void)f; (void)inicioMs; }
__attribute__((weak)) int  mclrc_buscar(const McFaixa *f, char *lrc, size_t n) { (void)f; (void)lrc; (void)n; return MC_ERRO_NAO_SUPORTA; }

static void qual(char *d, size_t n, const char *chave, const char *local) {
  char t[MC_ID];
  if (!local[0]) { d[0] = 0; return; }
  snprintf(t, sizeof t, "%s:%s", chave, local);
  snprintf(d, n, "%s", t);
}
static void qualFaixa(McFaixa *f, const char *chave) {
  qual(f->id, sizeof f->id, chave, f->id);
  qual(f->albumId, sizeof f->albumId, chave, f->albumId);
  qual(f->artistaId, sizeof f->artistaId, chave, f->artistaId);
}
static void qualAlbum(McAlbum *a, const char *chave) {
  qual(a->id, sizeof a->id, chave, a->id);
  qual(a->artistaId, sizeof a->artistaId, chave, a->artistaId);
}
// The source of a qualified id. 1 ok.
static int fonteDe(const char *id, McFonte *f, const char **local) {
  char k[16];
  return mcfontes_partir(id, k, sizeof k, local) && mcfontes_achar(k, f);
}
// Merge-keep: the first error is the answer only when nothing worked.

int mccat_artistas(McArtista *out, int max, int *n) {
  int e = MC_ERRO_NAO_SUPORTA, algum = 0;
  McFonte f;
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_artistas(&f.ss, out + *n, max - *n, &m);
    if (r != MC_OK) { e = r; continue; }
    algum = 1;
    for (int k = *n; k < *n + m; k++) {
      int dup = 0;
      qual(out[k].id, sizeof out[k].id, f.chave, out[k].id);
      for (int j = 0; j < *n && !dup; j++) dup = !strcasecmp(out[j].nome, out[k].nome);
      if (dup) { memmove(&out[k], &out[k + 1], sizeof *out * (size_t)(*n + m - k - 1)); m--; k--; }
    }
    *n += m;
  }
  return algum ? MC_OK : e;
}
int mccat_artista_albuns(const char *id, McAlbum *out, int max, int *n) {
  McFonte f;
  const char *loc;
  int r;
  *n = 0;
  if (!fonteDe(id, &f, &loc) || f.tipo != MCS_SUBSONIC) return MC_ERRO_NAO_SUPORTA;
  r = ss_artista_albuns(&f.ss, loc, out, max, n);
  for (int i = 0; i < *n; i++) qualAlbum(&out[i], f.chave);
  return r;
}
static int cmpAlbum(const void *a, const void *b) { return strcasecmp(((const McAlbum *)a)->nome, ((const McAlbum *)b)->nome); }
int mccat_albuns(const char *tipo, int offset, McAlbum *out, int max, int *n) {
  int e = MC_ERRO_NAO_SUPORTA, algum = 0, fontes = 0;
  McFonte f;
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    fontes++;
    r = ss_albuns(&f.ss, tipo, NULL, offset, out + *n, max - *n, &m);
    if (r != MC_OK) { e = r; continue; }
    algum = 1;
    for (int k = *n; k < *n + m; k++) qualAlbum(&out[k], f.chave);
    *n += m;
  }
  if (fontes > 1 && tipo && !strcmp(tipo, "alphabeticalByName")) qsort(out, (size_t)*n, sizeof *out, cmpAlbum);
  return algum ? MC_OK : e;
}
int mccat_album(const char *id, McAlbum *info, McFaixa *out, int max, int *n) {
  McFonte f;
  const char *loc;
  int r;
  *n = 0;
  if (!fonteDe(id, &f, &loc) || f.tipo != MCS_SUBSONIC) return MC_ERRO_NAO_SUPORTA;
  r = ss_album(&f.ss, loc, info, out, max, n);
  if (info && r == MC_OK) qualAlbum(info, f.chave);
  for (int i = 0; i < *n; i++) qualFaixa(&out[i], f.chave);
  return r;
}
int mccat_generos(McGenero *out, int max, int *n) {
  int e = MC_ERRO_NAO_SUPORTA, algum = 0;
  McFonte f;
  McGenero tmp[200];
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_generos(&f.ss, tmp, 200, &m);
    if (r != MC_OK) { e = r; continue; }
    algum = 1;
    for (int k = 0; k < m; k++) {
      int j;
      for (j = 0; j < *n && strcasecmp(out[j].nome, tmp[k].nome); j++) {}
      if (j < *n) { out[j].faixas += tmp[k].faixas; out[j].albuns += tmp[k].albuns; }
      else if (*n < max) out[(*n)++] = tmp[k];
    }
  }
  return algum ? MC_OK : e;
}
int mccat_genero_faixas(const char *genero, int offset, McFaixa *out, int max, int *n) {
  int e = MC_ERRO_NAO_SUPORTA, algum = 0;
  McFonte f;
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_genero_faixas(&f.ss, genero, offset, out + *n, max - *n, &m);
    if (r != MC_OK) { e = r; continue; }
    algum = 1;
    for (int k = *n; k < *n + m; k++) qualFaixa(&out[k], f.chave);
    *n += m;
  }
  return algum ? MC_OK : e;
}
int mccat_playlists(McPlaylist *out, int max, int *n) {
  int e = MC_OK;
  McFonte f;
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_playlists(&f.ss, out + *n, max - *n, &m);
    if (r != MC_OK) { e = r; continue; }
    for (int k = *n; k < *n + m; k++) qual(out[k].id, sizeof out[k].id, f.chave, out[k].id);
    *n += m;
  }
  for (int i = 0; i < mcl_n() && *n < max; i++) {
    McPlaylist *p = &out[(*n)++];
    memset(p, 0, sizeof *p);
    snprintf(p->id, sizeof p->id, "lc:%d", i);
    snprintf(p->nome, sizeof p->nome, "%s", mcl_nome(i));
    snprintf(p->dono, sizeof p->dono, "local");
    p->faixas = mcl_faixas(i, NULL);
  }
  return *n ? MC_OK : e;
}
int mccat_playlist(const char *id, McFaixa *out, int max, int *n) {
  McFonte f;
  const char *loc;
  int r;
  *n = 0;
  if (!strncmp(id, "lc:", 3)) {
    const McFaixa *v;
    int m = mcl_faixas(atoi(id + 3), &v);
    for (int i = 0; i < m && *n < max; i++) out[(*n)++] = v[i];
    return MC_OK;
  }
  if (!fonteDe(id, &f, &loc) || f.tipo != MCS_SUBSONIC) return MC_ERRO_NAO_SUPORTA;
  r = ss_playlist(&f.ss, loc, NULL, out, max, n);
  for (int i = 0; i < *n; i++) qualFaixa(&out[i], f.chave);
  return r;
}
int mccat_buscar(const char *q, McArtista *ar, int maxAr, int *nAr, McAlbum *al, int maxAl, int *nAl,
                 McFaixa *fx, int maxFx, int *nFx) {
  int e = MC_ERRO_NAO_SUPORTA, algum = 0;
  McFonte f;
  *nAr = *nAl = *nFx = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int a = 0, b = 0, c = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_buscar(&f.ss, q, 0, ar + *nAr, maxAr - *nAr, &a, al + *nAl, maxAl - *nAl, &b, fx + *nFx, maxFx - *nFx, &c);
    if (r != MC_OK) { e = r; continue; }
    algum = 1;
    for (int k = *nAr; k < *nAr + a; k++) qual(ar[k].id, sizeof ar[k].id, f.chave, ar[k].id);
    for (int k = *nAl; k < *nAl + b; k++) qualAlbum(&al[k], f.chave);
    for (int k = *nFx; k < *nFx + c; k++) qualFaixa(&fx[k], f.chave);
    *nAr += a; *nAl += b; *nFx += c;
  }
  // Local playlists' and favourites' WebDAV tracks match by title too.
  for (int i = 0; i < mcv_n() && *nFx < maxFx; i++) {
    const McFaixa *v = mcv_item(i);
    if (!strncmp(v->id, "dv", 2) && (strcasestr(v->titulo, q) || strcasestr(v->artista, q))) fx[(*nFx)++] = *v;
  }
  return algum || *nFx ? MC_OK : (mcfontes_n() ? e : MC_OK);
}
int mccat_favoritos(McFaixa *out, int max, int *n) {
  int e = MC_OK;
  McFonte f;
  *n = 0;
  for (int i = 0; mcfontes_copiar(i, &f); i++) {
    int m = 0, r;
    if (f.tipo != MCS_SUBSONIC) continue;
    r = ss_favoritos(&f.ss, out + *n, max - *n, &m);
    if (r != MC_OK) { e = r; continue; }
    for (int k = *n; k < *n + m; k++) qualFaixa(&out[k], f.chave);
    *n += m;
  }
  for (int i = 0; i < mcv_n() && *n < max; i++) out[(*n)++] = *mcv_item(i);
  return *n ? MC_OK : e;
}
int mccat_favoritar(McFaixa *f, int liga) {
  McFonte fo;
  const char *loc;
  if (fonteDe(f->id, &fo, &loc) && fo.tipo == MCS_SUBSONIC) {
    int r = ss_favoritar(&fo.ss, loc, liga);
    if (r == MC_OK) f->fav = liga;
    return r;
  }
  // WebDAV (no server favourites): kept on this TV.
  if (mcv_fav(f->id) != (liga != 0)) mcv_alternar(f);
  f->fav = liga;
  return MC_OK;
}
int mccat_salvar_playlist(const char *nome, const McFaixa *f, int n, int *servidor) {
  char k0[16] = "", k[16];
  int mesmo = n > 0;
  *servidor = 0;
  for (int i = 0; i < n && mesmo; i++) {
    if (!mcfontes_partir(f[i].id, k, sizeof k, NULL) || strncmp(k, "ss", 2)) mesmo = 0;
    else if (!k0[0]) snprintf(k0, sizeof k0, "%s", k);
    else if (strcmp(k, k0)) mesmo = 0;
  }
  if (mesmo) {
    McFonte fo;
    const char **ids = malloc(sizeof *ids * (size_t)n);
    char novo[96];
    int r;
    if (!ids || !mcfontes_achar(k0, &fo)) { free(ids); return MC_ERRO_OUTRO; }
    for (int i = 0; i < n; i++) ids[i] = strchr(f[i].id, ':') + 1;
    r = ss_playlist_criar(&fo.ss, nome, ids, n, novo, sizeof novo);
    free(ids);
    if (r == MC_OK) { *servidor = 1; return MC_OK; }
    // A server that refuses (read-only user): keep it locally instead.
  }
  {
    int i = mcl_criar(nome);
    if (i < 0) return MC_ERRO_OUTRO;
    for (int j = 0; j < n; j++) mcl_adicionar(i, &f[j]);
  }
  return MC_OK;
}

// ---------------------------------------------------------------- WebDAV
static void tituloDoArquivo(const char *nome, McFaixa *f) {
  const char *p = nome;
  char t[256], *ext;
  // "01 - Title.flac" -> track 1, "Title"
  if (p[0] >= '0' && p[0] <= '9') {
    int num = atoi(p);
    while (*p >= '0' && *p <= '9') p++;
    while (*p == ' ' || *p == '-' || *p == '.' || *p == '_') p++;
    if (*p) f->faixa = num; else p = nome;
  }
  snprintf(t, sizeof t, "%s", p);
  if ((ext = strrchr(t, '.'))) { snprintf(f->formato, sizeof f->formato, "%s", ext + 1); for (char *c = f->formato; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32; *ext = 0; }
  snprintf(f->titulo, sizeof f->titulo, "%s", t);
}
int mccat_pasta(const char *fonte, const char *href, DavItem *pastas, int maxP, int *nP, McFaixa *arqs, int maxA, int *nA) {
  McFonte f;
  DavItem *v;
  int n = 0, r;
  *nP = *nA = 0;
  if (!mcfontes_achar(fonte, &f) || f.tipo != MCS_WEBDAV) return MC_ERRO_NAO_SUPORTA;
  v = calloc(600, sizeof *v);
  if (!v) return MC_ERRO_OUTRO;
  r = dav_listar(&f.dv, href, v, 600, &n);
  for (int i = 0; i < n; i++) {
    if (v[i].pasta) { if (*nP < maxP) pastas[(*nP)++] = v[i]; continue; }
    if (*nA < maxA) {
      McFaixa *a = &arqs[(*nA)++];
      memset(a, 0, sizeof *a);
      snprintf(a->id, sizeof a->id, "%s:%s", f.chave, v[i].href);
      tituloDoArquivo(v[i].nome, a);
      a->tam = v[i].tam;
      snprintf(a->mime, sizeof a->mime, "%s", v[i].mime);
      a->fav = mcv_fav(a->id);
    }
  }
  free(v);
  return r == DAV_OK ? MC_OK : r == DAV_ERRO_LOGIN ? MC_ERRO_LOGIN : r == DAV_ERRO_REDE ? MC_ERRO_REDE : MC_ERRO_SERVIDOR;
}
int mccat_tags(McFaixa *v, int n) {
  for (int i = 0; i < n; i++) {
    McFonte f;
    const char *loc;
    unsigned char *b;
    long tam;
    long long total;
    int e;
    size_t prec = 0;
    McFmt m;
    McFaixa t;
    if (!fonteDe(v[i].id, &f, &loc) || f.tipo != MCS_WEBDAV) continue;
    b = dav_trecho(&f.dv, loc, 0, 65535, &tam, &total, &e);
    if (!b) continue;
    t = v[i];
    t.titulo[0] = 0;
    mcfmt_ler(b, (size_t)tam, total, &m, &t, &prec);
    free(b);
    if (prec && prec != (size_t)-1 && prec <= 4u << 20) {   // a big cover first: read up to the tags (4 MiB max)
      b = dav_trecho(&f.dv, loc, 0, (long long)prec - 1, &tam, &total, &e);
      if (b) { t = v[i]; t.titulo[0] = 0; mcfmt_ler(b, (size_t)tam, total, &m, &t, &prec); free(b); }
    }
    if (!t.titulo[0]) snprintf(t.titulo, sizeof t.titulo, "%s", v[i].titulo);
    if (!t.formato[0]) snprintf(t.formato, sizeof t.formato, "%s", v[i].formato);
    if (total > 0) t.tam = total;
    v[i] = t;
  }
  return MC_OK;
}
int mccat_letra(const McFaixa *f, char *lrc, size_t n) {
  McFonte fo;
  const char *loc;
  lrc[0] = 0;
  if (fonteDe(f->id, &fo, &loc) && fo.tipo == MCS_SUBSONIC) {
    McFaixa x = *f;
    snprintf(x.id, sizeof x.id, "%s", loc);
    if (ss_letra(&fo.ss, &x, lrc, n) == MC_OK && lrc[0]) return MC_OK;
  }
  return mclrc_buscar(f, lrc, n);
}
void mccat_capa(const char *id, const char *capa, int tam, char *url, size_t n) {
  McFonte f;
  url[0] = 0;
  if (!capa || !capa[0] || !fonteDe(id, &f, NULL) || f.tipo != MCS_SUBSONIC) return;
  ss_url_capa(&f.ss, capa, tam, url, n);
}
int mccat_testar(int tipo, const char *url, const char *usuario, const char *senha, char *desc, size_t n) {
  desc[0] = 0;
  if (tipo == MCS_SUBSONIC) {
    SsServidor s;
    SsInfo in;
    int r;
    memset(&s, 0, sizeof s);
    snprintf(s.url, sizeof s.url, "%s", url); snprintf(s.usuario, sizeof s.usuario, "%s", usuario); snprintf(s.senha, sizeof s.senha, "%s", senha);
    r = ss_ping(&s, &in);
    if (r == MC_OK) snprintf(desc, n, "%s%s%s%s", in.tipo[0] == 'n' ? "Navidrome" : in.tipo, in.versao[0] ? " " : "", in.versao,
                             in.openSubsonic ? " · OpenSubsonic" : "");
    return r;
  }
  if (tipo == MCS_WEBDAV) {
    DavServidor d;
    DavItem v[64];
    int m = 0, r, np = 0, na = 0;
    memset(&d, 0, sizeof d);
    snprintf(d.url, sizeof d.url, "%s", url); snprintf(d.usuario, sizeof d.usuario, "%s", usuario); snprintf(d.senha, sizeof d.senha, "%s", senha);
    r = dav_listar(&d, "", v, 64, &m);
    for (int i = 0; i < m; i++) { if (v[i].pasta) np++; else na++; }
    if (r == DAV_OK) { snprintf(desc, n, "WebDAV · %d / %d", np, na); return MC_OK; }
    return r == DAV_ERRO_LOGIN ? MC_ERRO_LOGIN : r == DAV_ERRO_REDE ? MC_ERRO_REDE : MC_ERRO_SERVIDOR;
  }
  return MC_ERRO_OUTRO;
}

// ---------------------------------------------------------------- player hooks
static int ganchoUrl(const McFaixa *t, char *url, size_t n, char *cab, size_t nc) {
  McFonte f;
  const char *loc;
  url[0] = cab[0] = 0;
  if (!fonteDe(t->id, &f, &loc)) return 0;
  if (f.tipo == MCS_SUBSONIC) { ss_url_stream(&f.ss, loc, url, n); return 1; }
  dav_url(&f.dv, loc, url, n);
  dav_cabecalho(&f.dv, cab, nc);
  return url[0] != 0;
}
typedef struct { McFaixa f; long long quando; int sub; } Scrob;
static void *fioScrob(void *u) {
  Scrob *s = u;
  McFonte f;
  const char *loc;
  if (fonteDe(s->f.id, &f, &loc) && f.tipo == MCS_SUBSONIC) ss_scrobble(&f.ss, loc, s->quando, s->sub);
  if (s->sub) mclfm_tocou(&s->f, s->quando); else mclfm_agora(&s->f);
  free(s);
  return NULL;
}
static void scrob(const McFaixa *t, long long quando, int sub) {
  pthread_t th;
  Scrob *s = malloc(sizeof *s);
  if (!s) return;
  s->f = *t; s->quando = quando; s->sub = sub;
  if (pthread_create(&th, NULL, fioScrob, s) != 0) { free(s); return; }
  pthread_detach(th);
}
static void ganchoAgora(const McFaixa *t) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  scrob(t, (long long)ts.tv_sec * 1000, 0);
}
static void ganchoTocou(const McFaixa *t, long long inicioMs) { scrob(t, inicioMs, 1); }
static int ganchoMais(const McFaixa *base, McFaixa *out, int max) {
  McFonte f;
  int n = 0;
  if (!fonteDe(base->id, &f, NULL) || f.tipo != MCS_SUBSONIC) return 0;
  // Same genre first; any genre when that is thin.
  if (base->genero[0]) ss_aleatorias(&f.ss, base->genero, out, max, &n);
  if (n < max / 2) { int m = 0; ss_aleatorias(&f.ss, NULL, out + n, max - n, &m); n += m; }
  for (int i = 0; i < n; i++) qualFaixa(&out[i], f.chave);
  return n;
}
static const McGanchos G = { ganchoUrl, ganchoAgora, ganchoTocou, ganchoMais };
const McGanchos *mccat_ganchos(void) { return &G; }
