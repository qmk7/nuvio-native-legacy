// QR TIME voice intents (Arabic, English, Portuguese). See intencao.c.
#ifndef QRT_INTENCAO_H
#define QRT_INTENCAO_H
#include <stddef.h>
enum { INT_NADA = 0, INT_BUSCA, INT_ESPORTES, INT_MUSICA, INT_AOVIVO, INT_SUGESTAO, INT_AJUSTES };
// INT_BUSCA fills `busca` with what to search; INT_NADA = search the text as is.
int intencao_analisar(const char *texto, char *busca, size_t n);
#endif
