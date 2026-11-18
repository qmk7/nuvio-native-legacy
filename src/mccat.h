// MUSIC CENTER: the unified library over every configured source. Lists are
// merged across Subsonic servers; WebDAV folders are browsed (their files'
// tags read by Range); local favourites, history and playlists come from
// mcfila.c. Ids out of here are QUALIFIED ("ss1a2b3c4d:<id>", "dv...:<href>",
// "lc:<n>" for a local playlist).
//
// Every function BLOCKS (network): the screen calls them on its worker. The
// player hooks (mccat_ganchos) send scrobbles on their own threads.
#ifndef NV_MCCAT_H
#define NV_MCCAT_H
#include "mcmodelo.h"
#include "mcplayer.h"
#include "webdav.h"

int mccat_artistas(McArtista *out, int max, int *n);
int mccat_artista_albuns(const char *id, McAlbum *out, int max, int *n);
// tipo: newest, recent, frequent, random, alphabeticalByName, starred
int mccat_albuns(const char *tipo, int offset, McAlbum *out, int max, int *n);
int mccat_album(const char *id, McAlbum *info, McFaixa *out, int max, int *n);
int mccat_generos(McGenero *out, int max, int *n);
int mccat_genero_faixas(const char *genero, int offset, McFaixa *out, int max, int *n);
int mccat_playlists(McPlaylist *out, int max, int *n);
int mccat_playlist(const char *id, McFaixa *out, int max, int *n);
int mccat_buscar(const char *q, McArtista *ar, int maxAr, int *nAr, McAlbum *al, int maxAl, int *nAl,
                 McFaixa *fx, int maxFx, int *nFx);
int mccat_favoritos(McFaixa *out, int max, int *n);
int mccat_favoritar(McFaixa *f, int liga);
// Saves a playlist on the source of its tracks when they all come from one
// Subsonic server, else as a local playlist. *servidor = 1 when on the server.
int mccat_salvar_playlist(const char *nome, const McFaixa *f, int n, int *servidor);
// WebDAV: a folder's subfolders and music files (titles from file names;
// mccat_tags fills the real ones).
int mccat_pasta(const char *fonte, const char *href, DavItem *pastas, int maxP, int *nP, McFaixa *arqs, int maxA, int *nA);
int mccat_tags(McFaixa *f, int n);
int mccat_letra(const McFaixa *f, char *lrc, size_t n);
// Cover art URL for a qualified id's source ("" when the source has none).
void mccat_capa(const char *idQualificado, const char *capa, int tamanho, char *url, size_t n);
// The player's hooks (URL, now playing, played, Party mode).
const McGanchos *mccat_ganchos(void);
// Test connection: MC_OK with a description ("Navidrome 0.53.3", "WebDAV: 2 folders").
int mccat_testar(int tipo, const char *url, const char *usuario, const char *senha, char *desc, size_t n);
#endif
