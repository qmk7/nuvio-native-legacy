// MUSIC CENTER: the audio OUTPUT a track goes to. The player (mcplayer.c)
// only drives this interface; each backend owns decoding and the device.
//
//   mcsaida_ums  LG webOS: the TV's own media pipeline (uMS over LS2, the
//                same one the video player uses). The TV decodes the original
//                file; the app cannot choose or read the HDMI/eARC output
//                format, so the info it gives is what the pipeline reports.
//   mcsaida_sdl  desktop: download, dr_libs decode, SDL audio. The device's
//                ACTUAL format is what SDL obtained, measured, and any
//                resampling/conversion is stated.
//   tests plug their own.
#ifndef NV_MCSAIDA_H
#define NV_MCSAIDA_H
#include "mcmodelo.h"

// MCO_PERDEU: another player (a movie, a channel, a trailer) took the output.
enum { MCO_CARREGANDO = 0, MCO_TOCANDO = 1, MCO_FIM = 2, MCO_TRANSICAO = 3, MCO_ERRO = -1, MCO_PERDEU = -2 };

typedef struct {
  char backend[24];        // "webOS media pipeline", "SDL (pulseaudio)"...
  char codec[16];          // decoder input as the backend sees it
  int  taxa, bits, canais; // OUTPUT format (0 = not known to the app)
  int  medido;             // 1: read from the real output path; 0: reported/unknown
  int  convertido;         // 1: the backend converted (resample, bit depth, channels)
  char nota[160];          // plain explanation shown on the Now Playing info
} McSaidaInfo;

typedef struct {
  const char *nome;
  // Start `url` (with header lines `cab`, may be ""); `f` describes it.
  int    (*abrir)(const char *url, const char *cab, const McFaixa *f, double inicio);
  // Optional: fetch the next track now so it starts without a gap.
  int    (*preparar)(const char *url, const char *cab, const McFaixa *f);
  void   (*pausar)(int pausado);
  void   (*buscar)(double seg);
  void   (*parar)(void);
  double (*pos)(void);
  double (*dur)(void);
  int    (*estado)(void);   // MCO_*; MCO_TRANSICAO once, when the prepared track took over
  void   (*info)(McSaidaInfo *out);
  void   (*bombear)(void);  // per frame (may be NULL)
} McSaida;

const McSaida *mcsaida_ums(void);   // NULL off webOS
const McSaida *mcsaida_sdl(void);   // NULL off the desktop build
const McSaida *mcsaida_padrao(void);
#endif
