// QR TIME INTRO: the brand video at every cold start, like a streaming app's
// ident. The system splash (deploy/app/splash.png) is the video's first
// frame, so the hand-over from webOS to the app is seamless; the home builds
// underneath while it plays and appears as the last (dark) frame fades.
//
// Picture: 90 JPEG frames (art/intro/f000..f089.jpg, 1280x720, 30 fps) decoded
// on a worker thread a few frames ahead and drawn by the app itself, so it
// never depends on the media pipeline. Sound: art/intro/intro.m4a through the
// TV's media pipeline (uMS, the same path as the Music Center); the clock
// starts when the sound starts (at most 0.8 s wait, frame 0 held meanwhile).
// Without sound (pipeline busy or failing) the picture plays alone.
// Any key skips it. Desktop builds play it only with QRT_INTRO=1 (tests).
#ifndef NV_QRINTRO_H
#define NV_QRINTRO_H
#include <SDL2/SDL.h>

// After gfx_iniciar. 1 = the intro will play (frames found).
int  qrintro_iniciar(const char *dirArte);
int  qrintro_ativa(void);
// Draws over the frame; 0 once finished (resources released).
int  qrintro_desenhar(Uint32 agora);
void qrintro_tecla(void);   // skip: fade out now, sound stops
#endif
