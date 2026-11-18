#include "webdav.h"
#include "rede.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
void dav_cabecalho(const DavServidor *s, char *linha, size_t n) {
  char par[240], b[400];
  if (!s->usuario[0]) { linha[0] = 0; return; }
  snprintf(par, sizeof par, "%s:%s", s->usuario, s->senha);
  b64((const unsigned char *)par, strlen(par), b, sizeof b);
  snprintf(linha, n, "Authorization: Basic %s", b);
  memset(par, 0, sizeof par);
}
void dav_decodificar(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; s && *s && k + 1 < n; s++) {
    if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
      char h[3] = { s[1], s[2], 0 };
      d[k++] = (char)strtol(h, NULL, 16);
      s += 2;
    } else d[k++] = *s;
  }
  d[k] = 0;
}
// scheme://host[:port] of the configured URL.
static void origem(const char *url, char *d, size_t n) {
  const char *p = strstr(url, "://");
  const char *barra = p ? strchr(p + 3, '/') : NULL;
  size_t len = barra ? (size_t)(barra - url) : strlen(url);
  snprintf(d, n, "%.*s", (int)len, url);
}
static const char *caminhoDe(const char *url) {
  const char *p = strstr(url, "://");
  p = p ? strchr(p + 3, '/') : NULL;
  return p ? p : "/";
}
void dav_url(const DavServidor *s, const char *href, char *url, size_t n) {
  char o[256];
  if (!href || !href[0]) { snprintf(url, n, "%s", s->url); return; }
  if (!strncmp(href, "http://", 7) || !strncmp(href, "https://", 8)) {
    // Only on the configured server: an href pointing elsewhere would get our credentials.
    origem(s->url, o, sizeof o);
    if (!strncasecmp(href, o, strlen(o)) && (href[strlen(o)] == '/' || !href[strlen(o)])) snprintf(url, n, "%s", href);
    else url[0] = 0;
    return;
  }
  origem(s->url, o, sizeof o);
  snprintf(url, n, "%s%s%s", o, href[0] == '/' ? "" : "/", href);
}
int dav_eh_musica(const char *nome) {
  static const char *const EXT[] = { "flac", "wav", "mp3", "m4a", "aac", "alac", "ogg", "oga", "opus", "aif", "aiff", NULL };
  const char *p = nome ? strrchr(nome, '.') : NULL;
  if (!p || nome[0] == '.') return 0;   // hidden files (._AppleDouble)
  for (int i = 0; EXT[i]; i++) if (!strcasecmp(p + 1, EXT[i])) return 1;
  return 0;
}

// ---------------------------------------------------------------- XML
// Next element whose LOCAL name is `nome` (any namespace prefix), from p.
// Returns the '<'; *corpo after '>'; *vazio for <x/>.
static const char *tag(const char *p, const char *fim, const char *nome, const char **corpo, int *vazio) {
  size_t nl = strlen(nome);
  for (; p && (p = strchr(p, '<')) && (!fim || p < fim); p++) {
    const char *q = p + 1, *loc, *f;
    if (*q == '/' || *q == '?' || *q == '!') continue;
    loc = q;
    while (*q && !isspace((unsigned char)*q) && *q != '>' && *q != '/') { if (*q == ':') loc = q + 1; q++; }
    if ((size_t)(q - loc) != nl || strncasecmp(loc, nome, nl)) continue;
    f = strchr(q, '>');
    if (!f) return NULL;
    if (vazio) *vazio = f > p && f[-1] == '/';
    if (corpo) *corpo = f + 1;
    return p;
  }
  return NULL;
}
// End of element `nome` opened before p: the '<' of its closing tag.
static const char *fecha(const char *p, const char *fim, const char *nome) {
  size_t nl = strlen(nome);
  for (; p && (p = strstr(p, "</")) && (!fim || p < fim); p += 2) {
    const char *q = p + 2, *loc = q;
    while (*q && *q != '>' && !isspace((unsigned char)*q)) { if (*q == ':') loc = q + 1; q++; }
    if ((size_t)(q - loc) == nl && !strncasecmp(loc, nome, nl)) return p;
  }
  return NULL;
}
static void textoXml(const char *a, const char *b, char *d, size_t n) {
  size_t k = 0;
  while (a < b && isspace((unsigned char)*a)) a++;
  while (b > a && isspace((unsigned char)b[-1])) b--;
  for (; a < b && k + 1 < n; a++) {
    if (*a == '&') {
      if (!strncmp(a, "&amp;", 5)) { d[k++] = '&'; a += 4; continue; }
      if (!strncmp(a, "&lt;", 4)) { d[k++] = '<'; a += 3; continue; }
      if (!strncmp(a, "&gt;", 4)) { d[k++] = '>'; a += 3; continue; }
      if (!strncmp(a, "&quot;", 6)) { d[k++] = '"'; a += 5; continue; }
      if (!strncmp(a, "&apos;", 6)) { d[k++] = '\''; a += 5; continue; }
    }
    d[k++] = *a;
  }
  d[k] = 0;
}
static int cmpItem(const void *a, const void *b) {
  const DavItem *x = a, *y = b;
  if (x->pasta != y->pasta) return y->pasta - x->pasta;
  return strcasecmp(x->nome, y->nome);
}
static void semBarra(const char *s, char *d, size_t n) {
  size_t l;
  dav_decodificar(s, d, n);
  l = strlen(d);
  while (l > 1 && d[l - 1] == '/') d[--l] = 0;
}
int dav_parse_lista(const char *xml, const char *self, DavItem *out, int max) {
  const char *p, *corpo, *fim = xml ? xml + strlen(xml) : NULL;
  char eu[768] = "";
  int n = 0;
  if (!xml || !tag(xml, fim, "multistatus", NULL, NULL)) return -1;
  if (self) semBarra(self[0] == '/' ? self : caminhoDe(self), eu, sizeof eu);
  for (p = xml; n < max && (p = tag(p, fim, "response", &corpo, NULL)); ) {
    const char *rf = fecha(corpo, fim, "response"), *c, *h;
    DavItem it;
    char hr[768] = "", cmp[768];
    int vazio = 0;
    if (!rf) break;
    memset(&it, 0, sizeof it);
    if ((h = tag(corpo, rf, "href", &c, NULL))) { const char *hf = fecha(c, rf, "href"); if (hf) textoXml(c, hf, hr, sizeof hr); }
    // Only the 200 propstat counts (a 404 one lists the properties it lacks).
    {
      const char *ps = corpo, *pc;
      while ((ps = tag(ps, rf, "propstat", &pc, NULL))) {
        const char *pf = fecha(pc, rf, "propstat"), *sc, *sf;
        char st[64] = "";
        if (!pf) break;
        if (tag(pc, pf, "status", &sc, NULL) && (sf = fecha(sc, pf, "status"))) textoXml(sc, sf, st, sizeof st);
        if (!st[0] || strstr(st, " 200")) {
          const char *rt, *rc;
          if ((rt = tag(pc, pf, "resourcetype", &rc, &vazio)) && !vazio) {
            const char *rtf = fecha(rc, pf, "resourcetype");
            if (rtf && tag(rc, rtf, "collection", NULL, NULL)) it.pasta = 1;
          }
          if (tag(pc, pf, "getcontentlength", &rc, NULL) && (rt = fecha(rc, pf, "getcontentlength"))) { char v[32]; textoXml(rc, rt, v, sizeof v); it.tam = atoll(v); }
          if (tag(pc, pf, "getcontenttype", &rc, NULL) && (rt = fecha(rc, pf, "getcontenttype"))) textoXml(rc, rt, it.mime, sizeof it.mime);
        }
        ps = pf + 1;
      }
    }
    p = rf + 1;
    if (!hr[0]) continue;
    // The listed folder itself is in the answer too.
    semBarra(hr[0] == '/' ? hr : caminhoDe(hr), cmp, sizeof cmp);
    if (eu[0] && !strcmp(cmp, eu)) continue;
    snprintf(it.href, sizeof it.href, "%s", hr);
    {
      char dec[768], *ult;
      semBarra(hr, dec, sizeof dec);
      ult = strrchr(dec, '/');
      snprintf(it.nome, sizeof it.nome, "%s", ult ? ult + 1 : dec);
    }
    if (!it.nome[0] || it.nome[0] == '.') continue;
    if (!it.pasta && !dav_eh_musica(it.nome)) continue;   // covers, cue sheets, logs
    out[n++] = it;
  }
  qsort(out, (size_t)n, sizeof *out, cmpItem);
  return n;
}

// ---------------------------------------------------------------- requests
int dav_listar(const DavServidor *s, const char *href, DavItem *out, int max, int *n) {
  static const char corpo[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getcontentlength/><d:getcontenttype/></d:prop></d:propfind>";
  char url[1200], aut[500];
  const char *cab[4] = { "Depth: 1", "Content-Type: application/xml; charset=utf-8", NULL, NULL };
  int st = 0;
  char *r;
  *n = 0;
  dav_url(s, href, url, sizeof url);
  if (!url[0]) return DAV_ERRO_OUTRO;
  dav_cabecalho(s, aut, sizeof aut);
  if (aut[0]) cab[2] = aut;
  r = rede_metodo_st("PROPFIND", url, 20, cab, corpo, &st);
  memset(aut, 0, sizeof aut);
  if (!r || st == 0) { free(r); return DAV_ERRO_REDE; }
  if (st == 401 || st == 403) { free(r); return DAV_ERRO_LOGIN; }
  if (st == 404) { free(r); return DAV_ERRO_NAO_ACHOU; }
  if (st != 207 && st != 200) { printf("[webdav] PROPFIND http %d\n", st); fflush(stdout); free(r); return DAV_ERRO_SERVIDOR; }
  *n = dav_parse_lista(r, url, out, max);
  free(r);
  if (*n < 0) { *n = 0; return DAV_ERRO_SERVIDOR; }
  return DAV_OK;
}
unsigned char *dav_trecho(const DavServidor *s, const char *href, long long ini, long long fim,
                          long *tam, long long *total, int *erro) {
  char url[1200], aut[500];
  int st = 0;
  int64_t tot = 0;
  char *r;
  *erro = DAV_OK;
  *tam = 0;
  dav_url(s, href, url, sizeof url);
  if (!url[0]) { *erro = DAV_ERRO_OUTRO; return NULL; }
  dav_cabecalho(s, aut, sizeof aut);
  r = rede_baixar_trecho64_cab(url, aut[0] ? aut : NULL, ini, fim, tam, &tot, &st, NULL);
  memset(aut, 0, sizeof aut);
  if (total) *total = tot;
  if (!r) *erro = st == 401 || st == 403 ? DAV_ERRO_LOGIN : st == 404 ? DAV_ERRO_NAO_ACHOU : st ? DAV_ERRO_SERVIDOR : DAV_ERRO_REDE;
  return (unsigned char *)r;
}
