// Ver sono.h.
#include "sono.h"
#include "ajustes.h"
#include "gfx.h"
#include "layout.h"
#include "idioma.h"
#include "ilha.h"
#include "mcplayer.h"
#include "player.h"
#include "video.h"
#include <stdio.h>
#include <string.h>

#if !defined(__APPLE__) && !defined(NV_LINUX_DESKTOP) && !defined(NV_TPK) && !defined(NV_ANDROID) && !defined(__EMSCRIPTEN__)
#define SONO_WEBOS 1
#endif

static int minutosVistos = -1;     // the setting as last seen (-1 = first frame)
static Uint32 prazo;               // when it fires (0 = off)
static int avisou;
static int apagada;                // 1 = the screen is off (ours)
static const SonoAlvo *alvo;

static void pausarVideo(void) { player_sono_pausar(); }
static void pausarMusica(void) { if (mcp_estado() == MCP_TOCANDO) mcp_alternar_pausa(); }
#ifdef SONO_WEBOS
static void respTela(const char *payload, void *u) {
  (void)u;
  printf("[sono] tvpower: %s\n", payload && strstr(payload, "\"returnValue\":true") ? "aceito" : "recusado (tela preta do app)");
  fflush(stdout);
}
#endif
static void apagarTela(int apagar) {
#ifdef SONO_WEBOS
  video_luna(apagar ? "luna://com.webos.service.tvpower/power/turnOffScreen"
                    : "luna://com.webos.service.tvpower/power/turnOnScreen", "{}", apagar ? respTela : NULL, NULL);
#else
  (void)apagar;
#endif
}
static void aviso(int seg) {
  char t[160];
  snprintf(t, sizeof t, i18n("Temporizador de sono: a tela desliga em %d s"), seg);
  if (player_aberto()) player_toast_ex(t, 8000, "aj_alarm-clock", 0);
  else ilha_avisar("sono", ILHA_INFO, "aj_alarm-clock", t, 8000, 0);
}
static const SonoAlvo REAL = { ajustes_sono_minutos, ajustes_sono_zerar, pausarVideo, pausarMusica, apagarTela, aviso };
static const SonoAlvo *A(void) { return alvo ? alvo : &REAL; }

void sono_teste_alvo(const SonoAlvo *a) { alvo = a; minutosVistos = -1; prazo = 0; avisou = 0; apagada = 0; }

void sono_atualizar(Uint32 agora) {
  int m = A()->minutos();
  if (!agora) agora = 1;
  if (minutosVistos < 0) {
    // At start the timer is always off: a timer left set yesterday must not
    // turn the screen off tonight.
    minutosVistos = 0;
    if (m) { A()->zerar(); m = 0; }
  }
  if (m != minutosVistos) {
    minutosVistos = m;
    avisou = 0;
    prazo = m ? agora + (Uint32)m * 60000u : 0;
    if (m && !prazo) prazo = 1;
    printf("[sono] %s\n", m ? "ligado" : "desligado");
    if (m) printf("[sono] dispara em %d min\n", m);
    fflush(stdout);
  }
  if (!prazo) return;
  if (!avisou && (int)(prazo - agora) <= 60000) { avisou = 1; A()->aviso((int)((prazo - agora + 999) / 1000)); }
  if ((int)(prazo - agora) > 0) return;
  prazo = 0;
  printf("[sono] disparou: pausa e tela desligada\n");
  fflush(stdout);
  A()->pausar_video();
  A()->pausar_musica();
  A()->apagar_tela(1);
  apagada = 1;
  A()->zerar();
  minutosVistos = 0;
}

int sono_entrada(const SDL_Event *e) {
  if (!apagada || !e) return 0;
  if (e->type != SDL_KEYDOWN && e->type != SDL_MOUSEBUTTONDOWN && e->type != SDL_FINGERDOWN) return 0;
  apagada = 0;
  A()->apagar_tela(0);
  printf("[sono] tela ligada pela tecla\n");
  fflush(stdout);
  return 1;
}

int sono_tela_apagada(void) { return apagada; }

void sono_desenhar(void) {
  GfxRect tela = { 0, 0, NV_TELA_W, NV_TELA_H };
  float g = gfx_opacidade_grupo, mlt = gfx_osd_mult;
  if (!apagada) return;
  gfx_opacidade_grupo = 1.0f; gfx_osd_mult = 1.0f;
  gfx_cor(tela, 0, 0, 0, 0, 1.0f);
  gfx_opacidade_grupo = g; gfx_osd_mult = mlt;
}
