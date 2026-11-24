// QR TIME: the TV's settings from a phone or computer on the home network,
// at a link that stays the same (see ajremoto.c).
#ifndef QRT_AJREMOTO_H
#define QRT_AJREMOTO_H
#include <SDL2/SDL.h>
void ajremoto_iniciar(void);       // registers CA_AJUSTES with celapps
int  ajremoto_abrir(void);          // page up (1) or no home network (0)
void ajremoto_renovar(void);        // new link; the old bookmark stops
void ajremoto_passo(Uint32 agora);  // app thread, each frame (celapps_atualizar)
#endif
