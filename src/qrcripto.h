// QR TIME backup encryption, built only from SHA-256 (FIPS 180-4) so it runs
// the same on every TV without libcrypto:
//   key       = PBKDF2-HMAC-SHA256(password, salt 16 random bytes, QRC_VOLTAS) -> 64 bytes
//               (32 for the stream, 32 for the tag: two independent keys)
//   stream    = HMAC-SHA256(kEnc, nonce || counter) blocks, XORed (a PRF in
//               counter mode)
//   tag       = HMAC-SHA256(kMac, header || ciphertext): encrypt-then-MAC,
//               checked in constant time BEFORE anything is decrypted
// File: "QRTB1\n" | salt[16] | voltas (4, big endian) | nonce[16] | ciphertext | tag[32].
// Checked against RFC 4231 (HMAC) and the PBKDF2-HMAC-SHA256 vectors of
// RFC 7914 section 11 (tests/qrtime_backup.c).
#ifndef QRT_QRCRIPTO_H
#define QRT_QRCRIPTO_H
#include <stddef.h>

#define QRC_VOLTAS 60000
#define QRC_CAB    (6 + 16 + 4 + 16)
#define QRC_TAG    32

void qrc_sha256(const unsigned char *p, size_t n, unsigned char out[32]);
void qrc_hmac(const unsigned char *k, size_t nk, const unsigned char *p, size_t n, unsigned char out[32]);
void qrc_pbkdf2(const char *senha, const unsigned char *sal, size_t nsal, unsigned voltas, unsigned char *out, size_t nout);
// Encrypts `p` (n bytes). malloc'd result, *nout set. NULL on failure
// (no randomness, no memory, empty password).
unsigned char *qrc_cifrar(const char *senha, const unsigned char *p, size_t n, size_t *nout);
// 1 = tag ok and *out (malloc'd, NUL-terminated) holds the plaintext;
// 0 = wrong password or damaged file (nothing decrypted); -1 = not a QR TIME backup.
int qrc_decifrar(const char *senha, const unsigned char *c, size_t n, unsigned char **out, size_t *nout);
// Tests: fixed salt/nonce instead of random ones (NULL = random again).
void qrc_teste_aleatorio(const unsigned char *fixo32);
#endif
