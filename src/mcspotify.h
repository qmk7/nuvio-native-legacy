// MUSIC CENTER: Spotify, METADATA ONLY. Spotify's terms allow no playback
// outside Spotify's own apps and SDKs, and there is no Spotify SDK for a
// native webOS app. What is permitted and done here: read a PUBLIC playlist
// through the official Web API (client-credentials token from the user's
// own Spotify developer app) and match its tracks, by artist and title, to
// the person's own library (Navidrome/Subsonic). The result is a local
// playlist of what was found; nothing is downloaded from Spotify.
//
// Since Nov 2024 Spotify no longer serves Spotify-owned / algorithmic
// playlists to new developer apps; user-made public playlists still work.
// Client id and secret live on this TV ("spotify.txt"), never logged.
// Calls BLOCK.
#ifndef NV_MCSPOTIFY_H
#define NV_MCSPOTIFY_H
#include "mcmodelo.h"

typedef struct { char titulo[160], artista[128], album[160]; int dur; } SpFaixa;

void mcsp_definir_base(const char *contas, const char *api);   // tests
int  mcsp_tem_chave(void);
int  mcsp_definir_chave(const char *id, const char *segredo);  // NULL keeps that half
// "https://open.spotify.com/playlist/<id>?si=..", "spotify:playlist:<id>" or the id.
int  mcsp_id_playlist(const char *entrada, char *id, size_t n);
// The playlist's name and tracks (up to max). MC_OK / MC_ERRO_*.
int  mcsp_playlist(const char *id, char *nome, size_t nn, SpFaixa *out, int max, int *n);
// Match to the library: the tracks found go to `out` (the caller, on the UI
// thread, saves them as the local playlist "<name> (Spotify)").
// *achadas / *total. MC_OK even when some were not found.
int  mcsp_importar(const char *entrada, McFaixa *out, int max, int *achadas, int *total, char *nome, size_t nn);
// Normalized comparison used by the matcher (tests).
int  mcsp_mesmo(const char *a, const char *b);
#endif
