// IPTV CONNECTION TEST (QR TIME): Settings > Live TV > account > "Test
// connection". One worker thread runs the provider's own protocol code for the
// chosen account (Xtream login + account info; M3U download + parse; Stalker
// handshake + channel list) and leaves a short, translated, credential-free
// result for the Settings row. The drawing thread only reads the result.
#ifndef NV_IPTVTESTE_H
#define NV_IPTVTESTE_H
#include "xtream.h"

enum { IPTVT_NADA = 0, IPTVT_TESTANDO, IPTVT_OK, IPTVT_FALHOU };

// Starts testing account `id` of type `tipo` (IPTV_XTREAM/M3U/STALKER). Does
// nothing while a test is running. 1 = started.
int iptvteste_iniciar(unsigned id, int tipo);
// State of the last test of account `id` (IPTVT_NADA for any other account,
// or after a profile change). `msg` gets the text to show (may be NULL).
int iptvteste_estado(unsigned id, char *msg, unsigned n);
// Forget the result (account edited/removed, profile changed).
void iptvteste_esquecer(void);

// Result text, without network (for the worker and the tests).
// Xtream: `veio` = the server answered with user_info; `http` its status.
int iptvteste_texto_xtream(int veio, int http, const XtreamConta *c, long long agora,
                           char *msg, unsigned n);
// M3U / Stalker: channels found (0 = empty or unreachable).
int iptvteste_texto_lista(int canais, char *msg, unsigned n);

#endif
