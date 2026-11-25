// QR TIME: "play this film" from Home Assistant (see casaha.c).
#ifndef QRT_CASAHA_H
#define QRT_CASAHA_H
#include <stddef.h>
int  casaha_tocar(const char *tipo, const char *q);          // UI thread; 1 = being looked up
int  casaha_pegar(char *imdb, size_t n);                     // app loop: 1 = an id to open and play
int  casaha_pendente(void);                                  // a request being looked up or waiting to open
// app.c: the account's own titles first (Continue watching, history, home
// rows); 1 = `id` is the one to play.
void casaha_definir_local(int (*f)(const char *q, const char *tipo, char *id, size_t n));
int  casaha_entre_tipos(const char *jFilme, const char *jSerie, const char *q, char *imdb, size_t n);   // tests
int  casaha_escolher(const char *json, const char *q, char *imdb, size_t n);   // tests
void casaha_teste_base(const char *base);                    // tests
#endif
