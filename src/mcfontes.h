// MUSIC CENTER: the music sources this profile configured (Navidrome /
// Subsonic servers and WebDAV folders), in "music-sources-p<N>.txt".
// Passwords are kept on this TV like the IPTV ones and never logged.
#ifndef NV_MCFONTES_H
#define NV_MCFONTES_H
#include "subsonic.h"
#include "webdav.h"

#define MCF_MAX 8
enum { MCS_SUBSONIC = 1, MCS_WEBDAV = 2 };

typedef struct {
  int  tipo;            // MCS_*
  char chave[16];       // "ss" / "dv" + 8 hex: the prefix of every qualified id
  char nome[64];
  SsServidor ss;        // MCS_SUBSONIC
  DavServidor dv;       // MCS_WEBDAV
} McFonte;

void mcfontes_carregar(void);           // this profile's file (again after a profile switch)
int  mcfontes_n(void);
int  mcfontes_copiar(int i, McFonte *out);           // 1 ok
int  mcfontes_achar(const char *chave, McFonte *out); // by key or by a qualified id "ssXXXXXXXX:..."
// Adds or replaces (same type + url + user). Fills f->chave. 1 saved.
int  mcfontes_salvar(McFonte *f);
int  mcfontes_remover(const char *chave);
// "ss1a2b3c4d:abc" -> fonte "ss1a2b3c4d", local "abc". 1 when well formed.
int  mcfontes_partir(const char *id, char *fonte, size_t nf, const char **local);
#endif
