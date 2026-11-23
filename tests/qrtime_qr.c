// QR TIME: the QR generator (src/qr.c) now goes up to version 10, so the Trakt
// PKCE link (~260 bytes) fits; it used to draw an empty white square. The
// fingerprints below are of symbols that an independent decoder (jsQR 1.4.0)
// read back exactly, when this test was written: a change in the encoder that
// alters them must be checked with a decoder again.
//   bash tests/qrtime_qr.sh
#include "qr.h"
#include <stdio.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
// FNV-1a over "<side>\n" + the rows of 0/1, like the dump that was decoded.
static unsigned long long impressao(const Qr *q) {
  unsigned long long h = 1469598103934665603ULL;
  char b[8];
  int n = snprintf(b, sizeof b, "%d\n", q->lado);
  for (int i = 0; i < n; i++) h = (h ^ (unsigned char)b[i]) * 1099511628211ULL;
  for (int y = 0; y < q->lado; y++) {
    for (int x = 0; x < q->lado; x++) h = (h ^ (unsigned char)(qr_modulo(q, x, y) ? '1' : '0')) * 1099511628211ULL;
    h = (h ^ (unsigned char)'\n') * 1099511628211ULL;
  }
  return h;
}
int main(int argc, char **argv) {
  char x140[141], z220[221], w271[272], w272[273], trakt[300];
  Qr q;
  memset(x140, 'x', 140); x140[140] = 0; memset(z220, 'z', 220); z220[220] = 0;
  memset(w271, 'w', 271); w271[271] = 0; memset(w272, 'w', 272); w272[272] = 0;
  snprintf(trakt, sizeof trakt, "https://trakt.tv/oauth/authorize?response_type=code&client_id=%s&redirect_uri=urn%%3Aietf%%3Awg%%3Aoauth%%3A2.0%%3Aoob"
           "&code_challenge=%sQ&code_challenge_method=S256",
           "a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4", "Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9Zx9");
  if (argc > 1) {   // print fingerprints (to refresh after a decoder check)
    const char *t[] = { "https://nuvio.tv/tv-login?code=fa0010cad8b5d2f512e58646ab82ca6b", x140, z220, trakt, w271 };
    for (int i = 0; i < 5; i++) { qr_gerar(&q, t[i]); printf("0x%016llxULL\n", impressao(&q)); }
    return 0;
  }
  CONFERE(qr_gerar(&q, "https://nuvio.tv/tv-login?code=fa0010cad8b5d2f512e58646ab82ca6b") && q.lado == 33, "login link: version 4 as before");
  CONFERE(qr_gerar(&q, x140) && q.lado == 45, "140 bytes: version 7 (was refused)");
  CONFERE(qr_gerar(&q, z220) && q.lado == 53, "220 bytes: version 9");
  CONFERE(qr_gerar(&q, trakt) && q.lado == 57 && strlen(trakt) == 261, "Trakt PKCE link (261 bytes): version 10");
  CONFERE(qr_gerar(&q, w271) && q.lado == 57, "271 bytes: the version-10 limit");
  CONFERE(!qr_gerar(&q, w272), "272 bytes: refused (too long)");
  {
    // Version information block (v7+): BCH(18,6) of 7 is 0x07C94 (ISO 18004 table D.1).
    unsigned v = 0;
    qr_gerar(&q, x140);
    for (int i = 0; i < 18; i++) v |= (unsigned)qr_modulo(&q, q.lado - 11 + i % 3, i / 3) << i;
    CONFERE(v == 0x07C94u, "version-7 information block = 0x07C94 (ISO table D.1)");
    v = 0;
    qr_gerar(&q, trakt);
    for (int i = 0; i < 18; i++) v |= (unsigned)qr_modulo(&q, i / 3, q.lado - 11 + i % 3) << i;
    CONFERE(v == 0x0A4D3u, "version-10 information block (bottom-left copy) = 0x0A4D3");
  }
  {
    const char *t[] = { "https://nuvio.tv/tv-login?code=fa0010cad8b5d2f512e58646ab82ca6b", x140, z220, trakt, w271 };
    static const unsigned long long esperado[] = { 0x17191ed2730d768eULL, 0xe557c88dae07ac72ULL, 0xe19a23c4e0e2a762ULL, 0x6c7d417c565b7cbdULL, 0x0e078a8da5440fbbULL };
    int iguais = 1;
    for (int i = 0; i < 5; i++) { qr_gerar(&q, t[i]); if (impressao(&q) != esperado[i]) iguais = 0; }
    CONFERE(iguais, "symbols identical to the ones jsQR decoded (v4, v7, v9, v10 Trakt, v10 full)");
  }
  return falhas ? 1 : 0;
}
