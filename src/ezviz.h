// QR TIME doorbell / camera alerts via EZVIZ's open platform. See ezviz.c.
#ifndef QRT_EZVIZ_H
#define QRT_EZVIZ_H
#include <SDL2/SDL.h>
#include <stddef.h>
typedef struct { char id[64], serial[32], nome[96], foto[512]; int tipo, cifrada; long long quando; } EzAlarme;
void ezviz_iniciar(void);                     // phone page (celapps)
void ezviz_passo(Uint32 agora);               // app thread: polls every 15 s when configured
int  ezviz_configurado(void);
int  ezviz_definir(const char *chave, const char *segredo);   // "" removes (stored as a secret)
// Parsers (tests).
int  ezviz_parse_token(const char *json, char *tok, size_t nt, char *area, size_t na, long long *expira);
int  ezviz_parse_alarmes(const char *json, EzAlarme *out, int max);   // -1 = error answer
void ezviz_definir_base(const char *url);
#endif
