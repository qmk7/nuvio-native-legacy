// MUSIC CENTER output on the desktop build: the file is fetched whole, decoded
// by dr_libs (mcdec.c) and played through an SDL audio device opened AT THE
// SOURCE FORMAT (rate, channels, 32-bit). SDL may give the device another
// format; then an SDL_AudioStream converts, and the info says so with the
// format SDL really obtained. When both match nothing touches the samples:
// what the decoder produced is what the device gets (verified in the tests
// with SDL's "disk" driver). Two consecutive tracks with the same format play
// without a gap (the next one is fetched 20 s before the end).
#include "mcsaida.h"
#include <stdio.h>
#include <string.h>

#if defined(NV_LINUX_DESKTOP) || defined(MC_SDL_TESTE)
#include "mcdec.h"
#include "rede.h"
#include <SDL2/SDL.h>
#include <pthread.h>
#include <stdlib.h>

typedef struct {
  McDec *dec;
  McDecInfo in;
  int pronto, falhou;      // download + open done / failed
  unsigned gen;
} Carga;

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static SDL_AudioDeviceID dev;
static SDL_AudioSpec obtido;
static SDL_AudioStream *conv;            // NULL when the device takes the source format as is
static int convTaxa, convCanais;         // the source format `conv` was built for
static Carga atual, prox;
static unsigned gen;
static volatile long long consumidos;    // source frames handed to the device for the current track
static double inicioSeg;
static int fim, transicao, pausado, temProx;
static char nomeDriver[24];

typedef struct { unsigned char *v; size_t n, cap; } Buf;
static int juntar(const char *d, size_t n, void *u) {
  Buf *b = u;
  if (b->n + n > b->cap) {
    size_t nc = (b->n + n) * 2;
    unsigned char *t;
    if (nc > (size_t)1 << 30) return 0;   // 1 GiB is not a music file
    t = realloc(b->v, nc);
    if (!t) return 0;
    b->v = t; b->cap = nc;
  }
  memcpy(b->v + b->n, d, n);
  b->n += n;
  return 1;
}
typedef struct { char url[4096], cab[700]; unsigned gen; int eProx; double inicio; } Pedido;
static void *fioCarga(void *u) {
  Pedido p = *(Pedido *)u;
  Buf b = { NULL, 0, 0 };
  const char *cab[4] = { NULL, NULL, NULL, NULL };
  char linhas[700], *sp, *l;
  int st = 0, k = 0, ok;
  McDec *d = NULL;
  McDecInfo in;
  memset((Pedido *)u, 0, sizeof p);
  free(u);
  snprintf(linhas, sizeof linhas, "%s", p.cab);
  for (l = strtok_r(linhas, "\n", &sp); l && k < 3; l = strtok_r(NULL, "\n", &sp)) cab[k++] = l;
  if (!strncmp(p.url, "file://", 7)) {   // an offline copy
    FILE *f = fopen(p.url + 7, "rb");
    char blk[65536];
    size_t r;
    ok = f != NULL;
    while (f && (r = fread(blk, 1, sizeof blk, f)) > 0) if (!juntar(blk, r, &b)) { ok = 0; break; }
    if (f) fclose(f);
  } else ok = rede_baixar_fluxo(p.url, 120, k ? cab : NULL, juntar, &b, 0, &st, NULL);
  memset(p.url, 0, sizeof p.url);
  memset(linhas, 0, sizeof linhas);
  if (ok && b.n) d = mcdec_abrir(b.v, b.n, &in);
  free(b.v);
  if (!ok || !d) { printf("[musica] desktop: %s (http %d)\n", ok ? "formato que este decodificador nao abre" : "falha ao baixar", st); fflush(stdout); }
  if (d && p.inicio > 0) mcdec_buscar(d, (long long)(p.inicio * in.taxa));
  pthread_mutex_lock(&trava);
  if (p.gen == gen) {
    Carga *c = p.eProx ? &prox : &atual;
    if (dev) SDL_LockAudioDevice(dev);
    if (c->dec) mcdec_fechar(c->dec);
    c->dec = d; d = NULL;
    c->in = in; c->gen = p.gen;
    c->pronto = c->dec != NULL; c->falhou = c->dec == NULL;
    if (dev) SDL_UnlockAudioDevice(dev);
  }
  pthread_mutex_unlock(&trava);
  mcdec_fechar(d);   // superseded
  return NULL;
}
static int pedir(const char *url, const char *cab, int eProx, double inicio) {
  pthread_t t;
  Pedido *p = calloc(1, sizeof *p);
  if (!p) return 0;
  snprintf(p->url, sizeof p->url, "%s", url);
  snprintf(p->cab, sizeof p->cab, "%s", cab ? cab : "");
  p->gen = gen; p->eProx = eProx; p->inicio = inicio;
  if (pthread_create(&t, NULL, fioCarga, p) != 0) { free(p); return 0; }
  pthread_detach(t);
  return 1;
}

// Audio thread: device format `obtido`, source atual.in.
static void callback(void *u, Uint8 *out, int len) {
  int bpf = (SDL_AUDIO_BITSIZE(obtido.format) / 8) * obtido.channels, feito = 0;
  int32_t tmp[4096 * 8];
  (void)u;
  while (feito < len) {
    if (!atual.dec || fim) { memset(out + feito, obtido.format == AUDIO_U8 ? 0x80 : 0, (size_t)(len - feito)); return; }
    if (!conv) {   // device takes the source format: copy exactly
      long want = (len - feito) / bpf, got;
      got = mcdec_ler(atual.dec, (int32_t *)(out + feito), want);
      consumidos += got;
      feito += (int)got * bpf;
      if (got < want) goto acabou;
      continue;
    }
    {
      int temp = SDL_AudioStreamGet(conv, out + feito, len - feito);
      if (temp > 0) { feito += temp; continue; }
      {
        long got = mcdec_ler(atual.dec, tmp, 4096 * 8 / atual.in.canais);
        if (got > 0) { consumidos += got; SDL_AudioStreamPut(conv, tmp, (int)(got * atual.in.canais * 4)); continue; }
        SDL_AudioStreamFlush(conv);
        temp = SDL_AudioStreamGet(conv, out + feito, len - feito);
        if (temp > 0) { feito += temp; continue; }
      }
    }
  acabou:
    // The track ended: continue straight into a prepared one of the same format.
    if (temProx && prox.pronto && prox.dec && prox.in.taxa == atual.in.taxa && prox.in.canais == atual.in.canais) {
      mcdec_fechar(atual.dec);
      atual = prox;
      memset(&prox, 0, sizeof prox);
      temProx = 0;
      consumidos = 0; inicioSeg = 0;
      transicao = 1;
      continue;
    }
    fim = 1;
  }
}
static int prepararDispositivo(const McDecInfo *in) {
  SDL_AudioSpec quero;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    printf("[musica] desktop: SDL audio indisponivel: %s\n", SDL_GetError());
    fflush(stdout);
    return 0;
  }
  if (dev && (convTaxa != in->taxa || convCanais != in->canais)) { SDL_CloseAudioDevice(dev); dev = 0; }
  if (!dev) {
    SDL_zero(quero);
    quero.freq = in->taxa; quero.format = AUDIO_S32SYS; quero.channels = (Uint8)in->canais; quero.samples = 4096;
    quero.callback = callback;
    dev = SDL_OpenAudioDevice(NULL, 0, &quero, &obtido, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (!dev) { printf("[musica] desktop: SDL_OpenAudioDevice: %s\n", SDL_GetError()); fflush(stdout); return 0; }
    snprintf(nomeDriver, sizeof nomeDriver, "%s", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
    if (conv) { SDL_FreeAudioStream(conv); conv = NULL; }
    if (obtido.freq != in->taxa || obtido.format != AUDIO_S32SYS || obtido.channels != in->canais)
      conv = SDL_NewAudioStream(AUDIO_S32SYS, (Uint8)in->canais, in->taxa, obtido.format, obtido.channels, obtido.freq);
    convTaxa = in->taxa; convCanais = in->canais;
    printf("[musica] saida SDL (%s): pedido %d Hz s32 %dch, obtido %d Hz %s%d %dch%s\n", nomeDriver, in->taxa, in->canais,
           obtido.freq, SDL_AUDIO_ISFLOAT(obtido.format) ? "f" : "s", SDL_AUDIO_BITSIZE(obtido.format), obtido.channels,
           conv ? " (convertendo)" : " (sem conversao)");
    fflush(stdout);
  }
  return 1;
}

static int abrir(const char *url, const char *cab, const McFaixa *f, double inicio) {
  (void)f;
  pthread_mutex_lock(&trava);
  gen++;
  if (dev) { SDL_PauseAudioDevice(dev, 1); SDL_LockAudioDevice(dev); }
  if (atual.dec) mcdec_fechar(atual.dec);
  if (prox.dec) mcdec_fechar(prox.dec);
  memset(&atual, 0, sizeof atual); memset(&prox, 0, sizeof prox);
  if (conv) SDL_AudioStreamClear(conv);
  consumidos = 0; inicioSeg = inicio; fim = 0; transicao = 0; temProx = 0; pausado = 0;
  if (dev) SDL_UnlockAudioDevice(dev);
  pthread_mutex_unlock(&trava);
  return pedir(url, cab, 0, inicio);
}
static int preparar(const char *url, const char *cab, const McFaixa *f) {
  (void)f;
  pthread_mutex_lock(&trava);
  temProx = 1;
  pthread_mutex_unlock(&trava);
  return pedir(url, cab, 1, 0);
}
static void pausar(int p) { pausado = p; if (dev) SDL_PauseAudioDevice(dev, p); }
static void buscar(double s) {
  if (!dev || !atual.dec) return;
  SDL_LockAudioDevice(dev);
  if (mcdec_buscar(atual.dec, (long long)(s * atual.in.taxa))) { consumidos = 0; inicioSeg = s; if (conv) SDL_AudioStreamClear(conv); fim = 0; }
  SDL_UnlockAudioDevice(dev);
}
static void parar(void) {
  pthread_mutex_lock(&trava);
  gen++;
  if (dev) { SDL_PauseAudioDevice(dev, 1); SDL_LockAudioDevice(dev); }
  if (atual.dec) mcdec_fechar(atual.dec);
  if (prox.dec) mcdec_fechar(prox.dec);
  memset(&atual, 0, sizeof atual); memset(&prox, 0, sizeof prox);
  temProx = 0;
  if (dev) SDL_UnlockAudioDevice(dev);
  pthread_mutex_unlock(&trava);
}
static double pos(void) { return atual.in.taxa > 0 ? inicioSeg + (double)consumidos / atual.in.taxa : 0; }
static double dur(void) { return atual.in.taxa > 0 ? (double)atual.in.frames / atual.in.taxa : 0; }
static int estado(void) {
  int r;
  pthread_mutex_lock(&trava);
  if (atual.falhou) r = MCO_ERRO;
  else if (!atual.pronto) r = MCO_CARREGANDO;
  else {
    if (!dev) {
      if (!prepararDispositivo(&atual.in)) { atual.falhou = 1; pthread_mutex_unlock(&trava); return MCO_ERRO; }
    } else if (convTaxa != atual.in.taxa || convCanais != atual.in.canais) {
      if (!prepararDispositivo(&atual.in)) { atual.falhou = 1; pthread_mutex_unlock(&trava); return MCO_ERRO; }
    }
    if (dev && !pausado && SDL_GetAudioDeviceStatus(dev) != SDL_AUDIO_PLAYING && !fim) SDL_PauseAudioDevice(dev, 0);
    if (dev) SDL_LockAudioDevice(dev);
    r = transicao ? MCO_TRANSICAO : fim ? MCO_FIM : MCO_TOCANDO;
    transicao = 0;
    if (dev) SDL_UnlockAudioDevice(dev);
  }
  pthread_mutex_unlock(&trava);
  return r;
}
static void info(McSaidaInfo *o) {
  memset(o, 0, sizeof *o);
  snprintf(o->backend, sizeof o->backend, "SDL (%s)", nomeDriver[0] ? nomeDriver : "-");
  snprintf(o->codec, sizeof o->codec, "%s", atual.in.codec);
  if (!dev) return;
  o->taxa = obtido.freq;
  o->bits = SDL_AUDIO_BITSIZE(obtido.format);
  o->canais = obtido.channels;
  o->medido = 1;
  o->convertido = conv != NULL;
  if (conv) snprintf(o->nota, sizeof o->nota, "The device took %d Hz / %d-bit / %dch: converted from %d Hz / %dch.",
                     obtido.freq, o->bits, obtido.channels, atual.in.taxa, atual.in.canais);
  else if (atual.in.bits) snprintf(o->nota, sizeof o->nota, "Sent to the device unchanged (source %d-bit in a 32-bit container).", atual.in.bits);
  else snprintf(o->nota, sizeof o->nota, "Sent to the device unchanged (decoded from a lossy source).");
}
static const McSaida SDLX = { "desktop", abrir, preparar, pausar, buscar, parar, pos, dur, estado, info, NULL };
const McSaida *mcsaida_sdl(void) { return &SDLX; }
#else
const McSaida *mcsaida_sdl(void) { return NULL; }
#endif
