// Ver songid.h.
#include "songid.h"
#include "dados.h"
#include "js.h"
#include "rede.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JANELA   (4L << 20)     // one Range request
#define TETO_REDE (96L << 20)   // never more than this per sample
#define TETO_SAIDA (3L << 20)   // the sample itself

static char base[200] = "https://api.audd.io/";
void songid_definir_base(const char *u) { snprintf(base, sizeof base, "%s", u && u[0] ? u : "https://api.audd.io/"); }

int songid_tem_chave(void) { char *t = dados_ler("audd.txt"); int r = t && t[0] && t[0] != '\n'; free(t); return r; }
int songid_definir_chave(const char *tok) {
  if (!tok || !tok[0]) return dados_apagar("audd.txt"), 1;
  for (const char *p = tok; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return 0;
  return dados_gravar("audd.txt", tok);
}

// ---------------------------------------------------------------- reading the file in windows
typedef struct { const char *url; unsigned char *b; long long ini; long n; long long baixado; int erro; } Leitor;
static int garantir(Leitor *L, long long off, long n) {
  long tam = 0;
  int st = 0, er = 0;
  if (L->b && off >= L->ini && off + n <= L->ini + L->n) return 1;
  if (L->baixado > TETO_REDE) { L->erro = SID_REDE; return 0; }
  free(L->b); L->b = NULL; L->n = 0;
  {
    long pedir = n > JANELA ? n : JANELA;
    L->b = (unsigned char *)rede_baixar_trecho_st(L->url, 25, off, off + pedir - 1, &tam, &st, &er, NULL, 0);
  }
  if (!L->b || tam <= 0) { free(L->b); L->b = NULL; L->erro = SID_REDE; return 0; }
  L->ini = off; L->n = tam; L->baixado += tam;
  return off + n <= L->ini + L->n;
}
static const unsigned char *em(Leitor *L, long long off) { return L->b + (off - L->ini); }

// EBML variable-length numbers. Id keeps its marker bits; size drops them
// (all ones = unknown size, -1).
static int vintId(Leitor *L, long long off, uint32_t *id) {
  const unsigned char *p;
  int len = 1;
  if (!garantir(L, off, 4) && !(L->b && off < L->ini + L->n)) return 0;
  p = em(L, off);
  while (len <= 4 && !(p[0] & (0x80 >> (len - 1)))) len++;
  if (len > 4 || off + len > L->ini + L->n) return 0;
  *id = 0;
  for (int i = 0; i < len; i++) *id = (*id << 8) | p[i];
  return len;
}
static int vintTam(Leitor *L, long long off, long long *v) {
  const unsigned char *p;
  int len = 1, todos;
  if (!garantir(L, off, 8) && !(L->b && off < L->ini + L->n)) return 0;
  p = em(L, off);
  while (len <= 8 && !(p[0] & (0x80 >> (len - 1)))) len++;
  if (len > 8 || off + len > L->ini + L->n) return 0;
  *v = p[0] & (0xFF >> len);
  todos = *v == (0xFF >> len);
  for (int i = 1; i < len; i++) { *v = (*v << 8) | p[i]; if (p[i] != 0xFF) todos = 0; }
  if (todos) *v = -1;
  return len;
}
// One element header at `off`: id, payload start, payload size (-1 unknown).
static int elem(Leitor *L, long long off, uint32_t *id, long long *dado, long long *tam) {
  int a = vintId(L, off, id), b;
  if (!a) return 0;
  b = vintTam(L, off + a, tam);
  if (!b) return 0;
  *dado = off + a + b;
  return 1;
}
static uint64_t uintEm(const unsigned char *p, long long n) { uint64_t v = 0; for (long long i = 0; i < n && i < 8; i++) v = (v << 8) | p[i]; return v; }
static double floatEm(const unsigned char *p, long long n) {
  if (n == 4) { uint32_t u = (uint32_t)uintEm(p, 4); float f; memcpy(&f, &u, 4); return f; }
  if (n == 8) { uint64_t u = uintEm(p, 8); double d; memcpy(&d, &u, 8); return d; }
  return 0;
}

typedef struct { int num, tipo; char codec[32]; unsigned char asc[8]; int nAsc; double freq; int canais; } Faixa;
static int prioridade(const char *c) {
  if (!strncmp(c, "A_AAC", 5)) return 1;
  if (!strcmp(c, "A_AC3")) return 2;
  if (!strcmp(c, "A_EAC3")) return 3;
  if (!strcmp(c, "A_MPEG/L3")) return 4;
  if (!strncmp(c, "A_DTS", 5)) return 5;
  if (!strcmp(c, "A_TRUEHD")) return 6;
  return 0;   // Opus, Vorbis, FLAC, PCM...: need a container; not offered
}
static void lerFaixa(Leitor *L, long long ini, long long fim, Faixa *f) {
  memset(f, 0, sizeof *f);
  for (long long o = ini; o < fim; ) {
    uint32_t id; long long d, t;
    if (!elem(L, o, &id, &d, &t) || t < 0 || !garantir(L, d, (long)t)) return;
    const unsigned char *p = em(L, d);
    if (id == 0xD7) f->num = (int)uintEm(p, t);
    else if (id == 0x83) f->tipo = (int)uintEm(p, t);
    else if (id == 0x86) snprintf(f->codec, sizeof f->codec, "%.*s", (int)(t < 31 ? t : 31), (const char *)p);
    else if (id == 0x63A2) { f->nAsc = t > 8 ? 8 : (int)t; memcpy(f->asc, p, (size_t)f->nAsc); }
    else if (id == 0xE1) {
      for (long long a = d; a < d + t; ) {
        uint32_t ia; long long da, ta;
        if (!elem(L, a, &ia, &da, &ta) || ta < 0 || !garantir(L, da, (long)ta)) break;
        if (ia == 0xB5) f->freq = floatEm(em(L, da), ta);
        else if (ia == 0x9F) f->canais = (int)uintEm(em(L, da), ta);
        a = da + ta;
      }
    }
    o = d + t;
  }
}

static const int FREQ[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
// AudioSpecificConfig -> profile, frequency index, channels (or from the track's Audio element).
static int aacParams(const Faixa *f, int *perfil, int *fi, int *ch) {
  if (f->nAsc >= 2) {
    *perfil = (f->asc[0] >> 3) - 1;
    *fi = ((f->asc[0] & 7) << 1) | (f->asc[1] >> 7);
    *ch = (f->asc[1] >> 3) & 15;
  } else {
    *perfil = strstr(f->codec, "MAIN") ? 0 : strstr(f->codec, "SSR") ? 2 : 1;
    *fi = -1;
    for (int i = 0; i < 13; i++) if ((int)(f->freq + 0.5) == FREQ[i]) *fi = i;
    *ch = f->canais;
  }
  if (*perfil < 0 || *perfil > 3) *perfil = 1;   // HE-AAC (5) etc.: LC core in ADTS
  return *fi >= 0 && *fi < 13 && *ch > 0 && *ch < 8;
}

typedef struct { unsigned char *b; size_t n, cap; } Saida;
static int por(Saida *s, const unsigned char *p, size_t n) {
  if (s->n + n > (size_t)TETO_SAIDA) return 0;
  if (s->n + n > s->cap) {
    size_t c = s->cap ? s->cap * 2 : 1 << 16;
    while (c < s->n + n) c *= 2;
    unsigned char *nb = realloc(s->b, c);
    if (!nb) return 0;
    s->b = nb; s->cap = c;
  }
  memcpy(s->b + s->n, p, n); s->n += n;
  return 1;
}
static int quadro(Saida *s, const Faixa *f, int aac, int perfil, int fi, int ch, const unsigned char *p, size_t n) {
  if (aac) {
    unsigned len = (unsigned)n + 7;
    unsigned char h[7] = { 0xFF, 0xF1, (unsigned char)(((perfil & 3) << 6) | ((fi & 15) << 2) | ((ch >> 2) & 1)),
                           (unsigned char)(((ch & 3) << 6) | ((len >> 11) & 3)), (unsigned char)((len >> 3) & 0xFF),
                           (unsigned char)(((len & 7) << 5) | 0x1F), 0xFC };
    if (len > 8191 || !por(s, h, 7)) return 0;
  }
  (void)f;
  return por(s, p, n);
}
// The frames of one Block (lacing: none, Xiph, fixed, EBML).
static int blocoQuadros(Saida *s, const Faixa *f, int aac, int perfil, int fi, int ch, const unsigned char *p, long long n, int cab) {
  int lac = (p[cab - 1] >> 1) & 3;
  const unsigned char *d = p + cab;
  long long resto = n - cab;
  if (resto <= 0) return 1;
  if (!lac) return quadro(s, f, aac, perfil, fi, ch, d, (size_t)resto);
  {
    int q = d[0] + 1;
    long long tam[256], usado = 0;
    const unsigned char *x = d + 1;
    resto -= 1;
    if (lac == 2) { for (int i = 0; i < q; i++) tam[i] = resto / q; }
    else if (lac == 1) {
      for (int i = 0; i < q - 1; i++) { long long v = 0; while (resto > 0 && *x == 0xFF) { v += 255; x++; resto--; } if (resto <= 0) return 1; v += *x++; resto--; tam[i] = v; usado += v; }
      tam[q - 1] = resto - usado;
    } else {
      // EBML lacing: first size a vint, then signed differences.
      long long v; int len = 1;
      while (len <= 8 && !(x[0] & (0x80 >> (len - 1)))) len++;
      if (len > 8 || len > resto) return 1;
      v = x[0] & (0xFF >> len); for (int i = 1; i < len; i++) v = (v << 8) | x[i];
      x += len; resto -= len; tam[0] = v; usado = v;
      for (int i = 1; i < q - 1; i++) {
        long long dv; len = 1;
        while (len <= 8 && !(x[0] & (0x80 >> (len - 1)))) len++;
        if (len > 8 || len > resto) return 1;
        dv = x[0] & (0xFF >> len); for (int k = 1; k < len; k++) dv = (dv << 8) | x[k];
        dv -= (1LL << (7 * len - 1)) - 1;
        x += len; resto -= len; tam[i] = tam[i - 1] + dv; usado += tam[i];
      }
      tam[q - 1] = resto - usado;
    }
    for (int i = 0; i < q; i++) {
      if (tam[i] <= 0 || tam[i] > resto) return 1;
      if (!quadro(s, f, aac, perfil, fi, ch, x, (size_t)tam[i])) return 0;
      x += tam[i]; resto -= tam[i];
    }
  }
  return 1;
}

unsigned char *songid_amostra(const char *url, double seg, double segundos, size_t *nOut, char *codec, size_t nc, int *erro) {
  Leitor L = { url, NULL, 0, 0, 0, 0 };
  long long segIni = -1, cuesPos = -1, o;
  uint64_t escala = 1000000;
  Faixa fx[16];
  int nf = 0, melhor = -1;
  Saida s = { NULL, 0, 0 };
  int aac = 0, perfil = 1, fi = 0, ch = 2, fim = 0;
  *erro = SID_ERRO; *nOut = 0; if (codec && nc) codec[0] = 0;
  // EBML header, then the Segment and its children up to the first Cluster.
  {
    uint32_t id; long long d, t;
    if (!elem(&L, 0, &id, &d, &t) || id != 0x1A45DFA3 || t < 0) { *erro = L.erro ? L.erro : SID_NAO_MKV; goto fora; }
    if (!elem(&L, d + t, &id, &segIni, &t) || id != 0x18538067) { *erro = SID_NAO_MKV; goto fora; }
  }
  for (o = segIni; o < segIni + (8L << 20); ) {
    uint32_t id; long long d, t;
    if (!elem(&L, o, &id, &d, &t) || t < 0) break;
    if (id == 0x1F43B675) break;   // first Cluster: the header is over
    if (id == 0x114D9B74 && garantir(&L, d, (long)t)) {   // SeekHead: where the Cues are
      for (long long a = d; a < d + t; ) {
        uint32_t ia; long long da, ta;
        if (!elem(&L, a, &ia, &da, &ta) || ta < 0) break;
        if (ia == 0x4DBB) {
          uint32_t alvo = 0; long long pos = -1;
          for (long long b = da; b < da + ta; ) {
            uint32_t ib; long long db, tb;
            if (!elem(&L, b, &ib, &db, &tb) || tb < 0 || !garantir(&L, db, (long)tb)) break;
            if (ib == 0x53AB) alvo = (uint32_t)uintEm(em(&L, db), tb);
            else if (ib == 0x53AC) pos = (long long)uintEm(em(&L, db), tb);
            b = db + tb;
          }
          if (alvo == 0x1C53BB6B && pos >= 0) cuesPos = segIni + pos;
        }
        a = da + ta;
      }
    } else if (id == 0x1549A966 && garantir(&L, d, (long)t)) {   // Info
      for (long long a = d; a < d + t; ) {
        uint32_t ia; long long da, ta;
        if (!elem(&L, a, &ia, &da, &ta) || ta < 0 || !garantir(&L, da, (long)ta)) break;
        if (ia == 0x2AD7B1) escala = uintEm(em(&L, da), ta);
        a = da + ta;
      }
    } else if (id == 0x1654AE6B && garantir(&L, d, (long)t)) {   // Tracks
      for (long long a = d; a < d + t && nf < 16; ) {
        uint32_t ia; long long da, ta;
        if (!elem(&L, a, &ia, &da, &ta) || ta < 0) break;
        if (ia == 0xAE) { lerFaixa(&L, da, da + ta, &fx[nf]); nf++; }
        a = da + ta;
      }
    } else if (id == 0x1C53BB6B) cuesPos = o;
    o = d + t;
  }
  for (int i = 0; i < nf; i++)
    if (fx[i].tipo == 2 && prioridade(fx[i].codec) && (melhor < 0 || prioridade(fx[i].codec) < prioridade(fx[melhor].codec))) melhor = i;
  if (melhor < 0) { *erro = nf ? SID_CODEC : SID_SEM_AUDIO; goto fora; }
  if (cuesPos < 0) { *erro = SID_NAO_MKV; goto fora; }   // no index: cannot seek without reading the whole file
  if (!escala) escala = 1000000;
  {
    // The last cue point at or before the position: its Cluster.
    uint64_t alvo = (uint64_t)(seg * 1e9 / (double)escala), melhorT = 0;
    long long cluster = -1;
    uint32_t id; long long d, t;
    if (!elem(&L, cuesPos, &id, &d, &t) || id != 0x1C53BB6B || t < 0 || t > (16L << 20) || !garantir(&L, d, (long)t)) { *erro = L.erro ? L.erro : SID_NAO_MKV; goto fora; }
    for (long long a = d; a < d + t; ) {
      uint32_t ia; long long da, ta;
      if (!elem(&L, a, &ia, &da, &ta) || ta < 0) break;
      if (ia == 0xBB) {
        uint64_t ct = 0; long long cp = -1;
        for (long long b = da; b < da + ta; ) {
          uint32_t ib; long long db, tb;
          if (!elem(&L, b, &ib, &db, &tb) || tb < 0) break;
          if (ib == 0xB3) ct = uintEm(em(&L, db), tb);
          else if (ib == 0xB7)
            for (long long c = db; c < db + tb; ) {
              uint32_t ic; long long dc, tc;
              if (!elem(&L, c, &ic, &dc, &tc) || tc < 0) break;
              if (ic == 0xF1) cp = (long long)uintEm(em(&L, dc), tc);
              c = dc + tc;
            }
          b = db + tb;
        }
        if (cp >= 0 && ct <= alvo && (cluster < 0 || ct >= melhorT)) { melhorT = ct; cluster = segIni + cp; }
      }
      a = da + ta;
    }
    if (cluster < 0) { *erro = SID_NAO_MKV; goto fora; }
    {
      const Faixa *f = &fx[melhor];
      uint64_t de = alvo, ate = alvo + (uint64_t)(segundos * 1e9 / (double)escala);
      aac = !strncmp(f->codec, "A_AAC", 5);
      if (aac && !aacParams(f, &perfil, &fi, &ch)) { *erro = SID_CODEC; goto fora; }
      // Clusters from there: blocks of the chosen audio track inside [de, ate).
      for (o = cluster; !fim; ) {
        uint64_t tc = 0;
        if (!elem(&L, o, &id, &d, &t) || id != 0x1F43B675) break;
        for (long long a = d; t < 0 || a < d + t; ) {
          uint32_t ia; long long da, ta;
          if (!elem(&L, a, &ia, &da, &ta) || ta < 0) { fim = 1; break; }
          if (ia == 0x1F43B675) { t = a - d; break; }   // unknown-size cluster ended
          if (ia == 0xE7 && garantir(&L, da, (long)ta)) tc = uintEm(em(&L, da), ta);
          else if (ia == 0xA3 || ia == 0xA0) {
            long long bd = da, bt = ta;
            if (ia == 0xA0) {   // BlockGroup: its Block
              bd = -1;
              for (long long b = da; b < da + ta; ) {
                uint32_t ib; long long db, tb;
                if (!elem(&L, b, &ib, &db, &tb) || tb < 0) break;
                if (ib == 0xA1) { bd = db; bt = tb; break; }
                b = db + tb;
              }
            }
            if (bd >= 0 && garantir(&L, bd, 12)) {
              const unsigned char *p = em(&L, bd);
              long long tn; int lt = vintTam(&L, bd, &tn);
              if (lt && tn == fx[melhor].num) {
                int16_t rel = (int16_t)((p[lt] << 8) | p[lt + 1]);
                uint64_t bt0 = tc + (uint64_t)(int64_t)rel;
                if (bt0 >= ate) { fim = 1; break; }
                if (bt0 >= de && garantir(&L, bd, (long)bt)) {
                  if (!blocoQuadros(&s, f, aac, perfil, fi, ch, em(&L, bd), bt, lt + 3)) { fim = 1; break; }
                }
              }
            }
          }
          a = da + ta;
          if (L.erro) { fim = 1; break; }
        }
        if (t < 0) break;
        o = d + t;
      }
    }
    {
      const char *c = fx[melhor].codec;
      if (codec && nc) snprintf(codec, nc, "%s", aac ? "aac" : !strcmp(c, "A_AC3") ? "ac3" : !strcmp(c, "A_EAC3") ? "eac3"
                                : !strcmp(c, "A_MPEG/L3") ? "mp3" : !strncmp(c, "A_DTS", 5) ? "dts" : "truehd");
    }
  }
  if (s.n < 2000) { *erro = L.erro ? L.erro : SID_SEM_AUDIO; free(s.b); s.b = NULL; goto fora; }
  *erro = SID_OK; *nOut = s.n;
fora:
  free(L.b);
  if (*erro != SID_OK) { free(s.b); return NULL; }
  return s.b;
}

// ---------------------------------------------------------------- AudD
int songid_parse(const char *json, SidMusica *m) {
  const char *r;
  memset(m, 0, sizeof *m);
  if (!json || !strstr(json, "\"status\":\"success\"")) return SID_ERRO;
  r = strstr(json, "\"result\"");
  if (!r) return SID_NAO_ACHOU;
  r += 8;
  while (*r == ' ' || *r == ':') r++;
  if (!strncmp(r, "null", 4) || *r != '{') return SID_NAO_ACHOU;
  {
    const char *f = js_fim(r);
    js_texto_raiz_em(r, f, "title", m->titulo, sizeof m->titulo);
    js_texto_raiz_em(r, f, "artist", m->artista, sizeof m->artista);
    js_texto_raiz_em(r, f, "album", m->album, sizeof m->album);
  }
  return m->titulo[0] ? SID_OK : SID_NAO_ACHOU;
}
static char *b64url(const unsigned char *p, size_t n) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t cap = (n + 2) / 3 * 4 * 3 + 1, k = 0;
  char *o = malloc(cap);
  if (!o) return NULL;
  for (size_t i = 0; i < n; i += 3) {
    unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    char q[4] = { A[(v >> 18) & 63], A[(v >> 12) & 63], i + 1 < n ? A[(v >> 6) & 63] : '=', i + 2 < n ? A[v & 63] : '=' };
    for (int j = 0; j < 4; j++) {   // form-urlencoded: + / = escaped
      if (q[j] == '+') { memcpy(o + k, "%2B", 3); k += 3; }
      else if (q[j] == '/') { memcpy(o + k, "%2F", 3); k += 3; }
      else if (q[j] == '=') { memcpy(o + k, "%3D", 3); k += 3; }
      else o[k++] = q[j];
    }
  }
  o[k] = 0;
  return o;
}
int songid_reconhecer(const char *url, double seg, SidMusica *out) {
  char *tok = dados_ler("audd.txt"), *b64, *corpo, *r, cod[16];
  const char *cabs[2] = { "Content-Type: application/x-www-form-urlencoded", NULL };
  unsigned char *a;
  size_t n;
  int erro, st = 0, res;
  memset(out, 0, sizeof *out);
  if (!tok || !tok[0]) { free(tok); return SID_SEM_CHAVE; }
  tok[strcspn(tok, "\r\n")] = 0;
  a = songid_amostra(url, seg, 8.0, &n, cod, sizeof cod, &erro);
  if (!a) { free(tok); return erro; }
  printf("[musica-id] amostra %s: %zu bytes\n", cod, n);
  fflush(stdout);
  b64 = b64url(a, n);
  free(a);
  if (!b64) { free(tok); return SID_ERRO; }
  corpo = malloc(strlen(b64) + strlen(tok) + 64);
  if (!corpo) { free(b64); free(tok); return SID_ERRO; }
  sprintf(corpo, "api_token=%s&audio=%s", tok, b64);
  free(b64);
  memset(tok, 0, strlen(tok)); free(tok);
  r = rede_postar_st(base, 30, cabs, corpo, &st);
  free(corpo);
  res = r ? songid_parse(r, out) : SID_REDE;
  printf("[musica-id] AudD HTTP %d -> %d\n", st, res);
  fflush(stdout);
  free(r);
  return res;
}

// ---------------------------------------------------------------- background
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int rodando, pronto, res;
static SidMusica achado;
typedef struct { char url[2048]; double seg; } Pedido;
static void *fio(void *arg) {
  Pedido *p = arg;
  SidMusica m;
  int r = songid_reconhecer(p->url, p->seg, &m);
  memset(p->url, 0, sizeof p->url);
  free(p);
  pthread_mutex_lock(&trava);
  achado = m; res = r; pronto = 1; rodando = 0;
  pthread_mutex_unlock(&trava);
  return NULL;
}
int songid_iniciar(const char *url, double seg) {
  pthread_t t;
  Pedido *p;
  if (!url || !url[0] || strlen(url) >= sizeof p->url) return 0;
  pthread_mutex_lock(&trava);
  if (rodando) { pthread_mutex_unlock(&trava); return 0; }
  rodando = 1; pronto = 0;
  pthread_mutex_unlock(&trava);
  if (!(p = calloc(1, sizeof *p))) { rodando = 0; return 0; }
  snprintf(p->url, sizeof p->url, "%s", url); p->seg = seg;
  if (pthread_create(&t, NULL, fio, p)) { free(p); rodando = 0; return 0; }
  pthread_detach(t);
  return 1;
}
int songid_resultado(SidMusica *out) {
  int r = -1;
  pthread_mutex_lock(&trava);
  if (pronto) { *out = achado; r = res; pronto = 0; }
  pthread_mutex_unlock(&trava);
  return r;
}
