// MD5 (RFC 1321). Only for the Subsonic token (md5(password + salt)), which is
// what that protocol defines; never used as a security primitive of our own.
#ifndef NV_MD5_H
#define NV_MD5_H
#include <stddef.h>
void md5_bytes(const void *dados, size_t n, unsigned char out[16]);
// Lowercase hex, 33 bytes with the NUL.
void md5_hex(const void *dados, size_t n, char out[33]);
#endif
