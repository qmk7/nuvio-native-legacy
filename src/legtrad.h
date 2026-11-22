// QR TIME automatic Arabic subtitles through DeepL. See legtrad.c.
#ifndef QRT_LEGTRAD_H
#define QRT_LEGTRAD_H
#include "legenda.h"
enum { LT_OK = 0, LT_SEM_LEGENDA, LT_SEM_CHAVE, LT_CHAVE_RECUSADA, LT_COTA, LT_REDE, LT_OCUPADO, LT_ERRO };
int  legtrad_tem_chave(void);
int  legtrad_definir_chave(const char *chave);   // "" removes; secret (deepl.txt)
int  legtrad_iniciar(void);                      // the subtitle on screen -> Arabic, in the background
int  legtrad_resultado(char **srt);              // -1 none yet; else LT_* and *srt (malloc'd) on LT_OK
// Pure parts (tests).
const char *legtrad_host(const char *chave);
char *legtrad_corpo(const LegendaCue *c, int n);
int   legtrad_parse(const char *json, char (*out)[768], int max);
char *legtrad_srt(const LegendaCue *c, int n);
#endif
