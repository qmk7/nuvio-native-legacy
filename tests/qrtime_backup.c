// QR TIME backup: crypto against published vectors, round trip, tamper and
// wrong-password refusal, the file list, archive build and restore.
//   bash tests/qrtime_backup.sh
#include "qrcripto.h"
#include "qrbackup.h"
#include "dados.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
static int existe0(const char *n) { char *t = dados_ler(n); int r = t != NULL; free(t); return r; }
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static void hx(const unsigned char *b, size_t n, char *o) { for (size_t i = 0; i < n; i++) sprintf(o + 2 * i, "%02x", b[i]); }

int main(void) {
  unsigned char d[64];
  char h[129];
  qrc_sha256((const unsigned char *)"abc", 3, d); hx(d, 32, h);
  CONFERE(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "SHA-256(\"abc\") (FIPS 180-4)");
  { unsigned char k[20]; memset(k, 0x0b, 20);
    qrc_hmac(k, 20, (const unsigned char *)"Hi There", 8, d); hx(d, 32, h);
    CONFERE(!strcmp(h, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "HMAC-SHA256 RFC 4231 case 1"); }
  qrc_hmac((const unsigned char *)"Jefe", 4, (const unsigned char *)"what do ya want for nothing?", 28, d); hx(d, 32, h);
  CONFERE(!strcmp(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"), "HMAC-SHA256 RFC 4231 case 2");
  qrc_pbkdf2("passwd", (const unsigned char *)"salt", 4, 1, d, 64); hx(d, 64, h);
  CONFERE(!strcmp(h, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"),
          "PBKDF2-HMAC-SHA256 RFC 7914 (c=1, 64 bytes)");
  qrc_pbkdf2("Password", (const unsigned char *)"NaCl", 4, 80000, d, 64); hx(d, 64, h);
  CONFERE(!strcmp(h, "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"),
          "PBKDF2-HMAC-SHA256 RFC 7914 (c=80000, 64 bytes)");
  {
    const char *txt = "xtream-p1.txt secret stuff\nline2";
    size_t n, m;
    unsigned char *c = qrc_cifrar("correct horse", (const unsigned char *)txt, strlen(txt), &n), *o = NULL;
    CONFERE(c && n == QRC_CAB + strlen(txt) + QRC_TAG && !memcmp(c, "QRTB1\n", 6) && !memmem(c, n, "secret", 6), "encrypted: header, size, no plaintext inside");
    CONFERE(qrc_decifrar("correct horse", c, n, &o, &m) == 1 && m == strlen(txt) && !memcmp(o, txt, m), "decrypts with the right password");
    free(o); o = NULL;
    CONFERE(qrc_decifrar("wrong horse!", c, n, &o, &m) == 0 && !o, "wrong password: refused, nothing decrypted");
    c[QRC_CAB + 3] ^= 1;
    CONFERE(qrc_decifrar("correct horse", c, n, &o, &m) == 0 && !o, "one flipped bit: refused (tag)");
    CONFERE(qrc_decifrar("x", (const unsigned char *)"hello world, not a backup at all.....................................", 70, &o, &m) == -1, "not a backup: -1");
    {
      size_t n2; unsigned char *c2 = qrc_cifrar("correct horse", (const unsigned char *)txt, strlen(txt), &n2);
      CONFERE(c2 && memcmp(c + 6, c2 + 6, 16), "a new random salt each time");
      free(c2);
    }
    free(c);
  }
  CONFERE(qrbackup_nome_ok("xtream-p1.txt") && qrbackup_nome_ok("music-sources-p3.txt") && qrbackup_nome_ok("tsdb.txt")
          && !qrbackup_nome_ok("../xtream-p1.txt") && !qrbackup_nome_ok("ajustes.txt") && !qrbackup_nome_ok("progresso.txt")
          && !qrbackup_nome_ok("sub/xtream-p1.txt") && !qrbackup_nome_ok("catalogo-x.bin"), "file list: provider data only, no paths");
  {
    size_t n; int arq;
    char *a;
    dados_iniciar(getenv("NUVIO_DADOS"));
    dados_gravar("xtream-p1.txt", "acct\tuser\tpw\n");
    dados_gravar("agenda-follows-p2.txt", "tt1\tShow\t\n");
    dados_gravar("progresso.txt", "not in backup");
    a = qrbackup_montar(&n, &arq);
    CONFERE(a && arq == 2 && strstr(a, "F 13 xtream-p1.txt\n") && !strstr(a, "not in backup"), "archive: the listed files only");
    dados_apagar("xtream-p1.txt"); dados_apagar("agenda-follows-p2.txt");
    CONFERE(qrbackup_aplicar(a, n) == 2 && (free(dados_ler("progresso.txt")), 1), "restore writes them back");
    { char *t = dados_ler("xtream-p1.txt"); CONFERE(t && !strcmp(t, "acct\tuser\tpw\n"), "restored content identical"); free(t); }
    { const char *m = "QRTIME-BACKUP 1\nF 3 ../evil.txt\nabc\nF 3 ajustes.txt\nabc\n";
    CONFERE(qrbackup_aplicar(m, strlen(m)) == 0 && !existe0("ajustes.txt"), "names off the list or with paths: skipped"); }
    CONFERE(qrbackup_aplicar("garbage", 7) == -1, "not an archive: -1");
    free(a);
  }
  return falhas ? 1 : 0;
}
