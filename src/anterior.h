// QR TIME "previously on": back to a series after 4+ days, the start of the
// episode offers the synopses of the two episodes before it. See anterior.c.
#ifndef QRT_ANTERIOR_H
#define QRT_ANTERIOR_H
#include <SDL2/SDL.h>
#include "catalogo.h"
void anterior_passo(Uint32 agora);   // app thread, every frame
// Pure part (tests): the two episodes before (t,e) in the list; count found.
int  anterior_achar(const CatEp *const *eps, int n, int t, int e, const CatEp *out[2]);
#endif
