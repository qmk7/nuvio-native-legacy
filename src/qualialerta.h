// QR TIME: a better version (4K / Dolby Vision) of a film on your list. See qualialerta.c.
#ifndef QRT_QUALIALERTA_H
#define QRT_QUALIALERTA_H
#include <SDL2/SDL.h>
void qualialerta_passo(Uint32 agora);               // app thread
void qualialerta_acao(const char *chave, int botao); // "quali:<imdb>": 1 = open
int  qualialerta_melhora(int alturaAntes, int dvAntes, int altura, int dv);   // tests
#endif
