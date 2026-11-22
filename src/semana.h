// QR TIME weekly summary (local watch log). See semana.c.
#ifndef QRT_SEMANA_H
#define QRT_SEMANA_H
#include <SDL2/SDL.h>
#include <time.h>
typedef struct { double segundos; int filmes, series; char topo[160], genero[80]; } SemResumo;
void semana_registrar(const char *imdb, const char *tipo, const char *titulo, const char *genero, double seg, time_t quando);
int  semana_resumo(time_t agora, SemResumo *r);   // last 7 days; 1 = something watched
void semana_passo(Uint32 agora);
time_t semana_ultimo(const char *imdb);       // noon of the last day this title was watched, 0 = never                  // Friday evening card
#endif
