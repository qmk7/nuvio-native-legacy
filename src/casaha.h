// QR TIME: "play this film" from Home Assistant (see casaha.c).
#ifndef QRT_CASAHA_H
#define QRT_CASAHA_H
#include <stddef.h>
int  casaha_tocar(const char *tipo, const char *q);          // UI thread; 1 = being looked up
int  casaha_pegar(char *imdb, size_t n);                     // app loop: 1 = an id to open and play
int  casaha_escolher(const char *json, const char *q, char *imdb, size_t n);   // tests
void casaha_teste_base(const char *base);                    // tests
#endif
