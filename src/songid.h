// QR TIME "WHAT'S THIS SONG?" (player button). webOS apps cannot capture the
// TV's sound or the remote's microphone, so the sample comes from THE FILE
// being played: about 8 s of its audio track at the current position, read
// with HTTP Range requests and written as a plain elementary stream (AC-3,
// E-AC-3, DTS, TrueHD and MP3 as they are; AAC with ADTS headers built from
// the track's AudioSpecificConfig). That sample, base64, goes to AudD
// (api.audd.io) with the person's own AudD token; nothing else is sent (no
// stream URL, no account data).
// Matroska only (most add-on sources). MP4/HLS/live channels: "not available
// for this source". Opus/Vorbis tracks: same (they need a container).
#ifndef QRT_SONGID_H
#define QRT_SONGID_H
#include <stddef.h>

enum { SID_OK = 0, SID_NAO_MKV, SID_SEM_AUDIO, SID_CODEC, SID_REDE, SID_SEM_CHAVE, SID_NAO_ACHOU, SID_ERRO };
typedef struct { char titulo[160], artista[160], album[160]; } SidMusica;

int  songid_tem_chave(void);
int  songid_definir_chave(const char *token);          // "" removes; stored as a secret (audd.txt)
// BLOCKS (worker). The ~8 s elementary stream from `url` at `seg`. *codec =
// "ac3", "eac3", "aac", "dts", "truehd", "mp3". malloc'd; NULL + *erro.
unsigned char *songid_amostra(const char *url, double seg, double segundos, size_t *n, char *codec, size_t nc, int *erro);
// BLOCKS (worker): the whole thing (sample + AudD). SID_*.
int  songid_reconhecer(const char *url, double seg, SidMusica *out);
// AudD answer (tests). SID_OK / SID_NAO_ACHOU / SID_ERRO.
int  songid_parse(const char *json, SidMusica *out);
void songid_definir_base(const char *url);              // tests (default https://api.audd.io/)

// Player button: start in the background, then poll on the app thread.
int  songid_iniciar(const char *url, double seg);       // 1 started
int  songid_resultado(SidMusica *out);                  // -1 running/none, else SID_*
// songidui.c (app thread): the button, the result card, its YouTube search.
void songidui_iniciar(const char *url, double seg);
void songidui_passo(void);
void songidui_acao(int botao);
#endif
