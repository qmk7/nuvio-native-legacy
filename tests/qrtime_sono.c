// QR TIME sleep timer (src/sono.c): fires once at the chosen time, warns one
// minute before, starts Off after a restart, the first key only wakes.
//   bash tests/qrtime_sono.sh
#include "sono.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>

// Stubs for what sono.c links against (the real ones come from the app).
float gfx_opacidade_grupo = 1.0f, gfx_osd_mult = 1.0f;
void gfx_cor(GfxRect r, float raio, float cr, float cg, float cb, float ca) { (void)r; (void)raio; (void)cr; (void)cg; (void)cb; (void)ca; }
int ajustes_sono_minutos(void) { return 0; }
void ajustes_sono_zerar(void) {}
void player_sono_pausar(void) {}
int mcp_estado(void) { return 0; }
void mcp_alternar_pausa(void) {}
int player_aberto(void) { return 0; }
void player_toast_ex(const char *t, unsigned ms, const char *i, int a) { (void)t; (void)ms; (void)i; (void)a; }
void ilha_retirar(const char *c) { (void)c; }
void ilha_avisar(const char *c, int t, const char *i, const char *x, unsigned ms, int k) { (void)c; (void)t; (void)i; (void)x; (void)ms; (void)k; }
const char *i18n(const char *s) { return s; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int min, zerou, video, musica, apagou, acendeu, avisos, avisoSeg;
static int fMin(void) { return min; }
static void fZerar(void) { zerou++; min = 0; }
static void fVideo(void) { video++; }
static void fMusica(void) { musica++; }
static void fTela(int a) { if (a) apagou++; else acendeu++; }
static void fAviso(int s) { avisos++; avisoSeg = s; }
static const SonoAlvo T = { fMin, fZerar, fVideo, fMusica, fTela, fAviso };

int main(void) {
  SDL_Event k; memset(&k, 0, sizeof k); k.type = SDL_KEYDOWN;
  // A timer still set from the last run is switched off at start.
  min = 30; sono_teste_alvo(&T);
  sono_atualizar(1000);
  CONFERE(zerou == 1 && min == 0 && !video && !apagou, "restart: a leftover timer goes back to Off, nothing fires");
  // Set 15 min at t=10 s.
  min = 15; sono_atualizar(10000);
  sono_atualizar(10000 + 13 * 60000);
  CONFERE(!avisos && !video, "13 min in: nothing yet");
  sono_atualizar(10000 + 14 * 60000);
  CONFERE(avisos == 1 && avisoSeg == 60 && !video, "one minute before: one notice (60 s)");
  sono_atualizar(10000 + 14 * 60000 + 500);
  CONFERE(avisos == 1, "the notice is not repeated");
  CONFERE(!sono_entrada(&k), "a key with the screen on is not swallowed");
  sono_atualizar(10000 + 15 * 60000);
  CONFERE(video == 1 && musica == 1 && apagou == 1 && zerou == 2 && min == 0 && sono_tela_apagada(),
          "at 15 min: video and music paused, screen off, setting back to Off");
  sono_atualizar(10000 + 40 * 60000);
  CONFERE(video == 1 && apagou == 1, "fires once only");
  CONFERE(sono_entrada(&k) && acendeu == 1 && !sono_tela_apagada(), "first key: only turns the screen on (swallowed)");
  CONFERE(!sono_entrada(&k), "next key acts normally");
  // Changed mind: set then Off before it fires.
  min = 60; sono_atualizar(100000);
  min = 0; sono_atualizar(100000 + 61 * 60000);
  CONFERE(video == 1 && apagou == 1, "switched Off before the time: nothing fires");
  // Changing the time restarts the count.
  min = 30; sono_atualizar(200000);
  min = 60; sono_atualizar(200000 + 29 * 60000);
  sono_atualizar(200000 + 31 * 60000);
  CONFERE(video == 1, "changing 30 -> 60 min restarts the count from the change");
  sono_atualizar(200000 + 29 * 60000 + 60 * 60000);
  CONFERE(video == 2, "...and fires at the new time");
  // Listen only: screen off now, nothing paused, the first key wakes it.
  sono_entrada(&k);   // wake from the timer above
  { int v0 = video, m0 = musica, a0 = apagou, c0 = acendeu;
    sono_so_audio();
    CONFERE(apagou == a0 + 1 && video == v0 && musica == m0 && sono_tela_apagada(), "listen only: screen off, nothing paused");
    sono_so_audio();
    CONFERE(apagou == a0 + 1, "listen only twice: one request");
    CONFERE(sono_entrada(&k) && acendeu == c0 + 1 && !sono_tela_apagada(), "listen only: first key turns the screen on"); }
  return falhas ? 1 : 0;
}
