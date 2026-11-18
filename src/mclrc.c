#include "mclrc.h"
#include "js.h"
#include "rede.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char base[200] = "https://lrclib.net/api";
void mclrc_definir_base(const char *u) { if (u && u[0]) snprintf(base, sizeof base, "%s", u); }

static void urlenc(const char *s, char *d, size_t n) {
  size_t k = 0;
  for (; s && *s && k + 4 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') d[k++] = (char)c;
    else k += (size_t)snprintf(d + k, n - k, "%%%02X", c);
  }
  d[k] = 0;
}
int mclrc_buscar(const McFaixa *f, char *lrc, size_t n) {
  // A generic client name: nothing about the person or the device.
  static const char *const cab[] = { "User-Agent: QRTIME/1.0", NULL };
  char a[400], t[400], al[400], url[1600];
  int st = 0;
  char *r;
  lrc[0] = 0;
  if (!f->artista[0] || !f->titulo[0]) return MC_ERRO_NAO_SUPORTA;
  urlenc(f->artista, a, sizeof a);
  urlenc(f->titulo, t, sizeof t);
  urlenc(f->album, al, sizeof al);
  if (f->dur > 0) snprintf(url, sizeof url, "%s/get?artist_name=%s&track_name=%s&album_name=%s&duration=%d", base, a, t, al, f->dur);
  else snprintf(url, sizeof url, "%s/get?artist_name=%s&track_name=%s&album_name=%s", base, a, t, al);
  r = rede_baixar_st(url, 12, cab, &st);
  if (!r || st == 0) { free(r); return MC_ERRO_REDE; }
  if (st == 404) { free(r); return MC_ERRO_NAO_SUPORTA; }
  if (!js_texto_linhas(r, NULL, "syncedLyrics", lrc, n) || !lrc[0]) js_texto_linhas(r, NULL, "plainLyrics", lrc, n);
  {
    const char *i = strstr(r, "\"instrumental\"");
    if (!lrc[0] && i && strstr(i, "true") && strstr(i, "true") - i < 20) snprintf(lrc, n, "%s", "\xe2\x99\xaa");   // instrumental: a note
  }
  free(r);
  return lrc[0] ? MC_OK : MC_ERRO_NAO_SUPORTA;
}
static int tempo(const char *p, int *ms, const char **fim) {
  // [mm:ss], [mm:ss.xx], [mm:ss.xxx]
  int m = 0, s = 0, frac = 0, dig = 0;
  if (*p != '[' || !isdigit((unsigned char)p[1])) return 0;
  p++;
  while (isdigit((unsigned char)*p)) m = m * 10 + (*p++ - '0');
  if (*p++ != ':' || !isdigit((unsigned char)*p)) return 0;
  while (isdigit((unsigned char)*p)) s = s * 10 + (*p++ - '0');
  if (*p == '.' || *p == ':') {
    p++;
    while (isdigit((unsigned char)*p) && dig < 3) { frac = frac * 10 + (*p++ - '0'); dig++; }
    while (isdigit((unsigned char)*p)) p++;
    if (dig == 1) frac *= 100; else if (dig == 2) frac *= 10;
  }
  if (*p != ']') return 0;
  *ms = (m * 60 + s) * 1000 + frac;
  *fim = p + 1;
  return 1;
}
static int cmpLinha(const void *a, const void *b) { return ((const McLinha *)a)->ms - ((const McLinha *)b)->ms; }
int mclrc_parse(const char *lrc, McLinha *out, int max, int *sinc) {
  int n = 0, comTempo = 0;
  const char *p = lrc;
  *sinc = 0;
  while (p && *p && n < max) {
    const char *f = strchr(p, '\n'), *c = p, *texto;
    size_t len = f ? (size_t)(f - p) : strlen(p);
    int tempos[16], nt = 0, ms;
    char lin[600];
    snprintf(lin, sizeof lin, "%.*s", (int)len, p);
    if (len && lin[len < sizeof lin ? len - 1 : sizeof lin - 2] == '\r') lin[strlen(lin) - 1] = 0;
    c = lin;
    while (nt < 16 && tempo(c, &ms, &texto)) { tempos[nt++] = ms; c = texto; }
    if (nt) {
      comTempo = 1;
      for (int i = 0; i < nt && n < max; i++) { out[n].ms = tempos[i]; snprintf(out[n].texto, sizeof out[n].texto, "%s", c); n++; }
    } else if (!(lin[0] == '[' && strchr(lin, ':') && lin[strlen(lin) - 1] == ']')) {   // skip [ar:..] [ti:..] tags
      out[n].ms = -1;
      snprintf(out[n].texto, sizeof out[n].texto, "%s", lin);
      n++;
    }
    p = f ? f + 1 : NULL;
  }
  if (comTempo) {
    int k = 0;
    for (int i = 0; i < n; i++) if (out[i].ms >= 0) out[k++] = out[i];   // untimed lines in a synced file: drop
    n = k;
    qsort(out, (size_t)n, sizeof *out, cmpLinha);
    *sinc = 1;
  } else {
    while (n && !out[n - 1].texto[0]) n--;
  }
  return n;
}
int mclrc_linha_em(const McLinha *v, int n, int ms) {
  int r = -1;
  for (int i = 0; i < n && v[i].ms <= ms; i++) r = i;
  return r;
}
