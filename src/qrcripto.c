// Ver qrcripto.h.
#include "qrcripto.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { uint32_t h[8]; unsigned char b[64]; uint64_t n; unsigned k; } Sha;
static const uint32_t K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void bloco(Sha *s, const unsigned char *p) {
  uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
  int i;
  for (i = 0; i < 16; i++) w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
  for (; i < 64; i++) {
    uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
    uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
  for (i = 0; i < 64; i++) {
    t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
    t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}
static void ini(Sha *s) {
  static const uint32_t I[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
  memcpy(s->h, I, sizeof I); s->n = 0; s->k = 0;
}
static void add(Sha *s, const unsigned char *p, size_t n) {
  while (n--) { s->b[s->k++] = *p++; s->n++; if (s->k == 64) { bloco(s, s->b); s->k = 0; } }
}
static void fim(Sha *s, unsigned char out[32]) {
  uint64_t bits = s->n * 8;
  s->b[s->k++] = 0x80;
  if (s->k > 56) { while (s->k < 64) s->b[s->k++] = 0; bloco(s, s->b); s->k = 0; }
  while (s->k < 56) s->b[s->k++] = 0;
  for (int i = 7; i >= 0; i--) s->b[s->k++] = (unsigned char)(bits >> (i * 8));
  bloco(s, s->b);
  for (int i = 0; i < 8; i++) { out[i*4] = (unsigned char)(s->h[i] >> 24); out[i*4+1] = (unsigned char)(s->h[i] >> 16); out[i*4+2] = (unsigned char)(s->h[i] >> 8); out[i*4+3] = (unsigned char)s->h[i]; }
}
void qrc_sha256(const unsigned char *p, size_t n, unsigned char out[32]) { Sha s; ini(&s); add(&s, p, n); fim(&s, out); }

// HMAC with the inner/outer states precomputed (PBKDF2 calls it 60000x).
typedef struct { Sha in, out; } Hmac;
static void hmacIni(Hmac *h, const unsigned char *k, size_t nk) {
  unsigned char kb[64], ip[64], op[64];
  memset(kb, 0, sizeof kb);
  if (nk > 64) qrc_sha256(k, nk, kb); else memcpy(kb, k, nk);
  for (int i = 0; i < 64; i++) { ip[i] = kb[i] ^ 0x36; op[i] = kb[i] ^ 0x5c; }
  ini(&h->in); add(&h->in, ip, 64);
  ini(&h->out); add(&h->out, op, 64);
  memset(kb, 0, sizeof kb);
}
static void hmacUm(const Hmac *h, const unsigned char *p, size_t n, const unsigned char *p2, size_t n2, unsigned char out[32]) {
  Sha a = h->in, b = h->out;
  unsigned char d[32];
  add(&a, p, n);
  if (p2) add(&a, p2, n2);
  fim(&a, d);
  add(&b, d, 32);
  fim(&b, out);
}
void qrc_hmac(const unsigned char *k, size_t nk, const unsigned char *p, size_t n, unsigned char out[32]) {
  Hmac h; hmacIni(&h, k, nk); hmacUm(&h, p, n, NULL, 0, out);
}
void qrc_pbkdf2(const char *senha, const unsigned char *sal, size_t nsal, unsigned voltas, unsigned char *out, size_t nout) {
  Hmac h;
  hmacIni(&h, (const unsigned char *)senha, strlen(senha));
  for (uint32_t bi = 1; nout; bi++) {
    unsigned char be[4] = { (unsigned char)(bi >> 24), (unsigned char)(bi >> 16), (unsigned char)(bi >> 8), (unsigned char)bi };
    unsigned char u[32], t[32];
    size_t m = nout < 32 ? nout : 32;
    hmacUm(&h, sal, nsal, be, 4, u);
    memcpy(t, u, 32);
    for (unsigned j = 1; j < voltas; j++) { hmacUm(&h, u, 32, NULL, 0, u); for (int x = 0; x < 32; x++) t[x] ^= u[x]; }
    memcpy(out, t, m);
    out += m; nout -= m;
  }
}

static const unsigned char *fixo;
void qrc_teste_aleatorio(const unsigned char *f) { fixo = f; }
static int aleatorio(unsigned char *d, size_t n, size_t off) {
  int f;
  size_t l = 0;
  if (fixo) { memcpy(d, fixo + off, n); return 1; }
  if ((f = open("/dev/urandom", O_RDONLY)) < 0) return 0;
  while (l < n) { ssize_t r = read(f, d + l, n - l); if (r <= 0) { close(f); return 0; } l += (size_t)r; }
  close(f);
  return 1;
}
static void fluxo(const unsigned char *kEnc, const unsigned char *nonce, unsigned char *p, size_t n) {
  Hmac h;
  unsigned char ctr[20], ks[32];
  hmacIni(&h, kEnc, 32);
  memcpy(ctr, nonce, 16);
  for (uint32_t c = 0; n; c++) {
    size_t m = n < 32 ? n : 32;
    ctr[16] = (unsigned char)(c >> 24); ctr[17] = (unsigned char)(c >> 16); ctr[18] = (unsigned char)(c >> 8); ctr[19] = (unsigned char)c;
    hmacUm(&h, ctr, 20, NULL, 0, ks);
    for (size_t i = 0; i < m; i++) p[i] ^= ks[i];
    p += m; n -= m;
  }
  memset(ks, 0, sizeof ks);
}
static void chaves(const char *senha, const unsigned char *sal, unsigned voltas, unsigned char k[64]) { qrc_pbkdf2(senha, sal, 16, voltas, k, 64); }

unsigned char *qrc_cifrar(const char *senha, const unsigned char *p, size_t n, size_t *nout) {
  unsigned char *o, k[64];
  if (!senha || !senha[0] || !nout) return NULL;
  o = malloc(QRC_CAB + n + QRC_TAG);
  if (!o) return NULL;
  memcpy(o, "QRTB1\n", 6);
  if (!aleatorio(o + 6, 16, 0) || !aleatorio(o + 26, 16, 16)) { free(o); return NULL; }
  o[22] = (unsigned char)(QRC_VOLTAS >> 24); o[23] = (unsigned char)(QRC_VOLTAS >> 16); o[24] = (unsigned char)(QRC_VOLTAS >> 8); o[25] = (unsigned char)QRC_VOLTAS;
  chaves(senha, o + 6, QRC_VOLTAS, k);
  if (n) memcpy(o + QRC_CAB, p, n);
  fluxo(k, o + 26, o + QRC_CAB, n);
  qrc_hmac(k + 32, 32, o, QRC_CAB + n, o + QRC_CAB + n);
  memset(k, 0, sizeof k);
  *nout = QRC_CAB + n + QRC_TAG;
  return o;
}
int qrc_decifrar(const char *senha, const unsigned char *c, size_t n, unsigned char **out, size_t *nout) {
  unsigned char k[64], t[32], dif = 0, *o;
  unsigned voltas;
  size_t m;
  if (out) *out = NULL;
  if (!c || n < QRC_CAB + QRC_TAG || memcmp(c, "QRTB1\n", 6)) return -1;
  voltas = (unsigned)c[22] << 24 | (unsigned)c[23] << 16 | (unsigned)c[24] << 8 | c[25];
  if (voltas < 1000 || voltas > 2000000) return -1;
  if (!senha || !senha[0]) return 0;
  m = n - QRC_CAB - QRC_TAG;
  chaves(senha, c + 6, voltas, k);
  qrc_hmac(k + 32, 32, c, QRC_CAB + m, t);
  for (int i = 0; i < 32; i++) dif |= (unsigned char)(t[i] ^ c[QRC_CAB + m + i]);   // constant time
  if (dif) { memset(k, 0, sizeof k); return 0; }
  o = malloc(m + 1);
  if (!o) { memset(k, 0, sizeof k); return 0; }
  memcpy(o, c + QRC_CAB, m);
  fluxo(k, c + 26, o, m);
  o[m] = 0;
  memset(k, 0, sizeof k);
  *out = o; *nout = m;
  return 1;
}
