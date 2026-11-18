#include "md5.h"
#include <stdint.h>
#include <string.h>

static const uint32_t K[64] = {
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
static const unsigned char R[64] = {
  7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };

static void bloco(uint32_t h[4], const unsigned char *p) {
  uint32_t w[16], a = h[0], b = h[1], c = h[2], d = h[3];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
  for (int i = 0; i < 64; i++) {
    uint32_t f, t;
    int g;
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
    else { f = c ^ (b | ~d); g = (7 * i) & 15; }
    t = d; d = c; c = b;
    f += a + K[i] + w[g];
    b += (f << R[i]) | (f >> (32 - R[i]));
    a = t;
  }
  h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}
void md5_bytes(const void *dados, size_t n, unsigned char out[16]) {
  uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
  const unsigned char *p = dados;
  unsigned char fim[128];
  size_t i = 0, resto;
  uint64_t bits = (uint64_t)n * 8;
  for (; i + 64 <= n; i += 64) bloco(h, p + i);
  resto = n - i;
  memset(fim, 0, sizeof fim);
  if (resto) memcpy(fim, p + i, resto);
  fim[resto] = 0x80;
  {
    size_t tot = resto + 1 + 8 <= 64 ? 64 : 128;
    for (int k = 0; k < 8; k++) fim[tot - 8 + k] = (unsigned char)(bits >> (8 * k));
    bloco(h, fim);
    if (tot == 128) bloco(h, fim + 64);
  }
  for (int k = 0; k < 4; k++) for (int j = 0; j < 4; j++) out[k * 4 + j] = (unsigned char)(h[k] >> (8 * j));
}
void md5_hex(const void *dados, size_t n, char out[33]) {
  static const char hx[] = "0123456789abcdef";
  unsigned char b[16];
  md5_bytes(dados, n, b);
  for (int i = 0; i < 16; i++) { out[i * 2] = hx[b[i] >> 4]; out[i * 2 + 1] = hx[b[i] & 15]; }
  out[32] = 0;
}
