// QR TIME: automatic reminders and goal notices for followed teams. See esportealerta.c.
#ifndef QRT_ESPORTEALERTA_H
#define QRT_ESPORTEALERTA_H
#include <SDL2/SDL.h>
#include <stddef.h>
#include <time.h>
#include "espn.h"
void esportealerta_passo(Uint32 agora);       // app thread, inside the app (not login)
void esportealerta_perfil_trocou(void);
void esportealerta_acao(const char *chave, int botao);   // "esportehl:<id>" notice: 1 = highlights
// Pure parts (tests).
int  esportealerta_lembrar(const EspEvento *e, time_t agora);   // 1 = reminder added now
int  esportealerta_gol(const EspEvento *velho, const EspEvento *novo, char *msg, size_t n);
#endif
