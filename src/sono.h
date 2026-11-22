// QR TIME sleep timer. Settings > Tela de descanso > Temporizador de sono, or
// its Control Center tile ("Dormir"). When the chosen time runs out: the film,
// series or channel pauses, the music pauses, and the screen goes off
// (webOS tvpower turnOffScreen; elsewhere a black frame). One minute before,
// a notice. The first key afterwards only turns the screen back on.
#ifndef QRT_SONO_H
#define QRT_SONO_H
#include <SDL2/SDL.h>

void sono_atualizar(Uint32 agora);   // once per frame
// A key or pointer event: 1 = it only woke the screen (swallow it).
int  sono_entrada(const SDL_Event *e);
void sono_desenhar(void);            // black frame while the screen is "off"
int  sono_tela_apagada(void);
// Test hook: what the timer would act on (NULL = the real player/music).
typedef struct {
  int  (*minutos)(void);
  void (*zerar)(void);
  void (*pausar_video)(void);
  void (*pausar_musica)(void);
  void (*apagar_tela)(int apagar);
  void (*aviso)(int segundos);
} SonoAlvo;
void sono_teste_alvo(const SonoAlvo *a);
#endif
