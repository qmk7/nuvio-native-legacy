// SPORT CENTER SCREEN (QR TIME). Tabs: My sports, All, Live, Upcoming,
// Finished, Leagues, Teams, Favourites; a filter bar (sport, league, day,
// hide scores); the match page with its channels (linked, alternates,
// suggestions from the guide, any channel by name), reminder and follows.
// Data: espn.c on a worker thread; store and suggestions: esporte.c.
#ifndef NV_ESPORTEUI_H
#define NV_ESPORTEUI_H
#include <SDL2/SDL.h>
#include "catalogo.h"

void esporteui_abrir(void);
void esporteui_evento(const SDL_Event *e);
void esporteui_atualizar(float dt, Uint32 agora);
void esporteui_desenhar(Uint32 agora);
int  esporteui_quer_sair(void);
// The user chose a channel to watch: the router plays it in the player.
int  esporteui_pediu_canal(CatItem *it);
// Opens the match page of event `id` (a reminder without a linked channel).
void esporteui_abrir_evento(const char *id);
void esporteui_perfil_trocou(void);

// Tests (Xvfb e2e): counts and the focused row.
int  esporteui_teste_n(void);
int  esporteui_teste_aba(void);
#endif
