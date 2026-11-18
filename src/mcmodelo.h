// MUSIC CENTER (QR TIME): the data every music module shares. Providers
// (subsonic.c, webdav.c) fill these; the catalog (mccat.c), the queue
// (mcfila.c), the player (mcplayer.c) and the screen (mcui.c) only read them.
//
// IDS. A track id is QUALIFIED by its source: "<fonte>:<id on that source>",
// where <fonte> is the configured source's own key ("ss1a2b3c4d" for a
// Subsonic server, "dv1a2b3c4d" for a WebDAV folder). These ids are private
// to that server: they never go to IMDb, TMDB, Trakt or any other service.
#ifndef NV_MCMODELO_H
#define NV_MCMODELO_H

#define MC_ID 512

typedef struct {
  char id[MC_ID];             // qualified
  char titulo[160], artista[128], album[160];
  char albumId[96], artistaId[96];   // on the same source (unqualified)
  char capa[96];              // the source's cover art id ("" none)
  char genero[64];
  int  faixa, disco, ano;
  int  dur;                   // seconds, 0 unknown
  // What the SOURCE says about the file (0 / "" = not reported). Never filled
  // with a guess: a 16-bit FLAC is 16, not "24".
  char formato[12];           // suffix: flac, mp3, m4a, wav...
  char mime[40];
  int  kbps, taxa, bits, canais;
  long long tam;
  int  fav;                   // starred on the source
} McFaixa;

typedef struct {
  char id[96], nome[160], artista[128], artistaId[96], capa[96], genero[64];
  int  ano, faixas, dur, fav;
} McAlbum;

typedef struct { char id[96], nome[128], capa[96]; int albuns, fav; } McArtista;
typedef struct { char id[96], nome[128], dono[64], capa[96]; int faixas, dur, publica; } McPlaylist;
typedef struct { char nome[64]; int faixas, albuns; } McGenero;

enum { MC_OK = 0, MC_ERRO_REDE, MC_ERRO_LOGIN, MC_ERRO_SERVIDOR, MC_ERRO_NAO_SUPORTA, MC_ERRO_OUTRO };
#endif
