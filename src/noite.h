// QR TIME "what should I watch tonight?" (Control Center tile). See noite.c.
#ifndef QRT_NOITE_H
#define QRT_NOITE_H
#include "catalogo.h"
void noite_sugerir(void);                         // shows the suggestion card
void noite_acao(const char *chave, int botao);    // "noite:<imdb>": 1 open, 2 another
// Pure parts (tests).
int  noite_peso(const CatItem *c, int filmePrimeiro);
int  noite_escolher(unsigned semente, int filmePrimeiro);   // catalogue index, -1 none
void noite_teste_esquecer(void);
#endif
