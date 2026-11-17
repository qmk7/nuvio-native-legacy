// SPORT CENTER STORE AND MATCHING (QR TIME). No drawing, no network: the
// screen (esporteui.c) and the tests (tests/qrtime_esporte.c) call this.
//
// PER PROFILE ("esporte-p<N>.txt", one fact per line):
//   time  <league>  <team id>  <name>     followed team
//   liga  <league>  <0|1>                 league followed or not (overrides the default)
//   spoiler <0|1>                         hide scores
//   canal <event id> <channel id> <name>  linked channel (first = main, the rest alternates)
//   nao   <event id> <channel id>         a suggestion the user said is wrong
//
// CHANNEL SUGGESTIONS (esporte_sugerir) read the user's own channels and their
// programme guide around kick-off, best evidence first:
//   1. a programme on air at kick-off whose title names BOTH teams -> "showing it"
//   2. the channel's own name names both teams (event channels)   -> "showing it"
//   3. a programme naming ONE team, or a broadcaster ESPN lists in the
//      channel name with a programme on air                         -> "maybe"
// Team names are compared word by word after folding case and dropping
// club words (FC, SC, CF, Club...). Guides written in another script (Arabic
// titles for English team names) give no match: then only manual linking
// helps, and the screen says so.
#ifndef NV_ESPORTE_H
#define NV_ESPORTE_H
#include <stddef.h>
#include <time.h>
#include "espn.h"

void esporte_carregar(void);            // this profile's file (profile change)
int  esporte_segue_liga(const char *liga);
void esporte_definir_liga(const char *liga, int seguir);
int  esporte_segue_time(const char *liga, const char *id);
void esporte_alternar_time(const char *liga, const char *id, const char *nome);
int  esporte_times_seguidos(char (*ligas)[32], char (*ids)[16], char (*nomes)[64], int max);
int  esporte_esconder_placar(void);
void esporte_definir_esconder_placar(int on);
// Is this event about something the profile follows (team or league)?
int  esporte_e_meu(const EspEvento *e);

#define ESP_CANAIS_MAX 8
typedef struct { char canal[80]; char nome[96]; } EspCanal;
int  esporte_canais(const char *evento, EspCanal *out, int max);   // main first
int  esporte_ligar(const char *evento, const char *canal, const char *nome);   // adds an alternate (or the main)
int  esporte_tornar_principal(const char *evento, const char *canal);
int  esporte_desligar(const char *evento, const char *canal);      // remove + remember "not this one"
int  esporte_recusado(const char *evento, const char *canal);

// ---- suggestions (pure) ----
typedef struct { const char *id, *nome, *categoria; } EspCanalInfo;
// Programmes of `canal` between `de` and `ate`: titles and start/end.
typedef int (*EspProgsFn)(const char *canal, time_t de, time_t ate,
                          char (*titulos)[128], time_t *ini, time_t *fim, int max, void *u);
enum { ESP_TALVEZ = 1, ESP_PASSA = 2 };
typedef struct { char canal[80], nome[96]; int pontos, certeza; char motivo[160]; } EspSugestao;
int  esporte_sugerir(const EspEvento *e, const EspCanalInfo *canais, int n, EspProgsFn progs, void *u,
                     EspSugestao *out, int max);
// Word-level team match used by the suggestions (tests).
int  esporte_time_em(const EspTime *t, const char *texto);
#endif
