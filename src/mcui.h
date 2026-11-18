// MUSIC CENTER SCREEN (QR TIME). Sections on the left (Home, Recently played,
// Artists, Albums, Tracks, Genres, Playlists, Favourites, Offline, Queue,
// Search, Services, Settings); pages for an album, an artist, a playlist,
// a genre and a WebDAV folder; the Now Playing view (progress, controls,
// source and output format, synced lyrics). Data comes from mccat.c on a
// worker thread; playback is mcplayer.c. The screen does no network or
// decoding itself.
#ifndef NV_MCUI_H
#define NV_MCUI_H
#include <SDL2/SDL.h>

void mcui_abrir(void);
void mcui_evento(const SDL_Event *e);
void mcui_atualizar(float dt, Uint32 agora);
void mcui_desenhar(Uint32 agora);
int  mcui_quer_sair(void);
void mcui_perfil_trocou(void);
// Every frame, on every screen: the player keeps going while the person
// browses the rest of the app.
void mcui_fundo(Uint32 agora);

// Tests.
int  mcui_teste_secao(void);
int  mcui_teste_n(void);
int  mcui_teste_zona(void);   // 0 menu, 1 list, 2 now playing, 3 form, 4 actions
#endif
