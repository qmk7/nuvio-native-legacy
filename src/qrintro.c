#include "qrintro.h"
#include "gfx.h"
#include "layout.h"
#include <SDL2/SDL_image.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__APPLE__) && !defined(NV_LINUX_DESKTOP) && !defined(NV_TPK) && !defined(NV_ANDROID) && !defined(__EMSCRIPTEN__)
#define INTRO_SOM_UMS 1
#include "video.h"
#endif

#define INTRO_N 90
#define INTRO_FPS 30
#define INTRO_ADIANTE 8          // frames decoded ahead of the one on screen
#define INTRO_ESPERA_SOM 800u    // max hold on frame 0 for the sound to start
#define INTRO_SAIDA_MS 380.0f    // last frame fades into the home

static char dir[600];
static int estado;               // 0 off, 1 waiting for sound, 2 playing, 3 fading, 4 done
static Uint32 t0, primeiro, saidaEm;
static int quadroAtual = -1;
static GLuint tex;
static int texW, texH;
static SDL_Surface *prontos[INTRO_N];
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sinal = PTHREAD_COND_INITIALIZER;
static int pedido, parar, fioVivo;
static pthread_t fio;
#ifdef INTRO_SOM_UMS
static char somUrl[700];
static int somNosso;
#endif

static void caminho(char *d, size_t n, int i) { snprintf(d, n, "%s/intro/f%03d.jpg", dir, i); }

// Decoder: keeps frames [pedido, pedido + INTRO_ADIANTE) ready.
static void *decodificar(void *u) {
  (void)u;
  for (int i = 0; i < INTRO_N; i++) {
    char c[700];
    SDL_Surface *s, *r;
    pthread_mutex_lock(&trava);
    while (!parar && i >= pedido + INTRO_ADIANTE) pthread_cond_wait(&sinal, &trava);
    if (parar) { pthread_mutex_unlock(&trava); break; }
    if (i < pedido) { pthread_mutex_unlock(&trava); continue; }   // already late: skip it
    pthread_mutex_unlock(&trava);
    caminho(c, sizeof c, i);
    s = IMG_Load(c);
    r = s ? SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ABGR8888, 0) : NULL;
    if (s) SDL_FreeSurface(s);
    pthread_mutex_lock(&trava);
    if (parar) { if (r) SDL_FreeSurface(r); pthread_mutex_unlock(&trava); break; }
    prontos[i] = r;
    pthread_mutex_unlock(&trava);
  }
  return NULL;
}

int qrintro_iniciar(const char *dirArte) {
  char c[700];
  FILE *f;
#if defined(NV_LINUX_DESKTOP) || defined(__APPLE__)
  { const char *q = getenv("QRT_INTRO"); if (!q || strcmp(q, "1")) return 0; }
#endif
  snprintf(dir, sizeof dir, "%s", dirArte ? dirArte : ".");
  caminho(c, sizeof c, INTRO_N - 1);
  if (!(f = fopen(c, "rb"))) { printf("[intro] frames not found: skipped\n"); fflush(stdout); return 0; }
  fclose(f);
  pedido = 0; parar = 0;
  if (pthread_create(&fio, NULL, decodificar, NULL) != 0) return 0;
  fioVivo = 1;
  estado = 1;
  quadroAtual = -1;
#ifdef INTRO_SOM_UMS
  snprintf(somUrl, sizeof somUrl, "file://%s/intro/intro.m4a", dir);
  somNosso = 0;
  if (video_iniciar_auto()) {
    video_parar();
    video_definir_cabecalhos("");
    video_definir_mp4(1);
    video_volume(100);
    somNosso = video_tocar(somUrl);
  }
  printf("[intro] sound: %s\n", somNosso ? "requested from the media pipeline" : "unavailable (picture only)");
#else
  printf("[intro] sound: none on this build (picture only)\n");
#endif
  fflush(stdout);
  return 1;
}
int qrintro_ativa(void) { return estado >= 1 && estado <= 3; }

#ifdef INTRO_SOM_UMS
static int somMeu(void) { const char *u = video_url_atual(); return somNosso && u && !strcmp(u, somUrl); }
static void somParar(void) { if (somMeu()) video_parar(); somNosso = 0; }
#endif

static void encerrar(void) {
  estado = 4;
  pthread_mutex_lock(&trava); parar = 1; pthread_cond_broadcast(&sinal); pthread_mutex_unlock(&trava);
  if (fioVivo) { pthread_join(fio, NULL); fioVivo = 0; }
  for (int i = 0; i < INTRO_N; i++) if (prontos[i]) { SDL_FreeSurface(prontos[i]); prontos[i] = NULL; }
  if (tex) { gfx_tex_esquecer(tex); glDeleteTextures(1, &tex); tex = 0; }
#ifdef INTRO_SOM_UMS
  somParar();
#endif
  printf("[intro] end after %u ms (last frame %d)\n", (unsigned)(SDL_GetTicks() - primeiro), quadroAtual);
  fflush(stdout);
}

void qrintro_tecla(void) {
  if (estado == 1 || estado == 2) {
    estado = 3; saidaEm = SDL_GetTicks();
#ifdef INTRO_SOM_UMS
    somParar();
#endif
    printf("[intro] skipped by a key\n"); fflush(stdout);
  }
}

static void subir(SDL_Surface *s) {
  if (!tex || texW != s->w || texH != s->h) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s->w, s->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texW = s->w; texH = s->h;
  } else {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, s->w, s->h, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
  }
  gfx_tex_esquecer(0);
}

int qrintro_desenhar(Uint32 agora) {
  int alvo;
  float alfa = 1.0f;
  if (!qrintro_ativa()) return 0;
  if (!primeiro) primeiro = agora;
  if (estado == 1) {
    int somComecou = 0;
#ifdef INTRO_SOM_UMS
    video_bombear();
    somComecou = somMeu() && (video_tocando() || video_falhou());
#endif
    alvo = 0;
    // Frame 0 is held (it is the system splash) until the sound starts.
    if (somComecou || agora - primeiro >= INTRO_ESPERA_SOM
#ifndef INTRO_SOM_UMS
        || prontos[0]
#endif
       ) {
      estado = 2; t0 = agora;
      printf("[intro] playing (waited %u ms for the sound)\n", (unsigned)(agora - primeiro)); fflush(stdout);
    }
  }
  if (estado == 2) {
    alvo = (int)((agora - t0) * INTRO_FPS / 1000u);
    if (alvo >= INTRO_N) { estado = 3; saidaEm = agora; alvo = INTRO_N - 1; }
  } else if (estado == 3) {
    float p = (float)(agora - saidaEm) / INTRO_SAIDA_MS;
    if (p >= 1.0f) { encerrar(); return 0; }
    alfa = (1.0f - p) * (1.0f - p);
    alvo = quadroAtual < 0 ? 0 : quadroAtual;
  } else alvo = 0;
  // Newest ready frame not after `alvo`; frames already passed are dropped.
  {
    SDL_Surface *s = NULL;
    int achou = -1;
    pthread_mutex_lock(&trava);
    for (int i = alvo; i > quadroAtual && i >= 0; i--) if (prontos[i]) { s = prontos[i]; prontos[i] = NULL; achou = i; break; }
    for (int i = 0; i < achou; i++) if (prontos[i]) { SDL_FreeSurface(prontos[i]); prontos[i] = NULL; }
    if (alvo > pedido) { pedido = alvo; pthread_cond_broadcast(&sinal); }
    pthread_mutex_unlock(&trava);
    if (s) { subir(s); SDL_FreeSurface(s); quadroAtual = achou; }
  }
  // Under the first decoded frame: the splash's own colour (no flash of the home).
  if (!tex) { gfx_cor((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, 0.0f, 6 / 255.0f, 21 / 255.0f, 50 / 255.0f, alfa); return 1; }
  gfx_tex_aspect_atual = 0.0f;
  gfx_rect((GfxRect){ 0, 0, NV_TELA_W, NV_TELA_H }, tex, GFX_TEXTO, 0, 0, 0, 0.0f, 1, 1, 1, alfa);
  return 1;
}
