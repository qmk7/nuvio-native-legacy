// MUSIC CENTER: the player engine. Plays the queue (mcfila.c) through an
// output backend (mcsaida.h); knows nothing about servers. The catalog
// (mccat.c) plugs in how to get a track's URL, and who hears "now playing"
// and "played" (Subsonic scrobble, Last.fm).
//
// Rules: previous restarts the track after 3 s; a play is "played" at half
// its length or 4 minutes (the Last.fm rule, also used for Subsonic); the
// position is saved every 10 s and on pause/stop for resume; Party mode
// appends more tracks before the queue runs out.
#ifndef NV_MCPLAYER_H
#define NV_MCPLAYER_H
#include "mcmodelo.h"
#include "mcsaida.h"

enum { MCP_PARADO = 0, MCP_CARREGANDO, MCP_TOCANDO, MCP_PAUSADO, MCP_ERRO };

typedef struct {
  // URL + header lines for a track. 1 ok.
  int  (*url)(const McFaixa *f, char *url, size_t n, char *cab, size_t nc);
  void (*agora)(const McFaixa *f);                         // started
  void (*tocou)(const McFaixa *f, long long inicioMs);     // counted as played
  // Party mode: up to `max` more tracks like `base`. Count. Runs on a worker.
  int  (*mais)(const McFaixa *base, McFaixa *out, int max);
} McGanchos;

void mcp_definir_saida(const McSaida *s);
void mcp_definir_ganchos(const McGanchos *g);
int  mcp_tocar(int i);            // queue index (play order)
// Continue the saved queue at its saved position (paused until play).
int  mcp_retomar(void);
void mcp_alternar_pausa(void);
int  mcp_proxima(void);
int  mcp_anterior(void);
void mcp_buscar(double seg);
void mcp_parar(void);
void mcp_atualizar(unsigned agoraMs);
void mcp_festa(int liga);
int  mcp_eh_festa(void);

int    mcp_estado(void);
double mcp_pos(void);
double mcp_dur(void);
void   mcp_saida(McSaidaInfo *out);
int    mcp_erro(void);            // last error: MC_ERRO_* or -1 decode/output
const char *mcp_backend(void);
#endif
