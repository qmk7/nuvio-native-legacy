// QR TIME morning greeting (weather, today's matches and episodes). See bomdia.c.
#ifndef QRT_BOMDIA_H
#define QRT_BOMDIA_H
#include <SDL2/SDL.h>
typedef struct { double temp, max, min; int codigo; } BdClima;
void bomdia_passo(Uint32 agora);                 // app thread, inside the app
int  bomdia_parse(const char *json, BdClima *c); // Open-Meteo answer (tests)
const char *bomdia_tempo(int codigoWmo);         // i18n key
#endif
