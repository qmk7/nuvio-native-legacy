// MUSIC CENTER output on LG webOS: the TV's own media pipeline (uMS), the
// same LS2 path the video player uses (video.c). The ORIGINAL file goes in
// (Subsonic format=raw, WebDAV as stored); the TV decodes it in its own
// pipeline and mixes it to whatever its sound output is set to (TV speakers,
// HDMI ARC/eARC as PCM or passthrough, optical). An app cannot choose that
// format nor read it back: the info below is what the pipeline reports about
// the INPUT, and the note says so. The real output format can be read on
// the receiver (the Samsung HW-Q990F shows the incoming signal).
#include "mcsaida.h"
#include <stdio.h>
#include <string.h>

#if !defined(__APPLE__) && !defined(NV_LINUX_DESKTOP) && !defined(NV_TPK) && !defined(NV_ANDROID) && !defined(__EMSCRIPTEN__)
#include "video.h"
#include "audioinfo.h"

#include "md5.h"
static McFaixa faixa;
static int aberto, buscarAo;
static char minhaUrl[33];   // md5 of our URL (the URL itself carries a token)
static double inicioPendente;

static int abrir(const char *url, const char *cab, const McFaixa *f, double inicio) {
  if (!video_iniciar_auto()) { printf("[musica] pipeline de midia indisponivel\n"); fflush(stdout); return 0; }
  faixa = *f;
  video_parar();
  video_definir_cabecalhos(cab ? cab : "");
  video_definir_mp4(!strcmp(f->formato, "m4a") || !strcmp(f->formato, "alac") || !strcmp(f->formato, "aac") || strstr(f->mime, "mp4") != NULL);
  video_volume(100);
  if (!video_tocar(url)) return 0;
  md5_hex(url, strlen(url), minhaUrl);
  aberto = 1;
  inicioPendente = inicio;
  buscarAo = inicio > 0;
  return 1;
}
static void pausar(int p) { if (aberto) video_pausar(p); }
static void buscar(double s) { if (aberto) video_buscar(s); }
static void parar(void) {
  if (aberto) {
    const char *u = video_url_atual();
    char h[33];
    md5_hex(u ? u : "", u ? strlen(u) : 0, h);
    if (!strcmp(h, minhaUrl)) { video_parar(); video_definir_cabecalhos(""); }   // never stop someone else's video
  }
  aberto = 0;
}
static double pos(void) { return aberto ? video_pos() : 0; }
static double dur(void) { return aberto ? video_duracao() : 0; }
static int estado(void) {
  if (!aberto) return MCO_FIM;
  {
    const char *u = video_url_atual();
    char h[33];
    md5_hex(u ? u : "", u ? strlen(u) : 0, h);
    if (strcmp(h, minhaUrl)) { aberto = 0; return MCO_PERDEU; }   // the pipeline plays something else now
  }
  if (video_falhou()) return MCO_ERRO;
  if (video_terminou()) return MCO_FIM;
  if (video_tocando() || video_pronto()) {
    if (buscarAo) { buscarAo = 0; video_buscar(inicioPendente); }
    return MCO_TOCANDO;
  }
  return MCO_CARREGANDO;
}
static void info(McSaidaInfo *o) {
  const VideoFaixa *a = video_audio(video_audio_atual());
  memset(o, 0, sizeof *o);
  snprintf(o->backend, sizeof o->backend, "webOS media pipeline");
  if (a && a->codec[0]) snprintf(o->codec, sizeof o->codec, "%s", audioinfo_codec(a->codec)[0] ? audioinfo_codec(a->codec) : a->codec);
  // Input channels as the pipeline reports them; rate and depth of the OUTPUT are not exposed.
  o->canais = a ? a->canais : 0;
  o->medido = 0;
  snprintf(o->nota, sizeof o->nota, "%s",
           "Decoded by the TV. Its HDMI/eARC output format is set in the TV's sound settings and is not visible to apps; "
           "check the receiver's display.");
}
static void bombear(void) { video_bombear(); }
static const McSaida UMS = { "webOS", abrir, NULL, pausar, buscar, parar, pos, dur, estado, info, bombear };
const McSaida *mcsaida_ums(void) { return &UMS; }
#else
const McSaida *mcsaida_ums(void) { return NULL; }
#endif

const McSaida *mcsaida_padrao(void) {
  const McSaida *s = mcsaida_ums();
  return s ? s : mcsaida_sdl();
}
