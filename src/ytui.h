// YOUTUBE SCREEN (QR TIME). Its own destination in the menu, not a trailer
// button: Trending, Search, Subscriptions, Playlists and Account tabs; channel
// and playlist pages; the video page. Lists come from the official YouTube
// Data API (ytapi.c) on a worker thread. A video PLAYS in the TV's official
// YouTube app (ondever_youtube_video) and the screen says so; QR TIME never
// plays, extracts or rehosts YouTube streams.
#ifndef NV_YTUI_H
#define NV_YTUI_H
#include <SDL2/SDL.h>

void ytui_abrir(void);
void ytui_evento(const SDL_Event *e);
void ytui_atualizar(float dt, Uint32 agora);
void ytui_desenhar(Uint32 agora);
int  ytui_quer_sair(void);
void ytui_perfil_trocou(void);

// Tests (Xvfb e2e).
int  ytui_teste_n(void);        // items in the current list
int  ytui_teste_aba(void);
int  ytui_teste_zona(void);     // 0 tabs, 1 list, 2 video page, 3 account
int  ytui_teste_erro(void);     // last YT_* of the current list
const char *ytui_teste_lancado(void);   // last video id handed to the official app
#endif
