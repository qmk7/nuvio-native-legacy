// QR TIME ACCOUNTS, PROVIDERS AND DATA (phase 8): Settings > Account and
// services > "Accounts, providers and data". Tab 1 lists every provider with
// its real state and what it supports (test, switch on/off, refresh, open its
// settings, TheSportsDB key). Tab 2 holds the data operations: schema
// version, cache size, and the explicit resets, each behind a confirmation
// whose default button is Cancel.
#ifndef NV_QRCONTASUI_H
#define NV_QRCONTASUI_H
#include <SDL2/SDL.h>

void qrcui_abrir(void);
void qrcui_evento(const SDL_Event *e);
void qrcui_atualizar(float dt, Uint32 agora);
void qrcui_desenhar(Uint32 agora);
int  qrcui_quer_sair(void);
// Where "Open settings" asked to go (consumed on read).
enum { QRCUI_IR_NADA = 0, QRCUI_IR_AJUSTES, QRCUI_IR_IPTV, QRCUI_IR_TRAKT, QRCUI_IR_MUSICA, QRCUI_IR_YOUTUBE, QRCUI_IR_ADDONS };
int  qrcui_pedido(void);

// Tests (Xvfb e2e).
int  qrcui_teste_aba(void);
int  qrcui_teste_n(void);
int  qrcui_teste_foco(void);
int  qrcui_teste_confirmando(void);   // -1 none, else the QRR_* waiting for confirmation
int  qrcui_teste_botao(void);         // focused button in the confirmation (0 = Cancel)
#endif
